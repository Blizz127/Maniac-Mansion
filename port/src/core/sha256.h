#ifndef MM_SHA256_H
#define MM_SHA256_H

#include <stddef.h>
#include <stdint.h>

void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void sha256_hex(const uint8_t *data, size_t len, char out[65]);

#endif
