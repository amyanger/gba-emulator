#define _POSIX_C_SOURCE 200809L

#include "slot_picker.h"
#include "frontend.h"
#include "gba.h"
#include "savestate/savestate.h"
#include "common.h"
#include "frontend/overlay_draw.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>


#define COL_DIM_OVERLAY  0x80000000u   /* full-screen dim */
#define COL_PANEL_BG     0xFF001020u   /* opaque panel — see no-alpha-blend note in overlay_draw.c */
#define COL_BORDER       0xFF334466u
#define COL_LABEL        0xFF88AACCu
#define COL_VALUE        0xFFFFFFFFu
#define COL_HIGHLIGHT    0xFF00FFCCu
#define COL_DIM          0xFF445566u
#define COL_STATUS_ERR   0xFFFF6666u

bool slot_picker_is_open(const Frontend* fe) {
    return fe && fe->slot_picker.mode != SLOT_PICKER_CLOSED;
}

static void refresh_labels(Frontend* fe) {
    SlotPicker* p = &fe->slot_picker;
    char path[512];
    for (int i = 0; i < SLOT_PICKER_NUM_SLOTS; i++) {
        savestate_slot_path(fe->rom_path, i, path, sizeof(path));
        struct stat st;
        if (stat(path, &st) == 0) {
            p->slot_present[i] = true;
            p->mtimes[i] = (int64_t)st.st_mtime;
            if (!savestate_peek_label(path, p->labels[i],
                                       SLOT_PICKER_LABEL_LEN)) {
                p->labels[i][0] = '\0';
            }
        } else {
            p->slot_present[i] = false;
            p->mtimes[i] = 0;
            p->labels[i][0] = '\0';
        }
    }
}

void slot_picker_open_list(Frontend* fe) {
    if (!fe) return;
    SlotPicker* p = &fe->slot_picker;
    p->mode = SLOT_PICKER_LIST;
    p->cursor = fe->savestate_slot;
    p->status[0] = '\0';
    p->was_paused = fe->paused;
    fe->paused = true;
    refresh_labels(fe);
}

void slot_picker_open_label_edit(Frontend* fe) {
    if (!fe) return;
    SlotPicker* p = &fe->slot_picker;
    char path[512];
    savestate_slot_path(fe->rom_path, fe->savestate_slot,
                        path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0) {
        LOG_INFO("Slot %d empty - opening picker", fe->savestate_slot);
        slot_picker_open_list(fe);
        snprintf(fe->slot_picker.status, sizeof(fe->slot_picker.status),
                 "Slot %d empty - save with F5 first to label it",
                 fe->savestate_slot);
        return;
    }
    p->mode = SLOT_PICKER_LABEL_EDIT;
    p->edit_slot = fe->savestate_slot;
    p->was_paused = fe->paused;
    fe->paused = true;

    /* Pre-populate buffer with existing label. */
    char existing[SLOT_PICKER_LABEL_LEN] = {0};
    if (savestate_peek_label(path, existing, sizeof(existing))) {
        strncpy(p->edit_buf, existing, sizeof(p->edit_buf) - 1);
        p->edit_buf[sizeof(p->edit_buf) - 1] = '\0';
    } else {
        p->edit_buf[0] = '\0';
    }
    p->status[0] = '\0';
    SDL_StartTextInput();
}

void slot_picker_close(Frontend* fe) {
    if (!fe) return;
    SlotPicker* p = &fe->slot_picker;
    if (p->mode == SLOT_PICKER_LABEL_EDIT) {
        SDL_StopTextInput();
    }
    p->mode = SLOT_PICKER_CLOSED;
    fe->paused = p->was_paused;
    frontend_overlay_clear(fe);
}

/* Apply the edit buffer to the slot file without touching live GBA state.
 * Pure buffer path: read file, upgrade v5->v6 if needed, set label, write
 * back. The live GBA emulation state is never loaded or mutated. */
static bool commit_label(Frontend* fe, GBA* gba) {
    (void)gba; /* no longer needed */
    SlotPicker* p = &fe->slot_picker;
    char path[512];
    savestate_slot_path(fe->rom_path, p->edit_slot, path, sizeof(path));

    /* Read the file. */
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(p->status, sizeof(p->status), "Open failed (slot %d)", p->edit_slot);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }
    uint8_t* buf = (uint8_t*)malloc((size_t)size);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return false; }

    /* Determine version. The label API requires v6, so upgrade v5
     * in place via a pure buffer transform — no live GBA touch. */
    uint32_t version = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8)
                     | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);

    uint8_t* v6 = NULL;
    size_t v6_size = 0;
    if (version == 6) {
        /* Already v6 — the label can be set in place on the current buffer. */
        v6 = buf;
        v6_size = (size_t)size;
        buf = NULL;
    } else {
        SaveStateResult r = savestate_buffer_upgrade_v5(
            buf, (size_t)size, &v6, &v6_size);
        free(buf);
        if (r != SS_OK) {
            snprintf(p->status, sizeof(p->status),
                     "Upgrade failed (err=%d)", r);
            return false;
        }
    }

    if (!savestate_buffer_set_label(v6, v6_size, p->edit_buf)) {
        free(v6);
        snprintf(p->status, sizeof(p->status), "Set label failed");
        return false;
    }

    /* NOTE: not atomic; a torn write can leave the .ssN file zero-length,
     * losing the saved state. Matches the existing savestate_save behavior. */
    f = fopen(path, "wb");
    if (!f) {
        free(v6);
        snprintf(p->status, sizeof(p->status), "Write failed");
        return false;
    }
    size_t wrote = fwrite(v6, 1, v6_size, f);
    fclose(f);
    free(v6);
    if (wrote != v6_size) {
        snprintf(p->status, sizeof(p->status), "Short write");
        return false;
    }
    return true;
}

bool slot_picker_handle_event(Frontend* fe, GBA* gba, const SDL_Event* ev) {
    if (!fe || !ev) return false;
    SlotPicker* p = &fe->slot_picker;
    if (p->mode == SLOT_PICKER_CLOSED) return false;

    if (p->mode == SLOT_PICKER_LIST) {
        if (ev->type != SDL_KEYDOWN) return true; /* swallow */
        SDL_Scancode sc = ev->key.keysym.scancode;
        switch (sc) {
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_F7:
            slot_picker_close(fe);
            return true;
        case SDL_SCANCODE_UP:
            p->cursor = (p->cursor + SLOT_PICKER_NUM_SLOTS - 1)
                        % SLOT_PICKER_NUM_SLOTS;
            return true;
        case SDL_SCANCODE_DOWN:
            p->cursor = (p->cursor + 1) % SLOT_PICKER_NUM_SLOTS;
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (p->slot_present[p->cursor]) {
                fe->savestate_slot = p->cursor;
                fe->load_requested = true;
                slot_picker_close(fe);
            } else {
                snprintf(p->status, sizeof(p->status),
                         "Slot %d empty", p->cursor);
            }
            return true;
        default:
            return true; /* swallow other input */
        }
    }

    /* LABEL_EDIT mode. */
    if (ev->type == SDL_KEYDOWN) {
        SDL_Scancode sc = ev->key.keysym.scancode;
        if (sc == SDL_SCANCODE_ESCAPE) {
            slot_picker_close(fe);
            return true;
        }
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
            if (commit_label(fe, gba)) {
                slot_picker_close(fe);
            }
            return true;
        }
        if (sc == SDL_SCANCODE_BACKSPACE) {
            size_t n = strlen(p->edit_buf);
            if (n > 0) p->edit_buf[n - 1] = '\0';
            return true;
        }
        return true; /* swallow */
    }
    if (ev->type == SDL_TEXTINPUT) {
        size_t n = strlen(p->edit_buf);
        size_t cap = sizeof(p->edit_buf) - 1;
        for (const char* s = ev->text.text; *s && n < cap; s++) {
            unsigned char b = (unsigned char)*s;
            if (b < 0x20 || b == 0x7F) continue;
            p->edit_buf[n++] = b;
        }
        p->edit_buf[n] = '\0';
        return true;
    }
    return true;
}

void slot_picker_render(Frontend* fe) {
    if (!fe) return;
    SlotPicker* p = &fe->slot_picker;
    if (p->mode == SLOT_PICKER_CLOSED) return;

    uint32_t* b = fe->overlay_buffer;
    int bw = SCREEN_WIDTH, bh = SCREEN_HEIGHT;

    /* Full-screen dim (only one semi-transparent layer per frame). */
    overlay_draw_rect(b, bw, bh, 0, 0, bw, bh, COL_DIM_OVERLAY);

    /* Centered panel: leave 8 px margin on each side. Opaque so the
     * dim/panel interaction is clean. */
    int pw = bw - 16, ph = bh - 16;
    int px = (bw - pw) / 2, py = (bh - ph) / 2;
    overlay_draw_rect(b, bw, bh, px, py, pw, ph, COL_PANEL_BG);
    overlay_draw_rect_outline(b, bw, bh, px, py, pw, ph, COL_BORDER);

    if (p->mode == SLOT_PICKER_LIST) {
        overlay_draw_text(b, bw, bh, px + 4, py + 4,
                          "SAVE STATE SLOTS", COL_HIGHLIGHT);
        for (int i = 0; i < SLOT_PICKER_NUM_SLOTS; i++) {
            int row_y = py + 16 + i * 10;
            uint32_t color = (i == p->cursor) ? COL_HIGHLIGHT :
                             p->slot_present[i] ? COL_VALUE : COL_DIM;
            const char* lab = p->slot_present[i]
                              ? (p->labels[i][0] ? p->labels[i] : "(unnamed)")
                              : "(empty)";
            overlay_draw_textf(b, bw, bh, px + 6, row_y, color,
                               "%c%d  %-16s",
                               (i == p->cursor) ? '>' : ' ', i, lab);
            if (p->slot_present[i]) {
                /* Format mtime as YY-MM-DD HH:MM (right-aligned). */
                time_t t = (time_t)p->mtimes[i];
                struct tm tm_local;
#if defined(_WIN32)
                localtime_s(&tm_local, &t);
#else
                localtime_r(&t, &tm_local);
#endif
                char ts[20];
                strftime(ts, sizeof(ts), "%y-%m-%d %H:%M", &tm_local);
                overlay_draw_text(b, bw, bh, px + pw - 8 * 14, row_y,
                                  ts, COL_DIM);
            }
        }
        overlay_draw_text(b, bw, bh, px + 4, py + ph - 12,
                          "Enter=load  Esc=close", COL_LABEL);
    } else { /* LABEL_EDIT */
        overlay_draw_textf(b, bw, bh, px + 4, py + 4, COL_HIGHLIGHT,
                           "EDIT LABEL - SLOT %d", p->edit_slot);
        overlay_draw_text(b, bw, bh, px + 6, py + 24, p->edit_buf,
                          COL_VALUE);
        /* Cursor caret. */
        int caret_x = px + 6 + (int)strlen(p->edit_buf) * 8;
        overlay_draw_rect(b, bw, bh, caret_x, py + 24, 2, 8, COL_HIGHLIGHT);

        overlay_draw_text(b, bw, bh, px + 4, py + ph - 12,
                          "Enter=save  Esc=cancel", COL_LABEL);
    }

    if (p->status[0]) {
        overlay_draw_text(b, bw, bh, px + 4, py + ph - 22,
                          p->status, COL_STATUS_ERR);
    }

    fe->overlay_dirty = true;
}
