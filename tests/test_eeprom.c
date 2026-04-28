#include "test_harness.h"
#include "cartridge/cartridge.h"
#include "cartridge/eeprom.h"

/* Bit-stream helpers — drive the EEPROM the same way DMA would, but in
 * isolation (no bus, no DMA, no Cartridge wrapper). */

static void send_bits(EEPROMChip* eep, uint64_t bits, int count) {
    for (int i = count - 1; i >= 0; i--) {
        eeprom_write_bit(eep, (uint8_t)((bits >> i) & 1));
    }
}

/* Send a full 4Kbit-style read command. count = 9. */
static void cmd_read_4k(EEPROMChip* eep, uint16_t addr) {
    eeprom_dma_begin(eep, 9);
    eeprom_write_bit(eep, 1); /* cmd[0] */
    eeprom_write_bit(eep, 1); /* cmd[1] = read */
    send_bits(eep, addr & 0x3F, 6);
    eeprom_write_bit(eep, 0); /* terminator */
}

static void cmd_read_64k(EEPROMChip* eep, uint16_t addr) {
    eeprom_dma_begin(eep, 17);
    eeprom_write_bit(eep, 1);
    eeprom_write_bit(eep, 1);
    send_bits(eep, addr & 0x3FFF, 14);
    eeprom_write_bit(eep, 0);
}

static void cmd_write_4k(EEPROMChip* eep, uint16_t addr, uint64_t data) {
    eeprom_dma_begin(eep, 73);
    eeprom_write_bit(eep, 1);
    eeprom_write_bit(eep, 0); /* cmd[1] = write */
    send_bits(eep, addr & 0x3F, 6);
    send_bits(eep, data, 64);
    eeprom_write_bit(eep, 0); /* terminator */
}

static void cmd_write_64k(EEPROMChip* eep, uint16_t addr, uint64_t data) {
    eeprom_dma_begin(eep, 81);
    eeprom_write_bit(eep, 1);
    eeprom_write_bit(eep, 0);
    send_bits(eep, addr & 0x3FFF, 14);
    send_bits(eep, data, 64);
    eeprom_write_bit(eep, 0);
}

/* Drain the 68-bit read response and reassemble the 64 data bits. */
static uint64_t read_response(EEPROMChip* eep) {
    eeprom_dma_begin(eep, 68);
    /* 4 ignored leading bits */
    for (int i = 0; i < 4; i++) (void)eeprom_read_bit(eep);
    uint64_t v = 0;
    for (int i = 0; i < 64; i++) {
        v = (v << 1) | (uint64_t)eeprom_read_bit(eep);
    }
    return v;
}

/* ---- Initialization ---- */

TEST(eeprom_init_state) {
    EEPROMChip eep;
    eeprom_init(&eep);
    ASSERT_EQ(eep.state, EEPROM_IDLE);
    ASSERT_EQ(eep.addr_bits, 0);
    ASSERT_EQ(eep.dirty, 0);
    /* Erased cells read 0xFF */
    ASSERT_EQ(eep.data[0], 0xFF);
    ASSERT_EQ(eep.data[EEPROM_MAX_SIZE - 1], 0xFF);
}

/* ---- Size detection from DMA count ---- */

TEST(eeprom_detects_4kbit_from_count_9) {
    EEPROMChip eep;
    eeprom_init(&eep);
    eeprom_dma_begin(&eep, 9);
    ASSERT_EQ(eep.addr_bits, 6);
}

TEST(eeprom_detects_64kbit_from_count_17) {
    EEPROMChip eep;
    eeprom_init(&eep);
    eeprom_dma_begin(&eep, 17);
    ASSERT_EQ(eep.addr_bits, 14);
}

TEST(eeprom_detects_4kbit_from_write_count_73) {
    EEPROMChip eep;
    eeprom_init(&eep);
    eeprom_dma_begin(&eep, 73);
    ASSERT_EQ(eep.addr_bits, 6);
}

TEST(eeprom_detects_64kbit_from_write_count_81) {
    EEPROMChip eep;
    eeprom_init(&eep);
    eeprom_dma_begin(&eep, 81);
    ASSERT_EQ(eep.addr_bits, 14);
}

/* ---- Read/write roundtrip ---- */

TEST(eeprom_4k_roundtrip) {
    EEPROMChip eep;
    eeprom_init(&eep);
    cmd_write_4k(&eep, 0x05, 0xDEADBEEFCAFEBABEULL);
    ASSERT_TRUE(eep.dirty);
    cmd_read_4k(&eep, 0x05);
    uint64_t got = read_response(&eep);
    ASSERT_EQ_HEX(got, 0xDEADBEEFCAFEBABEULL);
}

TEST(eeprom_64k_roundtrip) {
    EEPROMChip eep;
    eeprom_init(&eep);
    cmd_write_64k(&eep, 0x0123, 0x0011223344556677ULL);
    cmd_read_64k(&eep, 0x0123);
    uint64_t got = read_response(&eep);
    ASSERT_EQ_HEX(got, 0x0011223344556677ULL);
}

TEST(eeprom_unwritten_address_reads_all_ones) {
    EEPROMChip eep;
    eeprom_init(&eep);
    cmd_read_4k(&eep, 0x10);
    uint64_t got = read_response(&eep);
    ASSERT_EQ_HEX(got, 0xFFFFFFFFFFFFFFFFULL);
}

TEST(eeprom_independent_addresses_dont_collide) {
    EEPROMChip eep;
    eeprom_init(&eep);
    cmd_write_4k(&eep, 0x00, 0x1111111111111111ULL);
    cmd_write_4k(&eep, 0x3F, 0x2222222222222222ULL);
    cmd_read_4k(&eep, 0x00);
    ASSERT_EQ_HEX(read_response(&eep), 0x1111111111111111ULL);
    cmd_read_4k(&eep, 0x3F);
    ASSERT_EQ_HEX(read_response(&eep), 0x2222222222222222ULL);
}

/* ---- Ready poll ---- */

TEST(eeprom_ready_poll_returns_one) {
    EEPROMChip eep;
    eeprom_init(&eep);
    cmd_write_4k(&eep, 0x10, 0xABCDEF0123456789ULL);
    /* Game polls with count=1 until it sees a 1 bit. We always return 1
     * (no simulated write delay). */
    eeprom_dma_begin(&eep, 1);
    ASSERT_EQ(eeprom_read_bit(&eep), 1);
}

/* ---- Address-range gating ---- */

TEST(eeprom_addr_in_range_small_rom) {
    Cartridge cart = {0};
    cart.save_type = SAVE_EEPROM;
    cart.rom_size = 4 * 1024 * 1024; /* 4MB — small cart */
    /* Any address in the 0x0D... region is EEPROM */
    ASSERT_TRUE(eeprom_addr_in_range(&cart, 0x0D000000));
    ASSERT_TRUE(eeprom_addr_in_range(&cart, 0x0D000004));
    ASSERT_TRUE(eeprom_addr_in_range(&cart, 0x0DFFFF00));
    /* Outside 0x0D... — not EEPROM */
    ASSERT_TRUE(!eeprom_addr_in_range(&cart, 0x0C000000));
    ASSERT_TRUE(!eeprom_addr_in_range(&cart, 0x0E000000));
}

TEST(eeprom_addr_in_range_large_rom) {
    Cartridge cart = {0};
    cart.save_type = SAVE_EEPROM;
    cart.rom_size = 24 * 1024 * 1024; /* 24MB — > 16MB */
    /* Only the top 256 bytes are EEPROM; the rest of 0x0D... is ROM mirror. */
    ASSERT_TRUE(eeprom_addr_in_range(&cart, 0x0DFFFF00));
    ASSERT_TRUE(eeprom_addr_in_range(&cart, 0x0DFFFFFF));
    ASSERT_TRUE(!eeprom_addr_in_range(&cart, 0x0D000000));
    ASSERT_TRUE(!eeprom_addr_in_range(&cart, 0x0DFFFEFF));
}

TEST(eeprom_addr_in_range_disabled_when_no_eeprom_save_type) {
    Cartridge cart = {0};
    cart.save_type = SAVE_FLASH128;
    cart.rom_size = 4 * 1024 * 1024;
    ASSERT_TRUE(!eeprom_addr_in_range(&cart, 0x0D000000));
}

void run_eeprom_tests(void) {
    TEST_SUITE("eeprom");
    RUN_TEST(eeprom_init_state);
    RUN_TEST(eeprom_detects_4kbit_from_count_9);
    RUN_TEST(eeprom_detects_64kbit_from_count_17);
    RUN_TEST(eeprom_detects_4kbit_from_write_count_73);
    RUN_TEST(eeprom_detects_64kbit_from_write_count_81);
    RUN_TEST(eeprom_4k_roundtrip);
    RUN_TEST(eeprom_64k_roundtrip);
    RUN_TEST(eeprom_unwritten_address_reads_all_ones);
    RUN_TEST(eeprom_independent_addresses_dont_collide);
    RUN_TEST(eeprom_ready_poll_returns_one);
    RUN_TEST(eeprom_addr_in_range_small_rom);
    RUN_TEST(eeprom_addr_in_range_large_rom);
    RUN_TEST(eeprom_addr_in_range_disabled_when_no_eeprom_save_type);
}
