#include "test_harness.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cartridge/cartridge.h"
#include "cartridge/flash.h"

static void cart_init_for_test(Cartridge* cart, const char* save_path) {
    memset(cart, 0, sizeof(*cart));
    cart->save_type = SAVE_FLASH128;
    flash_init(&cart->flash, true);
    snprintf(cart->save_path, sizeof(cart->save_path), "%s", save_path);
    /* Force last_save_flush far in the past so the debounce window has elapsed. */
    cart->last_save_flush = 0;
}

static long file_size(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fclose(fp);
    return sz;
}

TEST(autosave_tick_no_op_when_clean) {
    Cartridge cart;
    cart_init_for_test(&cart, "test_autosave_clean.sav");
    remove(cart.save_path);

    cartridge_save_tick(&cart, (time_t)1000000);
    ASSERT_EQ(file_size(cart.save_path), -1);  /* file should not exist */
}

TEST(autosave_tick_within_debounce_window_skips) {
    Cartridge cart;
    cart_init_for_test(&cart, "test_autosave_debounce.sav");
    remove(cart.save_path);

    cart.save_dirty = true;
    cart.last_save_flush = 1000;
    /* 1001 - 1000 = 1 second elapsed, well below the 5s debounce. */
    cartridge_save_tick(&cart, (time_t)1001);
    ASSERT_EQ(file_size(cart.save_path), -1);
    ASSERT_TRUE(cart.save_dirty);  /* still dirty — flush was deferred */
}

TEST(autosave_tick_after_debounce_flushes) {
    Cartridge cart;
    cart_init_for_test(&cart, "test_autosave_flush.sav");
    remove(cart.save_path);

    /* Dirty save 10 seconds after last flush — should write. */
    cart.save_dirty = true;
    cart.last_save_flush = 1000;
    cartridge_save_tick(&cart, (time_t)1010);

    /* Flash 128K = 0x20000 bytes payload + 16 byte RTC trailer. */
    long sz = file_size(cart.save_path);
    ASSERT_EQ(sz, 0x20000 + 16);
    ASSERT_TRUE(!cart.save_dirty);
    remove(cart.save_path);
}

TEST(autosave_atomic_no_tmp_leftover) {
    Cartridge cart;
    cart_init_for_test(&cart, "test_autosave_atomic.sav");
    remove(cart.save_path);
    char tmp_path[300];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cart.save_path);
    remove(tmp_path);

    cart.save_dirty = true;
    cartridge_save_tick(&cart, (time_t)1000000);

    /* Final file must exist; .tmp must not. */
    ASSERT_TRUE(file_size(cart.save_path) > 0);
    ASSERT_EQ(file_size(tmp_path), -1);
    remove(cart.save_path);
}

TEST(autosave_write_marks_save_dirty) {
    Cartridge cart;
    cart_init_for_test(&cart, "test_autosave_dirty_flag.sav");
    ASSERT_TRUE(!cart.save_dirty);

    /* Writing into the save region (0x0E000000+) should set save_dirty. */
    cartridge_write8(&cart, 0x0E000000, 0xAA);
    ASSERT_TRUE(cart.save_dirty);
}

TEST(autosave_skips_when_save_type_none) {
    Cartridge cart;
    cart_init_for_test(&cart, "test_autosave_no_save.sav");
    cart.save_type = SAVE_NONE;
    cart.save_dirty = true;
    remove(cart.save_path);

    cartridge_save_tick(&cart, (time_t)1000000);
    ASSERT_EQ(file_size(cart.save_path), -1);
}

/* Build a relative path of exactly `len` chars by padding with "./" so no
 * directories need to be created. Ends in a ROM file name. */
static void build_padded_path(char* buf, size_t len) {
    const char* name = (len % 2 == 0) ? "cartlong_rom.gba" : "cartlong_rom1.gba";
    size_t name_len = strlen(name);
    size_t pos = 0;
    while (pos + name_len < len) {
        buf[pos++] = '.';
        buf[pos++] = '/';
    }
    memcpy(buf + pos, name, name_len + 1);
}

#define LONG_ROM_SIZE 0x200

static bool write_sram_rom(const char* path, uint8_t* rom) {
    for (uint32_t i = 0; i < LONG_ROM_SIZE; i++) rom[i] = (uint8_t)(i * 7);
    memcpy(&rom[0x100], "SRAM_V113", 9);
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;
    size_t n = fwrite(rom, 1, LONG_ROM_SIZE, fp);
    return fclose(fp) == 0 && n == LONG_ROM_SIZE;
}

/* A ROM path whose "%s.sav" truncates must never be saved over, and must
 * never produce a save at a truncated sibling path. */
static void check_long_rom_path(size_t len) {
    char path[300];
    build_padded_path(path, len);
    ASSERT_EQ(strlen(path), len);

    uint8_t rom[LONG_ROM_SIZE];
    if (!write_sram_rom(path, rom)) {
        /* Windows without long-path support caps the resolved path at
         * MAX_PATH (260), so these paths can't be created from a deep CWD. */
        printf("SKIP (cannot create %zu-char path)\n", len);
        return;
    }

    /* Possible truncated save targets: "<rom>.sav" cut to 255 chars. */
    char truncated[256];
    int n = snprintf(truncated, sizeof(truncated), "%s.sav", path);
    ASSERT_TRUE(n >= (int)sizeof(truncated));
    if (strcmp(truncated, path) != 0) remove(truncated);

    Cartridge cart;
    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(cartridge_load(&cart, path));
    ASSERT_EQ(cart.save_type, SAVE_SRAM);

    cart.sram[0] = 0x5A;
    cart.save_dirty = true;
    cartridge_save_to_file(&cart);
    cartridge_destroy(&cart);

    ASSERT_EQ(file_size(path), LONG_ROM_SIZE);
    uint8_t after[LONG_ROM_SIZE];
    FILE* fp = fopen(path, "rb");
    ASSERT_TRUE(fp != NULL);
    size_t got = fread(after, 1, LONG_ROM_SIZE, fp);
    fclose(fp);
    ASSERT_EQ(got, LONG_ROM_SIZE);
    ASSERT_MEM_EQ(after, rom, LONG_ROM_SIZE);
    if (strcmp(truncated, path) != 0) ASSERT_EQ(file_size(truncated), -1);

    remove(path);
}

TEST(save_path_truncation_equal_to_rom_path) {
    check_long_rom_path(255);
}

TEST(save_path_truncation_shortened) {
    check_long_rom_path(253);
}

TEST(save_path_too_long_rom_path) {
    check_long_rom_path(270);
}

TEST(save_roundtrip_with_rtc_trailer) {
    const char* path = "test_autosave_roundtrip.sav";
    Cartridge a;
    cart_init_for_test(&a, path);
    remove(path);
    for (uint32_t i = 0; i < 0x20000; i++) a.flash.data[i] = (uint8_t)(i ^ (i >> 8));
    a.rtc.offset_secs = -3600;
    a.save_dirty = true;
    cartridge_save_to_file(&a);
    ASSERT_TRUE(!a.save_dirty);
    ASSERT_EQ(file_size(path), 0x20000 + 16);

    Cartridge b;
    cart_init_for_test(&b, path);
    cartridge_load_save_file(&b);
    ASSERT_MEM_EQ(b.flash.data, a.flash.data, 0x20000);
    ASSERT_EQ(b.rtc.offset_secs, -3600);
    remove(path);
}

/* The second save must replace the first. MSVC rename() refuses to overwrite
 * an existing file, so this is the case that broke every save after the
 * first on Windows. */
TEST(save_overwrites_existing_save) {
    const char* path = "test_autosave_overwrite.sav";
    Cartridge a;
    cart_init_for_test(&a, path);
    remove(path);
    a.flash.data[0] = 0x11;
    a.save_dirty = true;
    cartridge_save_to_file(&a);
    ASSERT_TRUE(!a.save_dirty);

    a.flash.data[0] = 0x22;
    a.save_dirty = true;
    cartridge_save_to_file(&a);
    ASSERT_TRUE(!a.save_dirty);

    Cartridge b;
    cart_init_for_test(&b, path);
    cartridge_load_save_file(&b);
    ASSERT_EQ(b.flash.data[0], 0x22);
    remove(path);
}

void run_cartridge_autosave_tests(void) {
    TEST_SUITE("cartridge_autosave");
    RUN_TEST(autosave_tick_no_op_when_clean);
    RUN_TEST(autosave_tick_within_debounce_window_skips);
    RUN_TEST(autosave_tick_after_debounce_flushes);
    RUN_TEST(autosave_atomic_no_tmp_leftover);
    RUN_TEST(autosave_write_marks_save_dirty);
    RUN_TEST(autosave_skips_when_save_type_none);
    RUN_TEST(save_path_truncation_equal_to_rom_path);
    RUN_TEST(save_path_truncation_shortened);
    RUN_TEST(save_path_too_long_rom_path);
    RUN_TEST(save_roundtrip_with_rtc_trailer);
    RUN_TEST(save_overwrites_existing_save);
}
