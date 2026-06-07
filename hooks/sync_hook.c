#define _GNU_SOURCE

#include "common/hash.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static __thread int in_hook = 0;
static uint64_t sync_seq = 1000000000ULL;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static long tid(void) {
  return syscall(SYS_gettid);
}

static void build_path(char *out, size_t len) {
  const char *dir = getenv("ADR_TRACE_DIR");
  if (!dir || !*dir) {
    out[0] = '\0';
    return;
  }
  snprintf(out, len, "%s/events.jsonl", dir);
}

static void log_sync_event(const char *name, const char *phase, const void *object, long ret,
                           const char *extra_key, unsigned long extra_value) {
  if (in_hook) return;
  in_hook = 1;

  char path[4096];
  build_path(path, sizeof(path));
  if (!path[0]) {
    in_hook = 0;
    return;
  }

  int fd = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
  if (fd < 0) {
    in_hook = 0;
    return;
  }

  uint64_t seq = __sync_add_and_fetch(&sync_seq, 1);
  char body[2048];
  int n = snprintf(body, sizeof(body),
                   "{\"schema_version\":\"adr.trace.v1\",\"run_id\":\"%s\",\"seq\":%llu,"
                   "\"time_ns\":%llu,\"pid\":%d,\"tid\":%ld,\"etype\":\"SYNC\","
                   "\"name\":\"%s\",\"phase\":\"%s\",\"object_id\":\"%p\","
                   "\"args\":{\"%s\":%lu},\"ret\":%ld,\"errno\":%ld",
                   getenv("ADR_RUN_ID") ? getenv("ADR_RUN_ID") : "hook",
                   (unsigned long long)seq,
                   (unsigned long long)now_ns(),
                   getpid(), tid(), name, phase, object,
                   extra_key ? extra_key : "value", extra_value,
                   ret, ret < 0 ? -ret : 0);
  if (n > 0 && n < (int)sizeof(body)) {
    char hash[ADR_SHA256_HEX_LEN];
    adr_sha256_bytes_hex(body, (size_t)n, hash);
    char line[2300];
    int m = snprintf(line, sizeof(line), "%s,\"prev_hash\":\"hook\",\"event_hash\":\"%s\"}\n", body, hash);
    if (m > 0 && m < (int)sizeof(line)) write(fd, line, (size_t)m);
  }
  close(fd);
  in_hook = 0;
}

typedef int (*pthread_create_fn)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
typedef int (*pthread_join_fn)(pthread_t, void **);
typedef int (*pthread_mutex_lock_fn)(pthread_mutex_t *);
typedef int (*pthread_mutex_unlock_fn)(pthread_mutex_t *);
typedef int (*pthread_cond_wait_fn)(pthread_cond_t *, pthread_mutex_t *);
typedef int (*pthread_cond_signal_fn)(pthread_cond_t *);
typedef int (*pthread_cond_broadcast_fn)(pthread_cond_t *);

static void *resolve_symbol(const char *name) {
  void *sym = dlsym(RTLD_NEXT, name);
  if (!sym) _exit(127);
  return sym;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
  static pthread_create_fn real_fn;
  if (!real_fn) real_fn = (pthread_create_fn)resolve_symbol("pthread_create");
  log_sync_event("pthread_create", "enter", start_routine, 0, "thread_ptr", (unsigned long)thread);
  int ret = real_fn(thread, attr, start_routine, arg);
  log_sync_event("pthread_create", "leave", start_routine, ret, "thread", thread ? (unsigned long)*thread : 0);
  return ret;
}

int pthread_join(pthread_t thread, void **retval) {
  static pthread_join_fn real_fn;
  if (!real_fn) real_fn = (pthread_join_fn)resolve_symbol("pthread_join");
  log_sync_event("pthread_join", "enter", (void *)thread, 0, "thread", (unsigned long)thread);
  int ret = real_fn(thread, retval);
  log_sync_event("pthread_join", "leave", (void *)thread, ret, "thread", (unsigned long)thread);
  return ret;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  static pthread_mutex_lock_fn real_fn;
  if (!real_fn) real_fn = (pthread_mutex_lock_fn)resolve_symbol("pthread_mutex_lock");
  log_sync_event("pthread_mutex_lock", "enter", mutex, 0, "mutex", (unsigned long)mutex);
  int ret = real_fn(mutex);
  log_sync_event("pthread_mutex_lock", "leave", mutex, ret, "mutex", (unsigned long)mutex);
  return ret;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  static pthread_mutex_unlock_fn real_fn;
  if (!real_fn) real_fn = (pthread_mutex_unlock_fn)resolve_symbol("pthread_mutex_unlock");
  log_sync_event("pthread_mutex_unlock", "enter", mutex, 0, "mutex", (unsigned long)mutex);
  int ret = real_fn(mutex);
  log_sync_event("pthread_mutex_unlock", "leave", mutex, ret, "mutex", (unsigned long)mutex);
  return ret;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  static pthread_cond_wait_fn real_fn;
  if (!real_fn) real_fn = (pthread_cond_wait_fn)resolve_symbol("pthread_cond_wait");
  log_sync_event("pthread_cond_wait", "enter", cond, 0, "mutex", (unsigned long)mutex);
  int ret = real_fn(cond, mutex);
  log_sync_event("pthread_cond_wait", "leave", cond, ret, "mutex", (unsigned long)mutex);
  return ret;
}

int pthread_cond_signal(pthread_cond_t *cond) {
  static pthread_cond_signal_fn real_fn;
  if (!real_fn) real_fn = (pthread_cond_signal_fn)resolve_symbol("pthread_cond_signal");
  log_sync_event("pthread_cond_signal", "enter", cond, 0, "cond", (unsigned long)cond);
  int ret = real_fn(cond);
  log_sync_event("pthread_cond_signal", "leave", cond, ret, "cond", (unsigned long)cond);
  return ret;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
  static pthread_cond_broadcast_fn real_fn;
  if (!real_fn) real_fn = (pthread_cond_broadcast_fn)resolve_symbol("pthread_cond_broadcast");
  log_sync_event("pthread_cond_broadcast", "enter", cond, 0, "cond", (unsigned long)cond);
  int ret = real_fn(cond);
  log_sync_event("pthread_cond_broadcast", "leave", cond, ret, "cond", (unsigned long)cond);
  return ret;
}
