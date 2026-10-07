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

bool input_irq_condition(const InputState* input) {
    if (!BIT(input->keycnt, 14)) return false;

    uint16_t selected = input->keycnt & 0x03FF;
    uint16_t pressed = (uint16_t)(~input->keyinput & 0x03FF);

    // AND mode with no keys selected would be vacuously true; never fire.
    if (selected == 0) return false;
    if (BIT(input->keycnt, 15)) return (pressed & selected) == selected;
    return (pressed & selected) != 0;
}
