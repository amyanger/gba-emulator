//! IPS, UPS and BPS soft-patching for ROM loading, called from C through ffi.rs.
//! The patch is untrusted: every read and write is bounds-checked and a bad patch
//! returns an Error, never panics.

use crate::bytes::rd32;

/// Codes match ROM_PATCH_* in src/cartridge/rom_patch.h.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Error {
    NotPatch = 1,
    Damaged = 2,
    WrongRom = 3,
    TooBig = 4,
    BadResult = 5,
}

use Error::*;

/// Applies `patch` to `src` into `out` and returns the patched length.
/// `out.len()` is the largest result accepted. `src` and `out` must not overlap.
pub fn apply(src: &[u8], patch: &[u8], out: &mut [u8]) -> Result<usize, Error> {
    if patch.starts_with(b"BPS1") {
        bps(src, patch, out)
    } else if patch.starts_with(b"UPS1") {
        ups(src, patch, out)
    } else if patch.starts_with(b"PATCH") {
        ips(src, patch, out)
    } else {
        Err(NotPatch)
    }
}

const fn crc_table() -> [u32; 256] {
    let mut t = [0u32; 256];
    let mut i = 0;
    while i < 256 {
        let mut c = i as u32;
        let mut k = 0;
        while k < 8 {
            c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
            k += 1;
        }
        t[i] = c;
        i += 1;
    }
    t
}

static CRC_TABLE: [u32; 256] = crc_table();

pub fn crc32(b: &[u8]) -> u32 {
    !b.iter().fold(!0u32, |c, &x| CRC_TABLE[((c ^ u32::from(x)) & 0xFF) as usize] ^ (c >> 8))
}

struct Reader<'a> {
    b: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn done(&self) -> bool {
        self.pos >= self.b.len()
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8], Error> {
        let end = self.pos.checked_add(n).ok_or(Damaged)?;
        let s = self.b.get(self.pos..end).ok_or(Damaged)?;
        self.pos = end;
        Ok(s)
    }

    fn byte(&mut self) -> Result<u8, Error> {
        Ok(self.take(1)?[0])
    }

    fn be(&mut self, n: usize) -> Result<usize, Error> {
        Ok(self.take(n)?.iter().fold(0, |v, &x| (v << 8) | usize::from(x)))
    }

    // UPS/BPS varint: the +s after each continuation byte makes every
    // encoding unique (no redundant leading zeros).
    fn varint(&mut self) -> Result<u64, Error> {
        let (mut v, mut s) = (0u64, 1u64);
        for _ in 0..10 {
            let b = self.byte()?;
            v = u64::from(b & 0x7F).checked_mul(s).and_then(|x| v.checked_add(x)).ok_or(Damaged)?;
            if b & 0x80 != 0 {
                return Ok(v);
            }
            s = s.checked_mul(128).ok_or(Damaged)?;
            v = v.checked_add(s).ok_or(Damaged)?;
        }
        Err(Damaged)
    }

    fn size(&mut self) -> Result<usize, Error> {
        usize::try_from(self.varint()?).map_err(|_| Damaged)
    }
}

/// Shared UPS/BPS framing: checks the patch CRC, returns the body (between
/// the magic and the 12-byte footer) and the footer's source/target CRCs.
fn framed(patch: &[u8]) -> Result<(&[u8], u32, u32), Error> {
    let foot = patch.len().checked_sub(12).filter(|&f| f >= 4).ok_or(Damaged)?;
    let crc = |i| rd32(patch, foot + i).ok_or(Damaged);
    if crc(8)? != crc32(&patch[..foot + 8]) {
        return Err(Damaged);
    }
    Ok((&patch[4..foot], crc(0)?, crc(4)?))
}

fn check_source(src: &[u8], size: usize, crc: u32) -> Result<(), Error> {
    if src.len() != size || crc32(src) != crc {
        return Err(WrongRom);
    }
    Ok(())
}

fn target(out: &mut [u8], size: u64) -> Result<&mut [u8], Error> {
    let size = usize::try_from(size).map_err(|_| TooBig)?;
    out.get_mut(..size).ok_or(TooBig)
}

fn check_target(dst: &[u8], crc: u32) -> Result<usize, Error> {
    if crc32(dst) != crc {
        return Err(BadResult);
    }
    Ok(dst.len())
}

/// Moves a BPS copy cursor: bit 0 of `d` is the sign, the rest the distance.
fn seek(cur: usize, d: u64) -> Result<usize, Error> {
    let mag = usize::try_from(d >> 1).map_err(|_| Damaged)?;
    if d & 1 != 0 { cur.checked_sub(mag) } else { cur.checked_add(mag) }.ok_or(Damaged)
}

fn bps(src: &[u8], patch: &[u8], out: &mut [u8]) -> Result<usize, Error> {
    let (body, src_crc, dst_crc) = framed(patch)?;
    let mut r = Reader { b: body, pos: 0 };
    let src_size = r.size()?;
    let dst_size = r.varint()?;
    let meta = r.size()?;
    r.take(meta)?;
    check_source(src, src_size, src_crc)?;
    let dst = target(out, dst_size)?;

    let (mut o, mut src_rel, mut tgt_rel) = (0usize, 0usize, 0usize);
    while !r.done() {
        let cmd = r.varint()?;
        let n = usize::try_from(cmd >> 2).ok().and_then(|n| n.checked_add(1)).ok_or(Damaged)?;
        let end = o.checked_add(n).filter(|&e| e <= dst.len()).ok_or(Damaged)?;
        match cmd & 3 {
            0 => dst[o..end].copy_from_slice(src.get(o..end).ok_or(Damaged)?),
            1 => dst[o..end].copy_from_slice(r.take(n)?),
            2 => {
                src_rel = seek(src_rel, r.varint()?)?;
                let s_end = src_rel.checked_add(n).ok_or(Damaged)?;
                dst[o..end].copy_from_slice(src.get(src_rel..s_end).ok_or(Damaged)?);
                src_rel = s_end;
            }
            _ => {
                tgt_rel = seek(tgt_rel, r.varint()?)?;
                if tgt_rel >= o {
                    return Err(Damaged);
                }
                // Byte by byte on purpose: the source may overlap the bytes
                // being written, which is how BPS encodes runs.
                for i in o..end {
                    dst[i] = dst[tgt_rel];
                    tgt_rel += 1;
                }
            }
        }
        o = end;
    }
    if o != dst.len() {
        return Err(Damaged);
    }
    check_target(dst, dst_crc)
}

fn ups(src: &[u8], patch: &[u8], out: &mut [u8]) -> Result<usize, Error> {
    let (body, src_crc, dst_crc) = framed(patch)?;
    let mut r = Reader { b: body, pos: 0 };
    let src_size = r.size()?;
    let dst_size = r.varint()?;
    // Forward only: a patch is never applied backwards to "unpatch" a hack.
    check_source(src, src_size, src_crc)?;
    let dst = target(out, dst_size)?;

    for (i, d) in dst.iter_mut().enumerate() {
        *d = src.get(i).copied().unwrap_or(0);
    }
    let mut pos = 0usize;
    while !r.done() {
        pos = pos.checked_add(r.size()?).ok_or(Damaged)?;
        // XOR bytes up to and including a 0x00; writes past the target end are dropped.
        loop {
            let x = r.byte()?;
            if let Some(d) = dst.get_mut(pos) {
                *d ^= x;
            }
            pos = pos.checked_add(1).ok_or(Damaged)?;
            if x == 0 {
                break;
            }
        }
    }
    check_target(dst, dst_crc)
}

const IPS_EOF: usize = 0x45_4F46;

fn ips(src: &[u8], patch: &[u8], out: &mut [u8]) -> Result<usize, Error> {
    out.get_mut(..src.len()).ok_or(TooBig)?.copy_from_slice(src);
    let mut size = src.len();
    let mut r = Reader { b: patch, pos: 5 };
    loop {
        let off = r.be(3)?;
        if off == IPS_EOF {
            break;
        }
        let len = r.be(2)?;
        let (len, rle) = if len == 0 { (r.be(2)?, Some(r.byte()?)) } else { (len, None) };
        let end = off + len; // both under 2^24, cannot overflow
        if end > out.len() {
            return Err(TooBig);
        }
        if off > size {
            out[size..off].fill(0);
        }
        match rle {
            Some(b) => out[off..end].fill(b),
            None => out[off..end].copy_from_slice(r.take(len)?),
        }
        size = size.max(end);
    }
    // Flips' extension: exactly 3 more bytes are a big-endian truncate size.
    // Anything else after EOF is ignored, as mGBA does.
    if patch.len() - r.pos == 3 {
        let t = r.be(3)?;
        if t > size {
            return Err(Damaged);
        }
        size = t;
    }
    Ok(size)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn varint(mut v: u64, b: &mut Vec<u8>) {
        loop {
            let x = (v & 0x7F) as u8;
            v >>= 7;
            if v == 0 {
                b.push(0x80 | x);
                return;
            }
            b.push(x);
            v -= 1;
        }
    }

    fn read_varint(b: &[u8]) -> Result<u64, Error> {
        Reader { b, pos: 0 }.varint()
    }

    /// Header + footer around a UPS or BPS body. `dst` only feeds the CRC.
    fn frame(magic: &[u8], header: &[u8], body: &[u8], src: &[u8], dst: &[u8]) -> Vec<u8> {
        let mut p = magic.to_vec();
        p.extend_from_slice(header);
        p.extend_from_slice(body);
        p.extend_from_slice(&crc32(src).to_le_bytes());
        p.extend_from_slice(&crc32(dst).to_le_bytes());
        let c = crc32(&p);
        p.extend_from_slice(&c.to_le_bytes());
        p
    }

    fn bps_patch(src: &[u8], dst: &[u8], meta: &[u8], body: &[u8]) -> Vec<u8> {
        let mut h = Vec::new();
        varint(src.len() as u64, &mut h);
        varint(dst.len() as u64, &mut h);
        varint(meta.len() as u64, &mut h);
        h.extend_from_slice(meta);
        frame(b"BPS1", &h, body, src, dst)
    }

    fn act(b: &mut Vec<u8>, kind: u64, n: u64) {
        varint(((n - 1) << 2) | kind, b);
    }

    fn rel(b: &mut Vec<u8>, d: i64) {
        varint((d.unsigned_abs() << 1) | u64::from(d < 0), b);
    }

    fn ups_patch(src: &[u8], dst: &[u8], body: &[u8]) -> Vec<u8> {
        let mut h = Vec::new();
        varint(src.len() as u64, &mut h);
        varint(dst.len() as u64, &mut h);
        frame(b"UPS1", &h, body, src, dst)
    }

    fn run(src: &[u8], patch: &[u8], cap: usize) -> Result<Vec<u8>, Error> {
        let mut out = vec![0xEEu8; cap];
        let n = apply(src, patch, &mut out)?;
        out.truncate(n);
        Ok(out)
    }

    fn all_prefixes_fail(src: &[u8], patch: &[u8]) {
        for n in 0..patch.len() {
            assert!(run(src, &patch[..n], 64).is_err(), "prefix {n} accepted");
        }
    }

    #[test]
    fn crc_matches_ieee() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        assert_eq!(crc32(b""), 0);
    }

    #[test]
    fn varint_round_trip_and_limits() {
        for v in [0, 1, 127, 128, 16_511, 16_512, 1 << 40, u64::MAX] {
            let mut b = Vec::new();
            varint(v, &mut b);
            assert!(b.len() <= 10);
            assert_eq!(read_varint(&b), Ok(v), "{v}");
        }
        // 11 continuation bytes, and a 10-byte value past u64::MAX.
        assert_eq!(read_varint(&[0; 11]), Err(Damaged));
        assert_eq!(read_varint(&[0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0xFF]), Err(Damaged));
        assert_eq!(read_varint(&[0x00]), Err(Damaged));
    }

    const SRC: &[u8] = b"ABCDEFGH";
    const BPS_DST: &[u8] = b"ABxyFGHCDxyyyyyA";

    fn bps_all_actions() -> Vec<u8> {
        let mut b = Vec::new();
        act(&mut b, 0, 2); // SourceRead "AB"
        act(&mut b, 1, 2); // TargetRead "xy"
        b.extend_from_slice(b"xy");
        act(&mut b, 2, 3); // SourceCopy "FGH" from 5
        rel(&mut b, 5);
        act(&mut b, 2, 2); // SourceCopy "CD", back from 8 to 2
        rel(&mut b, -6);
        act(&mut b, 3, 2); // TargetCopy "xy" from 2
        rel(&mut b, 2);
        act(&mut b, 3, 4); // TargetCopy run of the last 'y' (overlapping)
        rel(&mut b, 6);
        act(&mut b, 3, 1); // TargetCopy "A", back from 14 to 0
        rel(&mut b, -14);
        bps_patch(SRC, BPS_DST, b"<meta/>", &b)
    }

    #[test]
    fn bps_every_action() {
        assert_eq!(run(SRC, &bps_all_actions(), 64).as_deref(), Ok(BPS_DST));
    }

    #[test]
    fn bps_errors() {
        let p = bps_all_actions();
        all_prefixes_fail(SRC, &p);
        assert_eq!(run(SRC, &p, BPS_DST.len() - 1), Err(TooBig));
        assert_eq!(run(b"ABCDEFGX", &p, 64), Err(WrongRom));
        assert_eq!(run(b"ABCDEFG", &p, 64), Err(WrongRom));
        let mut bad = p.clone();
        bad[20] ^= 1;
        assert_eq!(run(SRC, &bad, 64), Err(Damaged));

        // Valid framing, broken content.
        let mut b = Vec::new();
        act(&mut b, 1, 1);
        b.push(b'Q');
        assert_eq!(run(SRC, &bps_patch(SRC, b"Z", &[], &b), 64), Err(BadResult));
        let mut b = Vec::new();
        act(&mut b, 1, 1);
        b.push(b'Q');
        act(&mut b, 3, 1); // tgtRel 0 < o is fine
        rel(&mut b, 0);
        act(&mut b, 3, 1); // tgtRel 2 == o is not
        rel(&mut b, 1);
        assert_eq!(run(SRC, &bps_patch(SRC, b"QQQ", &[], &b), 64), Err(Damaged));
        let mut b = Vec::new();
        act(&mut b, 2, 1);
        rel(&mut b, -1); // before the source start
        assert_eq!(run(SRC, &bps_patch(SRC, b"A", &[], &b), 64), Err(Damaged));
        let mut b = Vec::new();
        act(&mut b, 0, 9); // SourceRead past the source end
        assert_eq!(run(SRC, &bps_patch(SRC, &[0; 9], &[], &b), 64), Err(Damaged));
        let mut b = Vec::new();
        act(&mut b, 0, 2); // stops short of the target size
        assert_eq!(run(SRC, &bps_patch(SRC, b"ABC", &[], &b), 64), Err(Damaged));
    }

    const UPS_SRC: &[u8] = b"ABCD";
    const UPS_DST: &[u8] = b"ABxDEF";

    fn ups_grow() -> Vec<u8> {
        // skip 2, XOR 'C'->'x', terminator copies 'D'; then "EF" over zeros,
        // whose terminator lands past the end and is dropped.
        let body = [2 | 0x80, b'C' ^ b'x', 0, 0x80, b'E', b'F', 0];
        ups_patch(UPS_SRC, UPS_DST, &body)
    }

    #[test]
    fn ups_grows_and_shrinks() {
        assert_eq!(run(UPS_SRC, &ups_grow(), 64).as_deref(), Ok(UPS_DST));
        let src = b"ABCDEF";
        let shrink = ups_patch(src, b"AzC", &[1 | 0x80, b'B' ^ b'z', 0]);
        assert_eq!(run(src, &shrink, 64).as_deref(), Ok(&b"AzC"[..]));
    }

    #[test]
    fn ups_errors() {
        let p = ups_grow();
        all_prefixes_fail(UPS_SRC, &p);
        assert_eq!(run(UPS_DST, &p, 64), Err(WrongRom)); // reverse direction
        assert_eq!(run(b"ABCE", &p, 64), Err(WrongRom));
        assert_eq!(run(UPS_SRC, &p, 5), Err(TooBig));
        let open = ups_patch(UPS_SRC, UPS_DST, &[0x80, 1]); // hunk without its 0x00
        assert_eq!(run(UPS_SRC, &open, 64), Err(Damaged));
        let wrong = ups_patch(UPS_SRC, b"ABCDEF", &[2 | 0x80, 1, 0]);
        assert_eq!(run(UPS_SRC, &wrong, 64), Err(BadResult));
    }

    fn ips(records: &[&[u8]], tail: &[u8]) -> Vec<u8> {
        let mut p = b"PATCH".to_vec();
        for r in records {
            p.extend_from_slice(r);
        }
        p.extend_from_slice(b"EOF");
        p.extend_from_slice(tail);
        p
    }

    #[test]
    fn ips_records() {
        let normal = ips(&[&[0, 0, 2, 0, 2, b'x', b'y']], &[]);
        assert_eq!(run(SRC, &normal, 64).as_deref(), Ok(&b"ABxyEFGH"[..]));
        let rle = ips(&[&[0, 0, 0, 0, 0, 0, 3, b'z']], &[]);
        assert_eq!(run(SRC, &rle, 64).as_deref(), Ok(&b"zzzDEFGH"[..]));
        let extend = ips(&[&[0, 0, 10, 0, 2, b'Q', b'Q']], &[]);
        assert_eq!(run(SRC, &extend, 64).as_deref(), Ok(&b"ABCDEFGH\0\0QQ"[..]));
        let zero_rle = ips(&[&[0, 0, 1, 0, 0, 0, 0, b'q']], &[]);
        assert_eq!(run(SRC, &zero_rle, 64).as_deref(), Ok(SRC));
        let trunc = ips(&[], &[0, 0, 4]);
        assert_eq!(run(SRC, &trunc, 64).as_deref(), Ok(&b"ABCD"[..]));
        let junk = ips(&[], &[1, 2]);
        assert_eq!(run(SRC, &junk, 64).as_deref(), Ok(SRC));

        // The last offset a record can use; 0x454F46 itself means EOF.
        let edge = ips(&[&[0x45, 0x4F, 0x45, 0, 1, 7]], &[]);
        let out = run(SRC, &edge, IPS_EOF + 16).unwrap();
        assert_eq!(out.len(), IPS_EOF);
        assert_eq!(out[IPS_EOF - 1], 7);
        assert!(out[SRC.len()..IPS_EOF - 1].iter().all(|&b| b == 0));
    }

    #[test]
    fn ips_errors() {
        let p = ips(&[&[0, 0, 2, 0, 2, b'x', b'y'], &[0, 0, 0, 0, 0, 0, 3, b'z']], &[]);
        all_prefixes_fail(SRC, &p);
        assert_eq!(run(SRC, &ips(&[], &[0, 0, 9]), 64), Err(Damaged));
        assert_eq!(run(SRC, &ips(&[&[0, 0, 7, 0, 2, 1, 2]], &[]), 8), Err(TooBig));
        assert_eq!(run(SRC, &ips(&[], &[]), 4), Err(TooBig));
        assert_eq!(run(SRC, b"PATCH\0\0\0\0\x05ab", 64), Err(Damaged));
    }

    #[test]
    fn not_a_patch() {
        assert_eq!(run(SRC, b"", 64), Err(NotPatch));
        assert_eq!(run(SRC, b"PATC", 64), Err(NotPatch));
        assert_eq!(run(SRC, b"hello world, not a patch", 64), Err(NotPatch));
    }

    #[test]
    fn random_damage_never_panics() {
        let mut rng = crate::test_util::Rng::new(0x5EED);
        let patches = [bps_all_actions(), ups_grow(), ips(&[&[0, 0, 2, 0, 2, b'x', b'y']], &[0, 0, 5])];
        for p in &patches {
            for _ in 0..2000 {
                let mut m = p.clone();
                let flips = 1 + rng.below(3);
                rng.mutate(&mut m[4..], flips);
                if !m.starts_with(b"PATCH") {
                    // Reseal the patch CRC so the damage reaches the parser.
                    let f = m.len() - 4;
                    let c = crc32(&m[..f]);
                    m[f..].copy_from_slice(&c.to_le_bytes());
                }
                let _ = run(SRC, &m, 64);
            }
        }
    }
}
