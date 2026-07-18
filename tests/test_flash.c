#include "test_harness.h"
#include "cartridge/flash.h"
#include "cartridge/cartridge.h"

/* Cartridge flash uses a Macronix-style command-sequence protocol:
 * specific bytes must be written to specific addresses to unlock
 * chip-identification, byte programming, sector erase, and bank
 * switching. Pokemon Emerald drives this every save.  These tests
 * pin the protocol's transitions and read paths. */

TEST(flash_init_128k_sets_macronix_ids) {
    FlashChip flash;
    flash_init(&flash, true);

    /* Manufacturer 0xC2 / device 0x09 → Macronix MX29L010. */
    ASSERT_EQ_HEX(flash.manufacturer, 0xC2);
    ASSERT_EQ_HEX(flash.device, 0x09);
    ASSERT_EQ(flash.state, FLASH_READY);
    ASSERT_EQ(flash.bank, 0);
}

TEST(flash_init_64k_sets_sanyo_ids) {
    FlashChip flash;
    flash_init(&flash, false);
    ASSERT_EQ_HEX(flash.manufacturer, 0x62);
    ASSERT_EQ_HEX(flash.device, 0x13);
}

TEST(flash_autoselect_returns_chip_ids) {
    /* Standard sequence: 0x5555=0xAA, 0x2AAA=0x55, 0x5555=0x90.
     * After this the chip enters Auto-Select mode where reads at
     * 0x0000 / 0x0001 return manufacturer / device. */
    FlashChip flash;
    flash_init(&flash, true);

    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0x90);
    ASSERT_EQ(flash.state, FLASH_AUTOSELECT);

    ASSERT_EQ_HEX(flash_read(&flash, 0x0000), 0xC2);
    ASSERT_EQ_HEX(flash_read(&flash, 0x0001), 0x09);

    /* 0xF0 exits autoselect. */
    flash_write(&flash, 0x0000, 0xF0);
    ASSERT_EQ(flash.state, FLASH_READY);
}

TEST(flash_byte_program_writes_data_then_returns_to_ready) {
    /* Byte program sequence: 0x5555=AA, 0x2AAA=55, 0x5555=A0,
     * then a single byte write to any address. */
    FlashChip flash;
    flash_init(&flash, true);

    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0xA0);
    ASSERT_EQ(flash.state, FLASH_WRITE);

    /* Flash byte-program can only clear bits (AND with existing,
     * which starts at 0xFF). */
    flash_write(&flash, 0x1234, 0x42);
    ASSERT_EQ_HEX(flash_read(&flash, 0x1234), 0x42);

    /* After the byte write, state returns to READY. */
    ASSERT_EQ(flash.state, FLASH_READY);
}

TEST(flash_sector_erase_clears_4kb_aligned_block) {
    /* Erase command requires two 3-step sequences. */
    FlashChip flash;
    flash_init(&flash, true);

    /* First, write a non-erased value via byte program. */
    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0xA0);
    flash_write(&flash, 0x3000, 0x42);
    ASSERT_EQ_HEX(flash_read(&flash, 0x3000), 0x42);

    /* Erase 4KB sector starting at 0x3000.
     * Sequence: AA 55 80 AA 55 30 (with the 30 at sector address). */
    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0x80);
    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x3000, 0x30);

    /* Sector should be reset to 0xFF. */
    ASSERT_EQ_HEX(flash_read(&flash, 0x3000), 0xFF);
    ASSERT_EQ_HEX(flash_read(&flash, 0x3FFF), 0xFF);
    ASSERT_EQ(flash.state, FLASH_READY);
}

TEST(flash_chip_erase_clears_all_data) {
    FlashChip flash;
    flash_init(&flash, true);

    /* Plant some data in two banks. Switch bank, write, switch back. */
    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0xA0);
    flash_write(&flash, 0x0000, 0x12);

    /* Chip erase: AA 55 80 AA 55 10 (with 10 at 0x5555). */
    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0x80);
    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0x10);

    ASSERT_EQ_HEX(flash_read(&flash, 0x0000), 0xFF);
    ASSERT_EQ(flash.state, FLASH_READY);
}

TEST(flash_bank_switch_selects_second_bank) {
    /* Bank switch: AA 55 B0, then write target bank to 0x0000. */
    FlashChip flash;
    flash_init(&flash, true);

    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0xB0);
    ASSERT_EQ(flash.state, FLASH_BANKSWITCH);

    flash_write(&flash, 0x0000, 1);
    ASSERT_EQ(flash.bank, 1);
    ASSERT_EQ(flash.state, FLASH_READY);
}

TEST(flash_bank_switch_ignored_on_64k_chip) {
    /* A 64K chip has no banks; the bank-switch command must not select
     * the (nonexistent) upper 64K.  A stray B0 sequence would otherwise
     * silently redirect all saves to the wrong half of the buffer. */
    FlashChip flash;
    flash_init(&flash, false);

    flash_write(&flash, 0x5555, 0xAA);
    flash_write(&flash, 0x2AAA, 0x55);
    flash_write(&flash, 0x5555, 0xB0);
    flash_write(&flash, 0x0000, 1);

    ASSERT_EQ(flash.bank, 0);
    ASSERT_EQ(flash.state, FLASH_READY);
}

TEST(flash_invalid_command_resets_state) {
    /* Wrong second-step value drops the FSM back to READY. */
    FlashChip flash;
    flash_init(&flash, true);

    flash_write(&flash, 0x5555, 0xAA);
    ASSERT_EQ(flash.state, FLASH_CMD1);
    flash_write(&flash, 0x2AAA, 0x42); /* wrong: need 0x55 */
    ASSERT_EQ(flash.state, FLASH_READY);
}

void run_flash_tests(void) {
    TEST_SUITE("flash");
    RUN_TEST(flash_init_128k_sets_macronix_ids);
    RUN_TEST(flash_init_64k_sets_sanyo_ids);
    RUN_TEST(flash_autoselect_returns_chip_ids);
    RUN_TEST(flash_byte_program_writes_data_then_returns_to_ready);
    RUN_TEST(flash_sector_erase_clears_4kb_aligned_block);
    RUN_TEST(flash_chip_erase_clears_all_data);
    RUN_TEST(flash_bank_switch_selects_second_bank);
    RUN_TEST(flash_bank_switch_ignored_on_64k_chip);
    RUN_TEST(flash_invalid_command_resets_state);
}
