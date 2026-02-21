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
