#include "test_harness.h"
#include "cheat/cheat_backup.h"
#include "cartridge/cartridge.h"
#include "cartridge/flash.h"

#include <stdio.h>

static Cartridge cart;

static void cart_for_test(const char* save_path) {
    memset(&cart, 0, sizeof(cart));
    cart.save_type = SAVE_FLASH128;
    flash_init(&cart.flash, true);
    snprintf(cart.save_path, sizeof(cart.save_path), "%s", save_path);
}

static void remove_all(const char* sav) {
    char p[300];
    remove(sav);
    for (int k = 1; k <= 6; k++) {
        snprintf(p, sizeof(p), "%s.cheatbak%d", sav, k);
        remove(p);
    }
}

/* First data byte of a saved file, or -1 if missing. */
static int first_byte(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    int c = fgetc(f);
    fclose(f);
    return c;
}

static int bak_byte(const char* sav, int k) {
    char p[300];
    snprintf(p, sizeof(p), "%s.cheatbak%d", sav, k);
    return first_byte(p);
}

TEST(cheat_backup_no_save_chip) {
    cart_for_test("test_cheatbak_none.sav");
    cart.save_type = SAVE_NONE;
    ASSERT_EQ(cheat_backup_save(&cart), CHEAT_BACKUP_NO_SAVE);
}

TEST(cheat_backup_no_file_yet) {
    const char* sav = "test_cheatbak_nofile.sav";
    remove_all(sav);
    cart_for_test(sav);
    ASSERT_EQ(cheat_backup_save(&cart), CHEAT_BACKUP_NO_SAVE);
    ASSERT_EQ(bak_byte(sav, 1), -1);
}

TEST(cheat_backup_copies_current_save) {
    const char* sav = "test_cheatbak_copy.sav";
    remove_all(sav);
    cart_for_test(sav);
    cart.flash.data[0] = 0x42;
    ASSERT_EQ(cartridge_save_to_file(&cart), CART_FLUSH_OK);
    ASSERT_EQ(cheat_backup_save(&cart), CHEAT_BACKUP_OK);
    ASSERT_EQ(bak_byte(sav, 1), 0x42);
    remove_all(sav);
}

TEST(cheat_backup_flushes_pending_data_first) {
    const char* sav = "test_cheatbak_flush.sav";
    remove_all(sav);
    cart_for_test(sav);
    cart.flash.data[0] = 0x01;
    ASSERT_EQ(cartridge_save_to_file(&cart), CART_FLUSH_OK);
    cart.flash.data[0] = 0x02;   /* newer in-game save, not on disk yet */
    cart.save_dirty = true;
    ASSERT_EQ(cheat_backup_save(&cart), CHEAT_BACKUP_OK);
    ASSERT_EQ(bak_byte(sav, 1), 0x02);
    ASSERT_TRUE(!cart.save_dirty);
    remove_all(sav);
}

TEST(cheat_backup_keeps_last_five) {
    const char* sav = "test_cheatbak_rotate.sav";
    remove_all(sav);
    cart_for_test(sav);
    for (int i = 1; i <= 6; i++) {
        cart.flash.data[0] = (uint8_t)i;
        ASSERT_EQ(cartridge_save_to_file(&cart), CART_FLUSH_OK);
        ASSERT_EQ(cheat_backup_save(&cart), CHEAT_BACKUP_OK);
    }
    ASSERT_EQ(bak_byte(sav, 1), 6);
    ASSERT_EQ(bak_byte(sav, 2), 5);
    ASSERT_EQ(bak_byte(sav, 5), 2);
    ASSERT_EQ(bak_byte(sav, 6), -1);
    remove_all(sav);
}

TEST(cheat_backup_fails_when_flush_fails) {
    cart_for_test("no_such_dir_cheatbak/x.sav");
    cart.save_dirty = true;
    ASSERT_EQ(cheat_backup_save(&cart), CHEAT_BACKUP_FAILED);
}

void run_cheat_backup_tests(void) {
    TEST_SUITE("cheat_backup");
    RUN_TEST(cheat_backup_no_save_chip);
    RUN_TEST(cheat_backup_no_file_yet);
    RUN_TEST(cheat_backup_copies_current_save);
    RUN_TEST(cheat_backup_flushes_pending_data_first);
    RUN_TEST(cheat_backup_keeps_last_five);
    RUN_TEST(cheat_backup_fails_when_flush_fails);
}
