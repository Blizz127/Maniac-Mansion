#ifndef MM_PNG_H
#define MM_PNG_H

#include <stdbool.h>
#include <stdint.h>

/* Writes an RGB PNG of a 256x240 NES framebuffer (stored deflate; no
 * compression library needed). */
bool png_write_nes(const char *path, const uint16_t *fb);
bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

#endif
