//! Nitro graphics (BTX0, BMD0/TEX0, NCGR, NCLR, font) to the palette-index
//! structs in src/hgss/hgss_gfx.h, mirroring the hgss_gfx.c it replaces.

use crate::bytes::{rd16, rd32};

pub const FOLLOW_FRAMES: usize = 8;
pub const FOLLOW_MAX: usize = 64;
pub const FONT_GLYPHS: usize = 512;

/// Mirrors of the structs in src/hgss/hgss_gfx.h (checked by tests/test_hgss_rs.c).
#[repr(C)]
pub struct FollowerGfx {
    pub size: u8,
    pub px: [[u8; FOLLOW_MAX * FOLLOW_MAX]; FOLLOW_FRAMES],
    pub pal: [[u16; 16]; 2],
}

#[repr(C)]
pub struct EmoteGfx {
    pub px: [[u8; 32 * 32]; 2],
    pub pal: [u16; 16],
}

#[repr(C)]
pub struct BallGfx {
    pub px: [u8; 16 * 16],
    pub pal: [u16; 16],
}

#[repr(C)]
pub struct BurstGfx {
    pub px: [u8; 32 * 32],
    pub pal: [u16; 16],
}

#[repr(C)]
pub struct FrameGfx {
    pub tile: [[u8; 64]; 18],
    pub pal: [u16; 16],
}

#[repr(C)]
pub struct Font {
    pub count: u16,
    pub width: [u8; FONT_GLYPHS],
    pub glyph: [[u8; 64]; FONT_GLYPHS],
    pub pal: [u16; 16],
}

/// True when [off, off + n) lies inside a buffer of `len` bytes.
fn in_range(off: usize, n: usize, len: usize) -> bool {
    off <= len && n <= len - off
}

#[derive(Clone, Copy, Default)]
struct Tex {
    off: usize,
    w: usize,
    h: usize,
    fmt: u8,
}

/// Dictionary at TEX0-relative `o`: (entry count, offset of the entries).
fn dict(t: &[u8], o: usize, unit: usize) -> Option<(usize, usize)> {
    if !in_range(o, 8, t.len()) {
        return None;
    }
    let count = usize::from(t[o + 1]);
    let data = o + usize::from(rd16(t, o + 6)?);
    if !in_range(data, 4, t.len()) || usize::from(rd16(t, data)?) != unit {
        return None;
    }
    if !in_range(data + 4, count * unit, t.len()) {
        return None;
    }
    Some((count, data + 4))
}

/// The TEX0 block that starts `t`: textures sorted by data offset, and palette
/// offsets. Every texture must use `fmt` (3 = 4bpp, 6 = A5I3) and each palette
/// needs `pal_bytes` bytes. Returns (texture count, palette count).
fn tex0(t: &[u8], fmt: u8, pal_bytes: usize, tex: &mut [Tex], pal: &mut [usize]) -> Option<(usize, usize)> {
    if t.len() < 0x3C {
        return None;
    }
    let tex_data = rd32(t, 0x14)? as usize;
    let pal_data = rd32(t, 0x38)? as usize;
    let (n, e) = dict(t, usize::from(rd16(t, 0x0E)?), 8)?;
    if n > tex.len() || tex_data > t.len() {
        return None;
    }
    for (i, x) in tex[..n].iter_mut().enumerate() {
        let p = rd16(t, e + 8 * i + 2)?;
        x.off = tex_data + (usize::from(rd16(t, e + 8 * i)?) << 3);
        x.w = 8usize << ((p >> 4) & 7);
        x.h = 8usize << ((p >> 7) & 7);
        x.fmt = ((p >> 10) & 7) as u8;
        let bytes = x.w * x.h / if fmt == 3 { 2 } else { 1 };
        if x.fmt != fmt || !in_range(x.off, bytes, t.len()) {
            return None;
        }
    }
    // Insertion sort like the C: n is at most 8, and a stable sort needs alloc.
    for i in 1..n {
        let mut j = i;
        while j > 0 && tex[j - 1].off > tex[j].off {
            tex.swap(j - 1, j);
            j -= 1;
        }
    }
    let (np, e) = dict(t, rd32(t, 0x34)? as usize, 4)?;
    if np > pal.len() || pal_data > t.len() {
        return None;
    }
    for (i, p) in pal[..np].iter_mut().enumerate() {
        *p = pal_data + (usize::from(rd16(t, e + 4 * i)?) << 3);
        if !in_range(*p, pal_bytes, t.len()) {
            return None;
        }
    }
    Some((n, np))
}

/// BTX0: the TEX0 offset sits at 0x10. Returns (TEX0 bytes, textures, palettes).
fn btx<'a>(b: &'a [u8], tex: &mut [Tex], pal: &mut [usize]) -> Option<(&'a [u8], usize, usize)> {
    if b.len() < 0x18 || !b.starts_with(b"BTX0") {
        return None;
    }
    let start = rd32(b, 0x10)? as usize;
    if !in_range(start, 0x3C, b.len()) {
        return None;
    }
    let t = &b[start..];
    let (n, np) = tex0(t, 3, 32, tex, pal)?;
    Some((t, n, np))
}

/// BMD0: block count at 0x0E, block offsets from 0x10; use the one tagged TEX0.
fn bmd<'a>(
    b: &'a [u8],
    fmt: u8,
    pal_bytes: usize,
    tex: &mut [Tex],
    pal: &mut [usize],
) -> Option<(&'a [u8], usize, usize)> {
    if b.len() < 0x10 || !b.starts_with(b"BMD0") {
        return None;
    }
    let blocks = usize::from(rd16(b, 0x0E)?);
    if blocks > 16 || !in_range(0x10, blocks * 4, b.len()) {
        return None;
    }
    for i in 0..blocks {
        let start = rd32(b, 0x10 + 4 * i)? as usize;
        if !in_range(start, 0x3C, b.len()) || !b[start..].starts_with(b"TEX0") {
            continue;
        }
        let t = &b[start..];
        let (n, np) = tex0(t, fmt, pal_bytes, tex, pal)?;
        return Some((t, n, np));
    }
    None
}

/// 4bpp to one palette index per byte, low nibble first. Fills all of `dst`.
fn unpack4(src: &[u8], dst: &mut [u8]) {
    for (pair, &b) in dst.chunks_exact_mut(2).zip(src) {
        pair[0] = b & 0x0F;
        pair[1] = b >> 4;
    }
}

fn read_pal(src: &[u8], dst: &mut [u16]) {
    for (d, c) in dst.iter_mut().zip(src.chunks_exact(2)) {
        *d = u16::from_le_bytes([c[0], c[1]]) & 0x7FFF;
    }
}

pub fn follower(b: &[u8], out: &mut FollowerGfx) -> Option<()> {
    let mut tex = [Tex::default(); FOLLOW_FRAMES];
    let mut pal = [0usize; 2];
    let (t, nt, np) = btx(b, &mut tex, &mut pal)?;
    if nt != FOLLOW_FRAMES || np < 1 {
        return None;
    }
    let s = tex[0].w;
    if (s != 32 && s != 64) || tex.iter().any(|x| x.w != s || x.h != s) {
        return None;
    }
    out.px.iter_mut().for_each(|f| f.fill(0));
    out.pal = [[0; 16]; 2];
    out.size = s as u8;
    for (k, x) in tex.iter().enumerate() {
        unpack4(t.get(x.off..)?, &mut out.px[k][..s * s]);
    }
    read_pal(t.get(pal[0]..)?, &mut out.pal[0]);
    read_pal(t.get(pal[if np > 1 { 1 } else { 0 }]..)?, &mut out.pal[1]);
    Some(())
}

pub fn emote(b: &[u8], out: &mut EmoteGfx) -> Option<()> {
    let mut tex = [Tex::default(); 2];
    let mut pal = [0usize; 1];
    let (t, nt, np) = btx(b, &mut tex, &mut pal)?;
    if nt < 1 || np != 1 || tex[..nt].iter().any(|x| x.w != 32 || x.h != 32) {
        return None;
    }
    unpack4(t.get(tex[0].off..)?, &mut out.px[0]);
    unpack4(t.get(tex[nt - 1].off..)?, &mut out.px[1]); // second frame, or the only one
    read_pal(t.get(pal[0]..)?, &mut out.pal);
    Some(())
}

pub fn frame(ncgr: &[u8], nclr: &[u8], out: &mut FrameGfx) -> Option<()> {
    if ncgr.len() < 0x30 + 18 * 32 || nclr.len() < 0x28 + 32 {
        return None;
    }
    if !ncgr.starts_with(b"RGCN") || !nclr.starts_with(b"RLCN") {
        return None;
    }
    for (tile, src) in out.tile.iter_mut().zip(ncgr[0x30..].chunks_exact(32)) {
        unpack4(src, tile);
    }
    read_pal(&nclr[0x28..], &mut out.pal);
    Some(())
}

pub fn font(f: &[u8], nclr: &[u8], out: &mut Font) -> Option<()> {
    if f.len() < 0x10 || nclr.len() < 0x28 + 32 || !nclr.starts_with(b"RLCN") {
        return None;
    }
    let hdr = rd32(f, 0)? as usize;
    let wstart = rd32(f, 4)? as usize;
    let count = usize::from(rd16(f, 8)?);
    if count == 0 || count > FONT_GLYPHS || f[0x0C..0x10] != [16, 16, 2, 2] {
        return None;
    }
    if !in_range(hdr, count * 64, f.len()) || !in_range(wstart, count, f.len()) {
        return None;
    }
    out.width.fill(0);
    out.glyph.iter_mut().for_each(|g| g.fill(0));
    out.pal = [0; 16];
    out.count = count as u16;
    for (g, src) in out.glyph.iter_mut().zip(f[hdr..hdr + count * 64].chunks_exact(64)) {
        g.copy_from_slice(src);
    }
    out.width[..count].copy_from_slice(&f[wstart..wstart + count]);
    read_pal(&nclr[0x28..], &mut out.pal);
    Some(())
}

/// 0 skip, 1 fg, 2 shadow, 3 bg for glyph pixel (x 0..15, y 0..15).
pub fn font_pixel(f: &Font, glyph: u16, x: u32, y: u32) -> u8 {
    if glyph >= f.count || x > 15 || y > 15 {
        return 0;
    }
    let Some(g) = f.glyph.get(usize::from(glyph)) else {
        return 0;
    };
    let (x, y) = (x as usize, y as usize);
    let base = 16 * ((y / 8) * 2 + x / 8);
    let (row, col) = (y % 8, x % 8);
    let byte = g[base + 2 * row + usize::from(col < 4)];
    (byte >> (6 - 2 * (col % 4))) & 3
}

pub fn ball(b: &[u8], out: &mut BallGfx) -> Option<()> {
    let mut tex = [Tex::default(); 1];
    let mut pal = [0usize; 1];
    let (t, nt, np) = bmd(b, 3, 16, &mut tex, &mut pal)?;
    if nt != 1 || np != 1 || tex[0].w != 16 || tex[0].h != 16 {
        return None;
    }
    out.px.fill(0);
    out.pal = [0; 16];
    unpack4(t.get(tex[0].off..)?, &mut out.px);
    // The retail ball palette holds 8 colours (16 bytes) at the end of TEX0.
    let colors = ((t.len() - pal[0]) / 2).min(16);
    read_pal(t.get(pal[0]..)?, &mut out.pal[..colors]);
    Some(())
}

pub fn burst(b: &[u8], out: &mut BurstGfx) -> Option<()> {
    let mut tex = [Tex::default(); 1];
    let mut pal = [0usize; 1];
    let (t, nt, np) = bmd(b, 6, 16, &mut tex, &mut pal)?;
    if nt != 1 || np != 1 || tex[0].w != 32 || tex[0].h != 32 {
        return None;
    }
    out.px.fill(0);
    out.pal = [0; 16];
    let src = t.get(tex[0].off..tex[0].off + 32 * 32)?;
    for (p, &s) in out.px.iter_mut().zip(src) {
        *p = if s >> 3 >= 16 { (s & 7) + 1 } else { 0 };
    }
    read_pal(t.get(pal[0]..)?, &mut out.pal[1..9]);
    Some(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::Rng;

    fn w16(b: &mut [u8], o: usize, v: u16) {
        b[o..o + 2].copy_from_slice(&v.to_le_bytes());
    }
    fn w32(b: &mut [u8], o: usize, v: u32) {
        b[o..o + 4].copy_from_slice(&v.to_le_bytes());
    }
    /// One dictionary at `o` (TEX0-relative), as in tests/test_hgss_gfx.c. Returns the entries offset.
    fn dict(t: &mut [u8], o: usize, count: u8, unit: u16) -> usize {
        t[o + 1] = count;
        w16(t, o + 6, 8);
        w16(t, o + 8, unit);
        w16(t, o + 10, 4 + unit * u16::from(count));
        o + 12
    }
    /// BTX0 with `nframes` 4bpp s x s textures (frame k: every pixel k + 1) and
    /// `npal` palettes (palette p colour c = p * 16 + c). Port of build_btx.
    fn build_btx(nframes: usize, s: usize, npal: usize) -> Vec<u8> {
        let mut b = vec![0u8; 0x8000];
        b[..4].copy_from_slice(b"BTX0");
        w32(&mut b, 0x10, 0x14);
        let t = &mut b[0x14..];
        let (tex_dict, pal_dict, tex_data) = (0x40usize, 0x100usize, 0x200usize);
        let frame_bytes = s * s / 2;
        let pal_data = tex_data + nframes * frame_bytes;
        w16(t, 0x0E, tex_dict as u16);
        w32(t, 0x14, tex_data as u32);
        w32(t, 0x34, pal_dict as u32);
        w32(t, 0x38, pal_data as u32);
        let te = dict(t, tex_dict, nframes as u8, 8);
        let lg: u16 = if s == 32 { 2 } else { 3 };
        for k in 0..nframes {
            w16(t, te + 8 * k, ((k * frame_bytes) >> 3) as u16);
            w16(t, te + 8 * k + 2, (lg << 4) | (lg << 7) | (3 << 10));
            let px = tex_data + k * frame_bytes;
            t[px..px + frame_bytes].fill(((k as u8 + 1) & 0xF) * 0x11);
        }
        let pe = dict(t, pal_dict, npal as u8, 4);
        for p in 0..npal {
            w16(t, pe + 4 * p, ((p * 32) >> 3) as u16);
            for c in 0..16 {
                w16(t, pal_data + p * 32 + c * 2, (p * 16 + c) as u16);
            }
        }
        b.truncate(0x14 + pal_data + npal * 32);
        b
    }
    /// BMD0 with an MDL0 stub then TEX0 holding one w x h texture. Port of build_bmd.
    fn build_bmd(w: usize, h: usize, fmt: u16, tex_bytes: usize, pal_colors: usize) -> Vec<u8> {
        let mut b = vec![0u8; 0x8000];
        b[..4].copy_from_slice(b"BMD0");
        w16(&mut b, 4, 0xFEFF);
        w16(&mut b, 0x0C, 0x10);
        w16(&mut b, 0x0E, 2);
        w32(&mut b, 0x10, 0x18);
        w32(&mut b, 0x14, 0x40);
        b[0x18..0x1C].copy_from_slice(b"MDL0");
        let t = &mut b[0x40..];
        t[..4].copy_from_slice(b"TEX0");
        let (tex_dict, pal_dict, tex_data) = (0x40usize, 0x100usize, 0x200usize);
        let pal_data = tex_data + tex_bytes;
        w16(t, 0x0E, tex_dict as u16);
        w32(t, 0x14, tex_data as u32);
        w32(t, 0x34, pal_dict as u32);
        w32(t, 0x38, pal_data as u32);
        let te = dict(t, tex_dict, 1, 8);
        let lw: u16 = if w == 16 { 1 } else { 2 };
        let lh: u16 = if h == 16 { 1 } else { 2 };
        w16(t, te, 0);
        w16(t, te + 2, (lw << 4) | (lh << 7) | (fmt << 10));
        let pe = dict(t, pal_dict, 1, 4);
        w16(t, pe, 0);
        for c in 0..pal_colors {
            w16(t, pal_data + c * 2, (0x100 + c) as u16);
        }
        b.truncate(0x40 + pal_data + pal_colors * 2);
        b
    }
    fn zeroed<T>() -> Box<T> {
        // Every gfx struct is plain u8/u16 arrays, for which all-zero is valid.
        unsafe { Box::new(std::mem::zeroed()) }
    }

    #[test]
    fn follower_and_emote_decode() {
        let mut g: Box<FollowerGfx> = zeroed();
        assert!(follower(&build_btx(8, 32, 2), &mut g).is_some());
        assert_eq!(g.size, 32);
        assert_eq!(g.px[0][0], 1);
        assert_eq!(g.px[7][31 * 32 + 31], 8);
        assert_eq!(g.pal[1][3], 19);
        assert!(follower(&build_btx(7, 32, 2), &mut g).is_none());
        let b = build_btx(8, 64, 1);
        assert!(follower(&b, &mut g).is_some());
        assert_eq!(g.size, 64);
        assert!(follower(&b[..b.len() - 4], &mut g).is_none());
        let mut e: Box<EmoteGfx> = zeroed();
        assert!(emote(&build_btx(2, 32, 1), &mut e).is_some());
        assert_eq!(e.px[1][5], 2);
        assert_eq!(e.pal[15], 15);
    }

    #[test]
    fn ball_and_burst_decode() {
        let mut b = build_bmd(16, 16, 3, 128, 8);
        b[0x240..0x2C0].fill(0x21);
        let mut g: Box<BallGfx> = zeroed();
        assert!(ball(&b, &mut g).is_some());
        assert_eq!((g.px[0], g.px[1]), (1, 2));
        assert_eq!(g.pal[5], 0x105);
        assert_eq!(g.pal[8], 0);
        let mut b = build_bmd(32, 32, 6, 1024, 8);
        b[0x241] = 16 << 3;
        b[0x242] = (31 << 3) | 7;
        let mut u: Box<BurstGfx> = zeroed();
        assert!(burst(&b, &mut u).is_some());
        assert_eq!(&u.px[..3], &[0, 1, 8]);
        assert_eq!(u.pal[8], 0x107);
        assert!(burst(&build_bmd(16, 16, 3, 128, 16), &mut u).is_none());
    }

    #[test]
    fn frame_and_font_decode() {
        let mut ncgr = vec![0u8; 0x30 + 0x240];
        let mut nclr = vec![0u8; 0x28 + 32];
        ncgr[..4].copy_from_slice(b"RGCN");
        nclr[..4].copy_from_slice(b"RLCN");
        ncgr[0x30 + 17 * 32 + 31] = 0x5A;
        w16(&mut nclr, 0x28 + 30, 0x7FFF);
        let mut f: Box<FrameGfx> = zeroed();
        assert!(frame(&ncgr, &nclr, &mut f).is_some());
        assert_eq!((f.tile[17][62], f.tile[17][63]), (0xA, 0x5));
        assert_eq!(f.pal[15], 0x7FFF);
        assert!(frame(&ncgr[..0x100], &nclr, &mut f).is_none());
        let mut font_data = vec![0u8; 0x10 + 2 * 64 + 2];
        w32(&mut font_data, 0, 0x10);
        w32(&mut font_data, 4, 0x10 + 128);
        w16(&mut font_data, 8, 2);
        font_data[0x0C..0x10].copy_from_slice(&[16, 16, 2, 2]);
        font_data[0x10 + 64] = 0b0100_0000; // glyph 1, sub-tile 0, row 0: pixel (4, 0) = 1
        font_data[0x10 + 128 + 1] = 9;
        let mut ft: Box<Font> = zeroed();
        assert!(font(&font_data, &nclr, &mut ft).is_some());
        assert_eq!(ft.count, 2);
        assert_eq!(ft.width[1], 9);
        assert_eq!(font_pixel(&ft, 1, 4, 0), 1);
        assert_eq!(font_pixel(&ft, 2, 0, 0), 0);
        assert_eq!(font_pixel(&ft, 1, 16, 0), 0);
    }

    #[test]
    fn garbage_never_panics() {
        let mut rng = Rng::new(0x6F78);
        let inputs = [build_btx(8, 32, 2), build_btx(2, 32, 1), build_bmd(16, 16, 3, 128, 8),
                      build_bmd(32, 32, 6, 1024, 8)];
        let (mut g, mut e): (Box<FollowerGfx>, Box<EmoteGfx>) = (zeroed(), zeroed());
        let (mut b, mut u): (Box<BallGfx>, Box<BurstGfx>) = (zeroed(), zeroed());
        let (mut f, mut ft): (Box<FrameGfx>, Box<Font>) = (zeroed(), zeroed());
        for _ in 0..5_000 {
            let mut data = inputs[rng.below(inputs.len())].clone();
            let flips = 1 + rng.below(8);
            rng.mutate(&mut data, flips);
            let cut = rng.below(data.len() + 1);
            let data = &data[..cut];
            let _ = follower(data, &mut g);
            let _ = emote(data, &mut e);
            let _ = ball(data, &mut b);
            let _ = burst(data, &mut u);
            let _ = frame(data, data, &mut f);
            let _ = font(data, data, &mut ft);
            let _ = font_pixel(&ft, rng.below(600) as u16, rng.below(20) as u32, rng.below(20) as u32);
        }
    }
}
