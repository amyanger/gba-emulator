#ifndef MP2K_DETECT_H
#define MP2K_DETECT_H

#include <stdbool.h>
#include <stdint.h>

/* Finds the MP2K (Sappy/m4a) sound driver in a ROM. SoundMain's first 48 bytes
 * are position independent and identical across games built with this SDK
 * revision; they are matched by CRC32 so no driver code lives in the repo.
 * Literal pool at SoundMain+0x6C: SOUND_INFO_PTR, ID_NUMBER, SoundMainRAM|1. */

#define MP2K_ID_NUMBER 0x68736D53u
#define MP2K_SOUNDMAIN_CRC 0x27EA7FCFu

typedef struct {
    uint32_t sound_main;      /* GBA address of SoundMain */
    uint32_t sound_info_ptr;  /* IWRAM word holding the SoundInfo pointer */
    uint32_t hook_addr;       /* first Thumb instruction of SoundMainRAM in IWRAM */
} Mp2kDriver;

bool mp2k_detect(const uint8_t* rom, uint32_t size, Mp2kDriver* out);
bool mp2k_detect_crc(const uint8_t* rom, uint32_t size, uint32_t crc, Mp2kDriver* out);

#endif // MP2K_DETECT_H
