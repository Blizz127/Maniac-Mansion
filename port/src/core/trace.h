/* Scripted input traces (.mmin).
 *
 * Text, one change per line: "<frame> <buttons>", where <frame> is the
 * 0-based index of the emulated frame from which the pad state applies
 * and <buttons> is '-' or names joined by '+': A B SELECT START UP DOWN
 * LEFT RIGHT. The state persists until the next line. An optional pad
 * prefix "2:" targets controller 2. '#' starts a comment. The special
 * line "end <frame>" sets the trace length. */
#ifndef MM_TRACE_H
#define MM_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t frame;
    uint8_t pad;
    uint8_t buttons;
} trace_event_t;

typedef struct {
    trace_event_t *ev;
    size_t n, cap;
    uint32_t end;
    size_t cursor;
    uint8_t cur[2];
} trace_t;

bool trace_load(trace_t *t, const char *path, char *err, size_t errlen);
void trace_free(trace_t *t);
/* Pad state for frame f (frames must be requested in increasing order). */
void trace_at(trace_t *t, uint32_t f, uint8_t out[2]);

#endif
