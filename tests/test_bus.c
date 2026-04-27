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
}
