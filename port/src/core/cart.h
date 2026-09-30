#ifndef MM_CART_H
#define MM_CART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *file;      /* whole file as read */
    size_t file_size;
    const uint8_t *prg; /* points into file */
    size_t prg_size;
    const uint8_t *chr; /* CHR-ROM (test ROMs only; the game uses CHR-RAM) */
    size_t chr_size;
    char sha256[65];     /* full file */
    char prg_sha256[65]; /* headerless PRG */
    bool known;          /* matches the supported retail dump */
} cart_t;

/* Failure classes; the values are the port's process exit codes
 * (docs/port-package.md). */
typedef enum {
    CART_OK = 0,
    CART_ERR_MISSING = 2,     /* no ROM given or found */
    CART_ERR_UNREADABLE = 3,  /* cannot open/read the file */
    CART_ERR_NOT_NES = 4,     /* no iNES header, ZIP, truncated */
    CART_ERR_WRONG_GAME = 5,  /* valid NES file, not the supported dump */
} cart_status_t;

/* Loads and identifies a ROM. With allow_unknown false, only the
 * supported Maniac Mansion (USA) dump is accepted. On failure, writes a
 * one-line, user-facing reason to err and returns false; *status (if not
 * NULL) receives the failure class. */
bool cart_load(cart_t *cart, const char *path, bool allow_unknown, char *err, size_t errlen);
bool cart_load_status(cart_t *cart, const char *path, bool allow_unknown, char *err, size_t errlen,
                      cart_status_t *status);
void cart_free(cart_t *cart);

#endif
