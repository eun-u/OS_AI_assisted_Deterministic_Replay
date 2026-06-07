#define _GNU_SOURCE

#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t left_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t right_lock = PTHREAD_MUTEX_INITIALIZER;

static void *left_worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&left_lock);
  usleep(20000);
  pthread_mutex_lock(&right_lock);
  return 0;
}

static void *right_worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&right_lock);
  usleep(20000);
  pthread_mutex_lock(&left_lock);
  return 0;
}

int main(void) {
  pthread_t left;
  pthread_t right;
  pthread_create(&left, 0, left_worker, 0);
  pthread_create(&right, 0, right_worker, 0);
  pthread_join(left, 0);
  pthread_join(right, 0);
  return 0;
}
