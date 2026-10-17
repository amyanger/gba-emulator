#include "test_harness.h"

#include <stdio.h>
#include <string.h>

#include "cartridge/cartridge.h"
#include "cartridge/rom_patch.h"

#define T_ROM "test_rompatch.gba"
#define T_IPS "test_rompatch.ips"
#define T_BPS "test_rompatch.bps"
#define T_JUNK "test_rompatch_junk.ips"

static void write_file(const char* path, const void* data, size_t n) {
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != NULL);
    fwrite(data, 1, n, f);
    fclose(f);
}

static void clean_files(void) {
    remove(T_ROM);
    remove(T_IPS);
    remove(T_BPS);
    remove(T_JUNK);
    remove(T_IPS ".sav");
    remove(T_ROM ".sav");
}

/* Writes 4 bytes at 0x10, and 2 bytes at 0x100 which is past the 0xC0-byte base ROM. */
static const uint8_t IPS_EXTEND[] = {'P', 'A', 'T', 'C', 'H', 0, 0, 0x10, 0, 4, 1, 2, 3, 4,
                                     0, 1, 0, 0, 2, 9, 8, 'E', 'O', 'F'};

TEST(rom_patch_ips_applies_and_takes_identity) {
    clean_files(); /* a failed earlier run leaves files behind */
    uint8_t rom[0xC0];
    memset(rom, 0xAA, sizeof(rom));
    write_file(T_ROM, rom, sizeof(rom));
    write_file(T_IPS, IPS_EXTEND, sizeof(IPS_EXTEND));

    Cartridge cart;
    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(cartridge_load(&cart, T_ROM, T_IPS));
    ASSERT_TRUE(cart.patched);
    ASSERT_EQ(cart.rom_size, 0x102u);
    ASSERT_EQ(cart.rom[0x10], 1);
    ASSERT_EQ(cart.rom[0x13], 4);
    ASSERT_EQ(cart.rom[0x100], 9);
    ASSERT_EQ(cart.rom[0x101], 8);
    ASSERT_EQ(cart.rom[0x20], 0xAA);
    ASSERT_TRUE(strcmp(cart.id_path, T_IPS) == 0);
    ASSERT_TRUE(strcmp(cart.save_path, T_IPS ".sav") == 0);
    cartridge_destroy(&cart);

    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(cartridge_load(&cart, T_ROM, NULL));
    ASSERT_TRUE(!cart.patched);
    ASSERT_TRUE(strcmp(cart.save_path, T_ROM ".sav") == 0);
    cartridge_destroy(&cart);
    clean_files();
}

TEST(rom_patch_garbage_patch_fails_load) {
    clean_files(); /* a failed earlier run leaves files behind */
    uint8_t rom[0xC0];
    memset(rom, 0, sizeof(rom));
    write_file(T_ROM, rom, sizeof(rom));
    write_file(T_JUNK, "not a patch at all", 18);

    Cartridge cart;
    memset(&cart, 0, sizeof(cart));
    ASSERT_TRUE(!cartridge_load(&cart, T_ROM, T_JUNK));
    ASSERT_EQ(cart.patch_status, ROM_PATCH_NOT_PATCH);
    ASSERT_TRUE(cart.rom == NULL);

    ASSERT_TRUE(!cartridge_load(&cart, T_ROM, "test_rompatch_missing.ips"));
    ASSERT_EQ(cart.patch_status, ROM_PATCH_CANT_READ);
    clean_files();
}

TEST(rom_patch_find_patch_prefers_bps) {
    clean_files(); /* a failed earlier run leaves files behind */
    char out[64];
    ASSERT_TRUE(!cartridge_find_patch(T_ROM, out, sizeof(out)));
    write_file(T_IPS, IPS_EXTEND, sizeof(IPS_EXTEND));
    ASSERT_TRUE(cartridge_find_patch(T_ROM, out, sizeof(out)));
    ASSERT_TRUE(strcmp(out, T_IPS) == 0);
    write_file(T_BPS, "x", 1);
    ASSERT_TRUE(cartridge_find_patch(T_ROM, out, sizeof(out)));
    ASSERT_TRUE(strcmp(out, T_BPS) == 0);
    clean_files();
}

void run_rom_patch_tests(void) {
    TEST_SUITE("rom_patch");
    RUN_TEST(rom_patch_ips_applies_and_takes_identity);
    RUN_TEST(rom_patch_garbage_patch_fails_load);
    RUN_TEST(rom_patch_find_patch_prefers_bps);
}
