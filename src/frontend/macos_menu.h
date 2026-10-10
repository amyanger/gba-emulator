#ifndef MACOS_MENU_H
#define MACOS_MENU_H

#include <stdint.h>

/* Enables the app menu's "Settings… ⌘," item (SDL creates it without an
 * action) so it posts an SDL event of the given type. No-op off macOS. */
#ifdef __APPLE__
void macos_menu_install_settings(uint32_t sdl_event_type);
#else
static inline void macos_menu_install_settings(uint32_t sdl_event_type) { (void)sdl_event_type; }
#endif

#endif // MACOS_MENU_H
