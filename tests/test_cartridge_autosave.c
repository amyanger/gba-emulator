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

void run_cartridge_autosave_tests(void) {
    TEST_SUITE("cartridge_autosave");
    RUN_TEST(autosave_tick_no_op_when_clean);
    RUN_TEST(autosave_tick_within_debounce_window_skips);
    RUN_TEST(autosave_tick_after_debounce_flushes);
    RUN_TEST(autosave_atomic_no_tmp_leftover);
    RUN_TEST(autosave_write_marks_save_dirty);
    RUN_TEST(autosave_skips_when_save_type_none);
}
