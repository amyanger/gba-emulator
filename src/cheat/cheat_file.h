#ifndef CHEAT_FILE_H
#define CHEAT_FILE_H

#include "cheat.h"

// Load cheats from a .cht file. Returns number of cheats loaded, or -1 on error.
int32_t cheat_file_load(CheatEngine* engine, const char* path);

// Save current cheats to a .cht file. Returns false on failure.
bool cheat_file_save(const CheatEngine* engine, const char* path);

#endif // CHEAT_FILE_H
