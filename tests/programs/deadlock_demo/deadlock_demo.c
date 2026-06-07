#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

static void *left(void *arg) {
  (void)arg;
  pthread_mutex_lock(&a);
  usleep(10000);
  pthread_mutex_lock(&b);
  pthread_mutex_unlock(&b);
  pthread_mutex_unlock(&a);
  return 0;
}

static void *right(void *arg) {
  (void)arg;
  pthread_mutex_lock(&b);
  usleep(10000);
  pthread_mutex_lock(&a);
  pthread_mutex_unlock(&a);
  pthread_mutex_unlock(&b);
  return 0;
}

int main(void) {
  pthread_t t1, t2;
  pthread_create(&t1, 0, left, 0);
  pthread_create(&t2, 0, right, 0);
  sleep(1);
  puts("deadlock_demo: likely blocked");
  return 0;
}
