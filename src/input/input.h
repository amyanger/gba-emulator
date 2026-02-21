#ifndef INPUT_H
#define INPUT_H

#include "common.h"

// Button bit positions (active LOW: 0=pressed, 1=released)
#define KEY_A      (1 << 0)
#define KEY_B      (1 << 1)
#define KEY_SELECT (1 << 2)
#define KEY_START  (1 << 3)
#define KEY_RIGHT  (1 << 4)
#define KEY_LEFT   (1 << 5)
#define KEY_UP     (1 << 6)
#define KEY_DOWN   (1 << 7)
#define KEY_R      (1 << 8)
#define KEY_L      (1 << 9)

struct InputState {
    uint16_t keyinput; // 0x04000130 — active LOW
    uint16_t keycnt;   // 0x04000132 — interrupt control
};
typedef struct InputState InputState;

void input_init(InputState* input);
void input_press(InputState* input, uint16_t key);
void input_release(InputState* input, uint16_t key);

#endif // INPUT_H
