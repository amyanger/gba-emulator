#include "test_harness.h"
#include "frontend/session_log.h"
#include <stdio.h>
#include <string.h>

#define PATH "session_log_test.log"

static void put(const char* text) {
    FILE* f = fopen(PATH, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static long file_size(void) {
    FILE* f = fopen(PATH, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

TEST(session_log_path_names) {
    char out[64];
    ASSERT_TRUE(session_log_path(out, sizeof(out), "a.gba", "", 0xBEEF, "run"));
    ASSERT_STR_EQ(out, "a.gba.0000BEEF.run");
    ASSERT_TRUE(session_log_path(out, sizeof(out), "a.gba", "fixed.txt", 0xBEEF, "run"));
    ASSERT_STR_EQ(out, "fixed.txt");
    ASSERT_TRUE(!session_log_path(out, sizeof(out), "", NULL, 1, "run"));
    ASSERT_TRUE(!session_log_path(out, 8, "a.gba", "", 1, "run"));   /* doesn't fit */
}

TEST(session_log_open_states) {
    SessionLogReader r;
    remove(PATH);
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_MISSING);
    put("");
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_EMPTY);
    put("something else\n");
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_FOREIGN);
    put("log 10\n");   /* header must match exactly, not just as a prefix */
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_FOREIGN);
    put("log 1\r\n");
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_OK);
    ASSERT_TRUE(session_log_close(&r, PATH));
    remove(PATH);
}

TEST(session_log_skips_and_repairs_torn_tail) {
    SessionLogReader r;
    char line[16];
    put("log 1\nfirst\nsecond\nhalf-writ");
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_OK);
    ASSERT_TRUE(session_log_next(&r, line, sizeof(line)));
    ASSERT_STR_EQ(line, "first\n");
    ASSERT_TRUE(session_log_next(&r, line, sizeof(line)));
    ASSERT_STR_EQ(line, "second\n");
    ASSERT_TRUE(!session_log_next(&r, line, sizeof(line)));
    ASSERT_EQ(r.bad, 1);
    long before = file_size();
    ASSERT_TRUE(session_log_close(&r, PATH));
    ASSERT_EQ(file_size(), before + 1);   /* newline added after the torn line */

    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_OK);
    while (session_log_next(&r, line, sizeof(line))) {}
    ASSERT_EQ(r.bad, 0);   /* the torn line is now whole (and the caller's parser rejects it) */
    ASSERT_TRUE(session_log_close(&r, PATH));
    ASSERT_EQ(file_size(), before + 1);   /* nothing to repair the second time */
    remove(PATH);
}

TEST(session_log_skips_overlong_lines) {
    SessionLogReader r;
    char line[8];
    put("log 1\nthis line is too long\nok\n");
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_OK);
    ASSERT_TRUE(session_log_next(&r, line, sizeof(line)));
    ASSERT_STR_EQ(line, "ok\n");
    ASSERT_TRUE(!session_log_next(&r, line, sizeof(line)));
    ASSERT_EQ(r.bad, 1);
    ASSERT_TRUE(session_log_close(&r, PATH));
    remove(PATH);
}

TEST(session_log_skips_nul_lines) {
    SessionLogReader r;
    char line[16];
    FILE* f = fopen(PATH, "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("log 1\n\0junk\nok\n", 1, 15, f);
    fclose(f);
    ASSERT_EQ(session_log_open(&r, PATH, "log 1"), SESSION_LOG_OK);
    ASSERT_TRUE(session_log_next(&r, line, sizeof(line)));
    ASSERT_STR_EQ(line, "ok\n");
    ASSERT_EQ(r.bad, 1);
    ASSERT_TRUE(session_log_close(&r, PATH));
    remove(PATH);
}

TEST(session_tid_gate_debounces) {
    SessionTidGate g = {0, 0};
    for (int i = 1; i < SESSION_ATTACH_FRAMES; i++) ASSERT_TRUE(!session_tid_step(&g, false, 0, 0xA));
    ASSERT_TRUE(session_tid_step(&g, false, 0, 0xA));
    for (int i = 0; i < 100; i++) ASSERT_TRUE(!session_tid_step(&g, true, 0xA, 0));   /* blank: never */
    /* A new ID that is interrupted by the attached one starts counting again. */
    for (int i = 0; i < SESSION_ATTACH_FRAMES - 1; i++) ASSERT_TRUE(!session_tid_step(&g, true, 0xA, 0xB));
    ASSERT_TRUE(!session_tid_step(&g, true, 0xA, 0xA));
    for (int i = 1; i < SESSION_ATTACH_FRAMES; i++) ASSERT_TRUE(!session_tid_step(&g, true, 0xA, 0xB));
    ASSERT_TRUE(session_tid_step(&g, true, 0xA, 0xB));
}

void run_session_log_tests(void) {
    RUN_TEST(session_log_path_names);
    RUN_TEST(session_log_open_states);
    RUN_TEST(session_log_skips_and_repairs_torn_tail);
    RUN_TEST(session_log_skips_overlong_lines);
    RUN_TEST(session_log_skips_nul_lines);
    RUN_TEST(session_tid_gate_debounces);
}
