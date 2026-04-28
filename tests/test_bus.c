#include "test_harness.h"
#include "gba.h"
#include "sio/sio.h"
#include "interrupt/interrupt.h"

/* Helper: create a fully wired GBA on the heap and return it.
 * The caller owns the pointer (but these are short-lived tests,
 * so we don't bother freeing). */
static GBA* make_gba(void) {
    GBA* gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    return gba;
}

/* ---- EWRAM -------------------------------------------------------- */

TEST(ewram_write_read) {
    GBA* gba = make_gba();
    bus_write32(&gba->bus, 0x02000000, 0xDEADBEEF);
    uint32_t val = bus_read32(&gba->bus, 0x02000000);
    ASSERT_EQ_HEX(val, 0xDEADBEEF);
}

TEST(ewram_mirror) {
    GBA* gba = make_gba();
    bus_write32(&gba->bus, 0x02000004, 0xCAFEBABE);
    /* EWRAM mirrors every 256KB: 0x02040004 wraps to 0x02000004 */
    uint32_t val = bus_read32(&gba->bus, 0x02040004);
    ASSERT_EQ_HEX(val, 0xCAFEBABE);
}

/* ---- IWRAM -------------------------------------------------------- */

TEST(iwram_write_read) {
    GBA* gba = make_gba();
    bus_write32(&gba->bus, 0x03000000, 0x12345678);
    uint32_t val = bus_read32(&gba->bus, 0x03000000);
    ASSERT_EQ_HEX(val, 0x12345678);
}

/* ---- Palette RAM: 8-bit writes duplicate the byte ----------------- */

TEST(palette_8bit_duplicate) {
    GBA* gba = make_gba();
    bus_write8(&gba->bus, 0x05000000, 0xAB);
    uint16_t val = bus_read16(&gba->bus, 0x05000000);
    ASSERT_EQ_HEX(val, 0xABAB);
}

/* ---- VRAM: 8-bit writes duplicate the byte ------------------------ */

TEST(vram_8bit_duplicate) {
    GBA* gba = make_gba();
    bus_write8(&gba->bus, 0x06000000, 0xCD);
    uint16_t val = bus_read16(&gba->bus, 0x06000000);
    ASSERT_EQ_HEX(val, 0xCDCD);
}

/* ---- OAM: 8-bit writes are ignored ------------------------------- */

TEST(oam_8bit_ignored) {
    GBA* gba = make_gba();
    /* OAM starts zeroed after gba_init (memset) */
    bus_write8(&gba->bus, 0x07000000, 0xFF);
    uint8_t val = bus_read8(&gba->bus, 0x07000000);
    ASSERT_EQ_HEX(val, 0x00);
}

/* ---- VRAM mirroring: 96KB wraps in 128KB space -------------------- */

TEST(vram_mirror) {
    GBA* gba = make_gba();
    /* VRAM is 96KB (0x00000-0x17FFF) in a 128KB address window.
     * Addresses 0x06018000-0x0601FFFF mirror back to 0x06010000-0x06017FFF
     * (offset -= 0x8000 when offset >= 0x18000). */
    bus_write16(&gba->bus, 0x06010000, 0xBEEF);
    uint16_t val = bus_read16(&gba->bus, 0x06018000);
    ASSERT_EQ_HEX(val, 0xBEEF);
}

/* ---- SIO dispatch: bus routes 0x120-0x12B and 0x134-0x135 to SIO module --- */

TEST(bus_dispatches_siocnt_writes_to_sio) {
    Bus* bus = calloc(1, sizeof(Bus));
    SIO sio;
    InterruptController ic;
    bus_init(bus);
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);
    bus->sio = &sio;

    bus_write16(bus, 0x04000128, 0x4082);
    ASSERT_EQ_HEX(sio.siocnt, 0x4082);
    ASSERT_EQ_HEX(bus_read16(bus, 0x04000128), 0x4082);
    free(bus);
}

TEST(bus_dispatches_rcnt_writes_to_sio) {
    Bus* bus = calloc(1, sizeof(Bus));
    SIO sio;
    InterruptController ic;
    bus_init(bus);
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);
    bus->sio = &sio;

    bus_write16(bus, 0x04000134, 0x8000);
    ASSERT_EQ_HEX(sio.rcnt, 0x8000);
    ASSERT_EQ_HEX(bus_read16(bus, 0x04000134), 0x8000);
    free(bus);
}

/* ---- WAITCNT-driven memory access timing ----
 *
 * The bus charges (table_value - 1) cycles into pending_cycles per access,
 * matching GBATEK's per-region timing on top of the implicit 1-cycle baseline
 * already counted by instruction handlers. These tests use a bare Bus to
 * isolate timing math from full-system wiring. */

static void prime(Bus* bus) {
    /* A few accesses to known-quiet regions to set last_access_addr without
     * polluting downstream measurements. Then drain. */
    (void)bus_read32(bus, 0x03000000);
    (void)bus_drain_pending(bus);
}

TEST(waitcnt_default_rom_n_is_4) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WAITCNT=0 → WS0 N=4 cycles. ROM region 0x08000000.
     * After init, last_access_addr=0 so this is non-sequential. */
    (void)bus_read16(bus, 0x08000000);
    /* Charged extras = 4 - 1 = 3 */
    ASSERT_EQ(bus_drain_pending(bus), 3);
    free(bus);
}

TEST(waitcnt_default_rom_32bit_n_plus_s) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WAITCNT=0 → WS0 N=4, S=2. 32-bit access non-seq = 4 + 2 = 6 cycles.
     * Charged extras = 6 - 1 = 5 */
    (void)bus_read32(bus, 0x08000000);
    ASSERT_EQ(bus_drain_pending(bus), 5);
    free(bus);
}

TEST(waitcnt_sequential_rom_uses_s) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* First access primes last_access_addr; second is sequential. */
    (void)bus_read16(bus, 0x08000000);
    (void)bus_drain_pending(bus);
    (void)bus_read16(bus, 0x08000002);
    /* WS0 S=2, extras = 2 - 1 = 1 */
    ASSERT_EQ(bus_drain_pending(bus), 1);
    free(bus);
}

TEST(waitcnt_write_updates_parsed_state) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WAITCNT=0x4014: WS0 N=2(0->4...wait let me work this out)
     *   bits 0-1   SRAM N: 00 -> 4
     *   bits 2-3   WS0 N : 01 -> 3
     *   bit  4     WS0 S : 1  -> 1
     *   bits 5-6   WS1 N : 00 -> 4
     *   bit  7     WS1 S : 0  -> 4
     *   bits 8-9   WS2 N : 00 -> 4
     *   bit  10    WS2 S : 1  -> 1
     *   bit  14    prefetch: 1
     * Encoded: bit2-3=01 -> 0x04, bit4=1 -> 0x10, bit10=1 -> 0x400,
     *          bit14=1 -> 0x4000 → 0x4414. */
    bus_write16(bus, 0x04000204, 0x4414);
    ASSERT_EQ(bus->wait_state.sram_n, 4);
    ASSERT_EQ(bus->wait_state.ws0_n, 3);
    ASSERT_EQ(bus->wait_state.ws0_s, 1);
    ASSERT_EQ(bus->wait_state.ws1_n, 4);
    ASSERT_EQ(bus->wait_state.ws1_s, 4);
    ASSERT_EQ(bus->wait_state.ws2_n, 4);
    ASSERT_EQ(bus->wait_state.ws2_s, 1);
    free(bus);
}

TEST(waitcnt_fast_rom_after_reconfigure) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* Configure WS0 N=3 (bits 2-3=10), S=1 (bit 4=1). Encoded: 0x18. */
    bus_write16(bus, 0x04000204, 0x0018);
    bus_drain_pending(bus); /* drop the I/O write's own charge */
    (void)bus_read16(bus, 0x08000000);
    /* N=2, extras = 2 - 1 = 1 */
    ASSERT_EQ(bus_drain_pending(bus), 1);
    free(bus);
}

TEST(waitcnt_iwram_is_one_cycle) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    prime(bus);
    (void)bus_read32(bus, 0x03000100);
    /* IWRAM is 1 cycle regardless of size → extras = 0 */
    ASSERT_EQ(bus_drain_pending(bus), 0);
    free(bus);
}

TEST(waitcnt_ewram_charges_extras) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    prime(bus);
    /* EWRAM 16-bit: 3 cycles → extras = 2 */
    (void)bus_read16(bus, 0x02000000);
    ASSERT_EQ(bus_drain_pending(bus), 2);
    /* EWRAM 32-bit: 6 cycles → extras = 5 */
    (void)bus_read32(bus, 0x02000010);
    ASSERT_EQ(bus_drain_pending(bus), 5);
    free(bus);
}

TEST(waitcnt_sram_is_n_only) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    prime(bus);
    /* SRAM N=4 (default). 8-bit bus, no S timing. Extras = 3. */
    (void)bus_read8(bus, 0x0E000000);
    ASSERT_EQ(bus_drain_pending(bus), 3);
    /* Sequential access still costs N (no S table for SRAM). */
    (void)bus_read8(bus, 0x0E000001);
    ASSERT_EQ(bus_drain_pending(bus), 3);
    free(bus);
}

void run_bus_tests(void) {
    TEST_SUITE("bus");
    RUN_TEST(ewram_write_read);
    RUN_TEST(ewram_mirror);
    RUN_TEST(iwram_write_read);
    RUN_TEST(palette_8bit_duplicate);
    RUN_TEST(vram_8bit_duplicate);
    RUN_TEST(oam_8bit_ignored);
    RUN_TEST(vram_mirror);
    RUN_TEST(bus_dispatches_siocnt_writes_to_sio);
    RUN_TEST(bus_dispatches_rcnt_writes_to_sio);
    RUN_TEST(waitcnt_default_rom_n_is_4);
    RUN_TEST(waitcnt_default_rom_32bit_n_plus_s);
    RUN_TEST(waitcnt_sequential_rom_uses_s);
    RUN_TEST(waitcnt_write_updates_parsed_state);
    RUN_TEST(waitcnt_fast_rom_after_reconfigure);
    RUN_TEST(waitcnt_iwram_is_one_cycle);
    RUN_TEST(waitcnt_ewram_charges_extras);
    RUN_TEST(waitcnt_sram_is_n_only);
}
