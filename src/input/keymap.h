#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

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

/* Reset the lookup table to the project's default keyboard bindings. */
void keymap_reset_defaults(void);

/* Replace the lookup table with the bindings parsed from a file.
 * Lines that fail to parse are logged and skipped; the rest still apply.
 * Returns true if the file was opened (parse errors do not fail this). */
bool keymap_load(const char* path);

/* Look up the GBA button mask for an SDL scancode. Returns 0 if unbound. */
uint16_t keymap_lookup(SDL_Scancode sc);

/* ---- Internal helpers, exposed for unit testing. SDL-free. ---- */

/* Resolve a GBA button name to its KEY_* mask. Returns 0 for unknown names.
 * Case-insensitive. Accepts: A B START SELECT UP DOWN LEFT RIGHT L R. */
uint16_t keymap_button_from_name(const char* name);

/* Parse one line of the form "BUTTON=KEY" (with optional surrounding
 * whitespace and trailing comments). On success, writes button and key
 * substrings into the provided buffers (each null-terminated) and returns
 * true. Returns false for blank/comment-only lines without setting the
 * outputs. The input line is mutated. */
bool keymap_parse_line(char* line, char** button_out, char** key_out);

#endif // KEYMAP_H
