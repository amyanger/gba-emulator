cmake_minimum_required(VERSION 3.16)
project(gba_emulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Debug vs Release
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(SDL2 REQUIRED)

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
    src/ppu/affine.c
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
    src/cheat/cheat.c
    src/cheat/cheat_file.c
    src/savestate/savestate.c
    src/screenshot/screenshot.c
    src/frontend/frontend.c
)

# X-Ray source files (only compiled when ENABLE_XRAY is ON)
set(XRAY_SOURCES
    src/frontend/xray/xray.c
    src/frontend/xray/xray_draw.c
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

target_link_libraries(gba_emulator PRIVATE ${SDL2_LIBRARIES} m)

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
    src/ppu/affine.c
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
    src/cheat/cheat.c
    src/cheat/cheat_file.c
    src/screenshot/screenshot.c
)

if(ENABLE_REWIND)
    list(APPEND LIB_SOURCES src/rewind/rewind.c src/rewind/rewind_lz4.c)
endif()

add_executable(gba_tests
    tests/test_runner.c
    tests/test_savestate.c
    tests/test_bus.c
    tests/test_cpu.c
    tests/test_rtc.c
    tests/test_rewind.c
    tests/test_screenshot.c
    ${LIB_SOURCES}
)

target_include_directories(gba_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(gba_tests PRIVATE m)

if(ENABLE_REWIND)
    target_compile_definitions(gba_tests PRIVATE ENABLE_REWIND)
endif()

add_test(NAME gba_tests COMMAND gba_tests)
