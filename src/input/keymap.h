#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_scancode.h>

/* Keymap module — translates SDL scancodes into GBA button bits (KEY_*).
 *
 * The lookup table is initialized to the project default mappings (Z=A,
 * X=B, Enter=START, etc.). Calling keymap_load() replaces the table with
 * the contents of a config file:
 *
 *   # Comments with #
 *   A=Z
 *   START=Return
 *   UP=Up
 *   L=A
 *
 * Button names: A B START SELECT UP DOWN LEFT RIGHT L R (case-insensitive).
 * Key names: any SDL scancode name (case-insensitive — see SDL_GetScancodeFromName).
 */

/* Extra actions beyond the 10 GBA buttons (KEY_* bits 0-9). */
#define KEYMAP_TURBO_A  (1u << 10)
#define KEYMAP_TURBO_B  (1u << 11)
#define KEYMAP_GBA_MASK 0x03FFu
/* Pad inputs: SDL controller buttons, then the two triggers as virtual buttons. */
#define KEYMAP_PAD_LT    ((int32_t)SDL_CONTROLLER_BUTTON_MAX)
#define KEYMAP_PAD_RT    ((int32_t)SDL_CONTROLLER_BUTTON_MAX + 1)
#define KEYMAP_PAD_COUNT ((int32_t)SDL_CONTROLLER_BUTTON_MAX + 2)
#define KEYMAP_PAD_NONE  (-1)

/* Reset the keyboard and controller tables to the project defaults. */
void keymap_reset_defaults(void);

/* Apply the bindings parsed from a file on top of the current tables (callers
 * reset to defaults first). Lines "PAD_<BUTTON>=<padname>" bind controller inputs.
 * Lines that fail to parse are logged and skipped; the rest still apply.
 * Returns true if the file was opened (parse errors do not fail this). */
bool keymap_load(const char* path);

/* Look up the GBA button mask for an SDL scancode. Returns 0 if unbound. */
uint16_t keymap_lookup(SDL_Scancode sc);

/* Action bound to a controller input. 0 if unbound / out of range. */
uint16_t keymap_pad_lookup(int32_t pad);
/* action: one GBA bit or a TURBO bit. A bind is total: it clears the action's
 * previous binding on that device and the target input's previous action. */
void keymap_bind_key(uint16_t action, SDL_Scancode sc);
void keymap_bind_pad(uint16_t action, int32_t pad);
void keymap_unbind(uint16_t action, bool pad);
/* Lowest scancode / pad index bound to the action, or UNKNOWN / KEYMAP_PAD_NONE. */
SDL_Scancode keymap_key_for(uint16_t action);
int32_t keymap_pad_for(uint16_t action);
/* Config-file name of a pad input (SDL string or "lefttrigger"/"righttrigger"); NULL if none. */
const char* keymap_pad_name(int32_t pad);
/* KEYMAP_PAD_NONE if unknown. */
int32_t keymap_pad_from_name(const char* name);
/* True when the key's SDL name is non-empty, has no '#', and reloads to the same scancode. */
bool keymap_key_bindable(SDL_Scancode sc);
/* Write both full tables ("None" marks an action with no binding on a device). Temp file plus rename. */
bool keymap_save(const char* path);

/* ---- Internal helpers, exposed for unit testing. SDL-free. ---- */

/* Resolve a GBA button name to its KEY_* mask. Returns 0 for unknown names.
 * Case-insensitive. Accepts: A B START SELECT UP DOWN LEFT RIGHT L R TURBO_A TURBO_B. */
uint16_t keymap_button_from_name(const char* name);

/* Parse one line of the form "BUTTON=KEY" (with optional surrounding
 * whitespace and trailing comments). On success, writes button and key
 * substrings into the provided buffers (each null-terminated) and returns
 * true. Returns false for blank/comment-only lines without setting the
 * outputs. The input line is mutated. */
bool keymap_parse_line(char* line, char** button_out, char** key_out);

#endif // KEYMAP_H
