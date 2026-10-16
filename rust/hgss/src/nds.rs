//! NDS ROM file system (FNT/FAT) and NARC archives. File I/O stays in C
//! (nds_open/nds_read in src/hgss/nds_fs.c); this only decides what to read.

use crate::bytes::{rd16, rd32};

/// An open ROM file.
pub trait Source {
    fn size(&self) -> u32;
    /// Fills `dst` from file offset `off`; false if any of it is past the end.
    fn read(&self, off: u32, dst: &mut [u8]) -> bool;
}

/// FNT and FAT positions from the ROM header (validated by nds_open in C).
#[derive(Clone, Copy)]
pub struct Tables {
    pub fnt: u32,
    pub fnt_size: u32,
    pub fat: u32,
    pub fat_size: u32,
}

/// Mirror of NdsSpan: absolute file offsets, end exclusive.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Span {
    pub start: u32,
    pub end: u32,
}

fn read<const N: usize>(src: &impl Source, off: u32) -> Option<[u8; N]> {
    let mut b = [0u8; N];
    src.read(off, &mut b).then_some(b)
}

fn span_ok(src: &impl Source, off: u32, len: u32) -> bool {
    off <= src.size() && len <= src.size() - off
}

/// Looks `name` up in directory `dir` (0xF000-based id): (is a directory, id).
fn fnt_lookup(src: &impl Source, t: &Tables, dir: u16, name: &[u8]) -> Option<(bool, u16)> {
    let idx = u32::from(dir & 0x0FFF);
    if 8 * idx + 8 > t.fnt_size {
        return None;
    }
    let main: [u8; 8] = read(src, t.fnt.checked_add(8 * idx)?)?;
    let mut off = t.fnt.checked_add(rd32(&main, 0)?)?;
    let mut file_id = rd16(&main, 4)?;
    let end = t.fnt.checked_add(t.fnt_size)?;
    while off < end {
        let [len] = read::<1>(src, off)?;
        off += 1;
        if len == 0 {
            return None;
        }
        let n = usize::from(len & 0x7F);
        let mut buf = [0u8; 127];
        if !src.read(off, &mut buf[..n]) {
            return None;
        }
        off = off.checked_add(n as u32)?;
        let is_dir = len & 0x80 != 0;
        let mut sub = 0;
        if is_dir {
            sub = u16::from_le_bytes(read::<2>(src, off)?);
            off = off.checked_add(2)?;
        }
        if buf[..n] == *name {
            return Some((is_dir, if is_dir { sub } else { file_id }));
        }
        if !is_dir {
            file_id = file_id.wrapping_add(1);
        }
    }
    None
}

/// Resolves a path like "a/0/8/1" to the file's span.
pub fn find(src: &impl Source, t: &Tables, path: &[u8]) -> Option<Span> {
    let mut dir = 0xF000u16;
    let mut parts = path.split(|&c| c == b'/').peekable();
    while let Some(name) = parts.next() {
        if name.is_empty() || name.len() > 127 {
            return None;
        }
        let (is_dir, id) = fnt_lookup(src, t, dir, name)?;
        if parts.peek().is_some() {
            if !is_dir {
                return None;
            }
            dir = id;
            continue;
        }
        let id = u32::from(id);
        if is_dir || 8 * id + 8 > t.fat_size {
            return None;
        }
        let e: [u8; 8] = read(src, t.fat.checked_add(8 * id)?)?;
        let span = Span { start: rd32(&e, 0)?, end: rd32(&e, 4)? };
        return (span.start <= span.end && span_ok(src, span.start, span.end - span.start)).then_some(span);
    }
    None
}

struct Layout {
    entries: u32,
    btaf_end: u32,
    count: u16,
    gmif_data: u32,
    gmif_end: u32,
}

fn layout(src: &impl Source, narc: Span) -> Option<Layout> {
    if narc.end < narc.start || narc.end - narc.start < 16 {
        return None;
    }
    let h: [u8; 16] = read(src, narc.start)?;
    if !h.starts_with(b"NARC") {
        return None;
    }
    let btaf = narc.start.checked_add(u32::from(rd16(&h, 0x0C)?))?;
    if btaf.checked_add(12)? > narc.end {
        return None;
    }
    let b: [u8; 12] = read(src, btaf)?;
    if !b.starts_with(b"BTAF") {
        return None;
    }
    let count = rd16(&b, 8)?;
    // The C added block sizes unchecked, so a huge one wrapped to an earlier offset.
    let btnf = btaf.checked_add(rd32(&b, 4)?)?;
    if btnf.checked_add(8)? > narc.end {
        return None;
    }
    let b: [u8; 8] = read(src, btnf)?;
    if !b.starts_with(b"BTNF") {
        return None;
    }
    let gmif = btnf.checked_add(rd32(&b, 4)?)?;
    if gmif.checked_add(8)? > narc.end {
        return None;
    }
    let b: [u8; 8] = read(src, gmif)?;
    if !b.starts_with(b"GMIF") {
        return None;
    }
    let gsize = rd32(&b, 4)?;
    if gsize < 8 || gsize > narc.end - gmif {
        return None;
    }
    Some(Layout { entries: btaf + 12, btaf_end: btnf, count, gmif_data: gmif + 8, gmif_end: gmif + gsize })
}

pub fn narc_count(src: &impl Source, narc: Span) -> Option<u16> {
    Some(layout(src, narc)?.count)
}

pub fn narc_member(src: &impl Source, narc: Span, index: u32) -> Option<Span> {
    let l = layout(src, narc)?;
    if index >= u32::from(l.count) {
        return None;
    }
    // The entry must sit inside the BTAF block; the C read it from wherever
    // the index pointed, even past the end of the NARC.
    let entry = l.entries.checked_add(8 * index)?;
    if entry.checked_add(8)? > l.btaf_end {
        return None;
    }
    let e: [u8; 8] = read(src, entry)?;
    let (s, t) = (rd32(&e, 0)?, rd32(&e, 4)?);
    if s > t || t > l.gmif_end - l.gmif_data {
        return None;
    }
    Some(Span { start: l.gmif_data + s, end: l.gmif_data + t })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::Rng;

    struct Mem<'a>(&'a [u8]);

    impl Source for Mem<'_> {
        fn size(&self) -> u32 {
            self.0.len() as u32
        }
        fn read(&self, off: u32, dst: &mut [u8]) -> bool {
            let off = off as usize;
            match self.0.get(off..off + dst.len()) {
                Some(s) => {
                    dst.copy_from_slice(s);
                    true
                }
                None => false,
            }
        }
    }

    fn w16(b: &mut [u8], o: usize, v: u16) {
        b[o..o + 2].copy_from_slice(&v.to_le_bytes());
    }
    fn w32(b: &mut [u8], o: usize, v: u32) {
        b[o..o + 4].copy_from_slice(&v.to_le_bytes());
    }

    /// Port of build_rom in tests/test_nds_fs.c: FNT 0x200, FAT 0x300,
    /// file "a/x" = NARC 0x400..0x444 with members "hi" and "abc".
    fn build_rom() -> Vec<u8> {
        let mut r = vec![0u8; 0x444];
        r[0x0C..0x10].copy_from_slice(b"TEST");
        w32(&mut r, 0x40, 0x200);
        w32(&mut r, 0x44, 0x40);
        w32(&mut r, 0x48, 0x300);
        w32(&mut r, 0x4C, 8);
        w32(&mut r, 0x200, 0x10);
        w16(&mut r, 0x206, 2);
        w32(&mut r, 0x208, 0x18);
        w16(&mut r, 0x20E, 0xF000);
        r[0x210] = 0x81;
        r[0x211] = b'a';
        w16(&mut r, 0x212, 0xF001);
        r[0x218] = 0x01;
        r[0x219] = b'x';
        w32(&mut r, 0x300, 0x400);
        w32(&mut r, 0x304, 0x444);
        r[0x400..0x404].copy_from_slice(b"NARC");
        w16(&mut r, 0x40C, 0x10);
        r[0x410..0x414].copy_from_slice(b"BTAF");
        w32(&mut r, 0x414, 0x1C);
        w16(&mut r, 0x418, 2);
        w32(&mut r, 0x420, 2);
        w32(&mut r, 0x424, 4);
        w32(&mut r, 0x428, 7);
        r[0x42C..0x430].copy_from_slice(b"BTNF");
        w32(&mut r, 0x430, 8);
        r[0x434..0x438].copy_from_slice(b"GMIF");
        w32(&mut r, 0x438, 16);
        r[0x43C..0x443].copy_from_slice(b"hi\0\0abc");
        r
    }

    fn tables(r: &[u8]) -> Tables {
        let at = |o: usize| r.get(o..o + 4).map_or(0, |s| u32::from_le_bytes(s.try_into().unwrap()));
        Tables { fnt: at(0x40), fnt_size: at(0x44), fat: at(0x48), fat_size: at(0x4C) }
    }

    #[test]
    fn find_and_members() {
        let r = build_rom();
        let (m, t) = (Mem(&r), tables(&r));
        let narc = find(&m, &t, b"a/x").unwrap();
        assert_eq!(narc, Span { start: 0x400, end: 0x444 });
        assert_eq!(narc_count(&m, narc), Some(2));
        assert_eq!(narc_member(&m, narc, 1), Some(Span { start: 0x440, end: 0x443 }));
        assert_eq!(narc_member(&m, narc, 2), None);
        for bad in [&b"a/y"[..], b"b/x", b"a", b"a/", b"/a/x", b""] {
            assert_eq!(find(&m, &t, bad), None);
        }
    }

    #[test]
    fn rejects_wrapping_block_size() {
        let mut r = build_rom();
        w32(&mut r, 0x414, 0xFFFF_FCF0);
        r[0x100..0x104].copy_from_slice(b"BTNF");
        w32(&mut r, 0x104, 0x334);
        let narc = Span { start: 0x400, end: 0x444 };
        assert_eq!(narc_count(&Mem(&r), narc), None);
    }

    #[test]
    fn rejects_entry_outside_btaf() {
        let mut r = build_rom();
        w16(&mut r, 0x418, 6);
        r.resize(0x450, 0);
        w32(&mut r, 0x448, 2);
        let narc = Span { start: 0x400, end: 0x444 };
        assert!(narc_member(&Mem(&r), narc, 1).is_some());
        assert_eq!(narc_member(&Mem(&r), narc, 5), None);
        assert_eq!(narc_count(&Mem(&r), Span { start: 0x444, end: 0x400 }), None);
    }

    #[test]
    fn garbage_never_panics() {
        let mut rng = Rng::new(0xD5);
        let good = build_rom();
        for _ in 0..20_000 {
            let mut r = good.clone();
            let flips = 1 + rng.below(6);
            rng.mutate(&mut r, flips);
            let cut = 0x200 + rng.below(r.len() - 0x200 + 1);
            let r = &r[..cut];
            let m = Mem(r);
            let t = tables(r);
            let _ = find(&m, &t, b"a/x");
            let narc = Span { start: rng.next_u64() as u32 % 0x500, end: rng.next_u64() as u32 % 0x500 };
            for narc in [narc, Span { start: 0x400, end: 0x444 }] {
                let _ = narc_count(&m, narc);
                for i in 0..8 {
                    let _ = narc_member(&m, narc, i);
                }
            }
        }
    }
}
