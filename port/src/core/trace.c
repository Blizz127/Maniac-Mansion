#include "trace.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nes.h"

static const struct { const char *name; uint8_t bit; } names[] = {
    {"A", PAD_A}, {"B", PAD_B}, {"SELECT", PAD_SELECT}, {"START", PAD_START},
    {"UP", PAD_UP}, {"DOWN", PAD_DOWN}, {"LEFT", PAD_LEFT}, {"RIGHT", PAD_RIGHT},
};

static bool parse_buttons(char *s, uint8_t *out)
{
    *out = 0;
    if (!strcmp(s, "-"))
        return true;
    for (char *tok = strtok(s, "+"); tok; tok = strtok(NULL, "+")) {
        size_t i;
        for (i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!strcasecmp(tok, names[i].name))
                break;
        if (i == sizeof names / sizeof names[0])
            return false;
        *out |= names[i].bit;
    }
    return true;
}

bool trace_load(trace_t *t, const char *path, char *err, size_t errlen)
{
    memset(t, 0, sizeof *t);
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, errlen, "cannot open trace '%s': %s", path, strerror(errno));
        return false;
    }
    char line[256];
    int ln = 0;
    uint32_t last = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char a[64], b[128];
        int k = sscanf(line, "%63s %127s", a, b);
        if (k <= 0)
            continue;
        if (!strcmp(a, "end") && k == 2) {
            t->end = (uint32_t)strtoul(b, NULL, 10);
            continue;
        }
        char *e;
        unsigned long fr = strtoul(a, &e, 10);
        uint8_t pad = 0, btn;
        if (k != 2 || *e || (fr < last && t->n)) {
            snprintf(err, errlen, "%s:%d: expected '<frame> <buttons>' with increasing frames", path, ln);
            fclose(f);
            trace_free(t);
            return false;
        }
        char *bs = b;
        if (bs[0] == '2' && bs[1] == ':') {
            pad = 1;
            bs += 2;
        } else if (bs[0] == '1' && bs[1] == ':') {
            bs += 2;
        }
        if (!parse_buttons(bs, &btn)) {
            snprintf(err, errlen, "%s:%d: unknown button in '%s'", path, ln, b);
            fclose(f);
            trace_free(t);
            return false;
        }
        if (t->n == t->cap) {
            t->cap = t->cap ? t->cap * 2 : 64;
            t->ev = realloc(t->ev, t->cap * sizeof *t->ev);
        }
        t->ev[t->n++] = (trace_event_t){(uint32_t)fr, pad, btn};
        last = (uint32_t)fr;
    }
    fclose(f);
    if (!t->end)
        t->end = last + 1;
    return true;
}

void trace_free(trace_t *t)
{
    free(t->ev);
    memset(t, 0, sizeof *t);
}

void trace_at(trace_t *t, uint32_t f, uint8_t out[2])
{
    while (t->cursor < t->n && t->ev[t->cursor].frame <= f) {
        t->cur[t->ev[t->cursor].pad] = t->ev[t->cursor].buttons;
        t->cursor++;
    }
    out[0] = t->cur[0];
    out[1] = t->cur[1];
}
