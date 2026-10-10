#ifndef CHEAT_BACKUP_H
#define CHEAT_BACKUP_H

typedef struct Cartridge Cartridge;

typedef enum { CHEAT_BACKUP_OK, CHEAT_BACKUP_NO_SAVE, CHEAT_BACKUP_FAILED } CheatBackupResult;

/* Copies the battery save to <save>.cheatbak1 before cheats run, after writing
 * any pending in-game save so the copy is current. Older copies shift to
 * cheatbak2..5; the oldest is dropped. NO_SAVE: no save chip or no file yet. */
CheatBackupResult cheat_backup_save(Cartridge* cart);

#endif // CHEAT_BACKUP_H
