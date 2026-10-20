cmake_minimum_required(VERSION 3.21)
project(gba_emulator
    VERSION 0.14.0
    DESCRIPTION "Game Boy Advance emulator (ARM7TDMI + scanline PPU + SDL2)"
    LANGUAGES C
)

# rom_picker_mac.m (the macOS "choose a ROM" dialog) is Objective-C.
if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings + per-config flags. MSVC needs separate handling because
# its driver doesn't understand GCC/Clang flags.
if(MSVC)
    add_compile_options(/W4 /wd4100 /wd4244 /wd4267 /wd4146 /wd4310)
    # MSVC deprecates standard C functions (fopen, strncpy, ...) as C4996,
    # which /WX turns into hard errors. The portable functions are intended.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    # Suppress Clang's pedantic warning about `, ##__VA_ARGS__` in the LOG
    # macros. The syntax is widely supported (GCC, Clang, MSVC) and is the
    # canonical way to make variadic macros optional in C11.
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)
    endif()
    set(CMAKE_C_FLAGS_DEBUG "-g -O0")
    set(CMAKE_C_FLAGS_RELEASE "-O2")
endif()
add_compile_definitions($<$<CONFIG:Debug>:DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Release-only: build "GBA Emulator.app" against an official SDL2.framework
# (tools/fetch_sdl2_framework.sh). Off by default so dev builds stay at
# build/gba_emulator for the goldens and scripts.
option(MACOS_APP_BUNDLE "Build a macOS .app bundle for release packaging" OFF)
set(MACOS_SDL2_FRAMEWORK "" CACHE PATH "SDL2.framework to bundle (MACOS_APP_BUNDLE only)")

if(MACOS_APP_BUNDLE)
    if(NOT APPLE OR NOT EXISTS "${MACOS_SDL2_FRAMEWORK}/Headers/SDL.h")
        message(FATAL_ERROR "MACOS_APP_BUNDLE needs macOS and -DMACOS_SDL2_FRAMEWORK=<dir>/SDL2.framework")
    endif()
    get_filename_component(SDL2_FRAMEWORK_DIR "${MACOS_SDL2_FRAMEWORK}" DIRECTORY)
    # -F lets <SDL2/SDL.h> resolve to SDL2.framework/Headers/SDL.h.
    add_compile_options("-F${SDL2_FRAMEWORK_DIR}")
    set(SDL2_INCLUDE_DIRS "")
    set(SDL2_LIBRARIES "-F${SDL2_FRAMEWORK_DIR}" "-framework SDL2")
    # gba_tests runs from the build tree during the release job.
    set(CMAKE_BUILD_RPATH "${SDL2_FRAMEWORK_DIR}")
else()
    find_package(SDL2 REQUIRED)
endif()

# Treat warnings as errors (used in CI; off by default so a new local
# compiler's extra warnings don't block development)
option(ENABLE_WERROR "Treat compiler warnings as errors" OFF)
if(ENABLE_WERROR)
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
endif()

# Address/UB sanitizers (Debug-oriented; used by the CI sanitizer job)
option(ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan" OFF)
if(ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# X-Ray hardware visualization mode (default ON)
option(ENABLE_XRAY "Enable Hardware X-Ray Mode (F2 hotkey)" ON)

# Rewind playback (default ON)
option(ENABLE_REWIND "Enable hold-Backspace-to-rewind playback" ON)

# Everything the emulator and gba_tests both link. savestate.c is app-only because
# tests/test_savestate.c #includes it to reach its static helpers.
set(CORE_SOURCES
    src/gba.c
    src/fileio.c
    src/cpu/arm7tdmi.c
    src/cpu/arm_instr.c
    src/cpu/thumb_instr.c
    src/cpu/bios_hle.c
    src/memory/bus.c
    src/memory/dma.c
    src/ppu/ppu.c
    src/ppu/background.c
    src/ppu/bitmap.c
    src/ppu/sprites.c
    src/ppu/effects.c
    src/apu/apu.c
    src/apu/channel.c
    src/apu/fifo.c
    src/mp2k/mp2k_detect.c
    src/mp2k/mp2k_mixer.c
    src/timer/timer.c
    src/interrupt/interrupt.c
    src/cartridge/cartridge.c
    src/cartridge/flash.c
    src/cartridge/sram.c
    src/cartridge/eeprom.c
    src/cartridge/rtc.c
    src/cartridge/gpio.c
    src/input/input.c
    src/input/keymap.c
    src/cheat/cheat.c
    src/cheat/cheat_file.c
    src/cheat/cheat_crypto.c
    src/cheat/cheat_parse.c
    src/cheat/cheat_decode.c
    src/cheat/cheat_backup.c
    src/cheat/cheat_report.c
    src/cheat/cheat_session.c
    src/screenshot/screenshot.c
    src/sio/sio.c
    src/sio/sio_link.c
    src/trace/trace.c
    src/game/game_mem.c
    src/game/emerald_profile.c
    src/game/game_profile.c
    src/game/frlg_profile.c
    src/game/gen3_text.c
    src/game/pokemon.c
    src/game/battle_info.c
    src/game/damage.c
    src/game/learnset.c
    src/game/rng3.c
    src/game/wildgen.c
    src/game/hunt_read.c
    src/game/hunt.c
    src/game/damage_read.c
    src/game/encounters.c
    src/game/game.c
    src/game/overworld.c
    src/game/feebas.c
    src/game/nuzlocke_read.c
    src/game/species_search.c
    src/game/runlog_read.c
    src/game/runlog.c
    src/game/run_track.c
    src/game/nuzlocke.c
    src/game/logline.c
    src/game/script_walk.c
    src/game/follower.c
    src/game/follower_ctx.c
    src/game/follower_places.c
    src/game/mirage.c
    src/game/berry.c
    src/game/daycare.c
    src/game/mon_pic.c
    src/game/badge_pic.c
    src/game/trainer_card.c
    src/hgss/nds_fs.c
    src/hgss/hgss_talk.c
    src/hgss/hgss_assets.c
    src/ui/ui_stb.c
    src/ui/ui_canvas.c
    src/ui/ui_font.c
    src/ui/ui_widgets.c
    src/ui/ui_input.c
    src/ui/ui_controls.c
    src/headless/fb_hash.c
    src/headless/headless.c
    src/headless/input_script.c
    src/frontend/frame_advance.c
    src/frontend/input_display.c
    src/frontend/slot_picker.c
    src/frontend/game_panel.c
    src/frontend/trainer_card_draw.c
    src/frontend/badge_draw.c
    src/frontend/berry_alerts.c
    src/frontend/daycare_alerts.c
    src/frontend/panel_layout.c
    src/frontend/audio_out.c
    src/frontend/frame_pacer.c
    src/frontend/toast.c
    src/frontend/prefs.c
    src/frontend/video_filter.c
    src/frontend/xbr.c
    src/frontend/super_xbr.c
    src/frontend/nuzlocke_session.c
    src/frontend/hunt_session.c
    src/frontend/run_session.c
    src/frontend/session_log.c
    src/frontend/run_panel.c
    src/frontend/hunt_panel.c
    src/frontend/library.c
    src/frontend/library_view.c
    src/frontend/search_view.c
    src/frontend/settings.c
    src/frontend/settings_cheats.c
    src/frontend/settings_video.c
    src/frontend/settings_audio.c
    src/frontend/settings_general.c
    src/frontend/settings_controls.c
    src/frontend/hud_draw.c
    src/frontend/overworld_draw.c
    src/frontend/follower_draw.c
    src/frontend/follower_talk_ui.c
    src/frontend/follower_ambient.c
    src/frontend/cry_player.c
    # Shared overlay drawing primitives (used by input_display, slot_picker,
    # and the X-Ray panels regardless of ENABLE_XRAY).
    src/frontend/overlay_draw.c
)

if(ENABLE_REWIND)
    list(APPEND CORE_SOURCES src/rewind/rewind.c src/rewind/rewind_lz4.c)
endif()

# X-Ray drawing is pure C (no SDL window), so the tests link it too; xray.c is the window.
if(ENABLE_XRAY)
    list(APPEND CORE_SOURCES
        src/frontend/xray/xray_draw.c
        src/frontend/xray/xray_ppu.c
        src/frontend/xray/xray_tiles.c
        src/frontend/xray/xray_audio.c
        src/frontend/xray/xray_cpu.c
        src/frontend/xray/xray_activity.c
    )
    if(ENABLE_REWIND)
        list(APPEND CORE_SOURCES src/frontend/xray/xray_rewind.c)
    endif()
endif()

set(SOURCES
    ${CORE_SOURCES}
    src/main.c
    src/savestate/savestate.c
    src/frontend/frontend.c
    src/frontend/follower_glue.c
    src/frontend/rom_picker.c
    src/frontend/library_run.c
)

if(ENABLE_XRAY)
    list(APPEND SOURCES src/frontend/xray/xray.c)
endif()

# ── Rust: HGSS ROM parsers (rust/hgss) ──────────────────────────────
# Always the release profile: the C tests drive this library, so a debug
# build of it only adds link-time trouble.
find_program(CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")
if(NOT CARGO OR NOT EXISTS "${CARGO}")
    message(FATAL_ERROR "cargo not found. rust/hgss (the HeartGold/SoulSilver parsers) needs a Rust toolchain, see https://rustup.rs")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
    message(FATAL_ERROR "rust/hgss builds for the host (${CMAKE_HOST_SYSTEM_PROCESSOR}) only, not CMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
endif()
set(HGSS_RS_DIR "${CMAKE_SOURCE_DIR}/rust/hgss")
set(HGSS_RS_TARGET_DIR "${CMAKE_BINARY_DIR}/rust")
if(MSVC)
    set(HGSS_RS_LIB "${HGSS_RS_TARGET_DIR}/release/hgss_rs.lib")
else()
    set(HGSS_RS_LIB "${HGSS_RS_TARGET_DIR}/release/libhgss_rs.a")
endif()
set(HGSS_RS_ENV "")
if(ENABLE_WERROR)
    list(APPEND HGSS_RS_ENV "RUSTFLAGS=-Dwarnings")
endif()
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND HGSS_RS_ENV "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
file(GLOB HGSS_RS_SOURCES CONFIGURE_DEPENDS "${HGSS_RS_DIR}/src/*.rs")
add_custom_command(
    OUTPUT "${HGSS_RS_LIB}"
    # make's jobserver fds do not reach cargo, which then warns, so hide the flags.
    COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS ${HGSS_RS_ENV}
            "${CARGO}" build --release --locked
            --manifest-path "${HGSS_RS_DIR}/Cargo.toml" --target-dir "${HGSS_RS_TARGET_DIR}"
    DEPENDS ${HGSS_RS_SOURCES} "${HGSS_RS_DIR}/Cargo.toml" "${HGSS_RS_DIR}/Cargo.lock"
    COMMENT "Building rust/hgss"
    VERBATIM)
add_custom_target(hgss_rs DEPENDS "${HGSS_RS_LIB}")

add_executable(gba_emulator ${SOURCES})
# GBA (with its cheat tables) lives on main's stack; Windows defaults to 1 MB.
if(MSVC)
    target_link_options(gba_emulator PRIVATE /STACK:8388608)
endif()

target_include_directories(gba_emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(gba_emulator PRIVATE ${SDL2_LIBRARIES} $<$<NOT:$<PLATFORM_ID:Windows>>:m>)

# Native file picker for launches without a ROM argument (double-click).
if(APPLE)
    target_sources(gba_emulator PRIVATE src/frontend/rom_picker_mac.m src/frontend/macos_menu.m)
    target_link_libraries(gba_emulator PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers")
elseif(WIN32)
    # .rc: exe icon. .manifest: UTF-8 code page so argv and fopen take Unicode paths.
    target_sources(gba_emulator PRIVATE src/frontend/rom_picker_win.c
                   res/gba_emulator.rc res/gba_emulator.manifest)
    target_link_libraries(gba_emulator PRIVATE comdlg32 shell32 ole32)
else()
    target_sources(gba_emulator PRIVATE src/frontend/rom_picker_linux.c)
endif()

if(MACOS_APP_BUNDLE)
    set(APP_NAME "GBA Emulator")
    set_source_files_properties(res/icon/app.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(gba_emulator PRIVATE res/icon/app.icns)
    set_target_properties(gba_emulator PROPERTIES
        OUTPUT_NAME "${APP_NAME}"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.amyanger.gba-emulator"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

# ── Test suite ──────────────────────────────────────────────────────
enable_testing()

set(TEST_SOURCES
    tests/test_runner.c
    tests/test_savestate.c
    tests/test_bus.c
    tests/test_cpu.c
    tests/test_dma.c
    tests/test_interrupt.c
    tests/test_timer.c
    tests/test_flash.c
    tests/test_cheat.c
    tests/test_cheat_crypto.c
    tests/test_cheat_parse.c
    tests/test_cheat_decode.c
    tests/test_cheat_file.c
    tests/test_cheat_backup.c
    tests/test_cheat_report.c
    tests/test_cheat_session.c
    tests/test_apu.c
    tests/test_audio_out.c
    tests/test_mp2k_detect.c
    tests/test_mp2k_mixer.c
    tests/test_pc_hook.c
    tests/test_ppu.c
    tests/test_input.c
    tests/test_rewind.c
    tests/test_vblank_observer.c
    tests/test_screenshot.c
    tests/test_trace.c
    tests/test_cartridge_autosave.c
    tests/test_rom_patch.c
    tests/test_keymap.c
    tests/test_eeprom.c
    tests/test_practice.c
    tests/test_fb_hash.c
    tests/test_input_script.c
    tests/test_game_mem.c
    tests/test_game_text.c
    tests/test_game_pokemon.c
    tests/test_game_battle.c
    tests/test_game_damage.c
    tests/test_learnset.c
    tests/test_rng3.c
    tests/test_wildgen.c
    tests/test_hunt_read.c
    tests/test_hunt.c
    tests/test_game_encounters.c
    tests/test_game_snapshot.c
    tests/test_mirage.c
    tests/test_berry.c
    tests/test_daycare.c
    tests/test_game_panel.c
    tests/test_overworld.c
    tests/test_feebas.c
    tests/test_nds_fs.c
    tests/test_hgss_msg.c
    tests/test_hgss_gfx.c
    tests/test_hgss_talk.c
    tests/test_hgss_rs.c
    tests/test_follower.c
    tests/test_follower_ctx.c
    tests/test_follower_places.c
    tests/test_follower_draw.c
    tests/test_follower_ambient.c
    tests/test_follower_talk_ui.c
    tests/test_cry_player.c
    tests/test_script_walk.c
    tests/test_ui_font_data.c
    tests/test_ui_canvas.c
    tests/test_ui_font.c
    tests/test_ui_widgets.c
    tests/test_ui_controls.c
    tests/test_settings.c
    tests/test_frame_pacer.c
    tests/test_panel_layout.c
    tests/test_toast.c
    tests/test_berry_alerts.c
    tests/test_daycare_alerts.c
    tests/test_prefs.c
    tests/test_video_filter.c
    tests/test_hud.c
    tests/test_overworld_draw.c
    tests/test_nuzlocke_read.c
    tests/test_mon_pic.c
    tests/test_badge_pic.c
    tests/test_trainer_card.c
    tests/test_trainer_card_draw.c
    tests/test_badge_draw.c
    tests/test_species_search.c
    tests/test_run_read.c
    tests/test_runlog.c
    tests/test_run_track.c
    tests/test_nuzlocke.c
    tests/test_nuzlocke_session.c
    tests/test_hunt_session.c
    tests/test_run_session.c
    tests/test_session_log.c
    tests/test_library.c
    tests/test_library_view.c
    tests/test_search_view.c
)

# Several tests rely on POSIX-only APIs (unistd.h, pthreads, AF_UNIX
# socketpair) and are skipped on Windows. The full suite still runs on
# Linux + macOS, which are the canonical CI surfaces for these subsystems.
if(NOT WIN32)
    list(APPEND TEST_SOURCES
        tests/test_rtc.c
        tests/test_sio.c
        tests/test_sio_link.c
    )
endif()

if(ENABLE_XRAY)
    list(APPEND TEST_SOURCES tests/test_xray.c)
endif()

add_executable(gba_tests ${TEST_SOURCES} ${CORE_SOURCES})

target_include_directories(gba_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(gba_tests PRIVATE ${SDL2_LIBRARIES} $<$<NOT:$<PLATFORM_ID:Windows>>:m>)

# Several savestate tests put GBA structs (~670 KB each) on the stack — up to
# two at a time for SIO roundtrip — which exceeds Windows' 1 MB default thread
# stack and crashes on entry to the test function. Match the 8 MB stack POSIX
# platforms give us by default.
if(MSVC)
    target_link_options(gba_tests PRIVATE /STACK:8388608)
endif()

# Both targets must agree: these change the GBA struct layout (gba.h).
foreach(target gba_emulator gba_tests)
    if(ENABLE_XRAY)
        target_compile_definitions(${target} PRIVATE ENABLE_XRAY)
    endif()
    if(ENABLE_REWIND)
        target_compile_definitions(${target} PRIVATE ENABLE_REWIND)
    endif()
endforeach()

foreach(target gba_emulator gba_tests)
    add_dependencies(${target} hgss_rs)
    target_link_libraries(${target} PRIVATE "${HGSS_RS_LIB}")
endforeach()

add_test(NAME gba_tests COMMAND gba_tests)

# ── Install + packaging ─────────────────────────────────────────────
include(GNUInstallDirs)

if(MACOS_APP_BUNDLE)
    set(APP_DIR "${APP_NAME}.app")
    install(TARGETS gba_emulator BUNDLE DESTINATION .)
    install(DIRECTORY "${MACOS_SDL2_FRAMEWORK}" DESTINATION "${APP_DIR}/Contents/Frameworks"
            USE_SOURCE_PERMISSIONS)
    install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/fonts/Inter-LICENSE.txt
            DESTINATION "${APP_DIR}/Contents/Resources")
    # Sign last: Apple Silicon won't run unsigned code, and adding the
    # framework invalidated the linker's ad-hoc signature.
    install(CODE "execute_process(COMMAND codesign --force --deep --sign - \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${APP_DIR}\" COMMAND_ERROR_IS_FATAL ANY)")
else()
    install(TARGETS gba_emulator
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # On Windows, ship SDL2.dll (and any other runtime deps) next to the .exe so
    # end users don't need a separate SDL2 install. TARGET_RUNTIME_DLLS resolves
    # to the imported SHARED targets the binary links against. Requires CMake 3.21+.
    if(WIN32)
        install(FILES $<TARGET_RUNTIME_DLLS:gba_emulator>
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(FILES
        ${CMAKE_SOURCE_DIR}/README.md
        ${CMAKE_SOURCE_DIR}/LICENSE
        ${CMAKE_SOURCE_DIR}/fonts/Inter-LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )
endif()

set(CPACK_PACKAGE_NAME "gba_emulator")
set(CPACK_PACKAGE_VENDOR "amyanger")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
if(MACOS_APP_BUNDLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-macOS-arm64")
    set(CPACK_DMG_VOLUME_NAME "GBA Emulator")
    # No click-through license on mount (and udifrez is gone on new macOS).
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
endif()
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/" "/\\\\.git/" "/\\\\.github/" "/roms/[^/]" "/saves/[^/]" "/bios/[^/]"
    "\\\\.gba$" "\\\\.sav$" "\\\\.ss[0-9]$" "\\\\.png$"
)
include(CPack)
