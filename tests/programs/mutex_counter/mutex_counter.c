#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int counter = 0;

static void *worker(void *arg) {
  int loops = *(int *)arg;
  for (int i = 0; i < loops; ++i) {
    pthread_mutex_lock(&lock);
    counter++;
    pthread_mutex_unlock(&lock);
  }
  return 0;
}

int main(void) {
  pthread_t a, b;
  int loops = 1000;
  pthread_create(&a, 0, worker, &loops);
  pthread_create(&b, 0, worker, &loops);
  pthread_join(a, 0);
  pthread_join(b, 0);
  printf("counter=%d\n", counter);
  return counter == 2000 ? 0 : 1;
}
