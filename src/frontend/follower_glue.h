#ifndef FOLLOWER_GLUE_H
#define FOLLOWER_GLUE_H

#include <stdint.h>
#include "frontend/frontend.h"

/* Before a frame: loads the lead's sprite, starts a talk on A, builds the extra OBJs.
 * The caller holds game input (KEYINPUT 0x03FF) while talk_active. */
void follower_glue_frame_begin(Frontend* fe, GBA* gba);
/* After a frame: runs the talk with the host keys (KEYINPUT, active low), cries and mood. */
void follower_glue_frame_end(Frontend* fe, const GBA* gba, uint16_t real_keys);
/* Ends a talk without applying its mood change. talk_abort leaves want_cry set. */
void follower_glue_talk_stop(FollowerView* v);

#endif // FOLLOWER_GLUE_H
