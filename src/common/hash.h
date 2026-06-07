#ifndef ADR_COMMON_HASH_H
#define ADR_COMMON_HASH_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ADR_SHA256_HEX_LEN 65

typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  unsigned long long bitlen;
  uint32_t state[8];
} adr_sha256_ctx;

void adr_sha256_init(adr_sha256_ctx *ctx);
void adr_sha256_update(adr_sha256_ctx *ctx, const uint8_t *data, size_t len);
void adr_sha256_final(adr_sha256_ctx *ctx, uint8_t hash[32]);
void adr_sha256_hex(const uint8_t hash[32], char out[ADR_SHA256_HEX_LEN]);
void adr_sha256_bytes_hex(const void *data, size_t len, char out[ADR_SHA256_HEX_LEN]);
int adr_sha256_file_hex(const char *path, char out[ADR_SHA256_HEX_LEN]);

#endif
