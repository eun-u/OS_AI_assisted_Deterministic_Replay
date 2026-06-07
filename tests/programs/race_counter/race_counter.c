#include <pthread.h>
#include <stdio.h>

static int counter = 0;

static void *worker(void *arg) {
  int loops = *(int *)arg;
  for (int i = 0; i < loops; ++i) {
    int tmp = counter;
    tmp++;
    counter = tmp;
  }
  return 0;
}

int main(void) {
  pthread_t a, b;
  int loops = 200000;
  pthread_create(&a, 0, worker, &loops);
  pthread_create(&b, 0, worker, &loops);
  pthread_join(a, 0);
  pthread_join(b, 0);
  printf("racy_counter=%d expected=%d\n", counter, loops * 2);
  return 0;
}
