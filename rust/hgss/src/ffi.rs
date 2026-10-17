//! The C ABI. Every function here is declared in a header under src/hgss and
//! must keep that exact signature.
use core::ffi::{c_char, c_void, CStr};
use core::mem::{offset_of, size_of};
use core::slice;

use crate::{gfx, msg, nds, patch};

/// # Safety
/// `p` points to `len` readable bytes, or is null.
unsafe fn bytes<'a>(p: *const u8, len: u32) -> &'a [u8] {
    if p.is_null() || len == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, len as usize)
    }
}

/// # Safety
/// `p` points to `n` readable u16s, or is null.
unsafe fn units<'a>(p: *const u16, n: u32) -> &'a [u16] {
    if p.is_null() || n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n as usize)
    }
}

/// # Safety
/// `p` points to `n` writable u16s, or is null.
unsafe fn units_mut<'a>(p: *mut u16, n: u32) -> &'a mut [u16] {
    if p.is_null() || n == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(p, n as usize)
    }
}

/// Layout probe for tests/test_hgss_rs.c; ids are in src/hgss/hgss_rs.h.
#[no_mangle]
pub extern "C" fn hgss_rs_layout(what: u32) -> u32 {
    let v = match what {
        0 => size_of::<msg::StrVars>(),
        1 => size_of::<gfx::FollowerGfx>(),
        2 => size_of::<gfx::EmoteGfx>(),
        3 => size_of::<gfx::BallGfx>(),
        4 => size_of::<gfx::BurstGfx>(),
        5 => size_of::<gfx::FrameGfx>(),
        6 => size_of::<gfx::Font>(),
        7 => size_of::<NdsRom>(),
        8 => size_of::<nds::Span>(),
        100 => offset_of!(NdsRom, size),
        101 => offset_of!(NdsRom, code),
        102 => offset_of!(NdsRom, fnt),
        103 => offset_of!(NdsRom, fnt_size),
        104 => offset_of!(NdsRom, fat),
        105 => offset_of!(NdsRom, fat_size),
        _ => 0,
    };
    v as u32
}

/// # Safety
/// `bank` points to `bank_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn hgss_msg_count(bank: *const u8, bank_len: u32) -> u16 {
    msg::count(bytes(bank, bank_len))
}

/// # Safety
/// `bank` points to `bank_len` bytes, `out` to `cap` u16s, `n` to one u32.
#[no_mangle]
pub unsafe extern "C" fn hgss_msg_decode(
    bank: *const u8,
    bank_len: u32,
    index: u32,
    out: *mut u16,
    cap: u32,
    n: *mut u32,
) -> bool {
    if n.is_null() {
        return false;
    }
    match msg::decode(bytes(bank, bank_len), index, units_mut(out, cap)) {
        Some(w) => {
            *n = w as u32;
            true
        }
        None => false,
    }
}

/// # Safety
/// `input` points to `n` u16s, `vars` to one HgssStrVars, `out` to `cap` u16s.
#[no_mangle]
pub unsafe extern "C" fn hgss_msg_expand(
    input: *const u16,
    n: u32,
    vars: *const msg::StrVars,
    out: *mut u16,
    cap: u32,
) -> u32 {
    if vars.is_null() {
        return 0;
    }
    msg::expand(units(input, n), &*vars, units_mut(out, cap)) as u32
}

/// # Safety
/// `input` points to `n` u16s.
#[no_mangle]
pub unsafe extern "C" fn hgss_msg_has_yesno(input: *const u16, n: u32) -> bool {
    msg::has_yesno(units(input, n))
}

#[no_mangle]
pub extern "C" fn hgss_char_from_gen3(c: u8) -> u16 {
    msg::char_from_gen3(c)
}

/// # Safety
/// `src` is readable up to its first 0xFF or `len` bytes, whichever comes
/// first; `dst` points to `cap` u16s.
#[no_mangle]
pub unsafe extern "C" fn hgss_str_from_gen3(src: *const u8, len: u32, dst: *mut u16, cap: u32) {
    // `len` is only a maximum and callers pass more than the buffer holds, so
    // never form a slice of `len` bytes. Scan exactly the bytes the C read.
    let limit = len.min(cap) as usize;
    let mut n = 0;
    while !src.is_null() && n < limit && *src.add(n) != 0xFF {
        n += 1;
    }
    msg::str_from_gen3(bytes(src, n as u32), units_mut(dst, cap));
}

/// # Safety
/// `btx` points to `len` bytes, `out` to one HgssFollowerGfx.
#[no_mangle]
pub unsafe extern "C" fn hgss_gfx_follower(btx: *const u8, len: u32, out: *mut gfx::FollowerGfx) -> bool {
    !out.is_null() && gfx::follower(bytes(btx, len), &mut *out).is_some()
}

/// # Safety
/// `btx` points to `len` bytes, `out` to one HgssEmoteGfx.
#[no_mangle]
pub unsafe extern "C" fn hgss_gfx_emote(btx: *const u8, len: u32, out: *mut gfx::EmoteGfx) -> bool {
    !out.is_null() && gfx::emote(bytes(btx, len), &mut *out).is_some()
}

/// # Safety
/// `bmd` points to `len` bytes, `out` to one HgssBallGfx.
#[no_mangle]
pub unsafe extern "C" fn hgss_gfx_ball(bmd: *const u8, len: u32, out: *mut gfx::BallGfx) -> bool {
    !out.is_null() && gfx::ball(bytes(bmd, len), &mut *out).is_some()
}

/// # Safety
/// `bmd` points to `len` bytes, `out` to one HgssBurstGfx.
#[no_mangle]
pub unsafe extern "C" fn hgss_gfx_burst(bmd: *const u8, len: u32, out: *mut gfx::BurstGfx) -> bool {
    !out.is_null() && gfx::burst(bytes(bmd, len), &mut *out).is_some()
}

/// # Safety
/// `ncgr`/`nclr` point to their lengths in bytes, `out` to one HgssFrameGfx.
#[no_mangle]
pub unsafe extern "C" fn hgss_gfx_frame(
    ncgr: *const u8,
    ncgr_len: u32,
    nclr: *const u8,
    nclr_len: u32,
    out: *mut gfx::FrameGfx,
) -> bool {
    !out.is_null() && gfx::frame(bytes(ncgr, ncgr_len), bytes(nclr, nclr_len), &mut *out).is_some()
}

/// # Safety
/// `font`/`nclr` point to their lengths in bytes, `out` to one HgssFont.
#[no_mangle]
pub unsafe extern "C" fn hgss_gfx_font(
    font: *const u8,
    len: u32,
    nclr: *const u8,
    nclr_len: u32,
    out: *mut gfx::Font,
) -> bool {
    !out.is_null() && gfx::font(bytes(font, len), bytes(nclr, nclr_len), &mut *out).is_some()
}

/// # Safety
/// `f` points to one HgssFont.
#[no_mangle]
pub unsafe extern "C" fn hgss_font_pixel(f: *const gfx::Font, glyph: u16, x: u32, y: u32) -> u8 {
    if f.is_null() {
        0
    } else {
        gfx::font_pixel(&*f, glyph, x, y)
    }
}

/// Mirror of NdsRom in src/hgss/nds_fs.h. `f` and `code` are only touched by C.
#[repr(C)]
#[allow(dead_code)]
pub struct NdsRom {
    f: *mut c_void,
    size: u32,
    code: [c_char; 5],
    fnt: u32,
    fnt_size: u32,
    fat: u32,
    fat_size: u32,
}

extern "C" {
    fn nds_read(r: *const NdsRom, off: u32, dst: *mut c_void, len: u32) -> bool;
}

struct Rom<'a>(&'a NdsRom);

impl nds::Source for Rom<'_> {
    fn size(&self) -> u32 {
        self.0.size
    }
    fn read(&self, off: u32, dst: &mut [u8]) -> bool {
        let Ok(len) = u32::try_from(dst.len()) else {
            return false;
        };
        unsafe { nds_read(self.0, off, dst.as_mut_ptr().cast(), len) }
    }
}

fn tables(r: &NdsRom) -> nds::Tables {
    nds::Tables { fnt: r.fnt, fnt_size: r.fnt_size, fat: r.fat, fat_size: r.fat_size }
}

/// # Safety
/// `r` is an NdsRom opened by nds_open, `path` a NUL-terminated string, `out` one NdsSpan.
#[no_mangle]
pub unsafe extern "C" fn nds_find(r: *const NdsRom, path: *const c_char, out: *mut nds::Span) -> bool {
    if r.is_null() || path.is_null() || out.is_null() {
        return false;
    }
    let r = &*r;
    match nds::find(&Rom(r), &tables(r), CStr::from_ptr(path).to_bytes()) {
        Some(s) => {
            *out = s;
            true
        }
        None => false,
    }
}

/// # Safety
/// `r` is an NdsRom opened by nds_open, `count` one u16.
#[no_mangle]
pub unsafe extern "C" fn narc_count(r: *const NdsRom, narc: nds::Span, count: *mut u16) -> bool {
    if r.is_null() || count.is_null() {
        return false;
    }
    match nds::narc_count(&Rom(&*r), narc) {
        Some(c) => {
            *count = c;
            true
        }
        None => false,
    }
}

/// # Safety
/// `r` is an NdsRom opened by nds_open, `out` one NdsSpan.
#[no_mangle]
pub unsafe extern "C" fn narc_member(r: *const NdsRom, narc: nds::Span, index: u32, out: *mut nds::Span) -> bool {
    if r.is_null() || out.is_null() {
        return false;
    }
    match nds::narc_member(&Rom(&*r), narc, index) {
        Some(s) => {
            *out = s;
            true
        }
        None => false,
    }
}

/// Returns 0 and sets `*out_len`, or a ROM_PATCH_* error code.
///
/// # Safety
/// Each pointer is null or points to its length in bytes (`out_len` to one
/// u32). `out` must not overlap `src` or `patch`.
#[no_mangle]
pub unsafe extern "C" fn rom_patch_apply(
    src: *const u8,
    src_len: u32,
    patch: *const u8,
    patch_len: u32,
    out: *mut u8,
    out_cap: u32,
    out_len: *mut u32,
) -> u32 {
    if patch.is_null() {
        return patch::Error::NotPatch as u32;
    }
    if src.is_null() || out.is_null() || out_len.is_null() {
        return patch::Error::Damaged as u32;
    }
    let out = slice::from_raw_parts_mut(out, out_cap as usize);
    match patch::apply(bytes(src, src_len), bytes(patch, patch_len), out) {
        Ok(n) => {
            *out_len = n as u32;
            0
        }
        Err(e) => e as u32,
    }
}
