#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge/cartridge.h"
#include "cartridge/rom_zip.h"
#include "frontend/library.h"
#include "game/game_profile.h"

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

#define T_DIR "test_romzip"
#define T_ZIP T_DIR "/Game.zip"

typedef struct {
    const char* name;
    const uint8_t* data;
    uint32_t len;
} ZipFile;

static void put16(FILE* f, uint32_t v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE* f, uint32_t v) {
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

/* Fields shared by the local and central headers, from the flags on. */
static void put_common(FILE* f, const ZipFile* z) {
    put16(f, 0);  /* flags */
    put16(f, 0);  /* method: stored */
    put32(f, 0);  /* time, date */
    put32(f, game_crc32(z->data, z->len));
    put32(f, z->len);
    put32(f, z->len);
    put16(f, (uint32_t)strlen(z->name));
}

/* A zip of stored (uncompressed) files. */
static void write_zip(const char* path, const ZipFile* files, uint32_t n) {
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != NULL);
    uint32_t offs[4];
    for (uint32_t i = 0; i < n; i++) {
        offs[i] = (uint32_t)ftell(f);
        fwrite("PK\x03\x04\x14\x00", 1, 6, f);
        put_common(f, &files[i]);
        put16(f, 0);
        fwrite(files[i].name, 1, strlen(files[i].name), f);
        fwrite(files[i].data, 1, files[i].len, f);
    }
    uint32_t cd = (uint32_t)ftell(f);
    for (uint32_t i = 0; i < n; i++) {
        fwrite("PK\x01\x02\x14\x00\x14\x00", 1, 8, f);
        put_common(f, &files[i]);
        put16(f, 0);
        put32(f, 0);
        put32(f, 0);
        put16(f, 0);  /* extra, comment, disk, attrs: 12 bytes */
        put32(f, offs[i]);
        fwrite(files[i].name, 1, strlen(files[i].name), f);
    }
    uint32_t cd_end = (uint32_t)ftell(f);
    fwrite("PK\x05\x06\0\0\0\0", 1, 8, f);
    put16(f, n);
    put16(f, n);
    put32(f, cd_end - cd);
    put32(f, cd);
    put16(f, 0);
    fclose(f);
}

static uint8_t s_rom[0x400];
static uint8_t s_big[150 * 1024];

static void make_rom(void) {
    for (uint32_t i = 0; i < sizeof(s_rom); i++) s_rom[i] = (uint8_t)(i * 7);
    memcpy(s_rom + 0xA0, "ZIPPED GAME ", 12);
    memcpy(s_rom + 0xAC, "AZPE", 4);
}

static void clean(void) {
    const char* names[] = {"My Game.gba", "Game.zip", "Two.zip", "None.zip", "Far.zip", "Bad.zip", "My Game.gba.sav",
                           "Game.zip.sav", "game.gba.sav"};
    char p[256];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(p, sizeof(p), T_DIR "/%s", names[i]);
        remove(p);
    }
    remove_dir(T_DIR);
}

TEST(rom_zip_loads_and_shares_saves_only_with_its_copy) {
    clean(); /* a failed earlier run leaves files behind */
    make_dir(T_DIR);
    make_rom();
    ZipFile files[] = {{"__MACOSX/._My Game.gba", (const uint8_t*)"junk", 4},
                       {"folder/My Game.gba", s_rom, sizeof(s_rom)}};
    write_zip(T_ZIP, files, 2);

    Cartridge cart;
    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_TRUE(cart.zipped);
    ASSERT_EQ(cart.rom_size, (uint32_t)sizeof(s_rom));
    ASSERT_TRUE(memcmp(cart.rom, s_rom, sizeof(s_rom)) == 0);
    ASSERT_STR_EQ(cart.game_code, "AZPE");
    ASSERT_STR_EQ(cart.id_path, T_ZIP); /* no unzipped copy: the zip is the identity */
    ASSERT_STR_EQ(cart.save_path, T_ZIP ".sav");
    cartridge_destroy(&cart);

    /* The same game unzipped next to the zip: both share its save. */
    FILE* f = fopen(T_DIR "/My Game.gba", "wb");
    ASSERT_TRUE(f != NULL);
    fwrite(s_rom, 1, sizeof(s_rom), f);
    fclose(f);
    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_STR_EQ(cart.id_path, T_DIR "/My Game.gba");
    ASSERT_STR_EQ(cart.save_path, T_DIR "/My Game.gba.sav");
    cartridge_destroy(&cart);

    /* A different game with that name keeps its own save. */
    f = fopen(T_DIR "/My Game.gba", "r+b");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0x300, SEEK_SET);
    fputc(0x11, f);
    fclose(f);
    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_STR_EQ(cart.id_path, T_ZIP);
    cartridge_destroy(&cart);
    remove(T_DIR "/My Game.gba");

    clean();
}

TEST(rom_zip_failures_say_why) {
    clean();
    make_dir(T_DIR);
    make_rom();
    Cartridge cart;
    memset(&cart, 0, sizeof(cart));

    ZipFile two[] = {{"a.gba", s_rom, sizeof(s_rom)}, {"b.gba", s_rom, sizeof(s_rom)}};
    write_zip(T_ZIP, two, 2);
    ASSERT_TRUE(!cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_EQ(cart.zip_status, ROM_ZIP_MULTI);
    ASSERT_TRUE(cart.rom == NULL);

    ZipFile none[] = {{"readme.txt", s_rom, sizeof(s_rom)}};
    write_zip(T_ZIP, none, 1);
    ASSERT_TRUE(!cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_EQ(cart.zip_status, ROM_ZIP_NO_GAME);

    /* Corrupt one byte of the game: the CRC catches it. */
    ZipFile one[] = {{"a.gba", s_rom, sizeof(s_rom)}};
    write_zip(T_ZIP, one, 1);
    FILE* f = fopen(T_ZIP, "r+b");
    ASSERT_TRUE(f != NULL);
    fseek(f, 30 + 5 + 0x10, SEEK_SET);
    fputc(0x5A, f);
    fclose(f);
    ASSERT_TRUE(!cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_EQ(cart.zip_status, ROM_ZIP_BAD_CRC);

    /* A plain ROM load clears the old status. */
    write_zip(T_ZIP, one, 1);
    ASSERT_TRUE(cartridge_load(&cart, T_ZIP, NULL));
    ASSERT_EQ(cart.zip_status, ROM_ZIP_OK);
    cartridge_destroy(&cart);
    clean();
}

static Library s_lib; /* large: keep off the stack */

static int find_game(const char* name) {
    for (uint32_t i = 0; i < s_lib.game_count; i++)
        if (strcmp(s_lib.games[i].name, name) == 0) return (int)i;
    return -1;
}

TEST(rom_zip_library_lists_zipped_games) {
    clean();
    make_dir(T_DIR);
    make_rom();
    ZipFile game[] = {{"game.gba", s_rom, sizeof(s_rom)}};
    write_zip(T_DIR "/Game.zip", game, 1);
    ZipFile none[] = {{"readme.txt", s_rom, sizeof(s_rom)}};
    write_zip(T_DIR "/None.zip", none, 1);
    /* The game sits between the zip's first 64 KB and last 128 KB, so the scan reads it whole. */
    ZipFile far[] = {{"a.bin", s_big, sizeof(s_big)}, {"far.gba", s_rom, sizeof(s_rom)}, {"b.bin", s_big, sizeof(s_big)}};
    write_zip(T_DIR "/Far.zip", far, 3);
    FILE* f = fopen(T_DIR "/Bad.zip", "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("PK\x03\x04 not really a zip", 1, 22, f);
    fclose(f);

    library_init(&s_lib, NULL);
    ASSERT_TRUE(library_set_folder(&s_lib, T_DIR));
    library_scan(&s_lib);
    ASSERT_EQ(s_lib.game_count, 2); /* not the zip without a game, nor the broken one */
    int g = find_game("Game");
    ASSERT_TRUE(g >= 0);
    ASSERT_STR_EQ(s_lib.games[g].title, "ZIPPED GAME");
    ASSERT_STR_EQ(s_lib.games[g].code, "AZPE");
    ASSERT_EQ(s_lib.games[g].size, (uint32_t)sizeof(s_rom));
    g = find_game("Far");
    ASSERT_TRUE(g >= 0);
    ASSERT_STR_EQ(s_lib.games[g].code, "AZPE");

    char out[LIB_NAME_LEN];
    library_display_name("/a/emerald.gba.zip", out, sizeof(out));
    ASSERT_STR_EQ(out, "Emerald");
    ASSERT_TRUE(library_is_gba("x.ZIP"));
    clean();
}

void run_rom_zip_tests(void) {
    RUN_TEST(rom_zip_loads_and_shares_saves_only_with_its_copy);
    RUN_TEST(rom_zip_failures_say_why);
    RUN_TEST(rom_zip_library_lists_zipped_games);
}
