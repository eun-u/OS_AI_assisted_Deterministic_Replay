#define _GNU_SOURCE

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

int main(void) {
  struct timespec ts;
  struct timeval tv;
  clock_gettime(CLOCK_REALTIME, &ts);
  gettimeofday(&tv, 0);
  printf("clock=%ld.%09ld\n", (long)ts.tv_sec, ts.tv_nsec);
  printf("timeval=%ld.%06ld\n", (long)tv.tv_sec, (long)tv.tv_usec);
  return 0;
}
