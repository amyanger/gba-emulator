#include "cheat/cheat_backup.h"
#include "cheat/cheat_file.h"
#include "cartridge/cartridge.h"
#include "fileio.h"

#include <stdio.h>

#define BACKUP_KEEP 5
#define PATH_LEN (sizeof(((Cartridge*)0)->save_path) + 24)

static bool copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) ok = false;
    if (ferror(in)) ok = false;
    fclose(in);
    if (fflush(out) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    return ok;
}

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

CheatBackupResult cheat_backup_save(Cartridge* cart) {
    if (cart->save_type == SAVE_NONE || cart->save_path[0] == '\0') return CHEAT_BACKUP_NO_SAVE;
    if (cart->save_dirty && cartridge_save_to_file(cart) == CART_FLUSH_FAILED) return CHEAT_BACKUP_FAILED;
    if (!file_exists(cart->save_path)) return CHEAT_BACKUP_NO_SAVE;

    char from[PATH_LEN], to[PATH_LEN];
    for (int k = BACKUP_KEEP - 1; k >= 1; k--) {
        snprintf(from, sizeof(from), "%s.cheatbak%d", cart->save_path, k);
        snprintf(to, sizeof(to), "%s.cheatbak%d", cart->save_path, k + 1);
        if (file_exists(from) && !fileio_replace(from, to)) return CHEAT_BACKUP_FAILED;
    }

    /* Copy to a temp name first so a failed copy never leaves a short cheatbak1. */
    snprintf(from, sizeof(from), "%s.cheatbak1.tmp", cart->save_path);
    snprintf(to, sizeof(to), "%s.cheatbak1", cart->save_path);
    if (!copy_file(cart->save_path, from) || !fileio_replace(from, to)) {
        remove(from);
        return CHEAT_BACKUP_FAILED;
    }
    return CHEAT_BACKUP_OK;
}
