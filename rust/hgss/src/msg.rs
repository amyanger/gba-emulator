//! Encrypted message banks and HGSS text, mirroring the hgss_msg.c it replaces.

use crate::bytes::{rd16, rd32};

pub const CH_END: u16 = 0xFFFF;
pub const CH_CTRL: u16 = 0xFFFE;
pub const CH_SPACE: u16 = 0x01DE;
pub const STRVARS: usize = 5;
pub const STRVAR_MAX: usize = 24;

/// Mirror of HgssStrVars in src/hgss/hgss_msg.h.
#[repr(C)]
pub struct StrVars {
    pub s: [[u16; STRVAR_MAX]; STRVARS],
}

pub fn count(bank: &[u8]) -> u16 {
    if bank.len() < 4 {
        0
    } else {
        rd16(bank, 0).unwrap_or(0)
    }
}

/// Entry `index` of an encrypted bank into `out`, CH_END-terminated.
/// Returns the length without the terminator.
pub fn decode(bank: &[u8], index: u32, out: &mut [u16]) -> Option<usize> {
    if index >= u32::from(count(bank)) || out.is_empty() {
        return None;
    }
    let idx = index as usize;
    if 4 + 8 * (idx + 1) > bank.len() {
        return None;
    }
    let i = index + 1;
    // The game's key schedule wraps in u32, so these multiplies wrap on purpose.
    let mut k = u32::from(rd16(bank, 2)?).wrapping_mul(765).wrapping_mul(i) & 0xFFFF;
    k |= k << 16;
    let off = (rd32(bank, 4 + 8 * idx)? ^ k) as usize;
    let len = (rd32(bank, 8 + 8 * idx)? ^ k) as usize;
    if off > bank.len() || len > (bank.len() - off) / 2 {
        return None;
    }
    let mut key = (i.wrapping_mul(596_947) & 0xFFFF) as u16;
    let mut w = 0;
    for c in 0..len {
        if w + 1 >= out.len() {
            break;
        }
        let ch = rd16(bank, off + 2 * c)? ^ key;
        key = key.wrapping_add(18_749);
        if ch == CH_END {
            break;
        }
        out[w] = ch;
        w += 1;
    }
    out[w] = CH_END;
    Some(w)
}

/// Replaces STRVAR control codes (cmd 0x0100-0x01FF, buffer = arg0 & 0xFF)
/// with `vars` and drops every other control code. Returns the length.
pub fn expand(input: &[u16], vars: &StrVars, out: &mut [u16]) -> usize {
    if out.is_empty() {
        return 0;
    }
    let n = input.len();
    let mut w = 0;
    let mut i = 0;
    while i < n && input[i] != CH_END {
        if input[i] == CH_CTRL {
            if i + 2 >= n {
                break;
            }
            let cmd = input[i + 1];
            let argc = usize::from(input[i + 2]);
            if (0x0100..=0x01FF).contains(&cmd) && argc >= 1 && i + 3 < n {
                let buf = usize::from(input[i + 3] & 0xFF);
                if let Some(var) = vars.s.get(buf) {
                    for &c in var {
                        if c == CH_END || w + 1 >= out.len() {
                            break;
                        }
                        out[w] = c;
                        w += 1;
                    }
                }
            }
            // The C did i += 2 + argc and then the for loop's i++.
            i += 3 + argc;
            continue;
        }
        if w + 1 < out.len() {
            out[w] = input[i];
            w += 1;
        }
        i += 1;
    }
    out[w] = CH_END;
    w
}

/// True if the line has a control code with cmd 0x0200 (YESNO).
pub fn has_yesno(input: &[u16]) -> bool {
    let n = input.len();
    let mut i = 0;
    while i + 1 < n && input[i] != CH_END {
        if input[i] == CH_CTRL {
            if input[i + 1] == 0x0200 {
                return true;
            }
            if i + 2 < n {
                i += 2 + usize::from(input[i + 2]);
            }
        }
        i += 1;
    }
    false
}

/// Gen 3 (Emerald) text byte to HGSS charcode, '?' (0x01AC) when unmapped.
/// Gen 3 charset: pokeemerald charmap.txt; HGSS: pokeheartgold charmap.txt.
pub fn char_from_gen3(c: u8) -> u16 {
    match c {
        0xBB..=0xD4 => 0x012B + u16::from(c - 0xBB), // A-Z
        0xD5..=0xEE => 0x0145 + u16::from(c - 0xD5), // a-z
        0xA1..=0xAA => 0x0121 + u16::from(c - 0xA1), // 0-9
        0x00 => CH_SPACE,
        0xAB => 0x01AB, // !
        0xAC => 0x01AC, // ?
        0xAD => 0x01AE, // .
        0xAE => 0x01BE, // -
        0xB0 => 0x01AF, // ellipsis
        0xB1 => 0x01B4, // left double quote
        0xB2 => 0x01B5, // right double quote
        0xB3 => 0x01B2, // left single quote
        0xB4 => 0x01B3, // right single quote
        0xB5 => 0x01BB, // male sign
        0xB6 => 0x01BC, // female sign
        0xB8 => 0x01AD, // ,
        0xBA => 0x01B1, // /
        0xF0 => 0x01C4, // :
        _ => 0x01AC,
    }
}

/// Gen 3 string (ends at 0xFF or the end of `src`) to HGSS charcodes, CH_END-terminated.
pub fn str_from_gen3(src: &[u8], dst: &mut [u16]) {
    if dst.is_empty() {
        return;
    }
    let mut w = 0;
    for &c in src {
        if c == 0xFF || w + 1 >= dst.len() {
            break;
        }
        dst[w] = char_from_gen3(c);
        w += 1;
    }
    dst[w] = CH_END;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::Rng;

    /// Port of build_bank in tests/test_hgss_msg.c (seed 0x1234).
    fn build_bank(lines: &[&[u16]]) -> Vec<u8> {
        let seed: u32 = 0x1234;
        let mut bank = vec![0u8; 4 + 8 * lines.len()];
        bank[0..2].copy_from_slice(&(lines.len() as u16).to_le_bytes());
        bank[2..4].copy_from_slice(&(seed as u16).to_le_bytes());
        for (n, line) in lines.iter().enumerate() {
            let i = n as u32 + 1;
            let mut k = seed.wrapping_mul(765).wrapping_mul(i) & 0xFFFF;
            k |= k << 16;
            let off = bank.len() as u32;
            bank[4 + 8 * n..8 + 8 * n].copy_from_slice(&(off ^ k).to_le_bytes());
            bank[8 + 8 * n..12 + 8 * n].copy_from_slice(&(line.len() as u32 ^ k).to_le_bytes());
            let mut key = (i.wrapping_mul(596_947) & 0xFFFF) as u16;
            for &c in line.iter() {
                bank.extend_from_slice(&(c ^ key).to_le_bytes());
                key = key.wrapping_add(18_749);
            }
        }
        bank
    }

    fn vars() -> StrVars {
        StrVars { s: [[CH_END; STRVAR_MAX]; STRVARS] }
    }

    #[test]
    fn decode_round_trip() {
        let bank = build_bank(&[&[0x012B, 0x0145, CH_END], &[0x0121, 0xE000, 0x0122, CH_END]]);
        assert_eq!(count(&bank), 2);
        let mut out = [0u16; 16];
        assert_eq!(decode(&bank, 1, &mut out), Some(3));
        assert_eq!(out[1], 0xE000);
        assert_eq!(out[3], CH_END);
        assert_eq!(decode(&bank, 2, &mut out), None);
        assert_eq!(decode(&bank[..10], 0, &mut out), None);
        assert_eq!(decode(&bank, 0, &mut []), None);
    }

    #[test]
    fn expand_fills_strvars_and_drops_other_ctrl() {
        let mut v = vars();
        v.s[0][..3].copy_from_slice(&[0x012B, 0x012C, CH_END]);
        let input = [CH_CTRL, 0x0101, 1, 0, 0x01AB, CH_CTRL, 0x0300, 1, 7, 0x01AC, CH_END];
        let mut out = [0u16; 16];
        assert_eq!(expand(&input[..10], &v, &mut out), 4);
        assert_eq!(&out[..5], &[0x012B, 0x012C, 0x01AB, 0x01AC, CH_END]);
        assert!(has_yesno(&[0x012B, CH_CTRL, 0x0200, 0]));
        assert!(!has_yesno(&input[..10]));
        assert_eq!(expand(&input, &v, &mut []), 0);
    }

    #[test]
    fn gen3_strings_stop_at_ff_and_cap() {
        assert_eq!(char_from_gen3(0xBB), 0x012B);
        assert_eq!(char_from_gen3(0xEE), 0x015E);
        assert_eq!(char_from_gen3(0x00), CH_SPACE);
        assert_eq!(char_from_gen3(0x7F), 0x01AC);
        let mut out = [0u16; 8];
        str_from_gen3(&[0xCA, 0xDD, 0xFF, 0xD5], &mut out);
        assert_eq!(&out[..3], &[0x013A, 0x014D, CH_END]);
        str_from_gen3(&[0xBB; 20], &mut out[..3]);
        assert_eq!(&out[..3], &[0x012B, 0x012B, CH_END]);
    }

    #[test]
    fn garbage_never_panics() {
        let mut rng = Rng::new(0x5EED);
        let good = build_bank(&[&[0x012B, CH_CTRL, 0x0101, 1, 0, CH_END], &[0x0121; 40]]);
        let v = vars();
        let mut out = [0u16; 64];
        for _ in 0..20_000 {
            let mut bank = if rng.below(2) == 0 {
                good.clone()
            } else {
                (0..rng.below(200)).map(|_| rng.next_u64() as u8).collect()
            };
            if !bank.is_empty() {
                let flips = 1 + rng.below(6);
                rng.mutate(&mut bank, flips);
            }
            let cap = rng.below(out.len() + 1);
            let _ = count(&bank);
            let _ = decode(&bank, rng.below(4) as u32, &mut out[..cap]);
            let input: Vec<u16> = (0..rng.below(40))
                .map(|_| match rng.below(4) {
                    0 => CH_CTRL,
                    1 => 0x0100 + rng.below(0x100) as u16,
                    2 => rng.below(8) as u16,
                    _ => rng.next_u64() as u16,
                })
                .collect();
            let _ = expand(&input, &v, &mut out[..cap]);
            let _ = has_yesno(&input);
        }
    }
}
