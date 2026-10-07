#ifndef HEADLESS_INPUT_SCRIPT_H
#define HEADLESS_INPUT_SCRIPT_H

#include "common.h"
#include "input/input.h"

/* Scripted keypad input for --headless runs (--input-script FILE).

   Format: one event per line,

       <frame> <press|release> <KEY>

   where <frame> is a 0-based decimal frame index (matching the index in
   --hash-out output) and KEY is one of A B SELECT START RIGHT LEFT UP DOWN
   R L (uppercase). Fields are separated by spaces or tabs. '#' starts a
   comment that runs to end of line; blank lines are ignored.

   An event takes effect before its frame runs. Frames must be listed in
   non-decreasing order; events sharing a frame apply in file order. Any
   malformed line rejects the whole script. */

#define INPUT_SCRIPT_MAX_EVENTS 1024

typedef struct {
    int32_t frame;
    uint16_t key;  // KEY_* mask from input/input.h
    bool press;    // true = press, false = release
} InputScriptEvent;

typedef struct {
    InputScriptEvent events[INPUT_SCRIPT_MAX_EVENTS];
    uint32_t count;
    uint32_t next;  // index of the next event to apply
} InputScript;

void input_script_init(InputScript* script);

/* Parse one line and append its event (if any) to `script`. `line_no` is
   only used in the error message. Returns false and logs an error on a
   malformed line, an out-of-order frame, or a full event table. */
bool input_script_parse_line(InputScript* script, const char* line, int line_no);

/* Initialise `script` and fill it from the file at `path`. Returns false
   (with an error logged) if the file can't be read or any line is bad. */
bool input_script_load(InputScript* script, const char* path);

/* Apply every not-yet-applied event whose frame is <= `frame`. */
void input_script_apply(InputScript* script, int32_t frame, InputState* input);

#endif // HEADLESS_INPUT_SCRIPT_H
