/* ROM identification. The game is never bundled: the user supplies their
 * own dump, which is checked against the No-Intro USA record (record 1360,
 * the identity the decomp also pins). A clean dump with a different header
 * (for example iNES 1.0 instead of NES 2.0) is accepted when the headerless
 * PRG matches. */
#include "cart.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

static const char USA_FULL_SHA256[] = "e59f95a80497779b861daa26e1b890929fd2f9939e78003898d9bff3ea3f6db2";
static const char USA_PRG_SHA256[] = "84f5377980d2fd44d71faec42f858b1e83540c2f55aba9236c3279d6dde8592a";

#define FAIL(code)            \
    do {                      \
        if (status)           \
            *status = (code); \
        return false;         \
    } while (0)

bool cart_load(cart_t *cart, const char *path, bool allow_unknown, char *err, size_t errlen)
{
    return cart_load_status(cart, path, allow_unknown, err, errlen, NULL);
}

bool cart_load_status(cart_t *cart, const char *path, bool allow_unknown, char *err, size_t errlen,
                      cart_status_t *status)
{
    if (status)
        *status = CART_OK;
    memset(cart, 0, sizeof *cart);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "cannot open ROM '%s': %s", path, strerror(errno));
        FAIL(CART_ERR_UNREADABLE);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, errlen, "cannot read ROM '%s'", path);
        FAIL(CART_ERR_UNREADABLE);
    }
    long size = ftell(f);
    rewind(f);
    if (size < 16 || size > 8 * 1024 * 1024) {
        fclose(f);
        snprintf(err, errlen, "'%s' is not an NES ROM (size %ld bytes; expected 262160)", path, size);
        FAIL(CART_ERR_NOT_NES);
    }
    cart->file = malloc((size_t)size);
    cart->file_size = (size_t)size;
    if (!cart->file || fread(cart->file, 1, cart->file_size, f) != cart->file_size) {
        fclose(f);
        cart_free(cart);
        snprintf(err, errlen, "cannot read ROM '%s'", path);
        FAIL(CART_ERR_UNREADABLE);
    }
    fclose(f);

    const uint8_t *h = cart->file;
    if (memcmp(h, "NES\x1a", 4) != 0) {
        if (!memcmp(h, "PK\x03\x04", 4))
            snprintf(err, errlen, "'%s' is a ZIP archive; extract the .nes file first", path);
        else
            snprintf(err, errlen, "'%s' has no iNES header (not an .nes file)", path);
        cart_free(cart);
        FAIL(CART_ERR_NOT_NES);
    }
    size_t prg = (size_t)h[4] * 16384;
    if ((h[7] & 0x0C) == 0x08) /* NES 2.0 PRG size MSB */
        prg |= (size_t)(h[9] & 0x0F) << 22;
    size_t off = 16 + ((h[6] & 0x04) ? 512 : 0);
    int mapper = (h[6] >> 4) | (h[7] & 0xF0);
    if (off + prg > cart->file_size || prg == 0) {
        snprintf(err, errlen, "'%s' is truncated (header says %zu KiB PRG)", path, prg / 1024);
        cart_free(cart);
        FAIL(CART_ERR_NOT_NES);
    }
    cart->prg = cart->file + off;
    cart->prg_size = prg;
    size_t chr = (size_t)h[5] * 8192;
    if (chr && off + prg + chr <= cart->file_size) {
        cart->chr = cart->prg + prg;
        cart->chr_size = chr;
    }
    sha256_hex(cart->file, cart->file_size, cart->sha256);
    sha256_hex(cart->prg, cart->prg_size, cart->prg_sha256);
    cart->known = !strcmp(cart->sha256, USA_FULL_SHA256) || !strcmp(cart->prg_sha256, USA_PRG_SHA256);

    if (!cart->known && !allow_unknown) {
        if (mapper != 1)
            snprintf(err, errlen, "'%s' is not Maniac Mansion (USA): mapper %d, expected MMC1 (1)", path, mapper);
        else if (prg != 262144)
            snprintf(err, errlen, "'%s' is not Maniac Mansion (USA): %zu KiB PRG, expected 256 KiB", path, prg / 1024);
        else
            snprintf(err, errlen,
                     "'%s' is not the supported Maniac Mansion (USA) dump (PRG SHA-256 %.16s..., expected %.16s...). "
                     "Other regions and modified ROMs are not supported yet",
                     path, cart->prg_sha256, USA_PRG_SHA256);
        cart_free(cart);
        FAIL(CART_ERR_WRONG_GAME);
    }
    /* Test ROMs: NROM maps like MMC1 at power-up (it never writes $8000+). */
    bool ok_mapper = mapper == 1 || (allow_unknown && mapper == 0);
    if (!ok_mapper || prg < 16384 || (!allow_unknown && (prg & (prg - 1)))) {
        snprintf(err, errlen, "'%s': only MMC1 ROMs with power-of-two PRG are supported (mapper %d)", path, mapper);
        cart_free(cart);
        FAIL(CART_ERR_WRONG_GAME);
    }
    return true;
}

void cart_free(cart_t *cart)
{
    free(cart->file);
    memset(cart, 0, sizeof *cart);
}
