#include "input.h"

void input_init(InputState* input) {
    input->keyinput = 0x03FF; // All buttons released (active LOW)
    input->keycnt = 0;
}

void input_press(InputState* input, uint16_t key) {
    input->keyinput &= ~key; // Clear bit = pressed
}

void input_release(InputState* input, uint16_t key) {
    input->keyinput |= key; // Set bit = released
}

void input_set_pressed(InputState* input, uint16_t pressed) {
    input->keyinput = (uint16_t)(~pressed & 0x03FF);
}

// Same bits as KEYMAP_TURBO_A/B in keymap.h (kept local so input.c stays SDL-free).
#define TURBO_A_BIT (1u << 10)
#define TURBO_B_BIT (1u << 11)

uint16_t input_combine(uint16_t kb, uint16_t pad, uint16_t stick, uint16_t turbo_held, uint32_t frame) {
    uint16_t turbo_buttons = 0;
    if ((frame / 2) % 2 == 0) {
        if (turbo_held & TURBO_A_BIT) turbo_buttons |= KEY_A;
        if (turbo_held & TURBO_B_BIT) turbo_buttons |= KEY_B;
    }
    return (uint16_t)((kb | pad | stick | turbo_buttons) & 0x03FF);
}

bool input_irq_condition(const InputState* input) {
    if (!BIT(input->keycnt, 14)) return false;

    uint16_t selected = input->keycnt & 0x03FF;
    uint16_t pressed = (uint16_t)(~input->keyinput & 0x03FF);

    // AND mode with no keys selected would be vacuously true; never fire.
    if (selected == 0) return false;
    if (BIT(input->keycnt, 15)) return (pressed & selected) == selected;
    return (pressed & selected) != 0;
}
