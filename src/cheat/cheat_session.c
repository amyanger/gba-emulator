#include "cheat/cheat_session.h"

CheatBackupResult cheat_session_backup(CheatEngine* engine, Cartridge* cart) {
    if (engine->backup_done) return CHEAT_BACKUP_NO_SAVE;
    CheatBackupResult r = cheat_backup_save(cart);
    if (r != CHEAT_BACKUP_FAILED) engine->backup_done = true;
    return r;
}

CheatEnableResult cheat_enable(CheatEngine* engine, uint32_t index, Cartridge* cart) {
    if (index >= engine->cheat_count || engine->cheats[index].broken) return CHEAT_ENABLE_INVALID;
    if (engine->cheats[index].enabled) return CHEAT_ENABLE_OK;
    bool first = !engine->backup_done;
    CheatBackupResult r = cheat_session_backup(engine, cart);
    if (r == CHEAT_BACKUP_FAILED) return CHEAT_ENABLE_BACKUP_FAILED;
    engine->cheats[index].enabled = true;
    return (first && r == CHEAT_BACKUP_OK) ? CHEAT_ENABLE_BACKED_UP : CHEAT_ENABLE_OK;
}
