#ifndef HGSS_RS_H
#define HGSS_RS_H

#include <stdint.h>

/* Test-only probe into rust/hgss: the Rust side's size of a struct shared with
 * C, or the offset of an NdsRom field. 0 if the id is unknown. */
enum {
    HGSS_RS_STRVARS = 0,
    HGSS_RS_FOLLOWER = 1,
    HGSS_RS_EMOTE = 2,
    HGSS_RS_BALL = 3,
    HGSS_RS_BURST = 4,
    HGSS_RS_FRAME = 5,
    HGSS_RS_FONT = 6,
    HGSS_RS_NDSROM = 7,
    HGSS_RS_NDSSPAN = 8,
    HGSS_RS_NDSROM_SIZE = 100,
    HGSS_RS_NDSROM_CODE = 101,
    HGSS_RS_NDSROM_FNT = 102,
    HGSS_RS_NDSROM_FNT_SIZE = 103,
    HGSS_RS_NDSROM_FAT = 104,
    HGSS_RS_NDSROM_FAT_SIZE = 105,
};

uint32_t hgss_rs_layout(uint32_t what);

#endif // HGSS_RS_H
