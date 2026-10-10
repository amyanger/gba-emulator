#ifndef CHEAT_SESSION_H
#define CHEAT_SESSION_H

#include "cheat/cheat.h"
#include "cheat/cheat_backup.h"

typedef enum {
    CHEAT_ENABLE_OK,              /* on (backup already done, or no save to back up) */
    CHEAT_ENABLE_BACKED_UP,       /* on, and the save was just backed up */
    CHEAT_ENABLE_BACKUP_FAILED,   /* stays off: the save couldn't be backed up */
    CHEAT_ENABLE_INVALID          /* bad index or a cheat that can't run */
} CheatEnableResult;

/* Backs the save up once per session, before the first cheat runs.
 * Marks the session done on OK or NO_SAVE. */
CheatBackupResult cheat_session_backup(CheatEngine* engine, Cartridge* cart);

/* The only way to turn a cheat on: runs the session backup first. */
CheatEnableResult cheat_enable(CheatEngine* engine, uint32_t index, Cartridge* cart);

#endif // CHEAT_SESSION_H
