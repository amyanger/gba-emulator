#include "frontend/daycare_alerts.h"
#include <string.h>

#define DC_INVALID_LIMIT 60   /* about a minute of unreadable data: re-baseline */

void daycare_alerts_reset(DaycareAlerts* a) {
    memset(a, 0, sizeof(*a));
}

void daycare_alerts_rebase(DaycareAlerts* a) {
    daycare_alerts_reset(a);
}

bool daycare_alerts_update(DaycareAlerts* a, bool valid, uint32_t pid, uint32_t sb1, bool in_battle) {
    if (!valid) {
        if (++a->invalid >= DC_INVALID_LIMIT) {
            a->baselined = false;
            a->cand_reads = 0;
            a->pending = false;
            a->invalid = DC_INVALID_LIMIT;
        }
        return false;
    }
    a->invalid = 0;

    if (a->cand_reads > 0 && pid == a->cand_pid && sb1 == a->cand_sb1) {
        if (a->cand_reads < 2) a->cand_reads++;
    } else {
        a->cand_pid = pid;
        a->cand_sb1 = sb1;
        a->cand_reads = 1;
    }

    if (a->cand_reads >= 2) {
        if (!a->baselined) {
            a->baselined = true;
            a->pid = pid;
        } else if (pid != a->pid) {
            if (a->pid == 0 && pid != 0) a->pending = true;
            else if (pid == 0) a->pending = false;
            a->pid = pid;
        }
    }

    if (a->pending && !in_battle) {
        a->pending = false;
        return true;
    }
    return false;
}
