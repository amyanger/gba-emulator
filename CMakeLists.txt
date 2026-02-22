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
    src/input/input.c
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

if(ENABLE_XRAY)
    list(APPEND SOURCES ${XRAY_SOURCES})
endif()

add_executable(gba_emulator ${SOURCES})

target_include_directories(gba_emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(gba_emulator PRIVATE ${SDL2_LIBRARIES})

if(ENABLE_XRAY)
    target_compile_definitions(gba_emulator PRIVATE ENABLE_XRAY)
endif()
