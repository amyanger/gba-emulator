#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GBA GBA;

#define SAVESTATE_VERSION 2
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

#endif // SAVESTATE_H
