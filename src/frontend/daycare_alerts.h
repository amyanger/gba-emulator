#ifndef DAYCARE_ALERTS_H
#define DAYCARE_ALERTS_H

#include <stdbool.h>
#include <stdint.h>

/* Edge detector for "an egg is waiting at the Daycare". Reads are confirmed twice, so one
 * bad read (a save block mid-move) never toasts. */
typedef struct {
    bool baselined;
    uint32_t pid;          /* last confirmed offspringPersonality (0 = none) */
    uint32_t cand_pid, cand_sb1;
    uint8_t cand_reads;    /* matching reads of cand_pid with the same sb1 */
    uint8_t invalid;       /* consecutive unreadable reads */
    bool pending;          /* edge seen during a battle, toast when it ends */
} DaycareAlerts;

void daycare_alerts_reset(DaycareAlerts* a);
/* Savestate load or rewind: the next valid reads baseline silently. */
void daycare_alerts_rebase(DaycareAlerts* a);
/* valid=false: unreadable. Returns true when the "Egg ready" toast should fire now. */
bool daycare_alerts_update(DaycareAlerts* a, bool valid, uint32_t pid, uint32_t sb1, bool in_battle);

#endif // DAYCARE_ALERTS_H
