#include "test_harness.h"
#include "gba.h"
#include "apu/apu.h"
#include <stdlib.h>
#include <string.h>

static GBA* make_gba(void) {
    GBA* gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    return gba;
}

TEST(fifo_full_write_drops_incoming_word) {
    /* GBATEK/mGBA: writing to a full FIFO drops the incoming data.  It
     * must NOT reset the queue — that would throw away ~32 queued
     * samples and cause an audible dropout. */
    GBA* gba = make_gba();
    APU* apu = &gba->apu;

    /* Fill FIFO A: 8 words x 4 bytes = 32 bytes (exactly full). */
    for (uint32_t w = 0; w < 8; w++) {
        apu_fifo_write(apu, 0, 0x01010101u * (w + 1));
    }
    ASSERT_EQ(apu->fifo_a.count, FIFO_SIZE);

    /* One more write while full: dropped, queue intact. */
    apu_fifo_write(apu, 0, 0xDEADBEEF);
    ASSERT_EQ(apu->fifo_a.count, FIFO_SIZE);

    /* First byte out must be from the FIRST word written (0x01),
     * proving the queue was not reset. */
    ASSERT_EQ(apu_fifo_pop(apu, 0), 0x01);

    free(gba);
}

TEST(apu_master_disable_still_emits_silence) {
    /* With master enable off the APU must still produce (silent)
     * samples at the normal rate — otherwise the SDL audio queue
     * starves and underruns when a game toggles SOUNDCNT_X. */
    GBA* gba = make_gba();
    APU* apu = &gba->apu;

    apu->soundcnt_x = 0; /* master off */
    uint32_t before = apu->write_pos;
    apu_tick(apu, (int)(apu->sample_period * 8));

    uint32_t produced = (apu->write_pos - before + SAMPLE_BUFFER_SIZE)
                      % SAMPLE_BUFFER_SIZE;
    ASSERT_TRUE(produced >= 8);
    for (uint32_t i = 0; i < produced * 2; i++) {
        ASSERT_EQ(apu->sample_buffer[(before * 2 + i)
                  % (SAMPLE_BUFFER_SIZE * 2)], 0);
    }

    free(gba);
}

TEST(square_retrigger_reloads_envelope_volume) {
    /* Hardware reloads the envelope's initial volume (NRx2 bits 4-7) on
     * every trigger.  A decayed channel must come back at full volume
     * when the game retriggers the note. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;
    APU* apu = &gba->apu;

    apu->soundcnt_x = 0x80; /* master on so register writes land */

    /* Ch1 envelope: initial volume 15, decreasing, period 1. */
    bus_write8(bus, 0x04000063, 0xF1);
    /* Trigger. */
    bus_write8(bus, 0x04000065, 0x80);
    ASSERT_EQ(apu->ch1.volume, 15);

    /* Let the envelope decay a few steps. */
    for (int i = 0; i < 5; i++) {
        square_channel_envelope(&apu->ch1);
    }
    ASSERT_TRUE(apu->ch1.volume < 15);

    /* Retrigger WITHOUT rewriting the envelope register: volume must
     * come back to the initial 15. */
    bus_write8(bus, 0x04000065, 0x80);
    ASSERT_EQ(apu->ch1.volume, 15);

    free(gba);
}

void run_apu_tests(void) {
    TEST_SUITE("apu");
    RUN_TEST(fifo_full_write_drops_incoming_word);
    RUN_TEST(apu_master_disable_still_emits_silence);
    RUN_TEST(square_retrigger_reloads_envelope_volume);
}
