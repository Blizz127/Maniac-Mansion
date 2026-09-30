#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/nes.h"
#include "palette.h"

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++)
                k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            crc_table[i] = k;
        }
    c = ~c;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t c = crc(0, hdr + 4, 4);
    c = crc(c, data, len);
    uint8_t t[4];
    be32(t, c);
    fwrite(t, 1, 4, f);
}

bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
    size_t row = (size_t)w * 3 + 1, raw_len = row * h;
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len), *q = z;
    *q++ = 0x78;
    *q++ = 0x01;
    uint32_t s1 = 1, s2 = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *q++ = off + n == raw_len;
        *q++ = n & 0xFF; *q++ = n >> 8;
        *q++ = ~n & 0xFF; *q++ = (~n >> 8) & 0xFF;
        memcpy(q, raw + off, n);
        q += n;
        for (size_t i = 0; i < n; i++) {
            s1 = (s1 + raw[off + i]) % 65521;
            s2 = (s2 + s1) % 65521;
        }
    }
    be32(q, (s2 << 16) | s1);
    q += 4;
    FILE *f = fopen(path, "wb");
    bool ok = f != NULL;
    if (ok) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        uint8_t ihdr[13];
        be32(ihdr, (uint32_t)w);
        be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        chunk(f, "IHDR", ihdr, 13);
        chunk(f, "IDAT", z, (uint32_t)(q - z));
        chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0;
    }
    free(raw);
    free(z);
    return ok;
}

bool png_write_nes(const char *path, const uint16_t *fb)
{
    static uint8_t rgb[NES_W * NES_H * 3];
    for (int i = 0; i < NES_W * NES_H; i++) {
        uint32_t c = nes_rgb(fb[i]);
        rgb[i * 3] = c >> 16;
        rgb[i * 3 + 1] = c >> 8;
        rgb[i * 3 + 2] = c;
    }
    return png_write_rgb(path, rgb, NES_W, NES_H);
}
