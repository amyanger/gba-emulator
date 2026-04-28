#ifndef SLOT_PICKER_H
#define SLOT_PICKER_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

typedef struct Frontend Frontend;
typedef struct GBA GBA;

#define SLOT_PICKER_LABEL_LEN 32  /* matches SAVESTATE_LABEL_LEN */
#define SLOT_PICKER_NUM_SLOTS 10

typedef enum {
    SLOT_PICKER_CLOSED,
    SLOT_PICKER_LIST,         /* F7: navigating slots */
    SLOT_PICKER_LABEL_EDIT,   /* F6: typing a label */
} SlotPickerMode;

typedef struct SlotPicker {
    SlotPickerMode mode;
    int32_t cursor;                    /* highlighted slot in LIST mode */
    int32_t edit_slot;                 /* slot being labeled in LABEL_EDIT */
    char edit_buf[SLOT_PICKER_LABEL_LEN];
    char labels[SLOT_PICKER_NUM_SLOTS][SLOT_PICKER_LABEL_LEN];
    int64_t mtimes[SLOT_PICKER_NUM_SLOTS];   /* unix epoch; 0 if empty */
    bool slot_present[SLOT_PICKER_NUM_SLOTS];
    char status[64];                   /* ephemeral status / error line */
    bool was_paused;                   /* prior pause state to restore on close */
} SlotPicker;

/* Open the slot list (F7). Reads slot label/presence from disk. */
void slot_picker_open_list(Frontend* fe);

/* Open the label-edit modal for the current slot (F6). No-op (with
 * status message) if the slot file does not yet exist. */
void slot_picker_open_label_edit(Frontend* fe);

/* Close any open modal and restore prior pause state. */
void slot_picker_close(Frontend* fe);

/* Returns true if a modal is open (gameplay input should be suppressed
 * and emulation should pause). */
bool slot_picker_is_open(const Frontend* fe);

/* Event dispatch. Returns true if the event was consumed (caller should
 * skip default handling). gba is needed for label commit (we load,
 * re-save with label). */
bool slot_picker_handle_event(Frontend* fe, GBA* gba, const SDL_Event* ev);

/* Per-frame render into fe->overlay_buffer. */
void slot_picker_render(Frontend* fe);

#endif /* SLOT_PICKER_H */
