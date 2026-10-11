#ifndef LIBRARY_RUN_H
#define LIBRARY_RUN_H

#include <stdbool.h>
#include <stddef.h>
#include "frontend/frontend.h"
#include "frontend/library.h"

/* library.txt in the app's preferences folder; false when SDL has none for us. */
bool library_file_path(char* out, size_t out_size);
/* Loads library.txt and applies --library-folder (only when no folder is stored). */
void library_prepare(Library* lib, const char* default_folder);
/* Shows the library in fe's window until a game is picked (true, path in out)
 * or the window is closed / Esc (false). error is shown in red when not NULL. */
bool library_run(Frontend* fe, Library* lib, const char* error, char* out, size_t out_size);
/* Adds a ROM that just loaded to the recents in library.txt. */
void library_record_launch(const char* rom_path);
/* Headless --library-out: renders the screen for folder at 720x480 points. */
bool library_render_png(const char* folder, float density, const char* png_path);

#endif // LIBRARY_RUN_H
