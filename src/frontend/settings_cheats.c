#include "frontend/settings.h"
#include "cheat/cheat_decode.h"
#include "cheat/cheat_file.h"
#include "cheat/cheat_parse.h"
#include "cheat/cheat_report.h"
#include "cheat/cheat_session.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

#define PASTE_H   96.0f
#define FIELD_H   28.0f
#define ADD_W     110.0f
#define ROW_H     46.0f
#define DEVICE_W  150.0f
#define DELETE_W  64.0f

static const char* const k_devices[CHEAT_DEV_COUNT] = {
    "CodeBreaker", "GameShark v1/v2", "Action Replay v3", "GameShark v1/v2 (raw)", "Action Replay v3 (raw)",
};

static void toast(SettingsContext* ctx, ToastKind kind, const char* title, const char* detail) {
    if (ctx->toast) ctx->toast(ctx->user, kind, title, detail);
}

static void save(SettingsContext* ctx) {
    if (ctx->cht_path && !cheat_file_save(ctx->cheats, ctx->cht_path))
        toast(ctx, TOAST_ERROR, "Couldn't save cheats", "Changes last until you quit");
}

/* Re-reads the paste box: parse, detect (or check the forced device), fill the name. */
static void refresh_status(SettingsCheatsPage* p, SettingsContext* ctx) {
    static CheatRawList raw;
    static CheatOp ops[CHEAT_MAX_OPS];
    p->status[0] = '\0';
    p->status_kind = SETTINGS_STATUS_NONE;
    if (p->paste.len == 0) return;
    if (ctx->cheats->cheat_count >= CHEAT_MAX_CHEATS) {
        snprintf(p->status, sizeof(p->status), "The cheat list is full (%d cheats)", CHEAT_MAX_CHEATS);
        p->status_kind = SETTINGS_STATUS_ERROR;
        return;
    }
    char err[160];
    if (!cheat_parse_text(p->paste_buf, &raw, err, sizeof(err))) {
        snprintf(p->status, sizeof(p->status), "%s", err);
        p->status_kind = SETTINGS_STATUS_ERROR;
        return;
    }
    if (!p->name_edited) ui_text_area_set(&p->name, raw.name);

    const char* codes = raw.count == 1 ? "code" : "codes";
    if (p->forced != CHEAT_DEV_AUTO) {
        CheatDecodeInfo info;
        uint32_t n;
        if (!cheat_decode(&raw, p->forced, ops, CHEAT_MAX_OPS, &n, &info)) {
            snprintf(p->status, sizeof(p->status), "Not valid as %s: %s", k_devices[p->forced], info.fail);
            p->status_kind = SETTINGS_STATUS_ERROR;
            return;
        }
        p->detected = p->forced;
        snprintf(p->status, sizeof(p->status), "%u %s, device chosen:", (unsigned)raw.count, codes);
        p->status_kind = SETTINGS_STATUS_OK;
        return;
    }
    CheatDetect d;
    cheat_detect(&raw, ctx->cheats->game_code, &d);
    if (!d.confident) {
        snprintf(p->status, sizeof(p->status), "Not sure which device these are for.");
        p->status_kind = SETTINGS_STATUS_UNSURE;
        return;
    }
    p->detected = d.device;
    snprintf(p->status, sizeof(p->status), "%u %s, detected:", (unsigned)raw.count, codes);
    p->status_kind = SETTINGS_STATUS_OK;
}

static void clear_form(SettingsCheatsPage* p) {
    ui_text_area_set(&p->paste, "");
    ui_text_area_set(&p->name, "");
    p->name_edited = false;
    p->forced = CHEAT_DEV_AUTO;
    p->status[0] = '\0';
    p->status_kind = SETTINGS_STATUS_NONE;
}

static void report_enable(SettingsContext* ctx, CheatEnableResult r) {
    if (r == CHEAT_ENABLE_BACKED_UP) toast(ctx, TOAST_OK, "Save backed up", "before using cheats");
    if (r == CHEAT_ENABLE_BACKUP_FAILED) toast(ctx, TOAST_ERROR, "Couldn't back up save", "Cheat stays off");
}

static void add_cheat(SettingsCheatsPage* p, SettingsContext* ctx) {
    char err[160], name[CHEAT_NAME_LEN];
    snprintf(name, sizeof(name), "%s", p->name_buf);
    int32_t idx = cheat_add_text(ctx->cheats, p->paste_buf, name[0] ? name : NULL, p->forced, err, sizeof(err));
    if (idx < 0) {
        snprintf(p->status, sizeof(p->status), "%s", err);
        p->status_kind = SETTINGS_STATUS_ERROR;
        return;
    }
    /* Added cheats start on, but only through cheat_enable so the save is backed up first. */
    ctx->cheats->cheats[idx].enabled = false;
    CheatEnableResult r = cheat_enable(ctx->cheats, (uint32_t)idx, ctx->cart);
    report_enable(ctx, r);
    save(ctx);
    toast(ctx, TOAST_OK, "Cheat added", ctx->cheats->cheats[idx].name);
    clear_form(p);
    p->scroll_to_end = true;
}

static void draw_status(UiCanvas* c, UiInput* in, SettingsCheatsPage* p, float x, float y, float w) {
    if (p->status_kind == SETTINGS_STATUS_NONE) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, w, UI_ALIGN_LEFT, UI_DIM,
                "Codes can be CodeBreaker, GameShark or Action Replay, encrypted or not.");
        return;
    }
    uint32_t color = p->status_kind == SETTINGS_STATUS_OK ? UI_GOOD : p->status_kind == SETTINGS_STATUS_UNSURE ? UI_WARN : UI_BAD;
    float tw = ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, w, UI_ALIGN_LEFT, color, p->status);
    if (p->status_kind == SETTINGS_STATUS_ERROR) return;
    const char* label = p->status_kind == SETTINGS_STATUS_UNSURE ? "Pick device" : k_devices[p->detected];
    float cw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, label) + 18.0f;
    UiRect chip = {x + tw + 8.0f, y - 2.0f, cw, 18.0f};
    if (ui_chip(c, in, ui_id("cheat-add-device", 0), chip, label, p->status_kind == SETTINGS_STATUS_OK ? UI_ACCENT : UI_WARN)) {
        p->popup = SETTINGS_POPUP_ADD;
        p->popup_anchor = chip;
        in->popup_open = true;
    }
}

static void row_detail(const CheatEntry* e, char* out, size_t len) {
    out[0] = '\0';
    if (e->broken) {
        snprintf(out, len, "%s", e->error);
        return;
    }
    if (e->markers && e->unsupported)
        snprintf(out, len, "%u master code line%s, %u line%s not supported", (unsigned)e->markers,
                 e->markers == 1 ? "" : "s", (unsigned)e->unsupported, e->unsupported == 1 ? "" : "s");
    else if (e->markers)
        snprintf(out, len, "%u master code line%s", (unsigned)e->markers, e->markers == 1 ? "" : "s");
    else if (e->unsupported)
        snprintf(out, len, "%u line%s not supported", (unsigned)e->unsupported, e->unsupported == 1 ? "" : "s");
}

/* Draws the rows; returns true if this frame's mouse press hit the confirming Delete button. */
static bool draw_rows(UiCanvas* c, UiInput* in, SettingsCheatsPage* p, SettingsContext* ctx, UiRect list) {
    CheatEngine* eng = ctx->cheats;
    float content_h = ROW_H * (float)eng->cheat_count;
    if (p->scroll_to_end) {
        p->list_scroll = content_h - list.h;
        p->scroll_to_end = false;
    }
    ui_scroll(in, list, content_h, &p->list_scroll);
    bool confirm_hit = false;

    if (eng->cheat_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, list.x, list.y + 10.0f, list.w, UI_ALIGN_LEFT, UI_DIM,
                "No cheats yet. Paste codes from a cheat site above.");
        return false;
    }

    ui_canvas_clip(c, list);
    for (uint32_t i = 0; i < eng->cheat_count; i++) {
        float y = list.y + ROW_H * (float)i - p->list_scroll;
        if (y + ROW_H < list.y || y > list.y + list.h) continue;
        CheatEntry* e = &eng->cheats[i];
        UiRect row = {list.x, y + 2.0f, list.w, ROW_H - 4.0f};
        ui_fill_round_rect(c, row, 6.0f, UI_SURFACE);

        bool on = e->enabled && !e->broken;
        if (ui_toggle(c, in, settings_id_toggle(i), (UiRect){row.x + 10.0f, row.y, 34.0f, row.h}, &on, !e->broken)) {
            if (on) report_enable(ctx, cheat_enable(eng, i, ctx->cart));
            else e->enabled = false;
            save(ctx);
        }

        char detail[96];
        row_detail(e, detail, sizeof(detail));
        float tx = row.x + 52.0f, tw = row.w - 52.0f - DEVICE_W - DELETE_W - 24.0f;
        float name_y = detail[0] ? row.y + 6.0f : row.y + row.h / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, tx, name_y, tw, UI_ALIGN_LEFT, e->broken ? UI_WARN : UI_TEXT, e->name);
        if (detail[0])
            ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, tx, row.y + 23.0f, tw, UI_ALIGN_LEFT, e->broken ? UI_WARN : UI_DIM,
                    detail);

        UiRect dev = {row.x + row.w - DELETE_W - DEVICE_W - 16.0f, row.y + (row.h - 20.0f) / 2, DEVICE_W, 20.0f};
        if (ui_chip(c, in, settings_id_device(i), dev, e->broken ? "Pick device" : k_devices[e->device],
                    e->broken ? UI_WARN : UI_ACCENT)) {
            p->popup = (int32_t)i;
            p->popup_anchor = dev;
            in->popup_open = true;
        }

        bool confirming = p->confirm_delete == (int32_t)i;
        UiRect del = {row.x + row.w - DELETE_W - 8.0f, row.y + (row.h - 24.0f) / 2, DELETE_W, 24.0f};
        bool del_click = ui_button(c, in, settings_id_delete(i), del, confirming ? "Delete?" : "\xC3\x97", true, confirming);
        if (in->focus == settings_id_toggle(i) && ui_input_has_key(in, UI_KEY_DELETE)) del_click = true;
        if (del_click) {
            if (confirming) {
                char name[CHEAT_NAME_LEN];
                snprintf(name, sizeof(name), "%s", e->name);
                cheat_remove(eng, i);
                p->confirm_delete = -1;
                save(ctx);
                toast(ctx, TOAST_INFO, "Cheat deleted", name);
                break;   /* rows shifted; draw the rest next frame */
            }
            p->confirm_delete = (int32_t)i;
            confirm_hit = true;
        } else if (confirming && in->pressed && in->hot == settings_id_delete(i)) {
            confirm_hit = true;
        }
    }
    ui_canvas_unclip(c);
    return confirm_hit;
}

static void draw_popup(UiCanvas* c, UiInput* in, SettingsCheatsPage* p, SettingsContext* ctx) {
    if (p->popup == SETTINGS_POPUP_NONE) return;
    if (p->popup >= (int32_t)ctx->cheats->cheat_count) {   /* its row was deleted */
        p->popup = SETTINGS_POPUP_NONE;
        in->popup_open = false;
        return;
    }
    int32_t current = p->popup == SETTINGS_POPUP_ADD ? (p->forced == CHEAT_DEV_AUTO ? -1 : (int32_t)p->forced)
                                                     : (int32_t)ctx->cheats->cheats[p->popup].device;
    int32_t pick = ui_popup_list(c, in, p->popup_anchor, k_devices, CHEAT_DEV_COUNT, current);
    if (pick == UI_POPUP_NONE) return;
    if (pick >= 0) {
        if (p->popup == SETTINGS_POPUP_ADD) {
            p->forced = (CheatDevice)pick;
        } else {
            CheatEntry* e = &ctx->cheats->cheats[p->popup];
            bool was_broken = e->broken, wanted_on = e->enabled;
            if (cheat_set_device(ctx->cheats, (uint32_t)p->popup, (CheatDevice)pick)) {
                /* A repaired cheat that the file had on is turned on the safe way. */
                if (was_broken && wanted_on) report_enable(ctx, cheat_enable(ctx->cheats, (uint32_t)p->popup, ctx->cart));
                save(ctx);
                toast(ctx, TOAST_INFO, "Device changed", k_devices[pick]);
            } else {
                toast(ctx, TOAST_ERROR, "Those codes don't work as", k_devices[pick]);
            }
        }
    }
    p->popup = SETTINGS_POPUP_NONE;
    in->popup_open = false;
}

void settings_cheats_draw(UiCanvas* c, UiInput* in, SettingsCheatsPage* p, SettingsContext* ctx, UiRect r) {
    bool pressed = in->pressed;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, r.x, r.y, r.w - 40.0f, UI_ALIGN_LEFT, UI_TEXT, "Cheats");
    char sub[160];
    const char* file = ctx->cht_path ? ctx->cht_path : "";
    const char* base = strrchr(file, '/');
    snprintf(sub, sizeof(sub), "Paste codes from a cheat site. Changes save to %s.", base ? base + 1 : file);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 26.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            ctx->cht_path ? sub : "Paste codes from a cheat site.");

    float y = r.y + 50.0f;
    bool changed = ui_text_area(c, in, SETTINGS_ID_PASTE, (UiRect){r.x, y, r.w, PASTE_H}, &p->paste,
                                "Paste codes here (\xE2\x8C\x98V)", true);
    y += PASTE_H + 10.0f;
    /* Refresh before drawing the name and Add, so they match this keystroke. */
    if (changed) {
        if (p->paste.len == 0) p->forced = CHEAT_DEV_AUTO;
        refresh_status(p, ctx);
    }

    UiRect name_r = {r.x, y, r.w - ADD_W - 10.0f, FIELD_H};
    if (ui_text_area(c, in, SETTINGS_ID_NAME, name_r, &p->name, "Name (optional)", false)) {
        p->name_edited = p->name.len > 0;
    }
    bool can_add = p->status_kind == SETTINGS_STATUS_OK && ctx->cheats->cheat_count < CHEAT_MAX_CHEATS;
    if (ui_button(c, in, SETTINGS_ID_ADD, (UiRect){r.x + r.w - ADD_W, y, ADD_W, FIELD_H}, "Add cheat", can_add, true))
        add_cheat(p, ctx);
    y += FIELD_H + 10.0f;
    draw_status(c, in, p, r.x, y, r.w);
    y += 26.0f;

    ui_hline(c, r.x, y, r.w, UI_DIVIDER);
    y += 10.0f;
    char head[48];
    snprintf(head, sizeof(head), "YOUR CHEATS (%u)", (unsigned)ctx->cheats->cheat_count);
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, head);
    y += 20.0f;

    bool confirm_hit = draw_rows(c, in, p, ctx, (UiRect){r.x, y, r.w, r.y + r.h - y});
    if (pressed && !confirm_hit && p->popup == SETTINGS_POPUP_NONE) p->confirm_delete = -1;

    CheatDevice before = p->forced;
    draw_popup(c, in, p, ctx);
    if (p->forced != before) refresh_status(p, ctx);
}
