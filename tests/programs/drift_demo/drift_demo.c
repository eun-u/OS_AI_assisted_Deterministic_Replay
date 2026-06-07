#include <stdio.h>
#include <unistd.h>

int main(void) {
  printf("pid=%ld\n", (long)getpid());
  return 0;
}
