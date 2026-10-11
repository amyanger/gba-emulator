#include "test_harness.h"
#include <stdio.h>

int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;
int _test_failed_flag = 0;

void run_savestate_tests(void);
void run_bus_tests(void);
void run_cpu_tests(void);
void run_dma_tests(void);
void run_interrupt_tests(void);
void run_timer_tests(void);
void run_flash_tests(void);
void run_cheat_tests(void);
void run_cheat_crypto_tests(void);
void run_cheat_parse_tests(void);
void run_cheat_decode_tests(void);
void run_cheat_file_tests(void);
void run_cheat_backup_tests(void);
void run_cheat_report_tests(void);
void run_cheat_session_tests(void);
void run_ui_controls_tests(void);
void run_settings_tests(void);
void run_frame_pacer_tests(void);
void run_apu_tests(void);
void run_audio_out_tests(void);
void run_ppu_tests(void);
void run_input_tests(void);
void run_rewind_tests(void);
void run_vblank_observer_tests(void);
void run_screenshot_tests(void);
void run_trace_tests(void);
void run_cartridge_autosave_tests(void);
void run_keymap_tests(void);
void run_eeprom_tests(void);
void run_practice_tests(void);
void run_fb_hash_tests(void);
void run_input_script_tests(void);
void run_game_mem_tests(void);
void run_game_text_tests(void);
void run_game_pokemon_tests(void);
void run_game_battle_tests(void);
void run_game_encounters_tests(void);
void run_game_snapshot_tests(void);
void run_game_panel_tests(void);
void run_overworld_tests(void);
void run_nds_fs_tests(void);
void run_hgss_msg_tests(void);
void run_hgss_gfx_tests(void);
void run_hgss_talk_tests(void);
void run_follower_tests(void);
void run_follower_ctx_tests(void);
void run_follower_places_tests(void);
void run_follower_draw_tests(void);
void run_follower_talk_ui_tests(void);
void run_cry_player_tests(void);
void run_script_walk_tests(void);
void run_ui_font_data_tests(void);
void run_ui_canvas_tests(void);
void run_ui_font_tests(void);
void run_ui_widgets_tests(void);
void run_panel_layout_tests(void);
#ifdef ENABLE_XRAY
void run_xray_tests(void);
#endif
void run_toast_tests(void);
void run_prefs_tests(void);
void run_video_filter_tests(void);
void run_hud_tests(void);
void run_overworld_draw_tests(void);
void run_nuzlocke_read_tests(void);
void run_nuzlocke_tests(void);
void run_nuzlocke_session_tests(void);
void run_library_tests(void);
void run_library_view_tests(void);
#ifndef _WIN32
// These suites depend on POSIX APIs (unistd.h, pthreads, AF_UNIX socketpair)
// and are not compiled into the Windows test binary.
void run_rtc_tests(void);
void run_sio_tests(void);
void run_sio_link_tests(void);
#endif

#define RUN_SUITE(fn) do { printf(">> " #fn "\n"); fflush(stdout); fn(); } while (0)

int main(void) {
    printf("=== GBA Emulator Test Suite ===\n\n");
    RUN_SUITE(run_savestate_tests);
    RUN_SUITE(run_bus_tests);
    RUN_SUITE(run_cpu_tests);
    RUN_SUITE(run_dma_tests);
    RUN_SUITE(run_interrupt_tests);
    RUN_SUITE(run_timer_tests);
    RUN_SUITE(run_flash_tests);
    RUN_SUITE(run_cheat_tests);
    RUN_SUITE(run_cheat_crypto_tests);
    RUN_SUITE(run_cheat_parse_tests);
    RUN_SUITE(run_cheat_decode_tests);
    RUN_SUITE(run_cheat_file_tests);
    RUN_SUITE(run_cheat_backup_tests);
    RUN_SUITE(run_cheat_report_tests);
    RUN_SUITE(run_cheat_session_tests);
    RUN_SUITE(run_ui_controls_tests);
    RUN_SUITE(run_settings_tests);
    RUN_SUITE(run_frame_pacer_tests);
    RUN_SUITE(run_apu_tests);
    RUN_SUITE(run_audio_out_tests);
    RUN_SUITE(run_ppu_tests);
    RUN_SUITE(run_input_tests);
    RUN_SUITE(run_rewind_tests);
    RUN_SUITE(run_vblank_observer_tests);
    RUN_SUITE(run_screenshot_tests);
    RUN_SUITE(run_trace_tests);
    RUN_SUITE(run_cartridge_autosave_tests);
    RUN_SUITE(run_keymap_tests);
    RUN_SUITE(run_eeprom_tests);
    RUN_SUITE(run_practice_tests);
    RUN_SUITE(run_fb_hash_tests);
    RUN_SUITE(run_input_script_tests);
    RUN_SUITE(run_game_mem_tests);
    RUN_SUITE(run_game_text_tests);
    RUN_SUITE(run_game_pokemon_tests);
    RUN_SUITE(run_game_battle_tests);
    RUN_SUITE(run_game_encounters_tests);
    RUN_SUITE(run_game_snapshot_tests);
    RUN_SUITE(run_game_panel_tests);
    RUN_SUITE(run_overworld_tests);
    RUN_SUITE(run_nds_fs_tests);
    RUN_SUITE(run_hgss_msg_tests);
    RUN_SUITE(run_hgss_gfx_tests);
    RUN_SUITE(run_hgss_talk_tests);
    RUN_SUITE(run_follower_tests);
    RUN_SUITE(run_follower_ctx_tests);
    RUN_SUITE(run_follower_places_tests);
    RUN_SUITE(run_follower_draw_tests);
    RUN_SUITE(run_follower_talk_ui_tests);
    RUN_SUITE(run_cry_player_tests);
    RUN_SUITE(run_script_walk_tests);
    RUN_SUITE(run_ui_font_data_tests);
    RUN_SUITE(run_ui_canvas_tests);
    RUN_SUITE(run_ui_font_tests);
    RUN_SUITE(run_ui_widgets_tests);
    RUN_SUITE(run_panel_layout_tests);
#ifdef ENABLE_XRAY
    RUN_SUITE(run_xray_tests);
#endif
    RUN_SUITE(run_toast_tests);
    RUN_SUITE(run_prefs_tests);
    RUN_SUITE(run_video_filter_tests);
    RUN_SUITE(run_hud_tests);
    RUN_SUITE(run_overworld_draw_tests);
    RUN_SUITE(run_nuzlocke_read_tests);
    RUN_SUITE(run_nuzlocke_tests);
    RUN_SUITE(run_nuzlocke_session_tests);
    RUN_SUITE(run_library_tests);
    RUN_SUITE(run_library_view_tests);
#ifndef _WIN32
    RUN_SUITE(run_rtc_tests);
    RUN_SUITE(run_sio_tests);
    RUN_SUITE(run_sio_link_tests);
#endif
    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
