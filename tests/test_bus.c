#include "test_harness.h"
#include "gba.h"
#include "sio/sio.h"
#include "interrupt/interrupt.h"
#include "memory/io_regs.h"

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

/* ---- VRAM: 8-bit writes to OBJ VRAM are ignored -------------------
 *
 * GBATEK: like OAM, 8-bit writes to OBJ tile VRAM are dropped.  The
 * byte-duplication rule only applies to BG VRAM.  The OBJ region starts
 * at offset 0x10000 in tile modes (0-2) and 0x14000 in bitmap modes. */

TEST(vram_8bit_obj_region_ignored_tile_mode) {
    GBA* gba = make_gba();
    gba->ppu.dispcnt = 0; /* mode 0: OBJ VRAM = 0x10000..0x17FFF */
    bus_write8(&gba->bus, 0x06010000, 0xCD);
    ASSERT_EQ_HEX(bus_read16(&gba->bus, 0x06010000), 0x0000);
}

TEST(vram_8bit_obj_region_bitmap_mode_boundary) {
    GBA* gba = make_gba();
    gba->ppu.dispcnt = 3; /* mode 3: BG data extends to 0x13FFF */

    /* Below 0x14000: still BG data, byte gets duplicated. */
    bus_write8(&gba->bus, 0x06012000, 0xCD);
    ASSERT_EQ_HEX(bus_read16(&gba->bus, 0x06012000), 0xCDCD);

    /* At/above 0x14000: OBJ VRAM, write ignored. */
    bus_write8(&gba->bus, 0x06014000, 0xEE);
    ASSERT_EQ_HEX(bus_read16(&gba->bus, 0x06014000), 0x0000);
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
 * The bus charges (total - 1) cycles into pending_cycles per access, on top
 * of the implicit 1-cycle baseline already counted by instruction handlers.
 * These tests use a bare Bus to isolate timing math from full-system wiring.
 *
 * GBATEK "GBA System Control", WAITCNT: the 4,3,2,8 / 2,1 / 4,1 / 8,1 values
 * are waitstates, and "the actual access time is 1 clock cycle PLUS the
 * number of waitstates". GBATEK "GBA Memory Map" lists the default totals:
 * GamePak ROM 5/5/8 cycles (8/16/32-bit), GamePak SRAM 5. A 32-bit ROM
 * access is two 16-bit accesses, the second always sequential (N+S). */

static void prime(Bus* bus) {
    /* A few accesses to known-quiet regions to set last_access_addr without
     * polluting downstream measurements. Then drain. */
    (void)bus_read32(bus, 0x03000000);
    (void)bus_drain_pending(bus);
}

TEST(waitcnt_default_rom_n_is_5) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WAITCNT=0 -> WS0 N = 1 + 4 waits = 5 cycles. ROM region 0x08000000.
     * After init, last_access_addr=0 so this is non-sequential. */
    (void)bus_read16(bus, 0x08000000);
    /* Charged extras = 5 - 1 = 4 */
    ASSERT_EQ(bus_drain_pending(bus), 4);
    free(bus);
}

TEST(waitcnt_default_rom_32bit_n_plus_s) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WAITCNT=0 -> WS0 N=5, S=3. 32-bit non-seq = 5 + 3 = 8 cycles, matching
     * GBATEK's "GamePak ROM 5/5/8". Charged extras = 8 - 1 = 7 */
    (void)bus_read32(bus, 0x08000000);
    ASSERT_EQ(bus_drain_pending(bus), 7);
    free(bus);
}

TEST(waitcnt_default_rom_32bit_sequential_s_plus_s) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    (void)bus_read32(bus, 0x08000000);
    (void)bus_drain_pending(bus);
    /* Sequential 32-bit, prefetch off: S+S = 3 + 3 = 6, extras = 5 */
    (void)bus_read32(bus, 0x08000004);
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
    /* WS0 S = 1 + 2 waits = 3, extras = 3 - 1 = 2 */
    ASSERT_EQ(bus_drain_pending(bus), 2);
    free(bus);
}

TEST(waitcnt_write_updates_parsed_state) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WAITCNT=0x4414 (fields hold totals = 1 + waitstates):
     *   bits 0-1   SRAM N: 00 -> 4 waits -> 5
     *   bits 2-3   WS0 N : 01 -> 3 waits -> 4
     *   bit  4     WS0 S : 1  -> 1 wait  -> 2
     *   bits 5-6   WS1 N : 00 -> 4 waits -> 5
     *   bit  7     WS1 S : 0  -> 4 waits -> 5
     *   bits 8-9   WS2 N : 00 -> 4 waits -> 5
     *   bit  10    WS2 S : 1  -> 1 wait  -> 2
     *   bit  14    prefetch: 1 */
    bus_write16(bus, 0x04000204, 0x4414);
    ASSERT_EQ(bus->wait_state.sram_n, 5);
    ASSERT_EQ(bus->wait_state.ws0_n, 4);
    ASSERT_EQ(bus->wait_state.ws0_s, 2);
    ASSERT_EQ(bus->wait_state.ws1_n, 5);
    ASSERT_EQ(bus->wait_state.ws1_s, 5);
    ASSERT_EQ(bus->wait_state.ws2_n, 5);
    ASSERT_EQ(bus->wait_state.ws2_s, 2);
    ASSERT_EQ(bus->wait_state.prefetch_enabled, true);
    free(bus);
}

TEST(waitcnt_fast_rom_after_reconfigure) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* WS0 N=2 waits (bits 2-3=10), S=1 wait (bit 4=1). Encoded: 0x18. */
    bus_write16(bus, 0x04000204, 0x0018);
    bus_drain_pending(bus); /* drop the I/O write's own charge */
    (void)bus_read16(bus, 0x08000000);
    /* N = 1 + 2 = 3, extras = 2 */
    ASSERT_EQ(bus_drain_pending(bus), 2);
    /* Sequential: S = 1 + 1 = 2, extras = 1 */
    (void)bus_read16(bus, 0x08000002);
    ASSERT_EQ(bus_drain_pending(bus), 1);
    free(bus);
}

TEST(waitcnt_cart_setting_4317) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* GBATEK: typical carts use WAITCNT=4317h: "WS0/ROM=3,1 clks; SRAM=8
     * clks; WS2/EEPROM: 8,8 clks; prefetch enabled" (all waitstates).
     * Write the low bits only (prefetch off) so S timing is visible. */
    bus_write16(bus, 0x04000204, 0x0317);
    bus_drain_pending(bus);
    ASSERT_EQ(bus->wait_state.sram_n, 9);
    ASSERT_EQ(bus->wait_state.ws0_n, 4);
    ASSERT_EQ(bus->wait_state.ws0_s, 2);
    ASSERT_EQ(bus->wait_state.ws2_n, 9);
    ASSERT_EQ(bus->wait_state.ws2_s, 9);
    /* WS0 32-bit non-seq: N+S = 4 + 2 = 6, extras = 5 */
    (void)bus_read32(bus, 0x08000000);
    ASSERT_EQ(bus_drain_pending(bus), 5);
    /* SRAM: 1 + 8 waits = 9, extras = 8 */
    (void)bus_read8(bus, 0x0E000000);
    ASSERT_EQ(bus_drain_pending(bus), 8);
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

TEST(waitcnt_prefetch_keeps_sequential_rom_at_s) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* Enable prefetch (bit 14) on top of default WAITCNT=0. */
    bus_write16(bus, 0x04000204, 0x4000);
    bus_drain_pending(bus); /* drop the I/O write's own charge */

    /* First ROM access is non-sequential, pays full N=5. Extras=4. */
    (void)bus_read16(bus, 0x08000000);
    ASSERT_EQ(bus_drain_pending(bus), 4);

    /* Prefetch doesn't make sequential reads cheaper on its own; the
     * savings come from bus_prefetch_stall. Still S=3, extras=2. */
    (void)bus_read16(bus, 0x08000002);
    ASSERT_EQ(bus_drain_pending(bus), 2);
    (void)bus_read16(bus, 0x08000004);
    ASSERT_EQ(bus_drain_pending(bus), 2);
    free(bus);
}

TEST(waitcnt_prefetch_does_not_affect_non_sequential) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    bus_write16(bus, 0x04000204, 0x4000); /* prefetch on, default N/S */
    bus_drain_pending(bus);

    /* First non-seq access: full N=5, extras=4. */
    (void)bus_read16(bus, 0x08000000);
    ASSERT_EQ(bus_drain_pending(bus), 4);

    /* Jump to a different ROM address: non-sequential, still full N. */
    (void)bus_read16(bus, 0x08001000);
    ASSERT_EQ(bus_drain_pending(bus), 4);
    free(bus);
}

TEST(waitcnt_prefetch_32bit_sequential_keeps_two_s) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    bus_write16(bus, 0x04000204, 0x4000);
    bus_drain_pending(bus);

    /* Non-seq 32-bit with prefetch on: N+S = 5 + 3 = 8, extras=7. */
    (void)bus_read32(bus, 0x08000000);
    ASSERT_EQ(bus_drain_pending(bus), 7);

    /* 32-bit sequential ROM with prefetch: still S+S = 6, extras=5. */
    (void)bus_read32(bus, 0x08000004);
    ASSERT_EQ(bus_drain_pending(bus), 5);
    free(bus);
}

/* Prefetch refunds: a Thumb instruction running from ROM loads from IWRAM
 * (1 cycle + 1 I cycle). While the CPU is off the cart bus, the buffer
 * fills, refunding upcoming S waits and the N penalty on the next fetch. */
static Bus* prefetch_bus(uint16_t waitcnt) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    bus_write16(bus, 0x04000204, waitcnt);
    bus->data_wait = 0;
    bus->data_read = false;
    bus_drain_pending(bus);
    return bus;
}

TEST(prefetch_refunds_during_non_rom_load) {
    Bus* bus = prefetch_bus(0x4000); /* WS0 N=4, S=2 waitstates */
    (void)bus_read16(bus, 0x03000000);
    bus_prefetch_stall(bus, 0x08000104, true);
    /* wait=2, stall=S+1=3: refund 3-2-(4-2)-3 = -4 */
    ASSERT_EQ(bus_drain_pending(bus), -4);
    free(bus);
}

TEST(prefetch_buffer_hit_still_costs_a_cycle) {
    Bus* bus = prefetch_bus(0x4010); /* WS0 N=4, S=1 waitstates */
    (void)bus_read16(bus, 0x03000000);
    bus_prefetch_stall(bus, 0x08000104, true);
    /* mGBA's formula gives -5 (a free fetch); the floor keeps it at -4. */
    ASSERT_EQ(bus_drain_pending(bus), -4);
    free(bus);
}

TEST(prefetch_no_refund_for_rom_data_or_when_disabled) {
    Bus* bus = prefetch_bus(0x4000);
    (void)bus_read16(bus, 0x08001000); /* ROM data access */
    bus_drain_pending(bus);
    bus_prefetch_stall(bus, 0x08000104, true);
    ASSERT_EQ(bus_drain_pending(bus), 0);
    free(bus);

    bus = prefetch_bus(0x0000); /* prefetch off */
    (void)bus_read16(bus, 0x03000000);
    bus_prefetch_stall(bus, 0x08000104, true);
    ASSERT_EQ(bus_drain_pending(bus), 0);
    free(bus);
}

TEST(prefetch_ignores_opcode_fetches) {
    Bus* bus = prefetch_bus(0x4000);
    (void)bus_fetch16(bus, 0x03000000);
    ASSERT_EQ(bus->data_wait, 0);
    (void)bus_read16(bus, 0x03000000);
    ASSERT_EQ(bus->data_wait, 1);
    free(bus);
}

TEST(waitcnt_prefetch_off_keeps_s_timing) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* Default WAITCNT=0 has prefetch off, S=3 for WS0. */
    ASSERT_EQ(bus->wait_state.prefetch_enabled, false);

    (void)bus_read16(bus, 0x08000000);
    bus_drain_pending(bus);
    /* Sequential ROM still pays S=3, extras=2. */
    (void)bus_read16(bus, 0x08000002);
    ASSERT_EQ(bus_drain_pending(bus), 2);
    free(bus);
}

TEST(open_bus_returns_latched_word_byte_at_offset) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    /* Simulate a pipeline fetch having latched a 32-bit instruction. */
    bus->open_bus = 0x12345678;
    /* Reads from unmapped region (>= 0x10000000) should split the latched
     * word by byte offset, not return the low byte for every byte. */
    ASSERT_EQ_HEX(bus_read8(bus, 0x10000000), 0x78);
    ASSERT_EQ_HEX(bus_read8(bus, 0x10000001), 0x56);
    ASSERT_EQ_HEX(bus_read8(bus, 0x10000002), 0x34);
    ASSERT_EQ_HEX(bus_read8(bus, 0x10000003), 0x12);
    /* And a 32-bit unmapped read returns the whole latched word. */
    ASSERT_EQ_HEX(bus_read32(bus, 0x10000000), 0x12345678);
    free(bus);
}

TEST(waitcnt_sram_is_n_only) {
    Bus* bus = calloc(1, sizeof(Bus));
    bus_init(bus);
    prime(bus);
    /* SRAM default: 1 + 4 waits = 5 (GBATEK memory map "GamePak SRAM 5").
     * 8-bit bus, no S timing. Extras = 4. */
    (void)bus_read8(bus, 0x0E000000);
    ASSERT_EQ(bus_drain_pending(bus), 4);
    /* Sequential access still costs N (no S table for SRAM). */
    (void)bus_read8(bus, 0x0E000001);
    ASSERT_EQ(bus_drain_pending(bus), 4);
    free(bus);
}

TEST(sound_master_off_preserves_wave_ram) {
    /* GBATEK: master disable resets 4000060h..4000081h only; wave RAM at
     * 4000090h..400009Fh must survive an off/on cycle. */
    GBA* gba = make_gba();
    bus_write16(&gba->bus, 0x04000084, 0x0080);
    for (uint32_t i = 0; i < 16; i++) {
        bus_write8(&gba->bus, 0x04000090 + i, (uint8_t)(0xA0 + i));
    }
    bus_write16(&gba->bus, 0x04000084, 0x0000);
    bus_write16(&gba->bus, 0x04000084, 0x0080);
    for (uint32_t i = 0; i < 16; i++) {
        ASSERT_EQ_HEX(bus_read8(&gba->bus, 0x04000090 + i), 0xA0 + i);
    }
}

/* ---- Keypad IRQ (KEYCNT 0x04000132, IF bit 12) -------------------- */

TEST(keycnt_write_with_key_held_sets_if_bit12) {
    GBA* gba = make_gba();
    input_press(&gba->input, KEY_A);
    ASSERT_EQ(gba->interrupts.irf & IRQ_KEYPAD, 0);
    bus_write16(&gba->bus, 0x04000132, (1 << 14) | KEY_A);
    ASSERT_EQ_HEX(gba->interrupts.irf & IRQ_KEYPAD, IRQ_KEYPAD);
}

TEST(keycnt_write_without_irq_enable_does_not_fire) {
    GBA* gba = make_gba();
    input_press(&gba->input, KEY_A);
    bus_write16(&gba->bus, 0x04000132, KEY_A);
    ASSERT_EQ(gba->interrupts.irf & IRQ_KEYPAD, 0);
}

TEST(keycnt_halfword_write_checks_full_value) {
    /* KEYCNT was OR mode on A. Rewriting it as AND(A|B) with only A held
     * must not fire from the intermediate low-byte state. */
    GBA* gba = make_gba();
    bus_write16(&gba->bus, 0x04000132, (1 << 14) | KEY_A);
    input_press(&gba->input, KEY_A);
    bus_write16(&gba->bus, 0x04000132, (1 << 15) | (1 << 14) | KEY_A | KEY_B);
    ASSERT_EQ(gba->interrupts.irf & IRQ_KEYPAD, 0);
}

TEST(keypad_irq_sampled_each_frame) {
    GBA* gba = make_gba();
    bus_write16(&gba->bus, 0x04000132, (1 << 14) | KEY_START);
    gba_run_frame(gba);
    ASSERT_EQ(gba->interrupts.irf & IRQ_KEYPAD, 0);

    input_press(&gba->input, KEY_START);
    gba_run_frame(gba);
    ASSERT_EQ_HEX(gba->interrupts.irf & IRQ_KEYPAD, IRQ_KEYPAD);

    /* Level-sampled: acknowledged while still held, it fires again. */
    bus_write16(&gba->bus, 0x04000202, IRQ_KEYPAD);
    ASSERT_EQ(gba->interrupts.irf & IRQ_KEYPAD, 0);
    gba_run_frame(gba);
    ASSERT_EQ_HEX(gba->interrupts.irf & IRQ_KEYPAD, IRQ_KEYPAD);

    /* Released: no new request after acknowledge. */
    input_release(&gba->input, KEY_START);
    bus_write16(&gba->bus, 0x04000202, IRQ_KEYPAD);
    gba_run_frame(gba);
    ASSERT_EQ(gba->interrupts.irf & IRQ_KEYPAD, 0);
}

/* ---- Timer sync ----------------------------------------------------- */

TEST(timer_enabled_mid_chunk_counts_only_cycles_after_the_write) {
    /* A timer switched on 500 cycles into a chunk must not be credited
     * with those 500 cycles when the chunk is ticked. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;
    gba->cpu.cycles_executed = 500;
    bus_write16(bus, REG_TM0CNT_L, 0);
    bus->pending_cycles = 0;
    bus_write16(bus, REG_TM0CNT_H, 0x80); /* enable, prescaler 1 */

    gba->cpu.halted = true; /* burn the chunk without running code */
    gba_run_cycles(gba, 1006);
    ASSERT_EQ(gba->timers[0].counter, 506);
}

TEST(timer_read_sees_counter_two_cycles_back) {
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;
    gba->cpu.cycles_executed = 100;
    bus_write16(bus, REG_TM0CNT_L, 0);
    bus->pending_cycles = 0;
    bus_write16(bus, REG_TM0CNT_H, 0x80);

    bus->pending_cycles = 0;
    gba->cpu.cycles_executed = 120;
    ASSERT_EQ(bus_read16(bus, REG_TM0CNT_L), 18);
}

TEST(timer_prescaler_drop_while_running_does_not_stall_slices) {
    /* Switching a running IRQ timer from prescaler 1024 to 1 keeps the
     * partial prescaler count, which then exceeds the new prescaler.
     * gba_run_cycles must still make progress instead of looping on an
     * empty slice. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;
    /* 256 ticks from overflow, fewer than the 1000 leftover cycles. */
    bus_write16(bus, REG_TM0CNT_L, 0xFF00);
    bus_write16(bus, REG_TM0CNT_H, 0xC3); /* enable, IRQ, prescaler 1024 */
    gba->cpu.halted = true;
    gba_run_cycles(gba, 1000);
    ASSERT_EQ(gba->timers[0].prescaler_counter, 1000);

    bus_write16(bus, REG_TM0CNT_H, 0xC0); /* same, prescaler 1 */
    gba_run_cycles(gba, 1006);
    ASSERT_TRUE(gba->timers[0].prescaler_counter < gba->timers[0].prescaler);
}

TEST(timer_read_right_after_a_sync_still_sees_two_cycles_back) {
    /* A TMxCNT write syncs every timer. A read one cycle later must
     * still see the counter as of 2 cycles back, which is before the
     * sync point. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;
    bus_write16(bus, REG_TM0CNT_L, 0);
    bus_write16(bus, REG_TM0CNT_H, 0x80); /* enable, prescaler 1 */

    bus->pending_cycles = 0;
    gba->cpu.cycles_executed = 50;
    bus_write16(bus, REG_TM1CNT_H, 0);    /* sync point at 50 */
    ASSERT_EQ(gba->timers[0].counter, 50);

    bus->pending_cycles = 0;
    gba->cpu.cycles_executed = 51;
    ASSERT_EQ(bus_read16(bus, REG_TM0CNT_L), 49);
}

TEST(timer_irq_enabled_mid_slice_is_taken_on_time) {
    /* An IRQ timer switched on partway into a slice must end the slice
     * so the overflow is raised when it happens, not at slice end. */
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;

    bus_write32(&gba->bus, 0x03000000, 0xE5810000); /* str r0, [r1] */
    bus_write32(&gba->bus, 0x03000004, 0xEAFFFFFE); /* b . */
    bus_write32(&gba->bus, 0x03000100, 0xEAFFFFFE); /* IRQ handler: b . */
    bus_write32(&gba->bus, 0x03007FFC, 0x03000100);

    cpu_switch_mode(cpu, CPU_MODE_SYS);
    cpu->cpsr = CPU_MODE_SYS; /* ARM, IRQs unmasked */
    cpu->regs[0] = 0x00C0FFF0;  /* reload FFF0; enable, IRQ, prescaler 1 */
    cpu->regs[1] = REG_TM0CNT_L;
    cpu->regs[REG_PC] = 0x03000000;
    cpu->pipeline_valid = false;
    gba->interrupts.ie = IRQ_TIMER0;
    gba->interrupts.ime = true;

    /* The overflow lands about 16 cycles in, well inside a 60 cycle run. */
    gba_run_cycles(gba, 60);
    ASSERT_EQ_HEX(gba->interrupts.irf & IRQ_TIMER0, IRQ_TIMER0);
    ASSERT_EQ_HEX(cpu_get_mode(cpu), CPU_MODE_IRQ);
}

TEST(tm0_ignores_count_up_bit) {
    /* GBATEK: the count-up bit is unused in TM0CNT_H, so TM0 still counts
     * with its prescaler. mGBA keeps the bit for readback. */
    GBA* gba = make_gba();
    bus_write16(&gba->bus, REG_TM0CNT_H, 0x0084); /* enable + count-up */
    ASSERT_EQ_HEX(bus_read16(&gba->bus, REG_TM0CNT_H), 0x0084);
    timer_tick(gba->timers, 10, &gba->interrupts, &gba->apu);
    ASSERT_EQ(gba->timers[0].counter, 10);
    ASSERT_EQ(timer_cycles_until_irq(gba->timers), INT32_MAX);
    free(gba);
}

void run_bus_tests(void) {
    TEST_SUITE("bus");
    RUN_TEST(ewram_write_read);
    RUN_TEST(ewram_mirror);
    RUN_TEST(iwram_write_read);
    RUN_TEST(palette_8bit_duplicate);
    RUN_TEST(vram_8bit_duplicate);
    RUN_TEST(vram_8bit_obj_region_ignored_tile_mode);
    RUN_TEST(vram_8bit_obj_region_bitmap_mode_boundary);
    RUN_TEST(oam_8bit_ignored);
    RUN_TEST(vram_mirror);
    RUN_TEST(bus_dispatches_siocnt_writes_to_sio);
    RUN_TEST(bus_dispatches_rcnt_writes_to_sio);
    RUN_TEST(waitcnt_default_rom_n_is_5);
    RUN_TEST(waitcnt_default_rom_32bit_n_plus_s);
    RUN_TEST(waitcnt_default_rom_32bit_sequential_s_plus_s);
    RUN_TEST(waitcnt_sequential_rom_uses_s);
    RUN_TEST(waitcnt_write_updates_parsed_state);
    RUN_TEST(waitcnt_fast_rom_after_reconfigure);
    RUN_TEST(waitcnt_cart_setting_4317);
    RUN_TEST(waitcnt_iwram_is_one_cycle);
    RUN_TEST(waitcnt_ewram_charges_extras);
    RUN_TEST(waitcnt_sram_is_n_only);
    RUN_TEST(waitcnt_prefetch_keeps_sequential_rom_at_s);
    RUN_TEST(waitcnt_prefetch_does_not_affect_non_sequential);
    RUN_TEST(waitcnt_prefetch_32bit_sequential_keeps_two_s);
    RUN_TEST(prefetch_refunds_during_non_rom_load);
    RUN_TEST(prefetch_buffer_hit_still_costs_a_cycle);
    RUN_TEST(prefetch_no_refund_for_rom_data_or_when_disabled);
    RUN_TEST(prefetch_ignores_opcode_fetches);
    RUN_TEST(waitcnt_prefetch_off_keeps_s_timing);
    RUN_TEST(open_bus_returns_latched_word_byte_at_offset);
    RUN_TEST(sound_master_off_preserves_wave_ram);
    RUN_TEST(keycnt_write_with_key_held_sets_if_bit12);
    RUN_TEST(keycnt_write_without_irq_enable_does_not_fire);
    RUN_TEST(keycnt_halfword_write_checks_full_value);
    RUN_TEST(keypad_irq_sampled_each_frame);
    RUN_TEST(timer_enabled_mid_chunk_counts_only_cycles_after_the_write);
    RUN_TEST(timer_read_sees_counter_two_cycles_back);
    RUN_TEST(timer_prescaler_drop_while_running_does_not_stall_slices);
    RUN_TEST(timer_read_right_after_a_sync_still_sees_two_cycles_back);
    RUN_TEST(timer_irq_enabled_mid_slice_is_taken_on_time);
    RUN_TEST(tm0_ignores_count_up_bit);
}
