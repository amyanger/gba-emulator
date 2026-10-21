#include "gba.h"
#include "cpu/arm7tdmi.h"
#include "cheat/cheat_backup.h"
#include "cheat/cheat_file.h"
#include "cheat/cheat_report.h"
#include "cheat/cheat_session.h"
#include "frontend/frontend.h"
#include "frontend/follower_glue.h"
#include "frontend/overlay_draw.h"
#include "frontend/input_display.h"
#include "frontend/slot_picker.h"
#include "frontend/toast.h"
#include "cartridge/rom_patch.h"
#include "cartridge/rom_zip.h"
#include "frontend/game_panel.h"
#include "frontend/rom_picker.h"
#include "frontend/run_session.h"
#include "frontend/library_run.h"
#include "frontend/overworld_draw.h"
#include "frontend/prefs.h"
#include "frontend/video_filter.h"
#include "frontend/trainer_card_draw.h"
#include "game/feebas.h"
#include "game/follower.h"
#include "game/game.h"
#include "game/overworld.h"
#include "headless/headless.h"
#include "mp2k/mp2k_detect.h"
#include "mp2k/mp2k_mixer.h"
#ifdef ENABLE_REWIND
#include "rewind/rewind.h"
#endif
#include "input/keymap.h"
#include "savestate/savestate.h"
#include "screenshot/screenshot.h"
#include "sio/sio_link.h"
#include "trace/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
static XRayState s_xray_state;
#endif

static GameState s_game;
static NuzSession s_nuz;
static bool s_nuz_on;        /* tracking runs in the window, and headless only with --nuzlocke-file */
static RunSession s_run;
static bool s_run_on;        /* run log in the window, and headless only with --run-file */
static HuntSession s_hunt;
static bool s_hunt_on;       /* shiny hunt in the window, and headless only with --hunt-file */
static bool s_rewinding;     /* nuz_tick runs on a rewound frame: no encounter edges */
static Frontend* s_toast_fe; /* shiny toasts; NULL in headless runs */
static OverworldFeed s_ow;
static Mp2kMixer s_mp2k;   /* ~112 KB of buffers: static, never on the stack */
static FollowerFeed s_fol;
static HgssAssets s_hgss;      /* ~150 KB: the HeartGold/SoulSilver ROM's follower data */
/* Search page results and the selected species' detail, rebuilt when the view's version moves. */
static uint16_t s_search_results[SPECIES_MAX];
static SpeciesDetail s_search_detail;
static uint32_t s_search_built_version = UINT32_MAX;   /* nothing built yet */
static uint16_t s_real_keys;   /* host keys held back from the game during a follower talk */
static uint16_t s_talk_keys;   /* keys held when a talk ended: hidden from the game until released */
static Prefs s_prefs;
static char s_keymap_path[PREFS_PATH_LEN]; /* <prefdir>/keymap.ini, empty when unavailable */

/* One emulated frame for the Nuzlocke tracker, the run log and the shiny hunt (also the
 * headless per-frame hook). The run log and the hunt use the facts the Nuzlocke session reads. */
static void nuz_tick(GBA* gba, void* ctx) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    NuzSession* nuz = (NuzSession*)ctx;
    if (s_nuz_on || s_run_on || s_hunt_on) nuz_session_frame(nuz, &m);
    if (s_run_on) run_session_frame(&s_run, &m, &nuz->facts);
    if (s_hunt_on) {
        char title[48], detail[48];
        if (hunt_session_frame(&s_hunt, &m, &nuz->facts, s_rewinding, title, detail) && s_toast_fe)
            frontend_toast(s_toast_fe, TOAST_OK, title, detail);
    }
}

/* Berry toasts, checked once a second whether or not the panel is open. */
static void berry_tick(Frontend* fe, GBA* gba) {
    static uint32_t frames;
    static BerryView view;   /* ~4 KB: keep it off the stack */
    if (!fe->berry_alerts || !game_has(s_game.profile, FEAT_BERRIES) || ++frames % 60 != 0) return;
    BerryToast toasts[BERRY_TOAST_MAX];
    game_read_berries(&s_game, gba, &view);
    uint8_t n = berry_alerts_update(&fe->berry_watch, &view, toasts, BERRY_TOAST_MAX);
    for (uint8_t i = 0; i < n; i++)
        frontend_toast_keyed(fe, toasts[i].key, TOAST_INFO, toasts[i].title, toasts[i].detail);
}

/* "Egg ready" toast, checked once a second whether or not the panel is open. */
static void daycare_tick(Frontend* fe, GBA* gba) {
    static uint32_t frames;
    static GameDaycare dc;   /* large: keep it off the stack */
    if (!s_game.profile || ++frames % 60 != 0) return;
    uint32_t pid = 0, sb1 = 0;
    bool ok = game_read_daycare_egg(&s_game, gba, &pid, &sb1);
    if (!daycare_alerts_update(&fe->daycare_watch, ok, pid, sb1, game_in_battle(&s_game, gba))) return;
    char detail[48];
    game_read_daycare_now(&s_game, gba, &dc);
    if (dc.egg_known) {
        char name[GAME_NAME_LEN];
        game_panel_title_case(dc.egg_species, name, sizeof(name));
        snprintf(detail, sizeof(detail), "%s%s", name, dc.egg_shiny ? ", shiny" : "");
    } else {
        snprintf(detail, sizeof(detail), "%s", s_game.profile->rules->daycare_toast);
    }
    frontend_toast_keyed(fe, "daycare:egg", TOAST_INFO, "Egg ready at the Daycare", detail);
}

/* Following Pokemon around one emulated frame: place the follower before it runs and hold
 * game input during a talk; afterwards restore input and advance the talk. */
static void follower_pre_frame(GBA* gba, void* ctx) {
    Frontend* fe = (Frontend*)ctx;
    s_real_keys = gba->input.keyinput;
    if (!fe->follower.enabled) return;
    follower_glue_frame_begin(fe, gba);
    uint16_t held = (uint16_t)(~s_real_keys & 0x03FF);
    if (talk_active(&fe->follower.talk)) {
        s_talk_keys = held;
        gba->input.keyinput = 0x03FF;
    } else {
        /* A held through the last page must not reach the game as a new press and talk again. */
        s_talk_keys &= held;
        gba->input.keyinput |= s_talk_keys;
    }
}

static void follower_post_frame(GBA* gba, void* ctx) {
    Frontend* fe = (Frontend*)ctx;
    gba->input.keyinput = s_real_keys;
    if (fe->follower.enabled) follower_glue_frame_end(fe, gba, s_real_keys);
}

/* Headless per-frame hooks (ctx = Frontend). */
static void headless_after(GBA* gba, void* ctx) {
    follower_post_frame(gba, ctx);
    nuz_tick(gba, &s_nuz);
}

/* The frame as the player sees it: with the follower's text box and emote during a talk or an
 * ambient bubble. gba.ppu.framebuffer itself stays untouched (hashes and F12 use it). */
static uint16_t* follower_composite(Frontend* fe, GBA* gba) {
    static uint16_t talk_fb[SCREEN_WIDTH * SCREEN_HEIGHT];
    const FollowerView* v = &fe->follower;
    if (!v->enabled || !fe->fol_feed || (!talk_active(&v->talk) && !ambient_active(&v->ambient)))
        return gba->ppu.framebuffer;
    memcpy(talk_fb, gba->ppu.framebuffer, sizeof(talk_fb));
    follower_view_overlay(&fe->follower, fe->fol_feed, talk_fb);
    return talk_fb;
}

/* File name without its directory, for short toast details. */
static const char* path_base(const char* p) {
    const char* s = strrchr(p, '/');
    const char* b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : p;
}

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static bool has_patch_ext(const char* p) {
    const char* dot = strrchr(p, '.');
    if (!dot) return false;
    char e[4] = {0};
    for (int i = 0; i < 3 && dot[1 + i]; i++) e[i] = (char)(dot[1 + i] | 0x20);
    return strlen(dot) == 4 && (!strcmp(e, "bps") || !strcmp(e, "ups") || !strcmp(e, "ips"));
}

/* Set by load_game for the startup toasts. */
static char s_patch_warn[48];
static bool s_patch_sep_save;

/* Picks the ROM/patch pair (--patch, an opened patch, then a sibling patch) and loads it. A bad
 * sibling patch falls back to the plain ROM; any other patch failure fails with a reason. */
static bool load_game(GBA* gba, const char* in, const char* explicit_patch, bool probe, char* base,
                      char* patch, size_t n, char* reason, size_t reason_n) {
    bool sibling = false;
    patch[0] = '\0';
    s_patch_warn[0] = '\0';
    s_patch_sep_save = false;
    snprintf(reason, reason_n, "Couldn't load %s as a GBA ROM", path_base(in));
    int w = snprintf(base, n, "%s", in);
    if (w < 0 || (size_t)w >= n) return false;
    if (explicit_patch) {
        w = snprintf(patch, n, "%s", explicit_patch);
    } else if (has_patch_ext(in)) {
        w = snprintf(patch, n, "%s", in);
        int stem = (int)(strrchr(in, '.') - in);
        static const char* const exts[] = {".gba", ".GBA", ".zip", ".ZIP"};
        for (size_t i = 0; i < 4; i++) {
            snprintf(base, n, "%.*s%s", stem, in, exts[i]);
            if (file_exists(base)) break;
        }
        if (!file_exists(base)) {
            snprintf(base, n, "%.*s.gba", stem, in);
            snprintf(reason, reason_n, "No %s (or .zip) next to the patch", path_base(base));
            return false;
        }
    } else if (probe && cartridge_find_patch(in, patch, n)) {
        sibling = true;
    }
    if (w < 0 || (size_t)w >= n) {
        patch[0] = '\0';
        return false;
    }
    if (patch[0]) {
        char a[1100], b[1100];
        snprintf(a, sizeof(a), "%s.sav", base);
        snprintf(b, sizeof(b), "%s.sav", patch);
        s_patch_sep_save = file_exists(a) && !file_exists(b);
    }
    if (gba_load_rom(gba, base, patch[0] ? patch : NULL)) return true;
    if (gba->cart.zip_status != ROM_ZIP_OK) {
        snprintf(reason, reason_n, "Couldn't open %s: %s", path_base(base),
                 rom_zip_reason(gba->cart.zip_status));
        return false;
    }
    if (patch[0] && gba->cart.patch_status != ROM_PATCH_OK) {
        if (!sibling) {
            snprintf(reason, reason_n, "Couldn't apply %s: %s", path_base(patch),
                     rom_patch_reason(gba->cart.patch_status));
            return false;
        }
        snprintf(s_patch_warn, sizeof(s_patch_warn), "%s", rom_patch_reason(gba->cart.patch_status));
        LOG_WARN("Patch %s not applied: %s", patch, s_patch_warn);
        patch[0] = '\0';
        s_patch_sep_save = false;
        return gba_load_rom(gba, base, NULL);
    }
    return false;
}

static void settings_toast(void* user, ToastKind kind, const char* title, const char* detail) {
    frontend_toast((Frontend*)user, kind, title, detail);
}

/* Serves a C / Shift+C press. Save blocks move on warps, so the card is only made from a
 * read that matches the previous pass; paused, the second pass reads the same memory. */
static void card_tick(Frontend* fe, GBA* gba) {
    static TrainerCardData prev, cur;
    static uint8_t tries;
    GameMem gm = {gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size};
    CardStatus st = trainer_card_read(&s_game, &gm, &cur);
    const char* warn = st == CARD_NOT_SUPPORTED ? "Trainer cards need a supported Pok\xC3\xA9mon game"
                     : st == CARD_NO_TEAM     ? "No team to show yet"
                     : st == CARD_FACILITY    ? "Not available here"
                                              : NULL;
    if (warn) {
        frontend_toast(fe, TOAST_WARN, warn, NULL);
        fe->card_requested = 0;
        tries = 0;
        return;
    }
    if (tries == 0 || cur.sig != prev.sig) {
        prev = cur;
        if (++tries > 10) {
            frontend_toast(fe, TOAST_ERROR, "Couldn't read your team", "Try again in a moment");
            fe->card_requested = 0;
            tries = 0;
        }
        return;
    }
    int sel = -1;
    if (fe->panel_visible &&
        game_panel_resolve_page((GamePage)fe->panel_page, s_game.snap.context) == GAME_PAGE_PARTY)
        sel = fe->panel_sel;
    NuzKey mvp;
    bool has_mvp = run_session_mvp_key(&s_run, &mvp);
    int ace = trainer_card_pick_ace(&cur, sel, has_mvp ? &mvp : NULL);
    CardStyle style = (CardStyle)(fe->card_requested - 1);
    char path[512];
    trainer_card_path(fe->rom_path, time(NULL), path, sizeof(path));
    if (trainer_card_save(&s_game, &gm, &cur, style, ace, path)) {
        LOG_INFO("Trainer card saved: %s", path);
        frontend_toast(fe, TOAST_OK, "Trainer card saved", path_base(path));
    } else {
        frontend_toast(fe, TOAST_ERROR, "Couldn't save trainer card", NULL);
    }
    fe->card_requested = 0;
    tries = 0;
}

/* Runs the query and the selected species' detail again after the Search view changed.
 * Both come from the ROM only; caught marks are drawn from the snapshot. */
static void search_rebuild(Frontend* fe, GBA* gba) {
    SearchView* v = &fe->search;
    if (!s_game.search.ok || v->version == s_search_built_version) return;
    search_view_set_results(v, species_search_query(&s_game.search, v->query, s_search_results));
    if (v->result_count > 0) {
        GameMem rom = {NULL, NULL, gba->cart.rom, gba->cart.rom_size};
        species_search_detail(&s_game.search, s_game.profile, &rom, s_search_results[v->sel], &s_search_detail);
        uint16_t visible = 0;
        uint16_t lines = game_panel_search_detail_lines(&s_search_detail, &visible);
        search_view_set_detail_lines(v, lines, visible);
    }
    s_search_built_version = v->version;
}

/* Present one frame: clear overlay, render HUD layers, then blit to screen. */
static void render_with_overlay(Frontend* fe, GBA* gba) {
    frontend_overlay_clear(fe);
    input_display_render(fe, gba);
    slot_picker_render(fe);
    if (fe->panel_visible && fe->panel_buffer) {
        game_update(&s_game, gba);
        /* Tracked only while visible: the next update after F9 still sees the change.
         * While the player types a search, a battle does not take the page away. */
        fe->panel_follow.hold = fe->search.focused;
        fe->panel_page = (uint8_t)game_panel_follow_battle((GamePage)fe->panel_page,
                                                           s_game.snap.context, &fe->panel_follow);
        search_rebuild(fe, gba);
        /* Caret blink: only while typing on the shown Search page. */
        uint32_t caret_phase = 0;
        if (fe->search.focused && frontend_search_shown(fe))
            caret_phase = (SDL_GetTicks() - fe->search_caret_ms) / UI_CARET_MS;
        /* Run changes only repaint while the Run page is shown; switching to it repaints anyway. */
        GamePage shown = game_panel_resolve_page((GamePage)fe->panel_page, s_game.snap.context);
        /* The battle target starts on the first foe and your first mon each battle; [ ] and , . pick them. */
        if (s_game.snap.context != GAME_CTX_BATTLE_WILD &&
            s_game.snap.context != GAME_CTX_BATTLE_TRAINER) {
            fe->foe_sel = 0;
            fe->ally_sel = 0;
        }
        uint8_t sel = shown == GAME_PAGE_COUNT ? (uint8_t)(fe->ally_sel * 2 + fe->foe_sel) : fe->panel_sel;
        bool run_shown = shown == GAME_PAGE_RUN;
        /* The hunt scans and rebuilds its view only while its page is shown (R10). */
        bool hunt_shown = shown == GAME_PAGE_HUNT;
        hunt_session_set_shown(&s_hunt, hunt_shown);
        if (hunt_shown) {
            GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
            hunt_session_present(&s_hunt, &m, SDL_GetTicks(), 16384);
        }
        /* Steps tick every step, so the Daycare is read (and compared) only while it shows. */
        static GameDaycare daycare;
        bool daycare_shown = shown == GAME_PAGE_PARTY;
        if (daycare_shown) game_read_daycare(&s_game, gba, &daycare);
        else {
            memset(&daycare, 0, sizeof(daycare));
            game_daycare_forget(&s_game);
        }
        /* Redraw only when something on the panel can have changed. */
        if (fe->panel_dirty || fe->last_page != fe->panel_page || fe->last_sel != sel ||
            fe->last_nuz_sub != fe->nuz_sub || fe->last_nuz_version != s_nuz.view_version ||
            fe->last_berry_sel != fe->berry_sel || fe->last_berry_alerts != fe->berry_alerts ||
            fe->last_search_version != fe->search.version || fe->last_caret_phase != caret_phase ||
            (run_shown && (fe->last_run_version != s_run.view_version ||
                           fe->last_run_scroll != fe->run_scroll || fe->last_run_details != fe->run_details)) ||
            (hunt_shown && fe->last_hunt_version != hunt_session_view(&s_hunt)->version) ||
            (daycare_shown && memcmp(&fe->last_daycare, &daycare, sizeof(daycare)) != 0) ||
            memcmp(&fe->last_snap, &s_game.snap, sizeof(s_game.snap)) != 0) {
            UiCanvas canvas;
            ui_canvas_init(&canvas, fe->panel_buffer, fe->panel_px_w, fe->panel_px_h,
                           fe->panel_density);
            GamePanelSearch search = {&fe->search, &s_game.search, s_search_results,
                                      fe->search.result_count > 0 ? &s_search_detail : NULL,
                                      caret_phase % 2 == 0};
            game_panel_draw(&canvas, &s_game.snap, (GamePage)fe->panel_page, sel, &s_nuz.view,
                            fe->nuz_sub, fe->berry_sel, fe->berry_alerts, &search, &s_run.view,
                            fe->run_scroll, fe->run_details, hunt_session_view(&s_hunt),
                            daycare_shown ? &daycare : NULL);
            fe->last_hunt_version = hunt_session_view(&s_hunt)->version;
            fe->last_search_version = fe->search.version;
            fe->last_caret_phase = caret_phase;
            fe->last_nuz_sub = fe->nuz_sub;
            fe->last_run_version = s_run.view_version;
            fe->last_run_scroll = fe->run_scroll;
            fe->last_run_details = fe->run_details;
            fe->last_berry_sel = fe->berry_sel;
            fe->last_berry_alerts = fe->berry_alerts;
            fe->last_nuz_version = s_nuz.view_version;
            SDL_UpdateTexture(fe->panel_texture, NULL, fe->panel_buffer,
                              fe->panel_px_w * (int)sizeof(uint32_t));
            fe->last_snap = s_game.snap;
            fe->last_daycare = daycare;
            fe->last_page = fe->panel_page;
            fe->last_sel = sel;
            fe->panel_dirty = false;
        }
    } else {
        hunt_session_set_shown(&s_hunt, false);
        game_daycare_forget(&s_game);
    }
    frontend_update_settings(fe, gba);
    frontend_update_overworld(fe, fe->overworld_xray && fe->ow_feed ? &fe->ow_feed->shown : NULL);
    frontend_update_hud(fe, SDL_GetTicks());
    frontend_present_frame(fe, follower_composite(fe, gba));
}

static void print_usage(const char* prog) {
    printf("Usage: %s [rom.gba] [options]\n", prog);
    printf("Without a ROM the game library opens.\n");
    printf("Options:\n");
    printf("  --bios <file>          Load GBA BIOS ROM\n");
    printf("  --scale <n>            Window scale factor (default: 3)\n");
    printf("  --patch <file>         Apply an IPS, UPS or BPS patch to the ROM in memory\n");
    printf("  --cheats <file>        Cheat file to use instead of <rom>.cht (.cht format)\n");
    printf("  --cheat-report         Print how each cheat was read (device, codes) and exit\n");
    printf("  --keymap <file>        Load custom keyboard bindings\n");
    printf("  --mute                 Start with audio muted (toggle in-game with M)\n");
    printf("  --headless             Run without SDL window or audio\n");
    printf("  --frames <n>           Headless: number of frames to run\n");
    printf("  --hash-out <file>      Headless: write per-frame framebuffer hashes\n");
    printf("  --screenshot-out <f>   Headless: write final-frame screenshot to file\n");
    printf("  --input-script <file>  Headless: scripted keypad input (see README)\n");
    printf("  --game-dump <file>     Headless: write the decoded game state after the run\n");
    printf("  --panel-out <file>     Headless: render the Pokemon Companion panel to a PNG after the run\n");
    printf("  --panel-density <f>    Headless: panel pixels per point, 0.5 to 4.0 (default: 1.0)\n");
    printf("  --panel-page <p>       Headless: panel page, auto|party|route|nuzlocke|berries|search|hunt|run\n");
    printf("                         (default: auto)\n");
    printf("  --panel-search <q>     Headless: Search page with this query typed in (implies --panel-page search)\n");
    printf("  --panel-select <n>     Headless: party slot (0-8; Daycare rows follow the party), Nuzlocke view 0-2 (routes, team, graveyard),\n");
    printf("                         the selected Search result (0 is the first), or 1 for Run details\n");
    printf("  --nuzlocke-file <f>    Headless: track a Nuzlocke run in this file, starting one if it has none\n");
    printf("  --run-file <f>         Headless: keep the run log (Run page) in this file\n");
    printf("  --hunt-file <f>        Headless: keep the shiny hunt (Hunt page) in this file\n");
    printf("  --library-folder <dir> Game folder for the library, used when none has been chosen yet\n");
    printf("  --library-out <file>   Render the library for --library-folder to a PNG and exit (no ROM)\n");
    printf("  --xray-out <file>      Headless: render the Hardware X-Ray window to a PNG after the run\n");
    printf("  --overworld-out <f>    Headless: final frame at 3x with the Overworld X-Ray drawn, as PNG\n");
    printf("  --card-out <file>      Headless: render the trainer card (team PNG, 1200x630) after the run\n");
    printf("  --card-style <s>       Headless: trainer card style, grid|ace (default: grid; --panel-select picks the ace)\n");
    printf("  --video-filter <name>  Screen filter, sharp|smooth|soft (overrides Settings for this run)\n");
    printf("  --color-correct        LCD color correction (overrides Settings for this run)\n");
    printf("  --video-out <file>     Headless: final frame through the screen filter for --scale, as PNG\n");
    printf("  --gift-audit <file>    Headless: list every NPC gift tag in the ROM (Emerald only)\n");
    printf("  --follower-hgss <rom>  Headless: Following Pokemon on, using this HeartGold/SoulSilver ROM\n");
    printf("  --link-master <path>   Listen for SIO peer at AF_UNIX path\n");
    printf("  --link-client <path>   Connect to SIO peer at AF_UNIX path\n");
    printf("  --trace <file>         Write per-instruction trace to file\n");
    printf("  --trace-from <hex>     Only trace instructions at PC >= hex\n");
    printf("  --trace-to <hex>       Only trace instructions at PC <= hex\n");
    printf("  --trace-frames <n>     Stop tracing after n frames\n");
}

int main(int argc, char* argv[]) {
    bool console_hidden = rom_picker_hide_console();

    /* No ROM argument (a double-click, or a launcher passing only options) opens the library. */
    static char picked_rom[1024];
    const char* rom_path = argc >= 2 && strncmp(argv[1], "--", 2) != 0 ? argv[1] : NULL;
    bool no_rom = rom_path == NULL;
    /* No terminal to read stderr: report failures in a dialog instead. */
    bool no_terminal = no_rom || console_hidden;
    const char* bios_path = NULL;
    const char* cheat_path = NULL;
    const char* patch_arg = NULL;
    bool cheat_report = false;
    const char* keymap_path = NULL;
    const char* link_master_path = NULL;
    const char* link_client_path = NULL;
    const char* trace_path = NULL;
    uint32_t trace_from = 0;
    uint32_t trace_to = 0;
    uint32_t trace_frames = 0;
    int scale = 3;
    bool start_muted = false;
    bool headless = false;
    int headless_frames = 0;
    const char* hash_out_path = NULL;
    const char* screenshot_out_path = NULL;
    const char* input_script_path = NULL;
    const char* game_dump_path = NULL;
    const char* panel_out_path = NULL;
    const char* panel_page_arg = NULL;
    const char* panel_density_arg = NULL;
    const char* panel_select_arg = NULL;
    const char* panel_search_arg = NULL;
    const char* xray_out_path = NULL;
    const char* overworld_out_path = NULL;
    const char* card_out_path = NULL;
    const char* card_style_arg = NULL;
    const char* video_filter_arg = NULL;
    const char* video_out_path = NULL;
    bool color_correct_arg = false;
    const char* nuzlocke_file = NULL;
    const char* run_file = NULL;
    const char* hunt_file = NULL;
    const char* gift_audit_path = NULL;
    const char* library_folder = NULL;
    const char* library_out_path = NULL;
    const char* follower_hgss_path = NULL;

    // Parse arguments
    for (int i = no_rom ? 1 : 2; i < argc; i++) {
        if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
            bios_path = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--patch") == 0 && i + 1 < argc) {
            patch_arg = argv[++i];
        } else if (strcmp(argv[i], "--cheats") == 0 && i + 1 < argc) {
            cheat_path = argv[++i];
        } else if (strcmp(argv[i], "--keymap") == 0 && i + 1 < argc) {
            keymap_path = argv[++i];
        } else if (strcmp(argv[i], "--link-master") == 0 && i + 1 < argc) {
            link_master_path = argv[++i];
        } else if (strcmp(argv[i], "--link-client") == 0 && i + 1 < argc) {
            link_client_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (strcmp(argv[i], "--trace-from") == 0 && i + 1 < argc) {
            trace_from = (uint32_t)strtoul(argv[++i], NULL, 16);
        } else if (strcmp(argv[i], "--trace-to") == 0 && i + 1 < argc) {
            trace_to = (uint32_t)strtoul(argv[++i], NULL, 16);
        } else if (strcmp(argv[i], "--trace-frames") == 0 && i + 1 < argc) {
            trace_frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--cheat-report") == 0) {
            cheat_report = true;
        } else if (strcmp(argv[i], "--mute") == 0) {
            start_muted = true;
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            headless_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hash-out") == 0 && i + 1 < argc) {
            hash_out_path = argv[++i];
        } else if (strcmp(argv[i], "--screenshot-out") == 0 && i + 1 < argc) {
            screenshot_out_path = argv[++i];
        } else if (strcmp(argv[i], "--input-script") == 0 && i + 1 < argc) {
            input_script_path = argv[++i];
        } else if (strcmp(argv[i], "--game-dump") == 0 && i + 1 < argc) {
            game_dump_path = argv[++i];
        } else if (strcmp(argv[i], "--panel-out") == 0 && i + 1 < argc) {
            panel_out_path = argv[++i];
        } else if (strcmp(argv[i], "--panel-density") == 0 && i + 1 < argc) {
            panel_density_arg = argv[++i];
        } else if (strcmp(argv[i], "--panel-page") == 0 && i + 1 < argc) {
            panel_page_arg = argv[++i];
        } else if (strcmp(argv[i], "--xray-out") == 0 && i + 1 < argc) {
            xray_out_path = argv[++i];
        } else if (strcmp(argv[i], "--overworld-out") == 0 && i + 1 < argc) {
            overworld_out_path = argv[++i];
        } else if (strcmp(argv[i], "--card-out") == 0 && i + 1 < argc) {
            card_out_path = argv[++i];
        } else if (strcmp(argv[i], "--card-style") == 0 && i + 1 < argc) {
            card_style_arg = argv[++i];
        } else if (strcmp(argv[i], "--video-filter") == 0 && i + 1 < argc) {
            video_filter_arg = argv[++i];
        } else if (strcmp(argv[i], "--color-correct") == 0) {
            color_correct_arg = true;
        } else if (strcmp(argv[i], "--video-out") == 0 && i + 1 < argc) {
            video_out_path = argv[++i];
        } else if (strcmp(argv[i], "--library-folder") == 0 && i + 1 < argc) {
            library_folder = argv[++i];
        } else if (strcmp(argv[i], "--library-out") == 0 && i + 1 < argc) {
            library_out_path = argv[++i];
        } else if (strcmp(argv[i], "--nuzlocke-file") == 0 && i + 1 < argc) {
            nuzlocke_file = argv[++i];
        } else if (strcmp(argv[i], "--run-file") == 0 && i + 1 < argc) {
            run_file = argv[++i];
        } else if (strcmp(argv[i], "--hunt-file") == 0 && i + 1 < argc) {
            hunt_file = argv[++i];
        } else if (strcmp(argv[i], "--gift-audit") == 0 && i + 1 < argc) {
            gift_audit_path = argv[++i];
        } else if (strcmp(argv[i], "--follower-hgss") == 0 && i + 1 < argc) {
            follower_hgss_path = argv[++i];
        } else if (strcmp(argv[i], "--panel-select") == 0 && i + 1 < argc) {
            panel_select_arg = argv[++i];
        } else if (strcmp(argv[i], "--panel-search") == 0 && i + 1 < argc) {
            panel_search_arg = argv[++i];
        } else {
            /* Also catches value-taking options with the value missing:
             * they fail the i + 1 < argc guard and fall through here. */
            LOG_ERROR("Unknown or incomplete option: %s", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (scale < 1 || scale > 10) {
        LOG_ERROR("--scale must be between 1 and 10 (got %d)", scale);
        return 1;
    }

    if (input_script_path && !headless) {
        LOG_ERROR("--input-script requires --headless");
        return 1;
    }

    if (game_dump_path && !headless) {
        LOG_ERROR("--game-dump requires --headless");
        return 1;
    }

    if ((panel_out_path || panel_density_arg || panel_page_arg || panel_select_arg || panel_search_arg) &&
        !headless) {
        LOG_ERROR("--panel-out, --panel-density, --panel-page, --panel-select and --panel-search require "
                  "--headless");
        return 1;
    }
    if (nuzlocke_file && !headless) {
        LOG_ERROR("--nuzlocke-file requires --headless");
        return 1;
    }
    if (run_file && !headless) {
        LOG_ERROR("--run-file requires --headless");
        return 1;
    }
    if (hunt_file && !headless) {
        LOG_ERROR("--hunt-file requires --headless");
        return 1;
    }
    if (xray_out_path && !headless) {
        LOG_ERROR("--xray-out requires --headless");
        return 1;
    }
    if (gift_audit_path && !headless) {
        LOG_ERROR("--gift-audit requires --headless");
        return 1;
    }
    if (overworld_out_path && !headless) {
        LOG_ERROR("--overworld-out requires --headless");
        return 1;
    }
    if ((card_out_path || card_style_arg) && !headless) {
        LOG_ERROR("--card-out and --card-style require --headless");
        return 1;
    }
    CardStyle card_style = CARD_STYLE_GRID;
    if (card_style_arg && !card_style_from_name(card_style_arg, &card_style)) {
        LOG_ERROR("--card-style must be grid or ace (got %s)", card_style_arg);
        return 1;
    }
    if (follower_hgss_path && !headless) {
        LOG_ERROR("--follower-hgss requires --headless (in the window, set the ROM in Settings > General)");
        return 1;
    }
    if (video_out_path && !headless) {
        LOG_ERROR("--video-out requires --headless");
        return 1;
    }
    VideoFilter video_filter = VIDEO_FILTER_SHARP;
    if (video_filter_arg && !video_filter_from_name(video_filter_arg, &video_filter)) {
        LOG_ERROR("--video-filter must be sharp, smooth or soft (got %s)", video_filter_arg);
        return 1;
    }
#ifndef ENABLE_XRAY
    if (xray_out_path) {
        LOG_ERROR("--xray-out needs X-Ray, which this build leaves out (ENABLE_XRAY=OFF)");
        return 1;
    }
#endif

    float panel_density = 1.0f;
    if (panel_density_arg) {
        char* end = NULL;
        panel_density = strtof(panel_density_arg, &end);
        if (end == panel_density_arg || *end != '\0' || !(panel_density >= 0.5f && panel_density <= 4.0f)) {
            LOG_ERROR("--panel-density must be between 0.5 and 4.0 (got %s)", panel_density_arg);
            return 1;
        }
    }

    GamePage panel_page = GAME_PAGE_AUTO;
    if (panel_page_arg) {
        if (strcmp(panel_page_arg, "party") == 0) {
            panel_page = GAME_PAGE_PARTY;
        } else if (strcmp(panel_page_arg, "route") == 0) {
            panel_page = GAME_PAGE_ENCOUNTERS;
        } else if (strcmp(panel_page_arg, "nuzlocke") == 0) {
            panel_page = GAME_PAGE_NUZLOCKE;
        } else if (strcmp(panel_page_arg, "berries") == 0) {
            panel_page = GAME_PAGE_BERRIES;
        } else if (strcmp(panel_page_arg, "search") == 0) {
            panel_page = GAME_PAGE_SEARCH;
        } else if (strcmp(panel_page_arg, "hunt") == 0) {
            panel_page = GAME_PAGE_HUNT;
        } else if (strcmp(panel_page_arg, "run") == 0) {
            panel_page = GAME_PAGE_RUN;
        } else if (strcmp(panel_page_arg, "auto") != 0) {
            LOG_ERROR("--panel-page must be auto, party, route, nuzlocke, berries, search, hunt or run (got %s)",
                      panel_page_arg);
            return 1;
        }
    }
    if (panel_search_arg) {
        if (panel_page_arg && panel_page != GAME_PAGE_SEARCH) {
            LOG_ERROR("--panel-search needs --panel-page search (got %s)", panel_page_arg);
            return 1;
        }
        panel_page = GAME_PAGE_SEARCH;
    }

    /* Search results are checked against the result count after the run. */
    uint8_t panel_select = 0;
    uint16_t search_select = 0;
    if (panel_select_arg) {
        char* end = NULL;
        long v = strtol(panel_select_arg, &end, 10);
        long max = panel_page == GAME_PAGE_SEARCH ? SPECIES_MAX - 1 : panel_page == GAME_PAGE_PARTY ? 8 : 5;
        if (end == panel_select_arg || *end != '\0' || v < 0 || v > max) {
            LOG_ERROR("--panel-select must be between 0 and %ld (got %s)", max, panel_select_arg);
            return 1;
        }
        if (panel_page == GAME_PAGE_SEARCH) search_select = (uint16_t)v;
        else panel_select = (uint8_t)v;
    }

    if (headless && (link_master_path || link_client_path)) {
        LOG_ERROR("--headless is incompatible with --link-master/--link-client");
        return 1;
    }

    if (library_out_path) {
        if (!library_render_png(library_folder, panel_density, library_out_path)) {
            LOG_ERROR("Failed to write --library-out file: %s", library_out_path);
            return 1;
        }
        return 0;
    }
    if (no_rom && headless) {
        LOG_ERROR("--headless needs a ROM");
        return 1;
    }

    /* The library runs in the game window, so the window opens before the ROM loads. */
    Frontend fe;
    bool fe_ready = false;
    static Library s_library;
    if (no_rom) {
        frontend_set_hints();
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (rom_picker_take_launch_drop(picked_rom, sizeof(picked_rom))) {
            rom_path = picked_rom;
        } else {
            /* No display to show the library on (CI, SSH): behave like a terminal launch. */
            const char* driver = SDL_GetCurrentVideoDriver();
            if (!driver || strcmp(driver, "dummy") == 0 || strcmp(driver, "offscreen") == 0) {
                SDL_Quit();
                print_usage(argv[0]);
                return 0;
            }
            if (!frontend_init(&fe, scale)) {
                LOG_ERROR("Failed to initialize frontend");
                rom_picker_alert("Couldn't open the game window.");
                return 1;
            }
            fe_ready = true;
            library_prepare(&s_library, library_folder);
            if (!library_run(&fe, &s_library, NULL, picked_rom, sizeof(picked_rom))) {
                frontend_destroy(&fe);
                return 0;
            }
            rom_path = picked_rom;
        }
    }

    // Initialize GBA
    GBA gba;
    gba_init(&gba);

    /* Prefs load before the BIOS so a saved BIOS file can apply. Headless never touches the user's prefs. */
    if (!headless) {
        char prefs_path[PREFS_PATH_LEN] = "";
        char* pref_dir = SDL_GetPrefPath("", "GBA Emulator");
        if (pref_dir) {
            int n = snprintf(prefs_path, sizeof(prefs_path), "%sprefs.ini", pref_dir);
            if (n < 0 || (size_t)n >= sizeof(prefs_path)) prefs_path[0] = '\0';
            n = snprintf(s_keymap_path, sizeof(s_keymap_path), "%skeymap.ini", pref_dir);
            if (n < 0 || (size_t)n >= sizeof(s_keymap_path)) s_keymap_path[0] = '\0';
            SDL_free(pref_dir);
        }
        prefs_init(&s_prefs, prefs_path);
        prefs_load(&s_prefs);
        LOG_INFO("Preferences: %s", prefs_path[0] ? prefs_path : "(none)");
    } else {
        prefs_init(&s_prefs, "");
    }
    const char* pref_bios = (!headless && !bios_path) ? prefs_get(&s_prefs, "bios_path") : NULL;
    bool bios_from_prefs = pref_bios && pref_bios[0];
    if (bios_from_prefs) bios_path = pref_bios;

    bool bios_loaded = false;
    if (bios_path) {
        if (!gba_load_bios(&gba, bios_path)) {
            LOG_WARN("Failed to load BIOS, continuing without it");
            cpu_skip_bios(&gba.cpu);
        } else {
            bios_loaded = true;
        }
    } else {
        // No BIOS provided — set CPU to post-BIOS state so execution
        // starts at the ROM entry point with correct stack pointers.
        cpu_skip_bios(&gba.cpu);
    }

    LinkPeer* link_peer = NULL;

    /* From the library, a file that won't load sends you back to it with the reason. */
    char base_path[1024], patch_file[1024], reason[160];
    while (fe_ready && !load_game(&gba, rom_path, patch_arg, !headless, base_path, patch_file,
                                  sizeof(base_path), reason, sizeof(reason))) {
        LOG_ERROR("Failed to load ROM: %s (%s)", rom_path, reason);
        patch_arg = NULL;
        if (!library_run(&fe, &s_library, reason, picked_rom, sizeof(picked_rom))) {
            frontend_destroy(&fe);
            gba_destroy(&gba);
            return 0;
        }
        rom_path = picked_rom;
    }
    if (!fe_ready && !load_game(&gba, rom_path, patch_arg, !headless, base_path, patch_file,
                                sizeof(base_path), reason, sizeof(reason))) {
        LOG_ERROR("Failed to load ROM: %s (%s)", rom_path, reason);
        if (no_terminal) rom_picker_alert(reason);
        return 1;
    }

    /* The library lists the base ROM; every per-game file follows the patch when one is applied. */
    if (!headless) library_record_launch(base_path);
    rom_path = gba.cart.id_path;
    if (gba.cart.patched) LOG_INFO("Patch applied: %s", rom_path);

    // Set up local link cable if requested. link_peer_listen blocks until a
    // peer connects, so we do this before the main loop starts.
    if ((link_master_path || link_client_path) && !link_lock_rom(rom_path)) {
        LOG_ERROR("%s is already open in another linked window. Link two copies of the ROM "
                  "(each keeps its own save), not the same file.", rom_path);
        gba_destroy(&gba);
        return 1;
    }
    if (link_master_path) {
        link_peer = link_peer_create();
        LOG_INFO("Listening for SIO peer at %s ...", link_master_path);
        if (!link_peer_listen(link_peer, link_master_path)) {
            LOG_ERROR("Failed to listen on %s", link_master_path);
            link_peer_shutdown(link_peer);
            link_peer = NULL;
        } else {
            LOG_INFO("SIO peer connected");
            gba_set_link_peer(&gba, link_peer);
        }
    } else if (link_client_path) {
        link_peer = link_peer_create();
        LOG_INFO("Connecting to SIO peer at %s ...", link_client_path);
        if (!link_peer_connect(link_peer, link_client_path)) {
            LOG_ERROR("Failed to connect to %s", link_client_path);
            link_peer_shutdown(link_peer);
            link_peer = NULL;
        } else {
            LOG_INFO("SIO peer connected");
            gba_set_link_peer(&gba, link_peer);
        }
    }

    game_init(&s_game, gba.cart.rom, gba.cart.rom_size);
    overworld_feed_init(&s_ow, game_has(s_game.profile, FEAT_OVERWORLD) ? s_game.profile : NULL);
    follower_feed_init(&s_fol, game_has(s_game.profile, FEAT_FOLLOWER) ? s_game.profile : NULL);
    s_nuz_on = !headless || nuzlocke_file;
    s_run_on = !headless || run_file;
    s_hunt_on = !headless || hunt_file;
    /* Headless runs never touch sidecar files next to the ROM, only the fixed paths given. */
    nuz_session_init(&s_nuz, s_game.profile, headless ? NULL : rom_path, headless ? nuzlocke_file : NULL);
    run_session_init(&s_run, s_game.profile, headless ? NULL : rom_path, headless ? run_file : NULL);
    hunt_session_init(&s_hunt, game_has(s_game.profile, FEAT_HUNT) ? s_game.profile : NULL,
                      headless ? NULL : rom_path, headless ? hunt_file : NULL);
    if (s_game.support == GAME_SUPPORT_MODIFIED || s_game.support == GAME_SUPPORT_OTHER_REVISION)
        LOG_WARN("Pokemon Companion (F9) is off: %s. It supports the original US/English Pokemon "
                 "Emerald, FireRed 1.0 and LeafGreen 1.1 ROMs.",
                 s_game.support == GAME_SUPPORT_MODIFIED ? "this ROM is a hack or modified copy"
                                                         : "this ROM revision isn't supported");

    // Cheats: --cheats FILE, else <rom>.cht next to the ROM when it exists.
    // Game ID lines in pasted codes are checked against the header word at 0xAC.
    if (gba.cart.rom_size >= 0xB0) {
        const uint8_t* id = &gba.cart.rom[0xAC];
        cheat_set_game_code(&gba.cheats, (uint32_t)id[0] | ((uint32_t)id[1] << 8) |
                                             ((uint32_t)id[2] << 16) | ((uint32_t)id[3] << 24));
    }
    char auto_cheat_path[1024];
    bool cheat_path_explicit = cheat_path != NULL;
    bool auto_path_ok = (size_t)snprintf(auto_cheat_path, sizeof(auto_cheat_path), "%s.cht", rom_path) <
                        sizeof(auto_cheat_path);
    if (!cheat_path && auto_path_ok) {
        FILE* probe = fopen(auto_cheat_path, "r");
        if (probe) {
            fclose(probe);
            cheat_path = auto_cheat_path;
        }
    }
    /* Where Settings saves cheats: the --cheats file, else <rom>.cht (created on first add). */
    const char* cheat_save_path = cheat_path_explicit ? cheat_path : auto_path_ok ? auto_cheat_path : NULL;
    int32_t cheats_loaded = -1;
    uint32_t cheats_skipped = 0, cheats_broken = 0;
    const char* first_broken = NULL;
    if (cheat_path) {
        cheats_loaded = cheat_file_load(&gba.cheats, cheat_path, &cheats_skipped);
        if (cheats_loaded < 0) {
            LOG_WARN("Failed to load cheat file: %s", cheat_path);
        } else {
            LOG_INFO("Loaded %d cheats from %s", cheats_loaded, cheat_path);
            for (uint32_t i = 0; i < gba.cheats.cheat_count; i++) {
                if (!gba.cheats.cheats[i].broken) continue;
                if (!first_broken) first_broken = gba.cheats.cheats[i].name;
                cheats_broken++;
                LOG_WARN("Cheat \"%s\" won't run: %s", gba.cheats.cheats[i].name, gba.cheats.cheats[i].error);
            }
        }
    }
    if (cheat_report) {
        if (cheat_path) printf("Cheat file: %s\n", cheat_path);
        else printf("No cheat file (looked for %s.cht)\n", rom_path);
        if (cheats_skipped) printf("%u block(s) skipped because their text is malformed (see log)\n",
                                   (unsigned)cheats_skipped);
        cheat_report_print(&gba.cheats, stdout);
        gba_destroy(&gba);
        return cheats_loaded < 0 && cheat_path_explicit ? 1 : 0;
    }

    // Back up the battery save before any cheat can touch it. If that fails,
    // cheats stay off for this session (the file keeps their saved state).
    CheatBackupResult cheat_backup = CHEAT_BACKUP_NO_SAVE;
    bool cheat_backup_ran = false;
    if (cheat_any_enabled(&gba.cheats)) {
        cheat_backup_ran = true;
        cheat_backup = cheat_session_backup(&gba.cheats, &gba.cart);
        if (cheat_backup == CHEAT_BACKUP_FAILED) {
            LOG_ERROR("Couldn't back up the save; cheats are off for this session");
            for (uint32_t i = 0; i < gba.cheats.cheat_count; i++) gba.cheats.cheats[i].enabled = false;
        } else if (cheat_backup == CHEAT_BACKUP_OK) {
            LOG_INFO("Save backed up to %s.cheatbak1 before using cheats", gba.cart.save_path);
        }
    }

    if (trace_path) {
        trace_init(trace_path, trace_from, trace_to, trace_frames);
    }

    if (headless) {
        if (headless_frames <= 0) {
            LOG_ERROR("--headless requires --frames <n> with n > 0");
            gba_destroy(&gba);
            return 1;
        }
        const GameProfile* gp = s_game.profile;
        bool no_berries = panel_page == GAME_PAGE_BERRIES && !game_has(gp, FEAT_BERRIES);
        bool no_hunt = panel_page == GAME_PAGE_HUNT && !game_has(gp, FEAT_HUNT);
        if (s_game.profile && (no_berries || no_hunt)) {
            LOG_ERROR("--panel-page %s: not available in %s", panel_page_arg, s_game.profile->name);
            gba_destroy(&gba);
            return 1;
        }
        // Fixed-size event table (~8 KB); static keeps it off the stack.
        static InputScript input_script;
        if (input_script_path && !input_script_load(&input_script, input_script_path)) {
            gba_destroy(&gba);
            return 1;
        }
        FILE* hash_out = stdout;
        if (hash_out_path) {
            hash_out = fopen(hash_out_path, "w");
            if (!hash_out) {
                LOG_ERROR("Failed to open --hash-out file: %s", hash_out_path);
                gba_destroy(&gba);
                return 1;
            }
        }
#ifdef ENABLE_XRAY
        if (xray_out_path) {
            /* No window: active only feeds the hooks and captures during the run. */
            xray_init(&s_xray_state);
            s_xray_state.active = true;
            gba.xray = &s_xray_state;
            g_xray = &s_xray_state;
        }
#endif
        if (gift_audit_path) {
            FILE* f = fopen(gift_audit_path, "w");
            GameMem gm = { gba.bus.ewram, gba.bus.iwram, gba.cart.rom, gba.cart.rom_size };
            bool ok = f && game_has(s_game.profile, FEAT_OVERWORLD) &&
                      overworld_gift_audit(s_game.profile, &gm, f);
            if (f) fclose(f);
            if (!ok) {
                LOG_ERROR("--gift-audit needs the original Pokemon Emerald (US) and a writable file: %s",
                          gift_audit_path);
                gba_destroy(&gba);
                return 1;
            }
        }
        /* A bare Frontend: only the observer hub and the follower fields are used. */
        memset(&fe, 0, sizeof(fe));
        fe.ow_feed = game_has(s_game.profile, FEAT_OVERWORLD) ? &s_ow : NULL;
        fe.overworld_xray = fe.ow_feed && (game_dump_path || overworld_out_path);
        if (follower_hgss_path) {
            if (!game_has(s_game.profile, FEAT_FOLLOWER)) {
                LOG_ERROR("--follower-hgss needs the original Pokemon Emerald (US)");
                gba_destroy(&gba);
                return 1;
            }
            if (!hgss_assets_open(&s_hgss, follower_hgss_path)) {
                LOG_ERROR("--follower-hgss %s: %s", follower_hgss_path, s_hgss.error);
                gba_destroy(&gba);
                return 1;
            }
            fe.fol_feed = &s_fol;
            fe.hgss = &s_hgss;
            follower_view_init(&fe.follower, &s_hgss, NULL);   /* no mood sidecar from headless runs */
            fe.follower.enabled = true;
        }
        frontend_sync_observer(&fe, &gba);
        if (overworld_out_path && !game_has(s_game.profile, FEAT_OVERWORLD))
            LOG_WARN("--overworld-out: not the original Pokemon Emerald (US), drawing the game only");
        /* Predictions only matter for a final dump or panel image. */
        hunt_session_set_shown(&s_hunt, game_dump_path || panel_out_path);
        int rc = headless_run(&gba, headless_frames, hash_out, input_script_path ? &input_script : NULL,
                              follower_pre_frame, headless_after, &fe);
        if (hash_out != stdout) fclose(hash_out);
        if (rc == 0 && screenshot_out_path) {
            if (!screenshot_save(follower_composite(&fe, &gba), screenshot_out_path)) {
                LOG_ERROR("Failed to write screenshot: %s", screenshot_out_path);
                rc = 1;
            }
        }
        static GameState dump_state;  /* large; keep off the stack */
        if (rc == 0 && (game_dump_path || panel_out_path || card_out_path)) {
            game_init(&dump_state, gba.cart.rom, gba.cart.rom_size);
            game_update(&dump_state, &gba);
        }
        /* The whole Next match search at once: nothing renders in between to spread it out. */
        if (rc == 0 && hunt_file && (game_dump_path || panel_out_path)) {
            GameMem gm = { gba.bus.ewram, gba.bus.iwram, gba.cart.rom, gba.cart.rom_size };
            hunt_session_present(&s_hunt, &gm, 0, HUNT_SCAN_LIMIT);
        }
        if (rc == 0 && game_dump_path) {
            FILE* f = fopen(game_dump_path, "w");
            static FeebasState feebas;
            static GameDaycare daycare;
            GameMem gm = { gba.bus.ewram, gba.bus.iwram, gba.cart.rom, gba.cart.rom_size };
            if (game_has(s_game.profile, FEAT_FEEBAS)) feebas_read(s_game.profile, &gm, &feebas);
            game_read_daycare(&dump_state, &gba, &daycare);
            if (!f || !game_dump(&dump_state.snap, f) || (dump_state.profile && !game_dump_daycare(&daycare, f)) ||
                (game_has(s_game.profile, FEAT_OVERWORLD) && !overworld_dump(&s_ow.shown, f)) ||
                (game_has(s_game.profile, FEAT_FEEBAS) && !feebas_dump(&feebas, f)) ||
                (nuzlocke_file && !nuz_session_dump(&s_nuz, f)) || (run_file && !run_session_dump(&s_run, f)) ||
                (hunt_file && !hunt_session_dump(&s_hunt, f))) {
                LOG_ERROR("Failed to write --game-dump file: %s", game_dump_path);
                rc = 1;
            }
            if (f) fclose(f);
        }
        if (rc == 0 && panel_out_path) {
            int w = (int)(600 * panel_density + 0.5f);
            int h = (int)(480 * panel_density + 0.5f);
            uint32_t* px = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
            if (!px) {
                LOG_ERROR("Out of memory rendering --panel-out");
                rc = 1;
            } else {
                /* The Search page as if the query had been typed in, focused, caret on. */
                static SearchView search_view;
                static uint16_t search_results[SPECIES_MAX];
                static SpeciesDetail search_detail;
                GamePanelSearch search = {&search_view, &dump_state.search, search_results, NULL, true};
                search_view_init(&search_view);
                search_view_text(&search_view, panel_search_arg ? panel_search_arg : "");
                search_view_set_focus(&search_view, true);
                uint16_t n = 0;
                if (panel_page == GAME_PAGE_SEARCH && dump_state.search.ok)
                    n = species_search_query(&dump_state.search, search_view.query, search_results);
                if (search_select > 0 && search_select >= n) {
                    LOG_ERROR("--panel-select must be below the %u search results (got %u)", (unsigned)n,
                              (unsigned)search_select);
                    rc = 1;
                }
                search_view.sel = search_select;
                search_view_set_results(&search_view, n);
                if (n > 0) {
                    GameMem rom = {NULL, NULL, gba.cart.rom, gba.cart.rom_size};
                    species_search_detail(&dump_state.search, dump_state.profile, &rom,
                                          search_results[search_view.sel], &search_detail);
                    search.detail = &search_detail;
                    uint16_t visible = 0;
                    uint16_t lines = game_panel_search_detail_lines(&search_detail, &visible);
                    search_view_set_detail_lines(&search_view, lines, visible);
                }
                /* Headless keeps no run log unless asked, so say what the Run page needs. */
                static const RunView no_run_file = { .state = RUN_VIEW_OFF, .reason = "Needs --run-file" };
                static const HuntView no_hunt_file = { .on = false, .reason = "Needs --hunt-file" };
                static GameDaycare panel_daycare;
                bool panel_party =
                    game_panel_resolve_page(panel_page, dump_state.snap.context) == GAME_PAGE_PARTY;
                if (panel_party) game_read_daycare(&dump_state, &gba, &panel_daycare);
                UiCanvas canvas;
                ui_canvas_init(&canvas, px, w, h, panel_density);
                game_panel_draw(&canvas, &dump_state.snap, panel_page, panel_select,
                                nuzlocke_file ? &s_nuz.view : NULL, (uint8_t)(panel_select % NUZ_SUB_COUNT),
                                panel_select, true, &search, run_file ? &s_run.view : &no_run_file, 0,
                                panel_page == GAME_PAGE_RUN && panel_select == 1,
                                hunt_file ? hunt_session_view(&s_hunt) : &no_hunt_file,
                                panel_party ? &panel_daycare : NULL);
                if (rc == 0 && !screenshot_save_argb(px, w, h, panel_out_path)) {
                    LOG_ERROR("Failed to write --panel-out file: %s", panel_out_path);
                    rc = 1;
                }
                free(px);
            }
        }
        if (rc == 0 && overworld_out_path) {
            enum { OW_OUT_SCALE = 3 };
            int w = SCREEN_WIDTH * OW_OUT_SCALE, h = SCREEN_HEIGHT * OW_OUT_SCALE;
            uint32_t* px = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
            if (!px) {
                LOG_ERROR("Out of memory rendering --overworld-out");
                rc = 1;
            } else {
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                        px[y * w + x] = gba_to_argb(
                            gba.ppu.framebuffer[(y / OW_OUT_SCALE) * SCREEN_WIDTH + x / OW_OUT_SCALE]);
                UiCanvas canvas;
                ui_canvas_init(&canvas, px, w, h, 1.0f);
                overworld_draw(&canvas, &s_ow.shown, (float)OW_OUT_SCALE);
                if (!screenshot_save_argb(px, w, h, overworld_out_path)) {
                    LOG_ERROR("Failed to write --overworld-out file: %s", overworld_out_path);
                    rc = 1;
                }
                free(px);
            }
        }
        if (rc == 0 && card_out_path) {
            GameMem gm = {gba.bus.ewram, gba.bus.iwram, gba.cart.rom, gba.cart.rom_size};
            static TrainerCardData card;
            CardStatus st = trainer_card_read(&dump_state, &gm, &card);
            if (st != CARD_OK) {
                LOG_ERROR("--card-out: %s", st == CARD_NOT_SUPPORTED ? "not a supported Pokemon ROM"
                                            : st == CARD_NO_TEAM   ? "no party yet"
                                                                   : "party is not readable (battle facility)");
                rc = 1;
            } else {
                NuzKey mvp;
                bool has_mvp = run_file && run_session_mvp_key(&s_run, &mvp);
                int ace = trainer_card_pick_ace(&card, panel_select_arg ? (int)panel_select : -1,
                                                has_mvp ? &mvp : NULL);
                if (!trainer_card_save(&dump_state, &gm, &card, card_style, ace, card_out_path)) {
                    LOG_ERROR("Failed to write --card-out file: %s", card_out_path);
                    rc = 1;
                }
            }
        }
        if (rc == 0 && video_out_path) {
            /* The filter's own output (its factor for --scale), before SDL stretches it. */
            VideoFrame fr;
            video_filter_run(gba.ppu.framebuffer, video_filter, color_correct_arg, scale, &fr);
            if (!screenshot_save_argb(fr.px, fr.w, fr.h, video_out_path)) {
                LOG_ERROR("Failed to write --video-out file: %s", video_out_path);
                rc = 1;
            }
        }
#ifdef ENABLE_XRAY
        if (rc == 0 && xray_out_path) {
            xray_capture_ppu_layers(&gba.ppu, &s_xray_state);
            xray_capture_audio(&gba.apu, &s_xray_state);
            UiCanvas canvas;
            ui_canvas_init(&canvas, s_xray_state.framebuffer, XRAY_FB_MAX_W, XRAY_FB_MAX_H,
                           XRAY_MAX_DENSITY);
            xray_draw(&canvas, &gba, &s_xray_state);
            if (!screenshot_save_argb(s_xray_state.framebuffer, XRAY_FB_MAX_W, XRAY_FB_MAX_H,
                                      xray_out_path)) {
                LOG_ERROR("Failed to write --xray-out file: %s", xray_out_path);
                rc = 1;
            }
        }
        g_xray = NULL;
#endif
        gba_destroy(&gba);
        return rc;
    }

    // Initialize frontend (SDL2), unless the library already opened the window
    if (!fe_ready && !frontend_init(&fe, scale)) {
        LOG_ERROR("Failed to initialize frontend");
        if (no_terminal) rom_picker_alert("Couldn't open the game window.");
        if (link_peer) link_peer_shutdown(link_peer);
        gba_destroy(&gba);
        return 1;
    }

    int rom_path_len = snprintf(fe.rom_path, sizeof(fe.rom_path), "%s", rom_path);
    if (rom_path_len < 0 || (size_t)rom_path_len >= sizeof(fe.rom_path)) {
        // Slot and screenshot names would be built from a cut-off path, so leave them off.
        LOG_WARN("ROM path too long; savestates and screenshots are disabled for this run");
        fe.rom_path[0] = '\0';
    }

    // Keyboard bindings (must come after frontend_init so SDL is up)
    // Load order: defaults, then the saved keymap.ini, then --keymap on top.
    keymap_reset_defaults();
    fe.keymap_path = s_keymap_path[0] ? s_keymap_path : NULL;
    if (fe.keymap_path) {
        FILE* kf = fopen(fe.keymap_path, "r"); /* keymap_load logs an error on a missing file */
        if (kf) {
            fclose(kf);
            keymap_load(fe.keymap_path);
        }
    }
    if (keymap_path) {
        keymap_load(keymap_path);
    }

    fe.prefs = &s_prefs;
    fe.ow_feed = game_has(s_game.profile, FEAT_OVERWORLD) ? &s_ow : NULL;
    fe.nuz = &s_nuz;
    fe.page_mask = game_panel_page_mask(s_game.profile);
    fe.search_ok = s_game.search.ok;
    fe.run_view = &s_run.view;
    fe.hunt = &s_hunt;
    s_toast_fe = &fe;
    VideoFilter pref_filter = VIDEO_FILTER_SHARP;
    video_filter_from_name(prefs_get(&s_prefs, "video_filter"), &pref_filter);
    fe.video_filter = video_filter_arg ? video_filter : pref_filter;
    {
        const char* vol = prefs_get(&s_prefs, "volume");
        char* end = NULL;
        long v = vol ? strtol(vol, &end, 10) : 100;
        bool ok = !vol || (end != vol && *end == '\0');
        fe.volume = (ok && v >= 0 && v <= 100) ? (int32_t)v : 100;
    }
    fe.start_muted = prefs_get_bool(&s_prefs, "start_muted", false);
    fe.muted = start_muted || fe.start_muted;
    {
        Mp2kDriver drv;
        bool found = mp2k_detect(gba.cart.rom, gba.cart.rom_size, &drv);
        if (found)
            LOG_INFO("MP2K sound driver: SoundMain 0x%08X, mixer 0x%08X", (unsigned)drv.sound_main,
                     (unsigned)drv.hook_addr);
        mp2k_mixer_init(&s_mp2k, found ? &drv : NULL);
        fe.mp2k = &s_mp2k;
        frontend_set_hq_music(&fe, &gba, prefs_get_bool(&s_prefs, "hq_music", false), false);
    }
    {
        const char* ff = prefs_get(&s_prefs, "ff_speed");
        fe.ff_speed = !ff ? 3 : strcmp(ff, "max") == 0 ? 0 : (ff[0] >= '2' && ff[0] <= '4' && !ff[1]) ? (uint8_t)(ff[0] - '0') : 3;
        fe.pause_on_blur = prefs_get_bool(&s_prefs, "pause_on_blur", false);
        fe.berry_alerts = prefs_get_bool(&s_prefs, "berry_alerts", true);
    }
    fe.color_correct = color_correct_arg || prefs_get_bool(&s_prefs, "color_correct", false);
    if (fe.ow_feed && prefs_get_bool(&s_prefs, "overworld_xray", false)) fe.overworld_xray = true;
    /* Following Pokemon: a saved HeartGold ROM that no longer opens counts as unset. */
    fe.fol_feed = game_has(s_game.profile, FEAT_FOLLOWER) ? &s_fol : NULL;
    fe.hgss = &s_hgss;
    {
        const char* hp = prefs_get(&s_prefs, "hgss_rom");
        if (fe.fol_feed && hp && hp[0] && !hgss_assets_open(&s_hgss, hp))
            LOG_WARN("Following Pokemon is off: HeartGold ROM %s: %s", hp, s_hgss.error);
    }
    follower_view_init(&fe.follower, &s_hgss, gba.cart.save_path);
    fe.follower.rng ^= (uint32_t)time(NULL) | 1u;   /* talks differ between sessions; headless keeps the fixed seed */
    fe.follower.enabled = fe.fol_feed && s_hgss.ok && prefs_get_bool(&s_prefs, "follower", false);
    frontend_sync_observer(&fe, &gba);

    if (!bios_loaded && bios_from_prefs)
        frontend_toast(&fe, TOAST_WARN, "BIOS file not found", path_base(pref_bios));
    else if (!bios_loaded)
        frontend_toast(&fe, TOAST_WARN, "Running without BIOS", "Using built-in BIOS");
    if (gba.cart.patched) {
        frontend_toast(&fe, TOAST_OK, "Patch applied", path_base(rom_path));
        if (s_patch_sep_save)
            frontend_toast(&fe, TOAST_INFO, "Separate save", "Your original save is untouched");
    } else if (s_patch_warn[0]) {
        frontend_toast(&fe, TOAST_WARN, "Patch not applied", s_patch_warn);
    }
    if (s_game.support == GAME_SUPPORT_MODIFIED || s_game.support == GAME_SUPPORT_OTHER_REVISION)
        frontend_toast(&fe, TOAST_WARN, "Pok\xC3\xA9mon Companion is off",
                       s_game.support == GAME_SUPPORT_MODIFIED ? "ROM hack not supported"
                                                               : "ROM revision not supported");
    if (cheat_path) {
        if (cheats_loaded < 0) {
            frontend_toast(&fe, TOAST_ERROR, "Couldn't load cheats", path_base(cheat_path));
        } else {
            char title[32];
            snprintf(title, sizeof(title), "%d %s loaded", (int)cheats_loaded,
                     cheats_loaded == 1 ? "cheat" : "cheats");
            frontend_toast(&fe, TOAST_OK, title, path_base(cheat_path));
        }
        uint32_t bad = cheats_broken + cheats_skipped;
        if (bad > 0) {
            char title[40];
            snprintf(title, sizeof(title), "%u %s couldn't be read", (unsigned)bad, bad == 1 ? "cheat" : "cheats");
            frontend_toast(&fe, TOAST_WARN, title, first_broken ? first_broken : "See the log");
        }
    }
    if (cheat_backup_ran && cheat_backup == CHEAT_BACKUP_OK)
        frontend_toast(&fe, TOAST_OK, "Save backed up", "before using cheats");
    else if (cheat_backup_ran && cheat_backup == CHEAT_BACKUP_FAILED)
        frontend_toast(&fe, TOAST_ERROR, "Couldn't back up save", "Cheats stay off");
    fe.link_active = link_peer != NULL;
    if (link_peer) frontend_toast(&fe, TOAST_INFO, "Link cable connected", NULL);
    fe.settings_ctx = (SettingsContext){&gba.cheats, &gba.cart, cheat_save_path, settings_toast, &fe};

    // Initialize audio
    frontend_audio_init(&fe);

#ifdef ENABLE_XRAY
    xray_init(&s_xray_state);
    gba.xray = &s_xray_state;
    g_xray = &s_xray_state;
#endif

    LOG_INFO("Starting emulation...");

    // Main loop
    bool prev_paused = false;
    bool save_fail_reported = false;
    while (fe.running && gba.running) {
        frontend_poll_input(&fe, &gba);
        if (fe.card_requested) card_tick(&fe, &gba);

        // Pause: skip emulation, keep window responsive, mute audio.
        bool now_paused = frontend_is_paused(&fe);
        if (now_paused != prev_paused) {
            if (now_paused) {
                frontend_audio_stop(&fe, &gba.apu);
            }
            prev_paused = now_paused;
        }
        if (now_paused) {
            toast_set_badge(&fe.toasts, BADGE_REWIND, false);
            if (fe.step_pending) {
                // Single-step: run exactly one frame then re-pause.
                fe.step_pending = false;
                frontend_input_frame(&fe, &gba);
                follower_pre_frame(&gba, &fe);
                gba_run_frame(&gba);
                follower_post_frame(&gba, &fe);
                if (gba.frame_complete) {
                    nuz_tick(&gba, &s_nuz);
                    CartFlushResult flush = cartridge_save_tick(&gba.cart, time(NULL));
                    if (toast_failure_edge(&save_fail_reported, flush != CART_FLUSH_NONE,
                                           flush == CART_FLUSH_OK))
                        frontend_toast(&fe, TOAST_ERROR, "Game save failed", "Progress may not be on disk");
                }
            }
            render_with_overlay(&fe, &gba);
#ifdef ENABLE_XRAY
            xray_render(&s_xray_state, &gba);
#endif
            frontend_audio_stop(&fe, &gba.apu);
            frontend_frame_sync(&fe);
            continue;
        }

        // Save state handling (at frame boundary)
        if (fe.save_requested || fe.load_requested) {
            char ss_path[512];
            savestate_slot_path(fe.rom_path, fe.savestate_slot,
                                ss_path, sizeof(ss_path));
            char slot_detail[16];
            snprintf(slot_detail, sizeof(slot_detail), "Slot %d", (int)fe.savestate_slot);
            if (fe.save_requested) {
                fe.save_requested = false;
                SaveStateResult res = savestate_save(&gba, ss_path);
                if (res == SS_OK) {
                    LOG_INFO("State saved to slot %d", fe.savestate_slot);
                    frontend_toast(&fe, TOAST_OK, "State saved", slot_detail);
                } else {
                    LOG_ERROR("Failed to save state (error %d)", res);
                    frontend_toast(&fe, TOAST_ERROR, "Couldn't save state", toast_savestate_error(res));
                }
            }
            if (fe.load_requested) {
                fe.load_requested = false;
                SaveStateResult res = savestate_load(&gba, ss_path);
                if (res == SS_OK) {
                    LOG_INFO("State loaded from slot %d", fe.savestate_slot);
                    frontend_toast(&fe, TOAST_OK, "State loaded", slot_detail);
                    frontend_audio_stop(&fe, &gba.apu);
                    overworld_feed_clear(&s_ow);
                    berry_alerts_rebase(&fe.berry_watch);
                    daycare_alerts_rebase(&fe.daycare_watch);
                    game_daycare_forget(&s_game);
                    frontend_hq_reset(&fe);
                    frontend_follower_reset(&fe, &gba);
                    hunt_session_discontinuity(&s_hunt);
#ifdef ENABLE_REWIND
                    rewind_clear(&gba.rewind);   // past is no longer valid
#endif
                } else {
                    LOG_ERROR("Failed to load state (error %d)", res);
                    if (res == SS_ERR_FILE_OPEN) {
                        char title[32];
                        snprintf(title, sizeof(title), "Slot %d is empty", (int)fe.savestate_slot);
                        frontend_toast(&fe, TOAST_ERROR, title, NULL);
                    } else {
                        frontend_toast(&fe, TOAST_ERROR, "Couldn't load state",
                                       toast_savestate_error(res));
                    }
                }
            }
        }

#ifdef ENABLE_REWIND
        bool rewind_active_now = fe.rewind_hold && rewind_depth(&gba.rewind) > 0;
        toast_set_badge(&fe.toasts, BADGE_REWIND, rewind_active_now);

        // Detect transitions OUT of rewind (cleanup audio + APU state)
        {
            static bool prev_rewind = false;
            if (prev_rewind && !rewind_active_now) {
                rewind_end(&gba.rewind);
                frontend_audio_stop(&fe, &gba.apu);
                frontend_hq_reset(&fe);
                hunt_session_discontinuity(&s_hunt);
            }
            // Detect transitions INTO rewind
            if (!prev_rewind && rewind_active_now) {
                rewind_begin(&gba.rewind);
                frontend_audio_stop(&fe, &gba.apu);
            }
            prev_rewind = rewind_active_now;
        }

        if (rewind_active_now) {
            // Reverse playback: pop one frame back, render, no audio.
            if (!rewind_step(&gba.rewind, &gba)) {
                // Hit oldest frame — freeze on screen until release.
            }
            overworld_feed_clear(&s_ow);
            berry_alerts_rebase(&fe.berry_watch);
            daycare_alerts_rebase(&fe.daycare_watch);
            game_daycare_forget(&s_game);
            frontend_follower_reset(&fe, &gba);
            s_rewinding = true;
            nuz_tick(&gba, &s_nuz);
            s_rewinding = false;
            render_with_overlay(&fe, &gba);
#ifdef ENABLE_XRAY
            xray_render(&s_xray_state, &gba);
#endif
            // Drain APU ring buffer (state was restored from snapshot)
            frontend_audio_stop(&fe, &gba.apu);
            frontend_frame_sync(&fe);
            continue;
        }
#endif /* ENABLE_REWIND */

        frontend_input_frame(&fe, &gba);
        follower_pre_frame(&gba, &fe);
        gba_run_frame(&gba);
        follower_post_frame(&gba, &fe);

        if (gba.frame_complete) {
            trace_frame_tick();
            nuz_tick(&gba, &s_nuz);
            berry_tick(&fe, &gba);
            daycare_tick(&fe, &gba);
            CartFlushResult flush = cartridge_save_tick(&gba.cart, time(NULL));
            if (toast_failure_edge(&save_fail_reported, flush != CART_FLUSH_NONE,
                                   flush == CART_FLUSH_OK))
                frontend_toast(&fe, TOAST_ERROR, "Game save failed", "Progress may not be on disk");
            bool ff_active = fe.ff_hold || fe.ff_toggle;

            // Clean up audio when fast-forward ends
            {
                static bool prev_ff = false;
                if (ff_active != prev_ff) {
                    if (!ff_active) {
                        // Returning to normal speed: clean audio state
                        frontend_audio_stop(&fe, &gba.apu);
                        fe.ff_frame_count = 0;
                        frame_pacer_reset(&fe.pacer);
                    }
                    prev_ff = ff_active;
                }
            }

            if (ff_active) {
                // Fast-forward: render every Nth frame, skip audio and sync
                uint32_t render_every = fe.ff_speed ? fe.ff_speed : fe.ff_frame_skip;
                if (fe.ff_frame_count++ % render_every == 0) {
                    render_with_overlay(&fe, &gba);
#ifdef ENABLE_XRAY
                    xray_render(&s_xray_state, &gba);
#endif
                }
                // Drain APU ring buffer to prevent stall
                frontend_audio_stop(&fe, &gba.apu);
                if (fe.ff_speed) frontend_pace(&fe, fe.ff_speed);
            } else {
                render_with_overlay(&fe, &gba);
                frontend_push_audio(&fe, &gba.apu);
#ifdef ENABLE_XRAY
                xray_render(&s_xray_state, &gba);
#endif
                frontend_frame_sync(&fe);
            }

            if (fe.screenshot_requested) {
                fe.screenshot_requested = false;
                char shot_path[512];
                screenshot_path(fe.rom_path, time(NULL), shot_path, sizeof(shot_path));
                if (screenshot_save(gba.ppu.framebuffer, shot_path)) {
                    LOG_INFO("Screenshot saved: %s", shot_path);
                    frontend_toast(&fe, TOAST_OK, "Screenshot saved", path_base(shot_path));
                } else {
                    frontend_toast(&fe, TOAST_ERROR, "Couldn't save screenshot", NULL);
                }
            }
        }
    }

    // Cleanup
    trace_shutdown();
    /* Only when the game changed it: a loaded savestate or rewind swaps in an
     * older save chip, and writing that would roll back the newest in-game save. */
    if (gba.cart.save_dirty) cartridge_save_to_file(&gba.cart);
    if (fe.follower.enabled) follower_mood_save(&fe.follower);   /* level-up and faint changes since the last talk */
#ifdef ENABLE_XRAY
    xray_destroy(&s_xray_state);
    g_xray = NULL;
#endif
    frontend_destroy(&fe);
    if (link_peer) link_peer_shutdown(link_peer);
    gba_destroy(&gba);

    return 0;
}
