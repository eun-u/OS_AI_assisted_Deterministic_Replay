#include <stdio.h>

int main(void) {
  puts("stdout-line-1");
  fprintf(stderr, "stderr-line-1\n");
  puts("stdout-line-2");
  return 0;
}
