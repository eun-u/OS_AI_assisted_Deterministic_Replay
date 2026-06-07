#define _GNU_SOURCE

#include "common/util.h"

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int adr_mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  size_t len;

  if (!path || !*path) return -1;
  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

  for (char *p = tmp + 1; *p; ++p) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0775) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0775) != 0 && errno != EEXIST) return -1;
  return 0;
}

int adr_join_path(char *out, size_t out_len, const char *a, const char *b) {
  int n = snprintf(out, out_len, "%s/%s", a, b);
  return n < 0 || (size_t)n >= out_len ? -1 : 0;
}

uint64_t adr_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

const char *adr_basename_const(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

void adr_json_escape(FILE *out, const char *s) {
  fputc('"', out);
  for (; s && *s; ++s) {
    unsigned char c = (unsigned char)*s;
    switch (c) {
      case '\\': fputs("\\\\", out); break;
      case '"': fputs("\\\"", out); break;
      case '\n': fputs("\\n", out); break;
      case '\r': fputs("\\r", out); break;
      case '\t': fputs("\\t", out); break;
      default:
        if (c < 0x20) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
  }
  fputc('"', out);
}

int adr_write_text_file(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  if (fputs(text, f) == EOF) {
    fclose(f);
    return -1;
  }
  return fclose(f);
}
