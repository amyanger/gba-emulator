cmake_minimum_required(VERSION 3.21)
project(gba_emulator
    VERSION 0.1.0
    DESCRIPTION "Game Boy Advance emulator (ARM7TDMI + scanline PPU + SDL2)"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings + per-config flags. MSVC needs separate handling because
# its driver doesn't understand GCC/Clang flags.
if(MSVC)
    add_compile_options(/W4 /wd4100 /wd4244 /wd4267 /wd4146 /wd4310)
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

find_package(SDL2 REQUIRED)

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

set(SOURCES
    src/main.c
    src/gba.c
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
    src/savestate/savestate.c
    src/screenshot/screenshot.c
    src/sio/sio.c
    src/sio/sio_link.c
    src/trace/trace.c
    src/frontend/frontend.c
    src/frontend/frame_advance.c
    src/frontend/input_display.c
    src/frontend/slot_picker.c
    # Shared overlay drawing primitives (used by input_display, slot_picker,
    # and the X-Ray panels regardless of ENABLE_XRAY).
    src/frontend/overlay_draw.c
    src/headless/fb_hash.c
    src/headless/headless.c
)

# X-Ray source files (only compiled when ENABLE_XRAY is ON)
set(XRAY_SOURCES
    src/frontend/xray/xray.c
    src/frontend/xray/xray_cpu.c
    src/frontend/xray/xray_activity.c
    src/frontend/xray/xray_ppu.c
    src/frontend/xray/xray_tiles.c
    src/frontend/xray/xray_audio.c
)

if(ENABLE_XRAY AND ENABLE_REWIND)
    list(APPEND XRAY_SOURCES src/frontend/xray/xray_rewind.c)
endif()

if(ENABLE_XRAY)
    list(APPEND SOURCES ${XRAY_SOURCES})
endif()

if(ENABLE_REWIND)
    list(APPEND SOURCES src/rewind/rewind.c src/rewind/rewind_lz4.c)
endif()

add_executable(gba_emulator ${SOURCES})

target_include_directories(gba_emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(gba_emulator PRIVATE ${SDL2_LIBRARIES} $<$<NOT:$<PLATFORM_ID:Windows>>:m>)

if(ENABLE_XRAY)
    target_compile_definitions(gba_emulator PRIVATE ENABLE_XRAY)
endif()

if(ENABLE_REWIND)
    target_compile_definitions(gba_emulator PRIVATE ENABLE_REWIND)
endif()

# ── Test suite ──────────────────────────────────────────────────────
enable_testing()

set(LIB_SOURCES
    src/gba.c
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
    src/screenshot/screenshot.c
    src/sio/sio.c
    src/sio/sio_link.c
    src/trace/trace.c
    src/headless/fb_hash.c
    src/headless/headless.c
    src/frontend/frame_advance.c
    src/frontend/input_display.c
    src/frontend/slot_picker.c
    # Shared overlay drawing primitives (used by input_display, slot_picker,
    # and the X-Ray panels regardless of ENABLE_XRAY).
    src/frontend/overlay_draw.c
)

if(ENABLE_REWIND)
    list(APPEND LIB_SOURCES src/rewind/rewind.c src/rewind/rewind_lz4.c)
endif()

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
    tests/test_apu.c
    tests/test_ppu.c
    tests/test_input.c
    tests/test_rewind.c
    tests/test_screenshot.c
    tests/test_trace.c
    tests/test_cartridge_autosave.c
    tests/test_keymap.c
    tests/test_eeprom.c
    tests/test_practice.c
    tests/test_fb_hash.c
    tests/test_ppu.c
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

add_executable(gba_tests ${TEST_SOURCES} ${LIB_SOURCES})

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

if(ENABLE_REWIND)
    target_compile_definitions(gba_tests PRIVATE ENABLE_REWIND)
endif()

add_test(NAME gba_tests COMMAND gba_tests)

# ── Install + packaging ─────────────────────────────────────────────
include(GNUInstallDirs)

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
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

set(CPACK_PACKAGE_NAME "gba_emulator")
set(CPACK_PACKAGE_VENDOR "admyanger")
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
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/" "/\\\\.git/" "/\\\\.github/" "/roms/[^/]" "/saves/[^/]" "/bios/[^/]"
    "\\\\.gba$" "\\\\.sav$" "\\\\.ss[0-9]$" "\\\\.png$"
)
include(CPack)
