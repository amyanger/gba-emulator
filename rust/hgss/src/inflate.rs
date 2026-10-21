//! RFC 1951 (deflate) decoder for zipped ROMs. The stream is untrusted: a bad
//! stream returns an Error, never panics. Output goes straight into the caller's
//! buffer, which also serves as the back-reference window.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The stream is malformed or ends early.
    Damaged,
    /// The output doesn't fit in the buffer.
    TooBig,
}

use Error::*;

const MAX_BITS: usize = 15;
const FAST_BITS: u32 = 9;

/// Canonical Huffman code. `fast` maps the next FAST_BITS input bits to
/// `(len << 9) | symbol` for codes of at most FAST_BITS, 0 otherwise.
struct Huff {
    count: [u16; MAX_BITS + 1],
    symbol: [u16; 288],
    fast: [u16; 1 << FAST_BITS],
}

impl Huff {
    /// Builds the code from per-symbol lengths. Oversubscribed codes are rejected; an
    /// incomplete one only when it has more than one symbol (as zlib allows).
    fn new(lengths: &[u8]) -> Result<Huff, Error> {
        let mut h = Huff { count: [0; MAX_BITS + 1], symbol: [0; 288], fast: [0; 1 << FAST_BITS] };
        for &l in lengths {
            h.count[l as usize] += 1;
        }
        let mut left: i32 = 1;
        for len in 1..=MAX_BITS {
            left = (left << 1) - i32::from(h.count[len]);
            if left < 0 {
                return Err(Damaged);
            }
        }
        let used = lengths.len() - usize::from(h.count[0]);
        if left > 0 && used > 1 {
            return Err(Damaged);
        }
        let mut offs = [0u16; MAX_BITS + 2];
        for len in 1..=MAX_BITS {
            offs[len + 1] = offs[len] + h.count[len];
        }
        // Canonical codes in symbol order; short ones also go into the fast table.
        let mut next_code = [0u32; MAX_BITS + 1];
        let mut code = 0u32;
        for (len, next) in next_code.iter_mut().enumerate().skip(1) {
            code = (code + u32::from(h.count[len - 1] * u16::from(len > 1))) << 1;
            *next = code;
        }
        for (sym, &l) in lengths.iter().enumerate() {
            let len = l as usize;
            if len == 0 {
                continue;
            }
            h.symbol[offs[len] as usize] = sym as u16;
            offs[len] += 1;
            let c = next_code[len];
            next_code[len] += 1;
            if len as u32 <= FAST_BITS {
                let rev = c.reverse_bits() >> (32 - len);
                let mut i = rev;
                while i < (1 << FAST_BITS) {
                    h.fast[i as usize] = ((len as u16) << 9) | sym as u16;
                    i += 1 << len;
                }
            }
        }
        Ok(h)
    }
}

struct Bits<'a> {
    src: &'a [u8],
    pos: usize,
    buf: u64,
    cnt: u32,
    /// Zero bits padded past the end of `src`, at the top of `buf`.
    pad: u32,
}

impl<'a> Bits<'a> {
    fn fill(&mut self) {
        while self.cnt <= 56 {
            let b = match self.src.get(self.pos) {
                Some(&b) => {
                    self.pos += 1;
                    b
                }
                None => {
                    self.pad += 8;
                    0
                }
            };
            self.buf |= u64::from(b) << self.cnt;
            self.cnt += 8;
        }
    }

    fn peek(&mut self, n: u32) -> u32 {
        if self.cnt < n {
            self.fill();
        }
        (self.buf & ((1u64 << n) - 1)) as u32
    }

    fn drop(&mut self, n: u32) -> Result<(), Error> {
        self.buf >>= n;
        self.cnt -= n;
        if self.cnt < self.pad {
            return Err(Damaged);
        }
        Ok(())
    }

    fn take(&mut self, n: u32) -> Result<u32, Error> {
        let v = self.peek(n);
        self.drop(n)?;
        Ok(v)
    }

    fn decode(&mut self, h: &Huff) -> Result<u16, Error> {
        let e = h.fast[self.peek(FAST_BITS) as usize];
        if e != 0 {
            self.drop(u32::from(e >> 9))?;
            return Ok(e & 0x1FF);
        }
        // Long code: walk it a bit at a time, as in zlib's puff.c.
        let (mut code, mut first, mut index) = (0i32, 0i32, 0i32);
        for len in 1..=MAX_BITS {
            code |= self.take(1)? as i32;
            let count = i32::from(h.count[len]);
            if code - count < first {
                return Ok(h.symbol[(index + code - first) as usize]);
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        Err(Damaged)
    }
}

const LEN_BASE: [u16; 29] = [
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
];
const LEN_EXTRA: [u8; 29] =
    [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
const DIST_BASE: [u16; 30] = [
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
];
const DIST_EXTRA: [u8; 30] =
    [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];

/// Decodes `src` into `out` and returns the output length. With `partial`, filling
/// `out` ends decoding successfully (for reading just a header); otherwise it is TooBig.
pub fn inflate(src: &[u8], out: &mut [u8], partial: bool) -> Result<usize, Error> {
    let mut bits = Bits { src, pos: 0, buf: 0, cnt: 0, pad: 0 };
    let mut n = 0usize;
    let full = |n: usize| if partial { Ok(n) } else { Err(TooBig) };
    loop {
        let last = bits.take(1)?;
        match bits.take(2)? {
            0 => {
                // Stored: skip to a byte boundary, then LEN, NLEN and LEN raw bytes.
                let skip = bits.cnt % 8;
                bits.drop(skip)?;
                let len = bits.take(16)?;
                if bits.take(16)? != !len & 0xFFFF {
                    return Err(Damaged);
                }
                for _ in 0..len {
                    if n == out.len() {
                        return full(n);
                    }
                    out[n] = bits.take(8)? as u8;
                    n += 1;
                }
            }
            1 => {
                let mut l = [0u8; 288];
                l[..144].fill(8);
                l[144..256].fill(9);
                l[256..280].fill(7);
                l[280..].fill(8);
                let lit = Huff::new(&l)?;
                let dist = Huff::new(&[5u8; 32])?; // 30 and 31 are invalid, rejected when decoded
                match codes(&mut bits, &lit, &dist, out, &mut n) {
                    Err(TooBig) => return full(n),
                    r => r?,
                }
            }
            2 => {
                let (lit, dist) = dynamic(&mut bits)?;
                match codes(&mut bits, &lit, &dist, out, &mut n) {
                    Err(TooBig) => return full(n),
                    r => r?,
                }
            }
            _ => return Err(Damaged),
        }
        if last == 1 {
            return Ok(n);
        }
    }
}

fn dynamic(bits: &mut Bits) -> Result<(Huff, Huff), Error> {
    const ORDER: [usize; 19] = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];
    let nlen = bits.take(5)? as usize + 257;
    let ndist = bits.take(5)? as usize + 1;
    let ncode = bits.take(4)? as usize + 4;
    if nlen > 286 || ndist > 30 {
        return Err(Damaged);
    }
    let mut cl = [0u8; 19];
    for &i in &ORDER[..ncode] {
        cl[i] = bits.take(3)? as u8;
    }
    let clh = Huff::new(&cl)?;
    let mut lens = [0u8; 286 + 30];
    let mut i = 0;
    while i < nlen + ndist {
        let sym = bits.decode(&clh)?;
        let (val, rep) = match sym {
            0..=15 => (sym as u8, 1),
            16 => {
                if i == 0 {
                    return Err(Damaged);
                }
                (lens[i - 1], 3 + bits.take(2)? as usize)
            }
            17 => (0, 3 + bits.take(3)? as usize),
            _ => (0, 11 + bits.take(7)? as usize),
        };
        if i + rep > nlen + ndist {
            return Err(Damaged);
        }
        lens[i..i + rep].fill(val);
        i += rep;
    }
    if lens[256] == 0 {
        return Err(Damaged); // no end-of-block code
    }
    Ok((Huff::new(&lens[..nlen])?, Huff::new(&lens[nlen..nlen + ndist])?))
}

fn codes(bits: &mut Bits, lit: &Huff, dist: &Huff, out: &mut [u8], n: &mut usize) -> Result<(), Error> {
    loop {
        let sym = bits.decode(lit)? as usize;
        if sym < 256 {
            if *n == out.len() {
                return Err(TooBig);
            }
            out[*n] = sym as u8;
            *n += 1;
        } else if sym == 256 {
            return Ok(());
        } else {
            let s = sym - 257;
            if s >= 29 {
                return Err(Damaged);
            }
            let len = usize::from(LEN_BASE[s]) + bits.take(u32::from(LEN_EXTRA[s]))? as usize;
            let d = bits.decode(dist)? as usize;
            if d >= 30 {
                return Err(Damaged);
            }
            let back = usize::from(DIST_BASE[d]) + bits.take(u32::from(DIST_EXTRA[d]))? as usize;
            if back > *n {
                return Err(Damaged);
            }
            // Byte by byte: the copy may overlap what it writes.
            for _ in 0..len {
                if *n == out.len() {
                    return Err(TooBig);
                }
                out[*n] = out[*n - back];
                *n += 1;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::Rng;

    // Made with python3 zlib.compressobj(9, zlib.DEFLATED, -15): one fixed block, and
    // testdata/fox.deflate one dynamic block.
    const HELLO_FIXED: [u8; 10] = [0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xf0, 0x40, 0x27, 0x01];

    #[test]
    fn stored_block() {
        let src = [0x01, 0x03, 0x00, 0xFC, 0xFF, b'a', b'b', b'c'];
        let mut out = [0u8; 8];
        assert_eq!(inflate(&src, &mut out, false), Ok(3));
        assert_eq!(&out[..3], b"abc");
    }

    #[test]
    fn stored_bad_nlen() {
        let src = [0x01, 0x03, 0x00, 0xFC, 0xFE, b'a', b'b', b'c'];
        assert_eq!(inflate(&src, &mut [0u8; 8], false), Err(Damaged));
    }

    #[test]
    fn fixed_with_back_reference() {
        let mut out = [0u8; 32];
        let n = inflate(&HELLO_FIXED, &mut out, false).unwrap();
        assert_eq!(&out[..n], b"Hello Hello Hello Hello");
    }

    #[test]
    fn dynamic_block() {
        let want: String = (0..40).map(|i| format!("the quick brown fox {i} jumps over the lazy dog {}; ", i * i)).collect();
        let mut out = vec![0u8; 4096];
        let n = inflate(include_bytes!("../testdata/fox.deflate"), &mut out, false).unwrap();
        assert_eq!(&out[..n], want.as_bytes());
    }

    #[test]
    fn too_big_and_partial() {
        let mut out = [0u8; 5];
        assert_eq!(inflate(&HELLO_FIXED, &mut out, false), Err(TooBig));
        assert_eq!(inflate(&HELLO_FIXED, &mut out, true), Ok(5));
        assert_eq!(&out, b"Hello");
    }

    #[test]
    fn truncated_is_damaged() {
        for cut in 0..HELLO_FIXED.len() {
            assert_eq!(inflate(&HELLO_FIXED[..cut], &mut [0u8; 32], false), Err(Damaged), "cut {cut}");
        }
    }

    #[test]
    fn reserved_block_type() {
        assert_eq!(inflate(&[0x07], &mut [0u8; 8], false), Err(Damaged));
    }

    #[test]
    fn random_streams_never_panic() {
        let mut rng = Rng::new(7);
        let mut out = vec![0u8; 1 << 16];
        for _ in 0..20000 {
            let mut src = vec![0u8; 1 + rng.below(200)];
            let flips = src.len() * 2;
            rng.mutate(&mut src, flips);
            let _ = inflate(&src, &mut out, false);
        }
    }
}
