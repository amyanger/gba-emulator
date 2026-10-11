#include "frontend.h"
#include "frame_advance.h"
#include "input_display.h"
#include "slot_picker.h"
#include "frontend/hud_draw.h"
#include "frontend/overworld_draw.h"
#include "frontend/macos_menu.h"
#include "gba.h"
#include "apu/apu.h"
#include "input/input.h"
#include "input/keymap.h"
#include "frontend/rom_picker.h"
#include "game/encounters.h"
#include "game/follower_ctx.h"
#include "game/follower_places.h"
#include "ui/ui_theme.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
#endif

/* SDL reads these at video init, so call before the first SDL_Init. */
void frontend_set_hints(void) {
    /* Per-monitor DPI awareness so Windows does not bitmap-stretch the window.
     * With DPI scaling on, SDL reports window sizes in points like macOS. */
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
#ifdef _WIN32
    /* Use the exe's icon resource (ID 1, res/gba_emulator.rc) for the window. */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
}

bool frontend_init(Frontend* fe, int scale) {
    memset(fe, 0, sizeof(*fe));
    frame_pacer_reset(&fe->pacer);
    fe->volume = 100;
    toast_init(&fe->toasts);
    settings_init(&fe->settings);

    frontend_set_hints();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        LOG_ERROR("SDL init failed: %s", SDL_GetError());
        return false;
    }

    fe->scale = scale;
    fe->window = SDL_CreateWindow("GBA Emulator", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH * scale,
                                  SCREEN_HEIGHT * scale,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!fe->window) {
        LOG_ERROR("SDL window creation failed: %s", SDL_GetError());
        return false;
    }

    fe->renderer = SDL_CreateRenderer(fe->window, -1, SDL_RENDERER_ACCELERATED);
    if (!fe->renderer) {
        LOG_ERROR("SDL renderer creation failed: %s", SDL_GetError());
        return false;
    }
    /* Rects are computed in drawable pixels, so logical scaling stays off. */
    SDL_RenderSetLogicalSize(fe->renderer, 0, 0);

    // 15-bit color texture (ABGR1555)
    fe->texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ABGR1555,
                                    SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!fe->texture) {
        LOG_ERROR("SDL texture creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(fe->texture, SDL_ScaleModeNearest);

    fe->overlay_texture = SDL_CreateTexture(fe->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!fe->overlay_texture) {
        LOG_ERROR("Failed to create overlay texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(fe->overlay_texture, SDL_BLENDMODE_BLEND);

    fe->overlay_buffer = (uint32_t*)calloc(SCREEN_WIDTH * SCREEN_HEIGHT,
                                           sizeof(uint32_t));
    if (!fe->overlay_buffer) {
        LOG_ERROR("Failed to allocate overlay buffer");
        return false;
    }
    fe->overlay_dirty = false;

    /* The panel texture and buffer are sized by frontend_apply_layout. */
    fe->panel_visible = false;
    fe->panel_page = GAME_PAGE_AUTO;

    fe->running = true;
    fe->savestate_slot = 0;
    fe->save_requested = false;
    fe->load_requested = false;

    fe->paused = false;
    fe->step_pending = false;
    fe->input_display_enabled = false;
    fe->ff_hold = false;
    fe->ff_toggle = false;
    fe->ff_frame_skip = 4;
    fe->ff_frame_count = 0;
    fe->ff_speed = 3;
    fe->pause_on_blur = false;
    fe->blur_paused = false;
    fe->had_focus = false;
    fe->link_active = false;

#ifdef ENABLE_REWIND
    fe->rewind_hold = false;
#endif

    fe->fullscreen = false;
    fe->muted = false;
    fe->audio_device = 0;
    fe->audio_target_bytes = 0;
    fe->audio_stopped = true;
    fe->kb_keys = 0;
    fe->pad_keys = 0;
    fe->stick_keys = 0;
    fe->turbo_frame = 0;
    fe->lt_down = false;
    fe->rt_down = false;
    fe->keymap_path = NULL;
    fe->slot_picker.mode = SLOT_PICKER_CLOSED;
    frontend_apply_layout(fe);
    fe->settings_event = SDL_RegisterEvents(1);
    if (fe->settings_event != (uint32_t)-1) macos_menu_install_settings(fe->settings_event);

    fe->controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            fe->controller = SDL_GameControllerOpen(i);
            if (fe->controller) {
                LOG_INFO("Controller connected: %s",
                         SDL_GameControllerName(fe->controller));
                frontend_toast(fe, TOAST_INFO, "Controller connected",
                               SDL_GameControllerName(fe->controller));
                break;
            }
        }
    }

    LOG_INFO("Frontend initialized (%dx scale)", scale);
    return true;
}

/* Fills the density and display fields of a layout input for the window's current display. */
static void frontend_measure(Frontend* fe, LayoutInput* in) {
    int win_w = 0, win_h = 0, out_w = 0, out_h = 0;
    SDL_GetWindowSize(fe->window, &win_w, &win_h);
    SDL_GetRendererOutputSize(fe->renderer, &out_w, &out_h);
    in->pt_to_px = (win_w > 0 && out_w > 0) ? (float)out_w / (float)win_w : 1.0f;
    /* Snap to 1/8 so a measured 2.2498 is treated as 2.25. */
    in->pt_to_px = (float)(int)(in->pt_to_px * 8.0f + 0.5f) / 8.0f;
    if (in->pt_to_px <= 0.0f) in->pt_to_px = 1.0f;

    /* X11 has no point/pixel split, so the only density hint is DPI. SDL2 derives
     * X11 DPI from the monitor's physical size, not from the desktop scale setting,
     * so ordinary monitors report noisy values around 1.0-1.5x. Trust it only when
     * it clearly indicates a high-DPI screen (>= 1.75x); otherwise use 1.0. */
    in->os_scale = 1.0f;
    int display = SDL_GetWindowDisplayIndex(fe->window);
    if (display < 0) display = 0;
    const char* driver = SDL_GetCurrentVideoDriver();
    float hdpi = 0.0f;
    if (driver && strcmp(driver, "x11") == 0 &&
        SDL_GetDisplayDPI(display, NULL, &hdpi, NULL) == 0 && hdpi > 0.0f) {
        float s = (float)(int)(hdpi / 96.0f * 4.0f + 0.5f) / 4.0f;
        if (s > 3.0f) s = 3.0f;
        if (s >= 1.75f) in->os_scale = s;
    }

    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(display, &usable) == 0) {
        in->usable_w_pt = usable.w;
        in->usable_h_pt = usable.h;
    } else {
        in->usable_w_pt = in->usable_h_pt = 0;
    }
}

/* Moves the window back inside the usable area of the display it was on before a resize
 * grew it. Uses the planned size, since X11 reports the new size only later. */
static void frontend_keep_on_screen(Frontend* fe, int display) {
    SDL_Rect bounds;
    if (SDL_GetDisplayUsableBounds(display, &bounds) != 0) return;

    int x = 0, y = 0, top = 0;
    SDL_GetWindowPosition(fe->window, &x, &y);
    if (SDL_GetWindowBordersSize(fe->window, &top, NULL, NULL, NULL) != 0) {
        /* SDL2 has no Cocoa borders getter; y there is the content top, under a 28pt title bar. */
        const char* driver = SDL_GetCurrentVideoDriver();
        top = (driver && strcmp(driver, "cocoa") == 0) ? 28 : 0;
    }

    PxRect usable = {bounds.x, bounds.y, bounds.w, bounds.h};
    int new_x = x, new_y = y;
    panel_layout_clamp_window(&new_x, &new_y, fe->plan.window_w, fe->plan.window_h, top,
                              &usable);
    if (new_x != x || new_y != y) SDL_SetWindowPosition(fe->window, new_x, new_y);
}

/* Recomputes the plan and rects. resize_window is false when reacting to a size change
 * the window already went through, so a window manager that clamps our request cannot
 * bounce resize events back and forth. */
static void settings_free(Frontend* fe) {
    if (fe->settings_texture) SDL_DestroyTexture(fe->settings_texture);
    fe->settings_texture = NULL;
    free(fe->settings_buffer);
    fe->settings_buffer = NULL;
    fe->settings_px_w = fe->settings_px_h = 0;
}

/* Window-sized layer, allocated only while Settings is open. */
static void settings_alloc(Frontend* fe) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(fe->renderer, &w, &h);
    fe->settings_density = settings_density(w, h, fe->plan.density);
    fe->settings_dirty = true;
    if (w == fe->settings_px_w && h == fe->settings_px_h && fe->settings_texture) return;
    settings_free(fe);
    if (w <= 0 || h <= 0) return;
    fe->settings_buffer = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    fe->settings_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!fe->settings_buffer || !fe->settings_texture) {
        LOG_ERROR("Failed to create the Settings texture: %s", SDL_GetError());
        settings_free(fe);
        return;
    }
    SDL_SetTextureBlendMode(fe->settings_texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(fe->settings_texture, SDL_ScaleModeNearest);
    fe->settings_px_w = w;
    fe->settings_px_h = h;
}

static void frontend_layout(Frontend* fe, bool resize_window) {
    LayoutInput in;
    in.game_scale = fe->scale;
    in.panel_visible = fe->panel_visible;
    frontend_measure(fe, &in);
    fe->plan = panel_layout_plan(&in);

    if (resize_window && !fe->fullscreen) {
        int cur_w = 0, cur_h = 0;
        SDL_GetWindowSize(fe->window, &cur_w, &cur_h);
        if (cur_w != fe->plan.window_w || cur_h != fe->plan.window_h) {
            int display = SDL_GetWindowDisplayIndex(fe->window);
            if (display < 0) display = 0;
            SDL_SetWindowSize(fe->window, fe->plan.window_w, fe->plan.window_h);
            frontend_keep_on_screen(fe, display);
            /* The new size or position can land on another display or change the pixel ratio. */
            frontend_measure(fe, &in);
            fe->plan = panel_layout_plan(&in);
        }
    }

    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(fe->renderer, &out_w, &out_h);
    panel_layout_rects(&fe->plan, fe->panel_visible, out_w, out_h, &fe->game_rect,
                       &fe->panel_rect);

    /* The canvas density follows the rect, which can be smaller than the plan when the
     * drawable is too small for the panel. */
    int full_w = (int)(PANEL_W_PT * fe->plan.density + 0.5f);
    int full_h = (int)(PANEL_H_PT * fe->plan.density + 0.5f);
    if (fe->panel_rect.w == full_w && fe->panel_rect.h == full_h) {
        fe->panel_density = fe->plan.density;
    } else {
        float dw = (float)fe->panel_rect.w / PANEL_W_PT;
        float dh = (float)fe->panel_rect.h / PANEL_H_PT;
        fe->panel_density = dw < dh ? dw : dh;
    }

    if (fe->panel_rect.w != fe->panel_px_w || fe->panel_rect.h != fe->panel_px_h) {
        if (fe->panel_texture) SDL_DestroyTexture(fe->panel_texture);
        fe->panel_texture = NULL;
        free(fe->panel_buffer);
        fe->panel_buffer = NULL;
        fe->panel_px_w = fe->panel_px_h = 0;
        if (fe->panel_rect.w > 0 && fe->panel_rect.h > 0) {
            fe->panel_buffer = (uint32_t*)calloc((size_t)fe->panel_rect.w * (size_t)fe->panel_rect.h,
                                                 sizeof(uint32_t));
            fe->panel_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  fe->panel_rect.w, fe->panel_rect.h);
            if (!fe->panel_buffer || !fe->panel_texture) {
                LOG_ERROR("Failed to create the panel texture: %s", SDL_GetError());
                if (fe->panel_texture) SDL_DestroyTexture(fe->panel_texture);
                fe->panel_texture = NULL;
                free(fe->panel_buffer);
                fe->panel_buffer = NULL;
            } else {
                /* Drawn 1:1, so nearest keeps every pixel as rendered. */
                SDL_SetTextureScaleMode(fe->panel_texture, SDL_ScaleModeNearest);
                fe->panel_px_w = fe->panel_rect.w;
                fe->panel_px_h = fe->panel_rect.h;
            }
        }
    }
    fe->panel_dirty = true;

    int hud_w = fe->game_rect.w;
    int hud_h = (int)ceilf(hud_strip_height_pt() * fe->plan.density);
    if (hud_h > fe->game_rect.h) hud_h = fe->game_rect.h;
    if (hud_w != fe->hud_px_w || hud_h != fe->hud_px_h) {
        if (fe->hud_texture) SDL_DestroyTexture(fe->hud_texture);
        fe->hud_texture = NULL;
        free(fe->hud_buffer);
        fe->hud_buffer = NULL;
        fe->hud_px_w = fe->hud_px_h = 0;
        if (hud_w > 0 && hud_h > 0) {
            fe->hud_buffer = (uint32_t*)calloc((size_t)hud_w * (size_t)hud_h, sizeof(uint32_t));
            fe->hud_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING, hud_w, hud_h);
            if (!fe->hud_buffer || !fe->hud_texture) {
                LOG_ERROR("Failed to create the HUD texture: %s", SDL_GetError());
                if (fe->hud_texture) SDL_DestroyTexture(fe->hud_texture);
                fe->hud_texture = NULL;
                free(fe->hud_buffer);
                fe->hud_buffer = NULL;
            } else {
                SDL_SetTextureBlendMode(fe->hud_texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(fe->hud_texture, SDL_ScaleModeNearest);
                fe->hud_px_w = hud_w;
                fe->hud_px_h = hud_h;
            }
        }
    }
    fe->hud_dirty = true;

    int ow_w = fe->game_rect.w, ow_h = fe->game_rect.h;
    if (ow_w != fe->ow_px_w || ow_h != fe->ow_px_h) {
        if (fe->ow_texture) SDL_DestroyTexture(fe->ow_texture);
        fe->ow_texture = NULL;
        free(fe->ow_buffer);
        fe->ow_buffer = NULL;
        fe->ow_px_w = fe->ow_px_h = 0;
        if (ow_w > 0 && ow_h > 0) {
            fe->ow_buffer = (uint32_t*)calloc((size_t)ow_w * (size_t)ow_h, sizeof(uint32_t));
            fe->ow_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, ow_w, ow_h);
            if (!fe->ow_buffer || !fe->ow_texture) {
                LOG_ERROR("Failed to create the overworld texture: %s", SDL_GetError());
                if (fe->ow_texture) SDL_DestroyTexture(fe->ow_texture);
                fe->ow_texture = NULL;
                free(fe->ow_buffer);
                fe->ow_buffer = NULL;
            } else {
                SDL_SetTextureBlendMode(fe->ow_texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(fe->ow_texture, SDL_ScaleModeNearest);
                fe->ow_px_w = ow_w;
                fe->ow_px_h = ow_h;
            }
        }
    }
    fe->ow_dirty = true;
    if (fe->settings.open) settings_alloc(fe);
}

void frontend_apply_layout(Frontend* fe) {
    frontend_layout(fe, true);
}

void frontend_toast(Frontend* fe, ToastKind kind, const char* title, const char* detail) {
    toast_push(&fe->toasts, SDL_GetTicks(), kind, title, detail);
}

void frontend_toast_keyed(Frontend* fe, const char* key, ToastKind kind, const char* title,
                          const char* detail) {
    toast_push_keyed(&fe->toasts, SDL_GetTicks(), key, kind, title, detail);
}

void frontend_update_hud(Frontend* fe, uint32_t now_ms) {
    bool badges = !fe->settings.open;   /* Settings covers the game; its dimming shows the pause */
    toast_set_badge(&fe->toasts, BADGE_PAUSED, badges && fe->paused);
    toast_set_badge(&fe->toasts, BADGE_FAST, badges && (fe->ff_hold || fe->ff_toggle));
    toast_set_badge(&fe->toasts, BADGE_MUTED, badges && fe->muted);
    toast_set_badge(&fe->toasts, BADGE_XRAY, badges && fe->overworld_xray);
    toast_tick(&fe->toasts, now_ms);
    fe->hud_shown = fe->hud_texture && hud_visible(&fe->toasts);
    if (!fe->hud_shown) {
        fe->hud_dirty = true;   /* redraw when something appears again */
        fe->toasts.changed = false;
        return;
    }
    if (fe->toasts.changed || fe->hud_dirty || toast_animating(&fe->toasts, now_ms)) {
        UiCanvas canvas;
        ui_canvas_init(&canvas, fe->hud_buffer, fe->hud_px_w, fe->hud_px_h, fe->plan.density);
        hud_draw(&canvas, &fe->toasts, now_ms);
        SDL_UpdateTexture(fe->hud_texture, NULL, fe->hud_buffer,
                          fe->hud_px_w * (int)sizeof(uint32_t));
        fe->toasts.changed = false;
        fe->hud_dirty = false;
    }
}

void frontend_toggle_overworld(Frontend* fe, GBA* gba) {
    if (!fe->ow_feed) {
        frontend_toast_keyed(fe, "owxray", TOAST_INFO, "Overworld X-Ray needs Pokemon Emerald",
                             "Original US ROM only");
        return;
    }
    fe->overworld_xray = !fe->overworld_xray;
    overworld_feed_clear(fe->ow_feed);
    frontend_sync_observer(fe, gba);
    frontend_toast_keyed(fe, "owxray", TOAST_INFO,
                         fe->overworld_xray ? "Overworld X-Ray on" : "Overworld X-Ray off", NULL);
    if (fe->prefs) {
        prefs_set_bool(fe->prefs, "overworld_xray", fe->overworld_xray);
        if (!prefs_save(fe->prefs)) LOG_WARN("Couldn't save preferences to %s", fe->prefs->path);
    }
}

void frontend_update_overworld(Frontend* fe, const OverworldSnapshot* s) {
    fe->ow_shown = s && s->visible && fe->ow_texture && !fe->settings.open;
    if (!fe->ow_shown) {
        fe->ow_dirty = true;   /* repaint when it shows again */
        return;
    }
    if (!fe->ow_dirty && memcmp(&fe->ow_last, s, sizeof(*s)) == 0) return;
    UiCanvas canvas;
    ui_canvas_init(&canvas, fe->ow_buffer, fe->ow_px_w, fe->ow_px_h, fe->plan.density);
    canvas.keep_alpha = true;
    ui_canvas_clear(&canvas, 0x00000000u);
    overworld_draw(&canvas, s, (float)fe->ow_px_w / (float)SCREEN_WIDTH / fe->plan.density);
    SDL_UpdateTexture(fe->ow_texture, NULL, fe->ow_buffer, fe->ow_px_w * (int)sizeof(uint32_t));
    memcpy(&fe->ow_last, s, sizeof(*s));
    fe->ow_dirty = false;
}

/* ---- Following Pokemon (F1) ----
 * Struct offsets from pret/pokeemerald (global.h, pokemon.h, item.h, global.fieldmap.h). */
#define FOL_SPECIES_INFO_SIZE 28
#define FOL_SI_TYPE1          6
#define FOL_SI_TYPE2          7
#define FOL_SI_GENDER_RATIO   0x10
#define FOL_ITEM_SIZE         44
#define FOL_ITEM_POCKET       0x1A
#define FOL_SPECIES_NAME_LEN  11
#define FOL_ITEM_NAME_LEN     14
#define FOL_PLAYER_NAME_LEN   8      /* 7 characters and 0xFF */
#define FOL_SB2_LOCAL_TIME    0x98   /* struct Time localTimeOffset {s16 days; s8 hours, minutes, seconds} */
#define FOL_AVATAR_OBJ_ID     0x05
#define FOL_OBJ_COUNT         16
#define FOL_OBJ_SIZE          0x24
#define FOL_OBJ_CUR           0x10
#define FOL_PAL_TAG_GRASS     0x1005 /* FLDEFF_PAL_TAG_GENERAL_1, the tall grass palette */
#define FOL_FAINT_MOOD        (-80)
#define FOL_NATIONAL_MAX      386

static uint8_t s_fol_rules[HGSS_RULES][HGSS_RULE_SIZE];
static HgssStrVars s_fol_vars;
static uint16_t s_fol_line[HGSS_MSG_MAX];
static uint16_t s_fol_text[HGSS_MSG_MAX];

void frontend_vblank(const GBA* gba, void* ctx) {
    Frontend* fe = (Frontend*)ctx;
    if (fe->overworld_xray && fe->ow_feed) overworld_capture(gba, fe->ow_feed);
    if (fe->follower.enabled && fe->fol_feed) follower_capture(gba, fe->fol_feed);
}

void frontend_sync_observer(Frontend* fe, GBA* gba) {
    bool any = (fe->overworld_xray && fe->ow_feed) || (fe->follower.enabled && fe->fol_feed);
    gba_set_vblank_observer(gba, any ? frontend_vblank : NULL, fe);
}

/* Ends a talk without applying its mood change. talk_abort leaves want_cry set. */
static void follower_talk_stop(FollowerView* v) {
    talk_abort(&v->talk);
    v->talk.want_cry = false;
    v->talk.phase = TALK_IDLE;
}

void frontend_follower_reset(Frontend* fe, GBA* gba) {
    if (fe->fol_feed) follower_reset(&fe->fol_feed->sim);
    follower_talk_stop(&fe->follower);
    fe->follower.cry.playing = false;
    memset(gba->ppu.extra_obj, 0, sizeof(gba->ppu.extra_obj));
}

void frontend_toggle_follower(Frontend* fe, GBA* gba) {
    if (!fe->fol_feed) {
        frontend_toast_keyed(fe, "follower", TOAST_INFO, "Following Pokemon needs Pokemon Emerald",
                             "Original US ROM only");
        return;
    }
    if (!fe->follower.enabled && (!fe->hgss || !fe->hgss->ok)) {
        frontend_toast_keyed(fe, "follower", TOAST_INFO, "Following Pokemon needs your HeartGold ROM",
                             "Set it in Settings > General");
        return;
    }
    fe->follower.enabled = !fe->follower.enabled;
    frontend_follower_reset(fe, gba);
    if (!fe->follower.enabled) follower_mood_save(&fe->follower);
    frontend_sync_observer(fe, gba);
    frontend_toast_keyed(fe, "follower", TOAST_INFO,
                         fe->follower.enabled ? "Following Pokemon on" : "Following Pokemon off", NULL);
    if (fe->prefs) {
        prefs_set_bool(fe->prefs, "follower", fe->follower.enabled);
        if (!prefs_save(fe->prefs)) LOG_WARN("Couldn't save preferences to %s", fe->prefs->path);
    }
}

static bool follower_national(const GameProfile* p, const GameMem* m, uint16_t species, uint16_t* national) {
    *national = species;
    if (species == 0) return false;
    if (species > 251 && !game_mem_read16(m, p->species_to_national + 2u * (species - 1u), national)) return false;
    return *national != 0;
}

/* Copies the tall grass palette from its OBJ palette slot once Emerald has loaded it. The
 * hardware colours are faded during fades, so those frames keep the last clean copy. */
static void follower_grass_palette(FollowerView* v, const GBA* gba, const GameMem* m, const FollowerFeed* fd) {
    const FollowerInput* in = &fd->in;
    if (!in->ok || in->fade_y != 0 || in->fade_active || in->weather_state == 1) return;
    for (uint32_t slot = 0; slot < 16; slot++) {
        uint16_t tag = 0;
        if (!game_mem_read16(m, fd->profile->sprite_palette_tags + 2u * slot, &tag) || tag != FOL_PAL_TAG_GRASS)
            continue;
        const uint8_t* pal = gba->ppu.palette_ram + 0x200 + 32u * slot;
        for (uint32_t i = 0; i < 16; i++) v->grass_pal[i] = (uint16_t)(pal[2 * i] | pal[2 * i + 1] << 8);
        v->grass_ok = true;
        return;
    }
}

static uint32_t follower_rand(void* ud) {
    FollowerView* v = (FollowerView*)ud;
    uint32_t x = v->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rng = x;
    return x;
}

/* Interactions we can play: no yes/no prompt, fashion item, leaf, or Johto-only line. */
static bool follower_script_ok(uint16_t result, void* ud) {
    FollowerView* v = (FollowerView*)ud;
    HgssTalkScript s;
    if (!hgss_assets_script(v->assets, result, &s) || s.yes_no || s.fashion != 0 || s.leaf != 0) return false;
    for (uint32_t i = 0; i < s.step_count && i < HGSS_STEPS; i++) {
        uint16_t msg = s.step[i].msg;
        if (msg == 0) continue;
        uint32_t n = 0;
        if (follower_place_msg_denied(msg) || !hgss_assets_line(v->assets, msg, s_fol_line, HGSS_MSG_MAX, &n) ||
            hgss_msg_has_yesno(s_fol_line, n))
            return false;
    }
    return true;
}

static uint8_t follower_adjacent_objects(const GameProfile* p, const GameMem* m, const FollowerInput* in) {
    uint8_t player = 0xFF, count = 0;
    game_mem_read8(m, p->player_avatar + FOL_AVATAR_OBJ_ID, &player);
    for (uint32_t i = 0; i < FOL_OBJ_COUNT; i++) {
        uint8_t obj[FOL_OBJ_CUR + 4];
        if (i == player || !game_mem_copy(m, p->object_events + i * FOL_OBJ_SIZE, obj, sizeof(obj)) || !(obj[0] & 1))
            continue;
        int32_t dx = (int16_t)(obj[FOL_OBJ_CUR] | obj[FOL_OBJ_CUR + 1] << 8) - in->cur_x;
        int32_t dy = (int16_t)(obj[FOL_OBJ_CUR + 2] | obj[FOL_OBJ_CUR + 3] << 8) - in->cur_y;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) count++;
    }
    return count;
}

/* Grass, cave floor and water tiles of a map that has land or water encounters. An
 * approximation of Emerald's per-metatile encounter attribute; it only picks lines. */
static bool follower_encounter_tile(const GameProfile* p, const GameMem* m, const FollowerInput* in) {
    uint8_t b = in->fol_behavior;
    bool land = b == 0x02 || b == 0x03 || b == 0x08 || b == 0x0B || b == 0x24;
    bool water = b == 0x10 || b == 0x11 || b == 0x12 || b == 0x14 || b == 0x15;
    if (!land && !water) return false;
    EncounterList enc[ENC_METHOD_COUNT];
    if (!encounters_find(m, p->wild_mon_headers, p->wild_header_cap, p->species_count, in->loc_group, in->loc_num,
                         enc))
        return false;
    return land ? enc[ENC_LAND].count > 0 : enc[ENC_WATER].count > 0;
}

/* The game's clock: RTC time minus the save's local time offset (pokeemerald RtcCalcLocalTime). */
static uint8_t follower_hour(const GBA* gba, const GameProfile* p, const GameMem* m) {
    time_t now = time(NULL) + (time_t)gba->cart.rtc.offset_secs;
    struct tm* tm = localtime(&now);
    int32_t minutes = tm ? tm->tm_hour * 60 + tm->tm_min : 12 * 60;
    uint32_t sb2 = 0;
    uint8_t h = 0, mi = 0;
    if (game_mem_read32(m, p->save_block2_ptr, &sb2) && game_mem_read8(m, sb2 + FOL_SB2_LOCAL_TIME + 2, &h) &&
        game_mem_read8(m, sb2 + FOL_SB2_LOCAL_TIME + 3, &mi))
        minutes -= (int8_t)h * 60 + (int8_t)mi;
    minutes %= 24 * 60;
    if (minutes < 0) minutes += 24 * 60;
    return (uint8_t)(minutes / 60);
}

static void follower_strvar(const GameMem* m, uint32_t addr, uint32_t len, uint16_t* dst) {
    uint8_t buf[HGSS_STRVAR_MAX];
    if (len > sizeof(buf) || !game_mem_copy(m, addr, buf, len)) return;
    hgss_str_from_gen3(buf, len, dst, HGSS_STRVAR_MAX);
}

/* Nickname, species, player, place and held item, as HGSS text. */
static void follower_strvars(const GameProfile* p, const GameMem* m, const FollowerInput* in) {
    const MonData* mon = &in->lead;
    for (uint32_t i = 0; i < HGSS_STRVARS; i++) s_fol_vars.s[i][0] = HGSS_CH_END;
    hgss_str_from_gen3(mon->nickname_raw, sizeof(mon->nickname_raw), s_fol_vars.s[0], HGSS_STRVAR_MAX);
    if (mon->species < p->species_count)
        follower_strvar(m, p->species_names + (uint32_t)mon->species * FOL_SPECIES_NAME_LEN, FOL_SPECIES_NAME_LEN,
                        s_fol_vars.s[1]);
    uint32_t sb2 = 0, name = 0;
    if (game_mem_read32(m, p->save_block2_ptr, &sb2))
        follower_strvar(m, sb2, FOL_PLAYER_NAME_LEN, s_fol_vars.s[2]);
    if (in->mapsec < p->region_map_count &&
        game_mem_read32(m, p->region_map_entries + 8u * in->mapsec + 4, &name))
        follower_strvar(m, name, HGSS_STRVAR_MAX, s_fol_vars.s[3]);
    if (mon->held_item != 0 && mon->held_item < p->item_count)
        follower_strvar(m, p->items + (uint32_t)mon->held_item * FOL_ITEM_SIZE, FOL_ITEM_NAME_LEN, s_fol_vars.s[4]);
}

/* Animation and text pages for the talk's current step. */
static void follower_talk_step(Frontend* fe) {
    static TalkStepData d;
    TalkPlayer* t = &fe->follower.talk;
    HgssAssets* a = fe->follower.assets;
    memset(&d, 0, sizeof(d));
    fe->fol_step = t->step;
    if (t->step < t->script.step_count && t->step < HGSS_STEPS) {
        const HgssTalkStep* s = &t->script.step[t->step];
        d.anim_n = hgss_assets_anim(a, s->anim, d.anim);
        if (d.anim_n > HGSS_ANIM_FRAMES) d.anim_n = HGSS_ANIM_FRAMES;
        uint32_t n = 0;
        if (s->msg && hgss_assets_line(a, s->msg, s_fol_line, HGSS_MSG_MAX, &n)) {
            uint32_t len = hgss_msg_expand(s_fol_line, n, &s_fol_vars, s_fol_text, HGSS_MSG_MAX);
            d.page_n = hgss_text_layout(&a->font, s_fol_text, len, d.pages, HGSS_PAGE_MAX);
            if (d.page_n > HGSS_PAGE_MAX) d.page_n = HGSS_PAGE_MAX;
        }
    }
    talk_set_step_data(t, &d);
}

/* A on the follower: pick an HGSS interaction for the lead and the place, and start it. */
static void follower_talk_begin(Frontend* fe, const GBA* gba, const GameMem* m) {
    FollowerView* v = &fe->follower;
    FollowerFeed* fd = fe->fol_feed;
    const GameProfile* p = fd->profile;
    const FollowerInput* in = &fd->in;
    const MonData* mon = &in->lead;
    HgssAssets* a = v->assets;
    uint16_t national = 0;
    if (!a || !a->ok || !in->lead_ok || !follower_national(p, m, mon->species, &national)) return;

    FollowerCtxExtra x;
    memset(&x, 0, sizeof(x));
    uint32_t info = p->species_info + (uint32_t)mon->species * FOL_SPECIES_INFO_SIZE;
    x.gender_ratio = 0xFF;
    game_mem_read8(m, info + FOL_SI_TYPE1, &x.type1);
    game_mem_read8(m, info + FOL_SI_TYPE2, &x.type2);
    game_mem_read8(m, info + FOL_SI_GENDER_RATIO, &x.gender_ratio);
    if (mon->held_item != 0 && mon->held_item < p->item_count)
        game_mem_read8(m, p->items + (uint32_t)mon->held_item * FOL_ITEM_SIZE + FOL_ITEM_POCKET, &x.item_pocket);
    if (national <= sizeof(a->species_class_tbl)) x.species_class = a->species_class_tbl[national - 1];
    x.adjacent_objects = follower_adjacent_objects(p, m, in);
    if (fe->overworld_xray && fe->ow_feed)
        for (uint32_t i = 0; i < fe->ow_feed->shown.marker_count && i < OW_MAX_MARKERS; i++)
            if (fe->ow_feed->shown.markers[i].kind == OW_MARK_HIDDEN) x.hidden_items++;
    x.encounter_tile = follower_encounter_tile(p, m, in);
    x.hour = follower_hour(gba, p, m);
    x.mood = follower_mood_get(v, mon->personality);
    static const uint8_t k_opposite[5] = {1, 2, 1, 4, 3};   /* game facing: 1 S, 2 N, 3 W, 4 E */
    uint8_t face = k_opposite[in->facing <= 4 ? in->facing : 0];
    x.follower_facing = face;

    HgssTalkCtx ctx;
    follower_ctx_build(in, &x, &ctx);
    hgss_assets_rules(a, follower_place_mapsec(in->map_type, in->mapsec, in->fol_behavior), s_fol_rules);
    int32_t pick = hgss_rule_pick(&s_fol_rules[0][0], &ctx, follower_rand, follower_script_ok, v);
    HgssTalkScript script;
    if (pick < 0 || !hgss_assets_script(a, hgss_rule_result(s_fol_rules[pick]), &script)) return;
    follower_strvars(p, m, in);
    talk_start(&v->talk, &script);
    follower_talk_step(fe);
    fd->sim.facing = face;   /* turn to the player */
}

static void follower_play_cry(FollowerView* v, const GBA* gba, const FollowerFeed* fd) {
    GameMem m = {gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size};
    uint16_t national = 0;
    uint32_t wave = 0;
    if (!fd->in.lead_ok || !follower_national(fd->profile, &m, fd->in.lead.species, &national) ||
        national > FOL_NATIONAL_MAX ||
        !game_mem_read32(&m, fd->profile->cry_table + 12u * (national - 1u) + 4, &wave) || wave < 0x08000000u ||
        wave - 0x08000000u >= gba->cart.rom_size)
        return;
    uint32_t off = wave - 0x08000000u;
    if (cry_decode(gba->cart.rom + off, gba->cart.rom_size - off, v->cry.pcm, CRY_MAX_SAMPLES, &v->cry.len,
                   &v->cry.rate))
        cry_start(&v->cry);
    else
        LOG_WARN("Couldn't decode the cry for national dex %u", (unsigned)national);
}

void follower_frame_begin(Frontend* fe, GBA* gba) {
    FollowerView* v = &fe->follower;
    FollowerFeed* fd = fe->fol_feed;
    if (!v->enabled || !fd || !fd->profile) {
        memset(gba->ppu.extra_obj, 0, sizeof(gba->ppu.extra_obj));
        return;
    }
    GameMem m = {gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size};
    follower_view_sync(v, &m, fd->profile, fd);
    follower_grass_palette(v, gba, &m, fd);
    bool shown = fd->sim.vis == FOL_VISIBLE && fd->in.overworld && !fd->in.in_transition && v->gfx_pid != 0 &&
                 v->gfx_pid == fd->sim.lead_pid;
    if (talk_active(&v->talk) && !shown)
        follower_talk_stop(v);
    else if (fd->sim.talk_request && shown && !talk_active(&v->talk))
        follower_talk_begin(fe, gba, &m);
    follower_view_objs(v, fd, gba->ppu.oam, gba->ppu.extra_obj);
}

void follower_frame_end(Frontend* fe, const GBA* gba, uint16_t real_keys) {
    FollowerView* v = &fe->follower;
    FollowerFeed* fd = fe->fol_feed;
    if (!v->enabled || !fd) return;
    uint16_t held = (uint16_t)(~real_keys & 0x03FF);
    uint16_t pressed = (uint16_t)(held & ~fe->fol_prev_keys);
    fe->fol_prev_keys = held;

    TalkPlayer* t = &v->talk;
    if (talk_active(t)) {
        talk_tick(t, pressed, held);
        if (t->phase == TALK_ANIM && t->step != fe->fol_step) follower_talk_step(fe);
        if (t->want_cry) {
            t->want_cry = false;
            follower_play_cry(v, gba, fd);
        }
    }
    if (t->phase == TALK_DONE) {
        if (t->mood_delta) follower_mood_add(v, fd->sim.lead_pid, t->mood_delta);
        follower_mood_save(v);
        t->phase = TALK_IDLE;
    }

    const Follower* s = &fd->sim;
    if (s->faint_pid) follower_mood_add(v, s->faint_pid, FOL_FAINT_MOOD);
    if (s->mood_delta && s->lead_pid) follower_mood_add(v, s->lead_pid, s->mood_delta);
    if (s->steps && s->vis == FOL_VISIBLE && s->lead_pid) {
        int32_t mood = follower_mood_get(v, s->lead_pid);
        int32_t n = s->steps < (mood < 0 ? -mood : mood) ? s->steps : (mood < 0 ? -mood : mood);
        if (n) follower_mood_add(v, s->lead_pid, (int16_t)(mood > 0 ? -n : n));
    }
}

void frontend_destroy(Frontend* fe) {
    if (fe->ow_texture) SDL_DestroyTexture(fe->ow_texture);
    free(fe->ow_buffer);
    fe->ow_buffer = NULL;
    if (fe->overlay_texture) SDL_DestroyTexture(fe->overlay_texture);
    if (fe->overlay_buffer) {
        free(fe->overlay_buffer);
        fe->overlay_buffer = NULL;
    }
    if (fe->panel_texture) SDL_DestroyTexture(fe->panel_texture);
    if (fe->panel_buffer) {
        free(fe->panel_buffer);
        fe->panel_buffer = NULL;
    }
    settings_free(fe);
    if (fe->hud_texture) SDL_DestroyTexture(fe->hud_texture);
    free(fe->hud_buffer);
    fe->hud_buffer = NULL;
    if (fe->video_texture) SDL_DestroyTexture(fe->video_texture);
    if (fe->texture) SDL_DestroyTexture(fe->texture);
    if (fe->renderer) SDL_DestroyRenderer(fe->renderer);
    if (fe->window) SDL_DestroyWindow(fe->window);
    if (fe->audio_device) SDL_CloseAudioDevice(fe->audio_device);
    if (fe->controller) SDL_GameControllerClose(fe->controller);
    SDL_Quit();
}

void frontend_set_video(Frontend* fe, VideoFilter filter, bool color_correct) {
    fe->video_filter = filter < VIDEO_FILTER_COUNT ? filter : VIDEO_FILTER_SHARP;
    fe->color_correct = color_correct;
    if (fe->prefs) {
        prefs_set(fe->prefs, "video_filter", video_filter_name(fe->video_filter));
        prefs_set_bool(fe->prefs, "color_correct", fe->color_correct);
        if (!prefs_save(fe->prefs)) LOG_WARN("Couldn't save preferences to %s", fe->prefs->path);
    }
}

void frontend_set_audio(Frontend* fe, int32_t volume, bool start_muted) {
    fe->volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    fe->start_muted = start_muted;
    if (fe->prefs) {
        char v[8];
        snprintf(v, sizeof(v), "%d", (int)fe->volume);
        prefs_set(fe->prefs, "volume", v);
        prefs_set_bool(fe->prefs, "start_muted", fe->start_muted);
        if (!prefs_save(fe->prefs)) LOG_WARN("Couldn't save preferences to %s", fe->prefs->path);
    }
}

void frontend_set_general(Frontend* fe, uint8_t ff_speed, bool pause_on_blur) {
    fe->ff_speed = (ff_speed >= 2 && ff_speed <= 4) ? ff_speed : 0;
    fe->pause_on_blur = pause_on_blur;
    if (fe->prefs) {
        if (fe->ff_speed == 0) {
            prefs_set(fe->prefs, "ff_speed", "max");
        } else {
            char v[4];
            snprintf(v, sizeof(v), "%d", (int)fe->ff_speed);
            prefs_set(fe->prefs, "ff_speed", v);
        }
        prefs_set_bool(fe->prefs, "pause_on_blur", fe->pause_on_blur);
        if (!prefs_save(fe->prefs)) LOG_WARN("Couldn't save preferences to %s", fe->prefs->path);
    }
}

bool frontend_is_paused(const Frontend* fe) { return fe->paused || fe->blur_paused; }

/* Draws the filtered game into dst. False when the filter is off or its texture failed,
 * so the caller draws the plain texture instead. */
static bool frontend_draw_video(Frontend* fe, const uint16_t* framebuffer, const SDL_Rect* dst) {
    if (!video_filter_active(fe->video_filter, fe->color_correct)) return false;
    VideoFrame fr;
    bool changed = video_filter_run(framebuffer, fe->video_filter, fe->color_correct, dst->w / SCREEN_WIDTH, &fr);
    if (!fe->video_texture || fe->video_tex_w != fr.w || fe->video_tex_h != fr.h) {
        if (fe->video_texture) SDL_DestroyTexture(fe->video_texture);
        fe->video_tex_w = fe->video_tex_h = 0;
        fe->video_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, fr.w, fr.h);
        if (!fe->video_texture) {
            LOG_ERROR("Failed to create the filter texture: %s", SDL_GetError());
            return false;
        }
        fe->video_tex_w = fr.w;
        fe->video_tex_h = fr.h;
        changed = true;
    }
    if (changed) SDL_UpdateTexture(fe->video_texture, NULL, fr.px, fr.w * (int)sizeof(uint32_t));
    SDL_SetTextureScaleMode(fe->video_texture, fr.linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    SDL_RenderCopy(fe->renderer, fe->video_texture, NULL, dst);
    return true;
}

void frontend_present_frame(Frontend* fe, uint16_t* framebuffer) {
    /* Letterbox areas use the panel background so the window reads as one surface. */
    SDL_SetRenderDrawColor(fe->renderer, (UI_BG >> 16) & 0xFF, (UI_BG >> 8) & 0xFF,
                           UI_BG & 0xFF, 0xFF);
    SDL_RenderClear(fe->renderer);
    SDL_Rect game_rect = {fe->game_rect.x, fe->game_rect.y, fe->game_rect.w, fe->game_rect.h};
    if (!frontend_draw_video(fe, framebuffer, &game_rect)) {
        SDL_UpdateTexture(fe->texture, NULL, framebuffer, SCREEN_WIDTH * sizeof(uint16_t));
        SDL_RenderCopy(fe->renderer, fe->texture, NULL, &game_rect);
    }
    if (fe->ow_shown && fe->ow_texture) SDL_RenderCopy(fe->renderer, fe->ow_texture, NULL, &game_rect);

    if (fe->overlay_dirty) {
        SDL_UpdateTexture(fe->overlay_texture, NULL, fe->overlay_buffer,
                          SCREEN_WIDTH * sizeof(uint32_t));
        SDL_RenderCopy(fe->renderer, fe->overlay_texture, NULL, &game_rect);
    }

    if (fe->panel_visible && fe->panel_texture) {
        SDL_Rect panel_rect = {fe->panel_rect.x, fe->panel_rect.y, fe->panel_px_w, fe->panel_px_h};
        SDL_RenderCopy(fe->renderer, fe->panel_texture, NULL, &panel_rect);
    }
    if (fe->settings.open && fe->settings_texture)
        SDL_RenderCopy(fe->renderer, fe->settings_texture, NULL, NULL);
    /* Toasts last, so "Cheat added" shows above Settings. */
    if (fe->hud_shown && fe->hud_texture) {
        SDL_Rect hud_rect = {fe->game_rect.x, fe->game_rect.y, fe->hud_px_w, fe->hud_px_h};
        SDL_RenderCopy(fe->renderer, fe->hud_texture, NULL, &hud_rect);
    }
    SDL_RenderPresent(fe->renderer);
}

void frontend_apply_input(Frontend* fe, GBA* gba) {
    input_set_pressed(&gba->input,
                      input_combine((uint16_t)(fe->kb_keys & 0x3FF), (uint16_t)(fe->pad_keys & 0x3FF),
                                    fe->stick_keys, (uint16_t)((fe->kb_keys | fe->pad_keys) & 0xC00),
                                    fe->turbo_frame));
}

void frontend_input_frame(Frontend* fe, GBA* gba) {
    fe->turbo_frame++;
    frontend_apply_input(fe, gba);
}

/* Trigger axis with hysteresis: press above 16000, release below 8000. */
static void frontend_trigger(Frontend* fe, GBA* gba, bool* down, int32_t pad, int16_t value) {
    uint16_t key = keymap_pad_lookup(pad);
    if (!*down && value > 16000) {
        *down = true;
        fe->pad_keys |= key;
    } else if (*down && value < 8000) {
        *down = false;
        fe->pad_keys &= (uint16_t)~key;
    } else {
        return;
    }
    frontend_apply_input(fe, gba);
}

static void frontend_handle_window_event(Frontend* fe, const SDL_Event* event) {
#ifdef ENABLE_XRAY
    /* Handle X-Ray window close button */
    if (event->window.event == SDL_WINDOWEVENT_CLOSE &&
        g_xray && g_xray->window_id == event->window.windowID) {
        xray_toggle(g_xray);
    }
#endif
    if (event->window.windowID == SDL_GetWindowID(fe->window)) {
        if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            frontend_layout(fe, false);
        }
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (event->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
            frontend_layout(fe, true);
        }
#endif
    }
}

/* ⌘, arrives only as the menu's event on macOS (also matching ⌘ here could
 * toggle twice for one press); Ctrl+, is the key everywhere. */
static bool frontend_settings_shortcut(Frontend* fe, const SDL_Event* e) {
    if (fe->settings_event != (uint32_t)-1 && e->type == fe->settings_event) return true;
    return e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.scancode == SDL_SCANCODE_COMMA &&
           (e->key.keysym.mod & KMOD_CTRL);
}

static void release_gba_keys(Frontend* fe, GBA* gba) {
    fe->kb_keys = 0;
    fe->pad_keys = 0;
    fe->stick_keys = 0;
    fe->lt_down = false;
    fe->rt_down = false;
    input_set_pressed(&gba->input, 0);
    fe->ff_hold = false;
#ifdef ENABLE_REWIND
    fe->rewind_hold = false;
#endif
}

void frontend_settings_open(Frontend* fe, GBA* gba) {
    if (fe->settings.open) return;
    if (slot_picker_is_open(fe)) slot_picker_close(fe);
    fe->settings_was_paused = fe->paused;
    fe->paused = true;
    release_gba_keys(fe, gba);
    float mx = fe->ui_input.mx, my = fe->ui_input.my;
    memset(&fe->ui_input, 0, sizeof(fe->ui_input));
    fe->ui_input.mx = mx;
    fe->ui_input.my = my;
    fe->ui_input.focus = SETTINGS_ID_PASTE;
    settings_open(&fe->settings);
    settings_alloc(fe);
    SDL_StartTextInput();
}

void frontend_settings_close(Frontend* fe, GBA* gba) {
    if (!fe->settings.open) return;
    settings_close(&fe->settings);
    SDL_StopTextInput();
    settings_free(fe);
    release_gba_keys(fe, gba);
    fe->paused = fe->settings_was_paused;
}

static void settings_mouse(Frontend* fe, int wx, int wy) {
    int win_w = 0, win_h = 0, out_w = 0, out_h = 0;
    SDL_GetWindowSize(fe->window, &win_w, &win_h);
    SDL_GetRendererOutputSize(fe->renderer, &out_w, &out_h);
    float px_per_unit = win_w > 0 ? (float)out_w / (float)win_w : 1.0f;
    settings_point_from_window((float)wx, (float)wy, px_per_unit, fe->settings_density, &fe->ui_input.mx,
                               &fe->ui_input.my);
}

static UiKey settings_key(const SDL_KeyboardEvent* k) {
    bool cmd = (k->keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
    switch (k->keysym.scancode) {
    case SDL_SCANCODE_TAB:       return (k->keysym.mod & KMOD_SHIFT) ? UI_KEY_SHIFT_TAB : UI_KEY_TAB;
    case SDL_SCANCODE_UP:        return UI_KEY_UP;
    case SDL_SCANCODE_DOWN:      return UI_KEY_DOWN;
    case SDL_SCANCODE_LEFT:      return UI_KEY_LEFT;
    case SDL_SCANCODE_RIGHT:     return UI_KEY_RIGHT;
    case SDL_SCANCODE_HOME:      return UI_KEY_HOME;
    case SDL_SCANCODE_END:       return UI_KEY_END;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:  return UI_KEY_ENTER;
    case SDL_SCANCODE_ESCAPE:    return UI_KEY_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return UI_KEY_BACKSPACE;
    case SDL_SCANCODE_DELETE:    return UI_KEY_DELETE;
    case SDL_SCANCODE_SPACE:     return UI_KEY_SPACE;
    case SDL_SCANCODE_A:         return cmd ? UI_KEY_SELECT_ALL : UI_KEY_NONE;
    default:                     return UI_KEY_NONE;
    }
}

/* While Settings is open every input goes to it; nothing reaches the GBA. */
static void settings_handle_event(Frontend* fe, const SDL_Event* e) {
    UiInput* in = &fe->ui_input;
    /* Ignore input aimed at other windows (X-Ray). */
    uint32_t main_id = SDL_GetWindowID(fe->window), id = main_id;
    switch (e->type) {
    case SDL_MOUSEMOTION: id = e->motion.windowID; break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: id = e->button.windowID; break;
    case SDL_MOUSEWHEEL: id = e->wheel.windowID; break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: id = e->key.windowID; break;
    case SDL_TEXTINPUT: id = e->text.windowID; break;
    default: break;
    }
    if (id != main_id) return;
    /* A listening Controls chip takes the next key or button, Escape included,
     * so it never reaches UiInput (where Escape would close Settings). */
    if (settings_controls_listening(&fe->settings.controls)) {
        SettingsControlsPage* cp = &fe->settings.controls;
        if (e->type == SDL_KEYDOWN) {
            if (!e->key.repeat) settings_controls_capture_key(cp, (int32_t)e->key.keysym.scancode);
            fe->settings_dirty = true;
            return;
        }
        if (e->type == SDL_CONTROLLERBUTTONDOWN) {
            settings_controls_capture_pad(cp, (int32_t)e->cbutton.button);
            fe->settings_dirty = true;
            return;
        }
        if (e->type == SDL_CONTROLLERAXISMOTION && e->caxis.value > 16000 &&
            (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
            settings_controls_capture_pad(cp, e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? KEYMAP_PAD_LT
                                                                                                : KEYMAP_PAD_RT);
            fe->settings_dirty = true;
            return;
        }
        if (e->type == SDL_TEXTINPUT) return;
    }
    switch (e->type) {
    case SDL_MOUSEMOTION:
        settings_mouse(fe, e->motion.x, e->motion.y);
        fe->settings_dirty = true;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT) break;
        settings_mouse(fe, e->button.x, e->button.y);
        in->down = e->type == SDL_MOUSEBUTTONDOWN;
        if (in->down) in->pressed = true;
        else in->released = true;
        break;
    case SDL_MOUSEWHEEL:
        /* y already follows the system's natural-scrolling setting; direction is informational. */
#if SDL_VERSION_ATLEAST(2, 0, 18)
        in->wheel += e->wheel.preciseY;
#else
        in->wheel += (float)e->wheel.y;
#endif
        break;
    case SDL_KEYDOWN: {
        bool cmd = (e->key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
        if (cmd && e->key.keysym.scancode == SDL_SCANCODE_V) {
            char* clip = SDL_GetClipboardText();
            if (clip) {
                ui_input_text(in, clip);
                SDL_free(clip);
            }
            break;
        }
        UiKey k = settings_key(&e->key);
        /* Repeats move the caret or delete text, but never press buttons:
         * a held Enter must not arm and then confirm Delete. */
        bool repeat_ok = k == UI_KEY_LEFT || k == UI_KEY_RIGHT || k == UI_KEY_UP || k == UI_KEY_DOWN ||
                         k == UI_KEY_BACKSPACE || k == UI_KEY_TAB || k == UI_KEY_SHIFT_TAB;
        if (k != UI_KEY_NONE && (!e->key.repeat || repeat_ok)) ui_input_key(in, k);
        break;
    }
    case SDL_TEXTINPUT:
        ui_input_text(in, e->text.text);
        break;
    default:
        break;
    }
}

/* Settings > General "Choose...": opens the HeartGold/SoulSilver ROM and remembers it. */
static void frontend_choose_hgss(Frontend* fe, GBA* gba) {
    char path[SETTINGS_PATH_LEN];
    RomPickResult r = rom_picker_choose_nds(path, sizeof(path));
    if (r == ROM_PICK_UNAVAILABLE) {
        frontend_toast(fe, TOAST_ERROR, "No file picker on this system", NULL);
        return;
    }
    if (r != ROM_PICK_OK || !fe->hgss) return;
    /* Nothing may draw from the assets while they reload. */
    bool was_on = fe->follower.enabled;
    if (was_on) follower_mood_save(&fe->follower);
    frontend_follower_reset(fe, gba);
    fe->follower.enabled = false;
    if (!hgss_assets_open(fe->hgss, path)) {
        LOG_WARN("HeartGold ROM %s: %s", path, fe->hgss->error);
        frontend_toast(fe, TOAST_ERROR, fe->hgss->error, NULL);
        const char* old = fe->prefs ? prefs_get(fe->prefs, "hgss_rom") : NULL;
        if (old && old[0]) hgss_assets_open(fe->hgss, old);   /* keep the ROM that worked */
    } else {
        frontend_toast(fe, TOAST_OK, "HeartGold ROM ready", NULL);
        if (fe->prefs && prefs_set(fe->prefs, "hgss_rom", path) && !prefs_save(fe->prefs))
            LOG_WARN("Couldn't save preferences to %s", fe->prefs->path);
    }
    /* Reload the sprite from the new assets; the mood comes back from the sidecar. */
    follower_view_init(&fe->follower, fe->hgss, gba->cart.save_path);
    fe->follower.rng ^= (uint32_t)time(NULL) | 1u;   /* talks differ between sessions */
    fe->follower.enabled = was_on && fe->hgss->ok;
    frontend_sync_observer(fe, gba);
}

void frontend_update_settings(Frontend* fe, GBA* gba) {
    if (!fe->settings.open) return;
    UiInput* in = &fe->ui_input;
    in->now_ms = SDL_GetTicks();
    bool text_focus = in->focus == SETTINGS_ID_PASTE || in->focus == SETTINGS_ID_NAME;
    uint32_t blink = text_focus ? (in->now_ms - in->caret_ms) / UI_CARET_MS : 0;
    if (!fe->settings_buffer) {   /* texture couldn't be made: don't leave an invisible modal */
        frontend_settings_close(fe, gba);
        return;
    }
    if (fe->settings_dirty || ui_input_pending(in) || blink != fe->settings_blink) {
        /* Effects applied after a widget was drawn (Tab focus, Add clearing the
         * boxes, a pop-up closing, a deleted row) show up in one more frame. */
        bool settle = ui_input_pending(in);
        UiCanvas canvas;
        ui_canvas_init(&canvas, fe->settings_buffer, fe->settings_px_w, fe->settings_px_h, fe->settings_density);
        ui_input_frame_begin(in);
        fe->settings.video.filter = fe->video_filter;
        fe->settings.video.color_correct = fe->color_correct;
        fe->settings.audio.volume = fe->volume;
        fe->settings.audio.start_muted = fe->start_muted;
        fe->settings.general.ff_speed = fe->ff_speed;
        fe->settings.general.pause_on_blur = fe->pause_on_blur;
        fe->settings.controls.pad_connected = fe->controller != NULL;
        {
            const char* bp = fe->prefs ? prefs_get(fe->prefs, "bios_path") : NULL;
            snprintf(fe->settings.general.bios_path, sizeof(fe->settings.general.bios_path), "%s", bp ? bp : "");
            const char* hp = fe->prefs ? prefs_get(fe->prefs, "hgss_rom") : NULL;
            snprintf(fe->settings.general.hgss_path, sizeof(fe->settings.general.hgss_path), "%s", hp ? hp : "");
        }
        fe->settings.general.hgss_ready = fe->hgss && fe->hgss->ok;
        fe->settings.general.follower = fe->follower.enabled;
        settings_draw(&canvas, in, &fe->settings, &fe->settings_ctx);
        ui_input_frame_end(in);
        if (fe->settings.video.changed) {
            fe->settings.video.changed = false;
            frontend_set_video(fe, fe->settings.video.filter, fe->settings.video.color_correct);
            settle = true;   /* redraw so the buttons show the new choice */
        }
        if (fe->settings.audio.changed) {
            fe->settings.audio.changed = false;
            frontend_set_audio(fe, fe->settings.audio.volume, fe->settings.audio.start_muted);
            settle = true;
        }
        if (fe->settings.general.changed) {
            fe->settings.general.changed = false;
            frontend_set_general(fe, fe->settings.general.ff_speed, fe->settings.general.pause_on_blur);
            settle = true;
        }
        if (fe->settings.controls.changed) {
            fe->settings.controls.changed = false;
            if (fe->keymap_path && fe->keymap_path[0] && !keymap_save(fe->keymap_path))
                frontend_toast(fe, TOAST_ERROR, "Couldn't save controls", NULL);
            release_gba_keys(fe, gba);
            settle = true;
        }
        SettingsGeneralPage* gp = &fe->settings.general;
        if (gp->choose_bios) {
            gp->choose_bios = false;
            char path[SETTINGS_PATH_LEN];
            RomPickResult r = rom_picker_choose_bios(path, sizeof(path));
            if (r == ROM_PICK_OK) {
                if (fe->prefs && prefs_set(fe->prefs, "bios_path", path)) {
                    snprintf(gp->bios_path, sizeof(gp->bios_path), "%s", path);
                    prefs_save(fe->prefs);
                    gp->status[0] = '\0';
                } else {
                    snprintf(gp->status, sizeof(gp->status), "Couldn't save the BIOS setting.");
                }
            } else if (r == ROM_PICK_UNAVAILABLE) {
                snprintf(gp->status, sizeof(gp->status), "No file picker on this system. Use --bios instead.");
            } else {
                gp->status[0] = '\0';
            }
            /* The picker is modal: drop the press that opened it. */
            fe->ui_input.down = false;
            fe->ui_input.active = 0;
            settle = true;
        }
        if (gp->choose_hgss) {
            gp->choose_hgss = false;
            frontend_choose_hgss(fe, gba);
            fe->ui_input.down = false;   /* the picker is modal: drop the press that opened it */
            fe->ui_input.active = 0;
            settle = true;
        }
        if (gp->follower_changed) {
            gp->follower_changed = false;
            if (gp->follower != fe->follower.enabled) frontend_toggle_follower(fe, gba);
            settle = true;
        }
        if (gp->clear_bios) {
            gp->clear_bios = false;
            if (fe->prefs && prefs_set(fe->prefs, "bios_path", "")) {
                prefs_save(fe->prefs);
                gp->bios_path[0] = '\0';
                gp->status[0] = '\0';
            }
            settle = true;
        }
        SDL_UpdateTexture(fe->settings_texture, NULL, fe->settings_buffer, fe->settings_px_w * (int)sizeof(uint32_t));
        fe->settings_dirty = settle;
        fe->settings_blink = text_focus ? (in->now_ms - in->caret_ms) / UI_CARET_MS : 0;
    }
    if (fe->settings.close_requested) frontend_settings_close(fe, gba);
}

void frontend_poll_input(Frontend* fe, GBA* gba) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* SDL_QUIT must always be honored, even when a modal is open. */
        if (event.type == SDL_QUIT) {
            fe->running = false;
            gba->running = false;
            continue;
        }

        /* Window events come before the slot picker, which swallows everything,
         * so the layout still follows resizes while it is open. */
        if (event.type == SDL_WINDOWEVENT) {
            frontend_handle_window_event(fe, &event);
            continue;
        }

        if (frontend_settings_shortcut(fe, &event)) {
            if (fe->settings.open) frontend_settings_close(fe, gba);
            else frontend_settings_open(fe, gba);
            continue;
        }
        bool hotplug = event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED;
        if (fe->settings.open && hotplug) fe->settings_dirty = true;   /* Controls shows whether a pad is connected */
        if (fe->settings.open && !hotplug) {
            settings_handle_event(fe, &event);
            continue;
        }

        /* If a slot-picker modal is open, route events to it first. Hotplug
         * still reaches the switch below so a removed pad is closed. */
        if (slot_picker_is_open(fe) && !hotplug) {
            if (slot_picker_handle_event(fe, gba, &event)) {
                continue;
            }
        }

        switch (event.type) {
        case SDL_KEYDOWN: {
            uint16_t key = keymap_lookup(event.key.keysym.scancode);
            if (key) {
                fe->kb_keys |= key;
                frontend_apply_input(fe, gba);
            }

            // Escape to quit
            /* Not on repeat: a held Esc that just closed Settings must not quit. */
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat) {
                fe->running = false;
                gba->running = false;
            }

#ifdef ENABLE_XRAY
            // F2 toggles X-Ray mode
            if (event.key.keysym.scancode == SDL_SCANCODE_F2 && g_xray) {
                xray_toggle(g_xray);
            }
#endif

            // F3 toggles input display HUD
            if (event.key.keysym.scancode == SDL_SCANCODE_F3 &&
                !event.key.repeat) {
                fe->input_display_enabled = !fe->input_display_enabled;
                LOG_INFO("Input display %s",
                         fe->input_display_enabled ? "ON" : "OFF");
                frontend_toast_keyed(fe, "input", TOAST_INFO,
                                     fe->input_display_enabled ? "Input display on" : "Input display off",
                                     NULL);
            }

            if (event.key.keysym.scancode == SDL_SCANCODE_F1 && !event.key.repeat)
                frontend_toggle_follower(fe, gba);
            if (event.key.keysym.scancode == SDL_SCANCODE_F4 && !event.key.repeat)
                frontend_toggle_overworld(fe, gba);

            if (event.key.keysym.scancode == SDL_SCANCODE_F9 && !event.key.repeat) {
                fe->panel_visible = !fe->panel_visible;
                frontend_apply_layout(fe);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F10 && !event.key.repeat) {
                fe->panel_page = (uint8_t)game_panel_next_page((GamePage)fe->panel_page);
                fe->panel_follow.restore = false;
            }
            /* [ and ] move the party selection (or switch Nuzlocke views) unless the keymap
             * binds them; N starts a Nuzlocke run from its page. */
            GamePage shown_page = game_panel_resolve_page((GamePage)fe->panel_page, fe->last_snap.context);
            if ((event.key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET ||
                 event.key.keysym.scancode == SDL_SCANCODE_RIGHTBRACKET) &&
                fe->panel_visible && keymap_lookup(event.key.keysym.scancode) == 0) {
                int delta = event.key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET ? -1 : 1;
                if (shown_page == GAME_PAGE_PARTY)
                    fe->panel_sel = game_panel_select(fe->panel_sel, delta, fe->last_snap.party_count);
                else if (shown_page == GAME_PAGE_NUZLOCKE)
                    fe->nuz_sub = game_panel_nuz_sub(fe->nuz_sub, delta);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_N && !event.key.repeat && fe->panel_visible &&
                shown_page == GAME_PAGE_NUZLOCKE && fe->nuz && keymap_lookup(SDL_SCANCODE_N) == 0 &&
                nuz_session_request_start(fe->nuz))
                frontend_toast(fe, TOAST_OK, "Nuzlocke run started", "Your current Pok\xC3\xA9mon joined the run");

            // Save state and screenshot files are named after the ROM; main.c clears
            // rom_path when it was too long to keep.
            bool has_rom_path = fe->rom_path[0] != '\0';
            SDL_Scancode scan = event.key.keysym.scancode;
            if (!has_rom_path && !event.key.repeat &&
                (scan == SDL_SCANCODE_F5 || scan == SDL_SCANCODE_F6 || scan == SDL_SCANCODE_F7 ||
                 scan == SDL_SCANCODE_F8 || scan == SDL_SCANCODE_F12)) {
                frontend_toast(fe, TOAST_WARN, "Savestates are off", "ROM path is too long");
            }

            // Save state hotkeys
            if (has_rom_path && scan == SDL_SCANCODE_F5) {
                fe->save_requested = true;
            }
            /* The picker swallows key-ups, so release held keys on open. */
            if (has_rom_path && scan == SDL_SCANCODE_F6 && !event.key.repeat) {
                release_gba_keys(fe, gba);
                slot_picker_open_label_edit(fe);
            }
            if (has_rom_path && scan == SDL_SCANCODE_F7 && !event.key.repeat) {
                release_gba_keys(fe, gba);
                slot_picker_open_list(fe);
            }
            if (has_rom_path && scan == SDL_SCANCODE_F8) {
                fe->load_requested = true;
            }

            // Screenshot hotkey
            if (has_rom_path && scan == SDL_SCANCODE_F12 && !event.key.repeat) {
                fe->screenshot_requested = true;
            }

            // Slot selection (number keys 0-9)
            // SDL scancodes: 1-9 are 30-38, 0 is 39 (not contiguous)
            {
                int32_t slot = -1;
                SDL_Scancode sc = event.key.keysym.scancode;
                if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
                    slot = sc - SDL_SCANCODE_1 + 1;
                } else if (sc == SDL_SCANCODE_0) {
                    slot = 0;
                }
                if (slot >= 0 && slot != fe->savestate_slot) {
                    fe->savestate_slot = slot;
                    LOG_INFO("Save state slot: %d", slot);
                    char detail[16];
                    snprintf(detail, sizeof(detail), "Slot %d", (int)slot);
                    frontend_toast(fe, TOAST_INFO, "Slot selected", detail);
                }
            }

            // Fast-forward: backtick toggles, Tab holds
            if (event.key.keysym.scancode == SDL_SCANCODE_GRAVE &&
                !event.key.repeat) {
                fe->ff_toggle = !fe->ff_toggle;
                LOG_INFO("Fast-forward %s", fe->ff_toggle ? "ON" : "OFF");
                frontend_toast_keyed(fe, "ff", TOAST_INFO,
                                     fe->ff_toggle ? "Fast forward on" : "Fast forward off", NULL);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_TAB) {
                fe->ff_hold = true;
            }
#ifdef ENABLE_REWIND
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) {
                fe->rewind_hold = true;
            }
#endif
            if (event.key.keysym.scancode == SDL_SCANCODE_F11 &&
                !event.key.repeat) {
                fe->fullscreen = !fe->fullscreen;
                SDL_SetWindowFullscreen(fe->window,
                    fe->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                frontend_apply_layout(fe);
                LOG_INFO("Fullscreen %s", fe->fullscreen ? "ON" : "OFF");
                frontend_toast_keyed(fe, "fullscreen", TOAST_INFO,
                                     fe->fullscreen ? "Fullscreen on" : "Fullscreen off", NULL);
            }

            // Pause toggle
            if (event.key.keysym.scancode == SDL_SCANCODE_SPACE &&
                !event.key.repeat) {
                fe->paused = !fe->paused;
                if (!fe->paused) fe->step_pending = false;  // discard any queued step
                LOG_INFO("Paused %s", fe->paused ? "ON" : "OFF");
                frontend_toast_keyed(fe, "pause", TOAST_INFO, fe->paused ? "Paused" : "Resumed", NULL);
            }

            // Frame advance: backslash steps one frame. Allow key-repeat so hold-to-step works.
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSLASH) {
                if (frame_advance_request(fe) == FRAME_ADVANCE_STEPPED) {
                    LOG_INFO("Frame advance");
                    frontend_toast(fe, TOAST_INFO, "Frame advance", NULL);
                }
            }

            // Mute toggle
            if (event.key.keysym.scancode == SDL_SCANCODE_M &&
                !event.key.repeat) {
                fe->muted = !fe->muted;
                LOG_INFO("Mute %s", fe->muted ? "ON" : "OFF");
                frontend_toast_keyed(fe, "mute", TOAST_INFO, fe->muted ? "Muted" : "Sound on", NULL);
            }
            break;
        }

        case SDL_KEYUP: {
            uint16_t key = keymap_lookup(event.key.keysym.scancode);
            if (key) {
                fe->kb_keys &= (uint16_t)~key;
                frontend_apply_input(fe, gba);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_TAB) {
                fe->ff_hold = false;
            }
#ifdef ENABLE_REWIND
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) {
                fe->rewind_hold = false;
            }
#endif
            break;
        }

        case SDL_CONTROLLERDEVICEADDED:
            if (!fe->controller && SDL_IsGameController(event.cdevice.which)) {
                fe->controller = SDL_GameControllerOpen(event.cdevice.which);
                if (fe->controller) {
                    LOG_INFO("Controller connected: %s",
                             SDL_GameControllerName(fe->controller));
                    frontend_toast(fe, TOAST_INFO, "Controller connected",
                                   SDL_GameControllerName(fe->controller));
                }
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (fe->controller &&
                event.cdevice.which ==
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(fe->controller))) {
                LOG_INFO("Controller disconnected");
                frontend_toast(fe, TOAST_INFO, "Controller disconnected", NULL);
                SDL_GameControllerClose(fe->controller);
                fe->controller = NULL;
                fe->pad_keys = 0;
                fe->stick_keys = 0;
                fe->lt_down = false;
                fe->rt_down = false;
                frontend_apply_input(fe, gba);
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            uint16_t key = keymap_pad_lookup(event.cbutton.button);
            if (key) {
                if (event.type == SDL_CONTROLLERBUTTONDOWN)
                    fe->pad_keys |= key;
                else
                    fe->pad_keys &= (uint16_t)~key;
                frontend_apply_input(fe, gba);
            }
            break;
        }

        case SDL_CONTROLLERAXISMOTION: {
            #define AXIS_THRESHOLD 8000
            int16_t v = event.caxis.value;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                fe->stick_keys &= (uint16_t)~(KEY_LEFT | KEY_RIGHT);
                if (v < -AXIS_THRESHOLD) fe->stick_keys |= KEY_LEFT;
                if (v > AXIS_THRESHOLD) fe->stick_keys |= KEY_RIGHT;
                frontend_apply_input(fe, gba);
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                fe->stick_keys &= (uint16_t)~(KEY_UP | KEY_DOWN);
                if (v < -AXIS_THRESHOLD) fe->stick_keys |= KEY_UP;
                if (v > AXIS_THRESHOLD) fe->stick_keys |= KEY_DOWN;
                frontend_apply_input(fe, gba);
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                frontend_trigger(fe, gba, &fe->lt_down, KEYMAP_PAD_LT, v);
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                frontend_trigger(fe, gba, &fe->rt_down, KEYMAP_PAD_RT, v);
            }
            #undef AXIS_THRESHOLD
            break;
        }
        }
    }
    /* Never blur-pause before the window first had focus (a launch from a
     * terminal can leave it unfocused) or while linked (one of two linked
     * windows is always unfocused). */
    bool focused = SDL_GetKeyboardFocus() != NULL;
    if (focused) fe->had_focus = true;
    fe->blur_paused = fe->pause_on_blur && fe->had_focus && !focused && !fe->link_active && !fe->settings.open;
}

void frontend_audio_init(Frontend* fe) {
    /* Open at the device's own rate and resample ourselves (audio_out.c), so
     * SDL does not insert its own converter. */
    int native_freq = 48000;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_AudioSpec native;
    if (SDL_GetDefaultAudioInfo(NULL, &native, 0) == 0 && native.freq > 0)
        native_freq = native.freq;
#endif

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = native_freq;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; /* Push mode via SDL_QueueAudio */

    fe->audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                           SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (fe->audio_device == 0) {
        LOG_WARN("SDL audio failed: %s", SDL_GetError());
        return;
    }

    /* Frame sync drains the queue down to this target each frame. It must
     * comfortably exceed one device pull (have.samples frames), or every
     * callback empties the queue and SDL pads with silence — audio comes out
     * as stutter/gaps. 3 pulls keeps >= 2 pulls of margin after each drain. */
    fe->audio_target_bytes =
        3u * (uint32_t)have.samples * have.channels * (uint32_t)sizeof(int16_t);

    audio_out_init(&fe->audio_out, CPU_FREQ / DEFAULT_SAMPLE_PERIOD, (uint32_t)have.freq);
    fe->audio_rate = (uint32_t)have.freq;
    fe->audio_stopped = true;

    SDL_PauseAudioDevice(fe->audio_device, 0);
    LOG_INFO("Audio initialized: %d Hz, %d channels", have.freq, have.channels);
}

/* Output frames per chunk; input chunks are sized so any device rate fits. */
#define AUDIO_CHUNK_FRAMES 2048
static int16_t s_audio_chunk[AUDIO_CHUNK_FRAMES * 2];
static int16_t s_scaled[SAMPLE_BUFFER_SIZE * 2];   /* input samples scaled by the volume */
static int16_t s_cry_chunk[AUDIO_CHUNK_FRAMES * 2];   /* the follower's cry before the volume */

/* Adds the follower's cry to n output frames at the user's volume. */
static void frontend_mix_cry(Frontend* fe, int16_t* out, uint32_t n) {
    CryPlayer* c = &fe->follower.cry;
    if (!c->playing || n > AUDIO_CHUNK_FRAMES) return;
    memset(s_cry_chunk, 0, n * 2 * sizeof(int16_t));
    cry_mix(c, s_cry_chunk, n, fe->audio_rate);
    if (fe->volume < 100) settings_audio_scale(s_cry_chunk, s_cry_chunk, n * 2, fe->volume);
    for (uint32_t i = 0; i < n * 2; i++) {
        int32_t v = out[i] + s_cry_chunk[i];
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

void frontend_audio_stop(Frontend* fe, APU* apu) {
    apu->read_pos = apu->write_pos;
    if (fe->audio_device == 0 || fe->audio_stopped) return;
    /* Appended after what is already queued, so the ramp starts exactly where
     * playback leaves off. */
    uint32_t n = audio_out_fade_out(&fe->audio_out, s_audio_chunk);
    SDL_QueueAudio(fe->audio_device, s_audio_chunk, n * 2 * (uint32_t)sizeof(int16_t));
    fe->audio_stopped = true;
}

void frontend_push_audio(Frontend* fe, APU* apu) {
    if (fe->audio_device == 0) return;

    if (fe->muted) {
        /* Drain APU ring without queueing to SDL so emulation pacing still works */
        frontend_audio_stop(fe, apu);
        return;
    }
    fe->audio_stopped = false;

    uint32_t max_in = (uint32_t)((AUDIO_CHUNK_FRAMES - 2) * fe->audio_out.step);
    if (max_in == 0) max_in = 1;
    while (apu->read_pos != apu->write_pos) {
        /* Contiguous run up to the ring's end or the writer. */
        uint32_t end = apu->write_pos > apu->read_pos ? apu->write_pos : SAMPLE_BUFFER_SIZE;
        uint32_t count = end - apu->read_pos;
        if (count > max_in) count = max_in;
        const int16_t* in = &apu->sample_buffer[apu->read_pos * 2];
        if (fe->volume < 100) {
            settings_audio_scale(s_scaled, in, count * 2, fe->volume);
            in = s_scaled;
        }
        uint32_t n = audio_out_process(&fe->audio_out, in, count, s_audio_chunk);
        frontend_mix_cry(fe, s_audio_chunk, n);
        SDL_QueueAudio(fe->audio_device, s_audio_chunk, n * 2 * (uint32_t)sizeof(int16_t));
        apu->read_pos = (apu->read_pos + count) % SAMPLE_BUFFER_SIZE;
    }
}

void frontend_pace(Frontend* fe, uint32_t speed) {
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t wait = frame_pacer_wait(&fe->pacer, SDL_GetPerformanceCounter(), frame_pacer_period(freq, speed));
    uint64_t ms = wait * 1000 / freq;
    if (ms > 1) SDL_Delay((uint32_t)(ms - 1));
    while (SDL_GetPerformanceCounter() < fe->pacer.next) {}
}

void frontend_frame_sync(Frontend* fe) {
    if (fe->audio_device != 0 && !fe->muted && !fe->audio_stopped) {
        /* Audio-driven sync: block until SDL's audio queue drains.
         * This ties emulation speed to the audio playback rate (~60fps).
         * Target is sized from the device pull size (see frontend_audio_init);
         * a fixed small target underruns on devices with large I/O buffers.
         * Cap iterations to avoid hanging if the audio device stalls. */
        uint32_t target = fe->audio_target_bytes;
        int max_wait = 100;
        while (SDL_GetQueuedAudioSize(fe->audio_device) > target && max_wait > 0) {
            SDL_Delay(1);
            max_wait--;
        }
        frame_pacer_reset(&fe->pacer);
    } else {
        /* Muted, no audio device, or not feeding audio (paused, Settings,
         * rewind): the queue is empty, so pace by deadline at the GBA frame rate. */
        frontend_pace(fe, 1);
    }
}
