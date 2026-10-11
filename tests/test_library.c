#include "test_harness.h"
#include "frontend/library.h"
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define remove_dir(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir(p, 0755)
#define remove_dir(p) rmdir(p)
#endif

#define DIR_A "lib_test_roms"
#define LIST "lib_test_library.txt"

/* Large: keep off the stack. */
static Library s_lib;

static void write_rom(const char* path, const char* title, const char* code, uint32_t size) {
    uint8_t hdr[LIB_HEADER_LEN];
    memset(hdr, 0, sizeof(hdr));
    if (title) memcpy(hdr + 0xA0, title, strlen(title));
    if (code) memcpy(hdr + 0xAC, code, 4);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(hdr, 1, size < sizeof(hdr) ? size : sizeof(hdr), f);
    for (uint32_t i = sizeof(hdr); i < size; i++) fputc(0, f);
    fclose(f);
}

static void make_folder(void) {
    make_dir(DIR_A);
    write_rom(DIR_A "/emerald.gba", "POKEMON EMER", "BPEE", 1024);
    write_rom(DIR_A "/jsmolka_arm.GBA", "GBA Tests", "1337", 512);
    write_rom(DIR_A "/blank.gba", NULL, NULL, 256);
    write_rom(DIR_A "/tiny.gba", NULL, NULL, 16);
    write_rom(DIR_A "/notes.txt", NULL, NULL, 10);
    write_rom(DIR_A "/.hidden.gba", NULL, NULL, 256);
}

static void clean_folder(void) {
    const char* names[] = {"emerald.gba", "jsmolka_arm.GBA", "blank.gba", "tiny.gba", "notes.txt", ".hidden.gba"};
    char p[256];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(p, sizeof(p), DIR_A "/%s", names[i]);
        remove(p);
    }
    remove_dir(DIR_A);
    remove(LIST);
}

static int find(const Library* lib, const char* name) {
    for (uint32_t i = 0; i < lib->game_count; i++)
        if (strcmp(lib->games[i].name, name) == 0) return (int)i;
    return -1;
}

TEST(library_header_parse) {
    uint8_t h[LIB_HEADER_LEN];
    char title[13], code[5];
    memset(h, 0, sizeof(h));
    memcpy(h + 0xA0, "POKEMON EMER", 12);
    memcpy(h + 0xAC, "BPEE", 4);
    library_parse_header(h, sizeof(h), title, code);
    ASSERT_STR_EQ(title, "POKEMON EMER");
    ASSERT_STR_EQ(code, "BPEE");
    memset(h, 0, sizeof(h));
    memcpy(h + 0xA0, "m3_demo  ", 9);
    h[0xA3] = 0x01;
    library_parse_header(h, sizeof(h), title, code);
    ASSERT_STR_EQ(title, "m3_?emo");
    ASSERT_STR_EQ(code, "");  /* blank code */
    library_parse_header(h, 0x20, title, code);  /* short file */
    ASSERT_STR_EQ(title, "");
}

TEST(library_display_names) {
    char out[LIB_NAME_LEN];
    library_display_name("/a/b/jsmolka_arm.gba", out, sizeof(out));
    ASSERT_STR_EQ(out, "Jsmolka Arm");
    library_display_name("C:\\roms\\fire-red v1.1.GBA", out, sizeof(out));
    ASSERT_STR_EQ(out, "Fire Red V1.1");
    library_display_name("emerald", out, sizeof(out));
    ASSERT_STR_EQ(out, "Emerald");
    char small[5];
    library_display_name("caf\xC3\xA9.gba", small, sizeof(small));
    ASSERT_STR_EQ(small, "Caf");  /* never half a UTF-8 character */
    ASSERT_TRUE(library_is_gba("x.GbA"));
    ASSERT_TRUE(!library_is_gba(".gba"));
    ASSERT_TRUE(!library_is_gba("x.gbc"));
}

TEST(library_scan_folder) {
    make_folder();
    library_init(&s_lib, NULL);
    ASSERT_TRUE(library_set_folder(&s_lib, DIR_A));
    library_scan(&s_lib);
    ASSERT_TRUE(!s_lib.folder_missing);
    ASSERT_EQ(s_lib.game_count, 4);  /* not notes.txt or the hidden file */
    ASSERT_STR_EQ(s_lib.games[0].name, "Blank");  /* sorted by name */
    int e = find(&s_lib, "Emerald");
    ASSERT_TRUE(e >= 0);
    ASSERT_STR_EQ(s_lib.games[e].title, "POKEMON EMER");
    ASSERT_STR_EQ(s_lib.games[e].code, "BPEE");
    ASSERT_EQ(s_lib.games[e].size, 1024);
    ASSERT_TRUE(!s_lib.games[e].companion);  /* wrong size for the retail ROM */
    ASSERT_TRUE(find(&s_lib, "Tiny") >= 0);
    ASSERT_EQ(s_lib.banner, -1);
    clean_folder();
}

TEST(library_missing_folder) {
    library_init(&s_lib, NULL);
    library_set_folder(&s_lib, "lib_test_no_such_dir");
    library_scan(&s_lib);
    ASSERT_TRUE(s_lib.folder_missing);
    ASSERT_EQ(s_lib.game_count, 0);
}

TEST(library_recents_order_dedupe_cap) {
    library_init(&s_lib, NULL);
    char p[32];
    for (int i = 0; i < 12; i++) {
        snprintf(p, sizeof(p), "/r/%d.gba", i);
        library_add_recent(&s_lib, p, 100 + i);
    }
    ASSERT_EQ(s_lib.recent_count, LIB_RECENTS);
    ASSERT_STR_EQ(s_lib.recents[0].path, "/r/11.gba");
    library_add_recent(&s_lib, "/r/5.gba", 500);
    ASSERT_EQ(s_lib.recent_count, LIB_RECENTS);
    ASSERT_STR_EQ(s_lib.recents[0].path, "/r/5.gba");
    ASSERT_EQ(s_lib.recents[0].when, 500);
    for (uint32_t i = 1; i < s_lib.recent_count; i++) ASSERT_TRUE(strcmp(s_lib.recents[i].path, "/r/5.gba") != 0);
}

TEST(library_recents_merge_banner_and_missing) {
    make_folder();
    library_init(&s_lib, NULL);
    library_set_folder(&s_lib, DIR_A);
    char path[LIB_PATH_LEN];
    ASSERT_TRUE(library_abspath(DIR_A "/emerald.gba", path, sizeof(path)));
    library_add_recent(&s_lib, "/no/such/file.gba", 50);
    library_add_recent(&s_lib, path, 100);
    library_scan(&s_lib);
    ASSERT_EQ(s_lib.recent_count, 1);  /* the missing one is dropped */
    ASSERT_EQ(s_lib.game_count, 4);    /* no duplicate for the recent inside the folder */
    ASSERT_TRUE(s_lib.banner >= 0);
    ASSERT_STR_EQ(s_lib.games[s_lib.banner].name, "Emerald");
    ASSERT_EQ(s_lib.games[s_lib.banner].played, 100);
    clean_folder();
}

TEST(library_file_round_trip_and_corrupt) {
    remove(LIST);
    library_init(&s_lib, LIST);
    ASSERT_TRUE(!library_load(&s_lib));
    library_set_folder(&s_lib, "/some/folder");
    library_add_recent(&s_lib, "/x/a.gba", 10);
    library_add_recent(&s_lib, "/x/b b.gba", 20);
    ASSERT_TRUE(library_save(&s_lib));
    static Library back;
    library_init(&back, LIST);
    ASSERT_TRUE(library_load(&back));
    ASSERT_STR_EQ(back.folder, s_lib.folder);  /* as stored: Windows makes it C:\\some\\folder */
    ASSERT_EQ(back.recent_count, 2);
    ASSERT_STR_EQ(back.recents[0].path, "/x/b b.gba");
    ASSERT_EQ(back.recents[1].when, 10);

    FILE* f = fopen(LIST, "w");
    ASSERT_TRUE(f != NULL);
    fputs("garbage\nrecent=abc|/x.gba\nrecent=5\nrecent=7|\nrecent=9|/ok.gba\r\nfolder=/f\n", f);
    fclose(f);
    library_init(&back, LIST);
    ASSERT_TRUE(library_load(&back));
    ASSERT_EQ(back.recent_count, 1);
    ASSERT_STR_EQ(back.recents[0].path, "/ok.gba");
    ASSERT_STR_EQ(back.folder, "/f");
    remove(LIST);
}

TEST(library_default_folder_only_when_unset) {
    library_init(&s_lib, NULL);
    ASSERT_TRUE(library_default_folder(&s_lib, "/first"));
    ASSERT_TRUE(!library_default_folder(&s_lib, "/second"));
    ASSERT_TRUE(strstr(s_lib.folder, "first") != NULL);
}

TEST(library_folder_cap) {
    make_dir(DIR_A);
    char p[64];
    for (int i = 0; i < LIB_FOLDER_MAX + 20; i++) {
        snprintf(p, sizeof(p), DIR_A "/g%03d.gba", i);
        write_rom(p, "T", "CODE", 200);
    }
    library_init(&s_lib, NULL);
    library_set_folder(&s_lib, DIR_A);
    library_scan(&s_lib);
    ASSERT_EQ(s_lib.game_count, LIB_FOLDER_MAX);
    for (int i = 0; i < LIB_FOLDER_MAX + 20; i++) {
        snprintf(p, sizeof(p), DIR_A "/g%03d.gba", i);
        remove(p);
    }
    remove_dir(DIR_A);
}

void run_library_tests(void) {
    RUN_TEST(library_header_parse);
    RUN_TEST(library_display_names);
    RUN_TEST(library_scan_folder);
    RUN_TEST(library_missing_folder);
    RUN_TEST(library_recents_order_dedupe_cap);
    RUN_TEST(library_recents_merge_banner_and_missing);
    RUN_TEST(library_file_round_trip_and_corrupt);
    RUN_TEST(library_default_folder_only_when_unset);
    RUN_TEST(library_folder_cap);
}
