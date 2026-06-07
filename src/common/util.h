#ifndef ADR_COMMON_UTIL_H
#define ADR_COMMON_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int adr_mkdir_p(const char *path);
int adr_join_path(char *out, size_t out_len, const char *a, const char *b);
uint64_t adr_now_ns(void);
const char *adr_basename_const(const char *path);
void adr_json_escape(FILE *out, const char *s);
int adr_write_text_file(const char *path, const char *text);

#endif
