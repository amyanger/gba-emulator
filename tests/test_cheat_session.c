#include "test_harness.h"
#include "cheat/cheat_session.h"
#include "cartridge/cartridge.h"
#include "cartridge/flash.h"

#include <stdio.h>

static Cartridge cart;
static CheatEngine eng;

static void setup(const char* save_path) {
    char err[128];
    memset(&cart, 0, sizeof(cart));
    cart.save_type = SAVE_FLASH128;
    flash_init(&cart.flash, true);
    snprintf(cart.save_path, sizeof(cart.save_path), "%s", save_path);
    cheat_init(&eng);
    cheat_add_text(&eng, "82025838 03E7\n", "a", CHEAT_DEV_AUTO, err, sizeof(err));
    cheat_add_text(&eng, "82025840 0001\n", "b", CHEAT_DEV_AUTO, err, sizeof(err));
    eng.cheats[0].enabled = eng.cheats[1].enabled = false;
}

static void cleanup(const char* sav) {
    char p[300];
    remove(sav);
    for (int k = 1; k <= 5; k++) {
        snprintf(p, sizeof(p), "%s.cheatbak%d", sav, k);
        remove(p);
    }
}

static bool exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

TEST(cheat_enable_backs_up_once_per_session) {
    const char* sav = "test_session_once.sav";
    cleanup(sav);
    setup(sav);
    ASSERT_EQ(cartridge_save_to_file(&cart), CART_FLUSH_OK);
    ASSERT_EQ(cheat_enable(&eng, 0, &cart), CHEAT_ENABLE_BACKED_UP);
    ASSERT_TRUE(eng.cheats[0].enabled);
    ASSERT_EQ(cheat_enable(&eng, 1, &cart), CHEAT_ENABLE_OK);
    ASSERT_TRUE(exists("test_session_once.sav.cheatbak1"));
    ASSERT_TRUE(!exists("test_session_once.sav.cheatbak2"));
    cleanup(sav);
}

TEST(cheat_enable_failed_backup_keeps_cheat_off) {
    setup("no_such_dir_session/x.sav");
    cart.save_dirty = true;
    ASSERT_EQ(cheat_enable(&eng, 0, &cart), CHEAT_ENABLE_BACKUP_FAILED);
    ASSERT_TRUE(!eng.cheats[0].enabled);
}

TEST(cheat_enable_without_save_file_proceeds) {
    const char* sav = "test_session_nosave.sav";
    cleanup(sav);
    setup(sav);
    ASSERT_EQ(cheat_enable(&eng, 0, &cart), CHEAT_ENABLE_OK);
    ASSERT_TRUE(eng.cheats[0].enabled);
}

TEST(cheat_enable_refuses_broken_and_bad_index) {
    const char* sav = "test_session_broken.sav";
    cleanup(sav);
    setup(sav);
    eng.cheats[1].broken = true;
    ASSERT_EQ(cheat_enable(&eng, 1, &cart), CHEAT_ENABLE_INVALID);
    ASSERT_EQ(cheat_enable(&eng, 9, &cart), CHEAT_ENABLE_INVALID);
    ASSERT_TRUE(!eng.cheats[1].enabled);
}

void run_cheat_session_tests(void) {
    TEST_SUITE("cheat_session");
    RUN_TEST(cheat_enable_backs_up_once_per_session);
    RUN_TEST(cheat_enable_failed_backup_keeps_cheat_off);
    RUN_TEST(cheat_enable_without_save_file_proceeds);
    RUN_TEST(cheat_enable_refuses_broken_and_bad_index);
}
