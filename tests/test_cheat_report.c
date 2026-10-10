#include "test_harness.h"
#include "cheat/cheat_report.h"

#include <stdio.h>

static char out[4096];

static void report(const CheatEngine* e) {
    FILE* f = tmpfile();
    if (!f) { out[0] = '\0'; return; }
    cheat_report_print(e, f);
    rewind(f);
    size_t n = fread(out, 1, sizeof(out) - 1, f);
    out[n] = '\0';
    fclose(f);
}

TEST(cheat_report_lists_device_ops_and_problems) {
    static CheatEngine eng;
    char err[128];
    CheatRawList raw = {0};
    cheat_init(&eng);
    cheat_set_game_code(&eng, 0x45455042u);
    cheat_add_text(&eng, "Money\n82025838 03E7\n72000100 0001\n", NULL, CHEAT_DEV_AUTO, err, sizeof(err));
    raw.lines[0] = (CheatRawLine){0x88000000u, 0x0001u, 4};
    raw.count = 1;
    cheat_add_raw(&eng, "Bad", &raw, CHEAT_DEV_CODEBREAKER, true, true);
    report(&eng);
    ASSERT_TRUE(strstr(out, "BPEE") != NULL);
    ASSERT_TRUE(strstr(out, "Money") != NULL);
    ASSERT_TRUE(strstr(out, "CodeBreaker") != NULL);
    ASSERT_TRUE(strstr(out, "write16 [02025838] = 03E7") != NULL);
    ASSERT_TRUE(strstr(out, "if16 [02000100] == 0001") != NULL);
    ASSERT_TRUE(strstr(out, "Bad") != NULL);
    ASSERT_TRUE(strstr(out, "won't run") != NULL);
    ASSERT_TRUE(strstr(out, "outside game RAM") != NULL);
}

void run_cheat_report_tests(void) {
    TEST_SUITE("cheat_report");
    RUN_TEST(cheat_report_lists_device_ops_and_problems);
}
