//! Reads the one .gba game out of a zip, called from C through ffi.rs. The zip is
//! untrusted: every offset is bounds-checked and a bad zip returns an Error.

use crate::bytes::{rd16, rd32};
use crate::inflate::inflate;
use crate::patch::crc32;

/// Codes match ROM_ZIP_* in src/cartridge/rom_zip.h.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Error {
    NotZip = 1,
    Damaged = 2,
    NoGame = 3,
    Unsupported = 4,
    Multi = 5,
    BadCrc = 6,
}

use Error::*;

/// The game entry, from the central directory (local header sizes can be zero or
/// 0xFFFFFFFF when the zip was written with a data descriptor, so they are ignored).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Entry {
    pub size: u32,
    pub crc: u32,
    method: u16,
    csize: u32,
    local: u32,
    name: (usize, usize),
}

/// Finds the end-of-central-directory record (the caller has seen the zip signature, so a
/// missing one means a cut-off file): the last one whose comment ends the file,
/// else (some writers pad the file) the last one whose directory fits before it.
fn eocd(zip: &[u8]) -> Result<usize, Error> {
    let lo = zip.len().saturating_sub(22 + 0xFFFF + 0x10000);
    let last = zip.len().checked_sub(22).ok_or(Damaged)?;
    let fits = |i: usize, exact: bool| {
        let (Some(clen), Some(size), Some(off)) = (rd16(zip, i + 20), rd32(zip, i + 12), rd32(zip, i + 16)) else {
            return false;
        };
        let dir_ok = off == u32::MAX || u64::from(off) + u64::from(size) <= i as u64;
        zip[i..].starts_with(b"PK\x05\x06") && dir_ok && (!exact || i + 22 + usize::from(clen) == zip.len())
    };
    for exact in [true, false] {
        if let Some(i) = (lo..=last).rev().find(|&i| fits(i, exact)) {
            return Ok(i);
        }
    }
    Err(Damaged)
}

fn is_game(name: &[u8]) -> bool {
    let base = name.rsplit(|&c| c == b'/' || c == b'\\').next().unwrap_or(name);
    !name.starts_with(b"__MACOSX/")
        && !base.starts_with(b"._")
        && base.len() > 4
        && base[base.len() - 4..].eq_ignore_ascii_case(b".gba")
}

/// The single .gba entry in the central directory.
pub fn find(zip: &[u8]) -> Result<Entry, Error> {
    if !zip.starts_with(b"PK\x03\x04") {
        return Err(NotZip);
    }
    let e = eocd(zip)?;
    let (disk, cd_disk) = (rd16(zip, e + 4).ok_or(Damaged)?, rd16(zip, e + 6).ok_or(Damaged)?);
    let count = rd16(zip, e + 10).ok_or(Damaged)?;
    let cd = rd32(zip, e + 16).ok_or(Damaged)?;
    if disk != 0 || cd_disk != 0 || count == 0xFFFF || cd == u32::MAX {
        return Err(Unsupported); // split archive or zip64
    }
    let mut at = cd as usize;
    let mut found = None;
    for _ in 0..count {
        if rd32(zip, at) != Some(0x0201_4B50) {
            return Err(Damaged);
        }
        let nlen = usize::from(rd16(zip, at + 28).ok_or(Damaged)?);
        let skip = nlen + usize::from(rd16(zip, at + 30).ok_or(Damaged)?) + usize::from(rd16(zip, at + 32).ok_or(Damaged)?);
        let name = zip.get(at + 46..at + 46 + nlen).ok_or(Damaged)?;
        if is_game(name) {
            if found.is_some() {
                return Err(Multi);
            }
            found = Some(Entry {
                size: rd32(zip, at + 24).ok_or(Damaged)?,
                crc: rd32(zip, at + 16).ok_or(Damaged)?,
                method: rd16(zip, at + 10).ok_or(Damaged)?,
                csize: rd32(zip, at + 20).ok_or(Damaged)?,
                local: rd32(zip, at + 42).ok_or(Damaged)?,
                name: (at + 46, nlen),
            });
            let flags = rd16(zip, at + 8).ok_or(Damaged)?;
            let ent = found.as_ref().ok_or(Damaged)?;
            if flags & 0x2041 != 0 || (ent.method != 0 && ent.method != 8) {
                return Err(Unsupported); // encrypted, or not store/deflate
            }
            if ent.size == u32::MAX || ent.csize == u32::MAX || ent.local == u32::MAX {
                return Err(Unsupported); // zip64
            }
        }
        at += 46 + skip;
    }
    found.ok_or(NoGame)
}

/// The compressed bytes of `ent`, located through its local header.
fn data<'a>(zip: &'a [u8], ent: &Entry) -> Result<&'a [u8], Error> {
    let l = ent.local as usize;
    if rd32(zip, l) != Some(0x0403_4B50) {
        return Err(Damaged);
    }
    let start = l + 30 + usize::from(rd16(zip, l + 26).ok_or(Damaged)?) + usize::from(rd16(zip, l + 28).ok_or(Damaged)?);
    zip.get(start..start + ent.csize as usize).ok_or(Damaged)
}

/// Unpacks the game into `out` and returns how many bytes were written. A game bigger
/// than `out` fills it and stops (for reading just a header), with no CRC check; the
/// caller compares the count with `Entry::size`.
pub fn read(zip: &[u8], out: &mut [u8]) -> Result<(Entry, usize), Error> {
    let ent = find(zip)?;
    let src = data(zip, &ent)?;
    let whole = ent.size as usize <= out.len();
    let cap = out.len().min(ent.size as usize);
    let out = &mut out[..cap];
    let n = if ent.method == 0 {
        if ent.csize != ent.size {
            return Err(Damaged);
        }
        let len = out.len();
        out.copy_from_slice(&src[..len]);
        len
    } else {
        inflate(src, out, !whole).map_err(|_| Damaged)?
    };
    if whole && (n != ent.size as usize || crc32(out) != ent.crc) {
        return Err(if n == ent.size as usize { BadCrc } else { Damaged });
    }
    Ok((ent, n))
}

/// CON, PRN, AUX, NUL, COM1-9 and LPT1-9 name devices on Windows, whatever the extension.
fn reserved_on_windows(base: &[u8]) -> bool {
    let stem = base.split(|&c| c == b'.').next().unwrap_or(base);
    let numbered = stem.len() == 4
        && (stem[..3].eq_ignore_ascii_case(b"COM") || stem[..3].eq_ignore_ascii_case(b"LPT"))
        && stem[3].is_ascii_digit()
        && stem[3] != b'0';
    numbered || [b"CON", b"PRN", b"AUX", b"NUL"].iter().any(|w| stem.eq_ignore_ascii_case(*w))
}

/// The entry's base name if it is safe to use as a file name on every OS, else None.
pub fn name<'a>(zip: &'a [u8], ent: &Entry) -> Option<&'a [u8]> {
    let full = zip.get(ent.name.0..ent.name.0 + ent.name.1)?;
    let base = full.rsplit(|&c| c == b'/' || c == b'\\').next()?;
    let bad = |c: &u8| *c < 0x20 || *c == 0x7F || b":*?\"<>|".contains(c);
    let ok = !base.is_empty()
        && !base.starts_with(b".")
        && !base.ends_with(b" ")
        && !base.iter().any(bad)
        && core::str::from_utf8(base).is_ok()
        && !reserved_on_windows(base);
    ok.then_some(base)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::Rng;

    /// A zip of (name, data, method) entries, deflated with fixed blocks where asked.
    struct Z {
        body: Vec<u8>,
        cd: Vec<u8>,
        n: u16,
    }

    impl Z {
        fn new() -> Z {
            Z { body: Vec::new(), cd: Vec::new(), n: 0 }
        }

        fn add(&mut self, name: &str, data: &[u8], method: u16, comp: &[u8]) -> &mut Z {
            let off = self.body.len() as u32;
            let mut h = Vec::new();
            h.extend_from_slice(&[0, 0]); // flags
            h.extend_from_slice(&method.to_le_bytes());
            h.extend_from_slice(&[0; 4]); // time, date
            h.extend_from_slice(&crc32(data).to_le_bytes());
            h.extend_from_slice(&(comp.len() as u32).to_le_bytes());
            h.extend_from_slice(&(data.len() as u32).to_le_bytes());
            h.extend_from_slice(&(name.len() as u16).to_le_bytes());
            self.body.extend_from_slice(b"PK\x03\x04\x14\x00");
            self.body.extend_from_slice(&h);
            self.body.extend_from_slice(&[0, 0]);
            self.body.extend_from_slice(name.as_bytes());
            self.body.extend_from_slice(comp);
            self.cd.extend_from_slice(b"PK\x01\x02\x14\x00\x14\x00");
            self.cd.extend_from_slice(&h);
            self.cd.extend_from_slice(&[0; 12]); // extra, comment, disk, attrs
            self.cd.extend_from_slice(&off.to_le_bytes());
            self.cd.extend_from_slice(name.as_bytes());
            self.n += 1;
            self
        }

        fn stored(&mut self, name: &str, data: &[u8]) -> &mut Z {
            self.add(name, data, 0, data)
        }

        fn build(&self) -> Vec<u8> {
            let mut z = self.body.clone();
            z.extend_from_slice(&self.cd);
            z.extend_from_slice(b"PK\x05\x06\0\0\0\0");
            z.extend_from_slice(&self.n.to_le_bytes());
            z.extend_from_slice(&self.n.to_le_bytes());
            z.extend_from_slice(&(self.cd.len() as u32).to_le_bytes());
            z.extend_from_slice(&(self.body.len() as u32).to_le_bytes());
            z.extend_from_slice(&[0, 0]);
            z
        }
    }

    fn rom() -> Vec<u8> {
        (0..300u32).map(|i| (i * 7) as u8).collect()
    }

    #[test]
    fn stored_game_skips_mac_junk() {
        let r = rom();
        let z = Z::new().stored("__MACOSX/._Game.GBA", b"junk").stored("dir/", b"").stored("dir/Game.GBA", &r).build();
        let mut out = vec![0u8; 1024];
        let (ent, n) = read(&z, &mut out).unwrap();
        assert_eq!((n, ent.size), (300, 300));
        assert_eq!(&out[..n], &r[..]);
        assert_eq!(name(&z, &ent), Some(&b"Game.GBA"[..]));
    }

    #[test]
    fn deflated_game() {
        let hello = b"Hello Hello Hello Hello";
        let comp = [0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xf0, 0x40, 0x27, 0x01];
        let z = Z::new().add("h.gba", hello, 8, &comp).build();
        let mut out = vec![0u8; 64];
        assert_eq!(read(&z, &mut out).map(|r| r.1), Ok(hello.len()));
        assert_eq!(&out[..hello.len()], hello);
    }

    #[test]
    fn header_only_read() {
        let r = rom();
        let z = Z::new().stored("a.gba", &r).build();
        let mut out = [0u8; 0xC0];
        let (ent, n) = read(&z, &mut out).unwrap();
        assert_eq!((n, ent.size, ent.crc), (0xC0, 300, crc32(&r)));
        assert_eq!(&out[..], &r[..0xC0]);
    }

    #[test]
    fn fake_eocd_in_comment() {
        let mut z = Z::new().stored("a.gba", &rom()).build();
        let n = z.len();
        // The comment holds a fake record that would claim an empty zip.
        z[n - 2..].copy_from_slice(&24u16.to_le_bytes());
        z.extend_from_slice(b"PK\x05\x06");
        z.extend_from_slice(&[0u8; 20]);
        assert!(read(&z, &mut vec![0u8; 1024]).is_ok());
    }

    #[test]
    fn padding_after_eocd() {
        let mut z = Z::new().stored("a.gba", &rom()).build();
        z.extend_from_slice(&[0u8; 9842]);
        assert!(read(&z, &mut vec![0u8; 1024]).is_ok());
    }

    #[test]
    fn errors() {
        let r = rom();
        let mut out = vec![0u8; 1024];
        assert_eq!(read(b"not a zip at all, no", &mut out), Err(NotZip));
        let whole = Z::new().stored("a.gba", &r).build();
        assert_eq!(read(&whole[..200], &mut out), Err(Damaged));
        assert_eq!(read(&Z::new().stored("a.txt", &r).build(), &mut out), Err(NoGame));
        assert_eq!(read(&Z::new().stored("a.gba", &r).stored("b.gba", &r).build(), &mut out), Err(Multi));
        assert_eq!(read(&Z::new().add("a.gba", &r, 14, &r).build(), &mut out), Err(Unsupported));
        let mut bad = Z::new().stored("a.gba", &r).build();
        bad[40] ^= 1; // a byte of the game data
        assert_eq!(read(&bad, &mut out), Err(BadCrc));
        let mut enc = Z::new().stored("a.gba", &r).build();
        let cd = enc.len() - 22 - (46 + 5);
        enc[cd + 8] |= 1;
        assert_eq!(read(&enc, &mut out), Err(Unsupported));
    }

    #[test]
    fn unsafe_names_are_refused() {
        for (n, ok) in [("a/b/Ok Game.gba", true), ("..\\x.gba", true), ("c:x.gba", false), (".h.gba", false), ("a?.gba", false), ("con.gba", false), ("Com3.gba", false), ("Console.gba", true)] {
            let z = Z::new().stored(n, &rom()).build();
            let ent = find(&z).unwrap();
            assert_eq!(name(&z, &ent).is_some(), ok, "{n}");
        }
    }

    #[test]
    fn mutated_zips_never_panic() {
        let base = Z::new().stored("x/y.gba", &rom()).stored("z.txt", b"zz").build();
        let mut rng = Rng::new(3);
        let mut out = vec![0u8; 1024];
        for _ in 0..20000 {
            let mut z = base.clone();
            let flips = 1 + rng.below(8);
            rng.mutate(&mut z, flips);
            let cut = z.len() - rng.below(40);
            if let Ok((ent, _)) = read(&z[..cut], &mut out) {
                let _ = name(&z[..cut], &ent);
            }
        }
    }
}
