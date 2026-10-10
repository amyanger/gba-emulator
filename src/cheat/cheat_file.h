#ifndef CHEAT_FILE_H
#define CHEAT_FILE_H

#include "cheat/cheat.h"

/* .cht files: one block per cheat.
 *
 *   [CodeBreaker]            (or GameShark, ActionReplay, GameSharkRaw, ActionReplayRaw)
 *   Infinite Money           (or name=Infinite Money)
 *   enabled=true
 *   device=detected          (or override; blocks without it are auto-detected)
 *   82025838 03E7
 *
 * Returns the number of cheats loaded, or -1 if the file can't be opened.
 * Blocks whose text is malformed are skipped and counted in *skipped. */
int32_t cheat_file_load(CheatEngine* engine, const char* path, uint32_t* skipped);

/* Writes every cheat, via a temp file renamed over path. */
bool cheat_file_save(const CheatEngine* engine, const char* path);

/* rename() that replaces an existing dst on every platform. */
bool cheat_file_replace(const char* src, const char* dst);

#endif // CHEAT_FILE_H
