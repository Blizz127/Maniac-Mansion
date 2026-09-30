#ifndef MM_PALETTE_H
#define MM_PALETTE_H

#include <stdint.h>

/* 2C02 palette as 0xRRGGBB; pixel value = index | emphasis << 6. */
uint32_t nes_rgb(uint16_t px);

#endif
