#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GBA GBA;

/* v3: PPU chunk size fixed (was overcounted by 36 bytes; v2 files silently
 * dropped APU/timer/IRQ/cart/input chunks on load). v2 files are rejected.
 * v4: SIO subsystem added; v3 files are rejected (they lack SIO state).
 * v5: EEPROM data + protocol state added to CART chunk; v4 rejected.
 * v6: 32-byte UTF-8 slot label appended after reserved field in the header
 *     (offset 0x30). Header grows from 48 to 80 bytes. v5 files still load
 *     with an empty label and upgrade to v6 on next save — labels are
 *     descriptive metadata, not runtime state, so the prior
 *     reject-old-versions rule does not apply. Versions <= v4 remain
 *     rejected. */
#define SAVESTATE_VERSION           6
#define SAVESTATE_VERSION_LEGACY_V5 5
#define SAVESTATE_LABEL_LEN         32  /* bytes including NUL terminator */
#define SAVESTATE_MAGIC   0x53414247  /* "GBAS" little-endian */
typedef enum {
    SS_OK,
    SS_ERR_FILE_OPEN,
    SS_ERR_FILE_WRITE,
    SS_ERR_FILE_READ,
    SS_ERR_BAD_MAGIC,
    SS_ERR_BAD_VERSION,
    SS_ERR_ROM_MISMATCH,
    SS_ERR_CORRUPT,
    SS_ERR_TRUNCATED
} SaveStateResult;

SaveStateResult savestate_save(GBA* gba, const char* path);
SaveStateResult savestate_load(GBA* gba, const char* path);
void savestate_slot_path(const char* rom_path, int32_t slot, char* out, size_t out_size);

/* Buffer-based variants — used by rewind and (internally) by the file API.
 * On success, savestate_save_to_buffer transfers ownership of *out to the
 * caller, who must release it with free(). */
SaveStateResult savestate_save_to_buffer(GBA* gba, uint8_t** out, size_t* out_size);
SaveStateResult savestate_load_from_buffer(GBA* gba, const uint8_t* buf, size_t size);

/* Read just the label out of an in-memory savestate buffer (or v5 buffer,
 * which always reports empty). Returns false if the buffer header is
 * malformed. The output is always NUL-terminated. If
 * out_size < SAVESTATE_LABEL_LEN, the label is truncated to out_size-1
 * bytes. */
bool savestate_buffer_peek_label(const uint8_t* buf, size_t size,
                                 char* out, size_t out_size);

/* Read the label from a savestate file without restoring CPU/PPU state. */
bool savestate_peek_label(const char* path, char* out, size_t out_size);

/* Set the label on an already-loaded buffer in place. The buffer MUST be
 * at v6. `label` must be non-NULL — pass "" to clear the existing label.
 * Filters control characters (< 0x20 and 0x7F) and truncates to 31
 * visible bytes (UTF-8 multi-byte continuation bytes are preserved).
 * Returns false if the buffer is not v6 or arguments are invalid. */
bool savestate_buffer_set_label(uint8_t* buf, size_t size, const char* label);

#endif // SAVESTATE_H
