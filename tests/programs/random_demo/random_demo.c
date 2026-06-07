#define _GNU_SOURCE

#include <stdio.h>
#include <sys/random.h>

int main(void) {
  unsigned char buf[16];
  ssize_t n = getrandom(buf, sizeof(buf), 0);
  if (n < 0) {
    perror("getrandom");
    return 1;
  }
  printf("random:");
  for (ssize_t i = 0; i < n; ++i) printf("%02x", buf[i]);
  printf("\n");
  return 0;
}
