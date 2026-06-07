#define _GNU_SOURCE

#include "common/hash.h"
#include "common/util.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  long syscall_no;
  unsigned long args[6];
  char path[PATH_MAX];
} syscall_entry_t;

typedef struct {
  long syscall_no;
  char name[64];
  char sha256[ADR_SHA256_HEX_LEN];
  long ret;
  long offset;
  long fd;
  size_t length;
  char path[PATH_MAX];
} replay_event_t;

typedef struct {
  replay_event_t *items;
  size_t len;
  size_t cap;
} replay_events_t;

typedef struct {
  long fd;
  char path[PATH_MAX];
} fd_path_t;

typedef struct {
  const char *run_id;
  FILE *events;
  FILE *payload;
  uint64_t seq;
  char prev_hash[ADR_SHA256_HEX_LEN];
} trace_writer_t;

static const char *syscall_name(long no) {
  switch (no) {
    case SYS_read: return "read";
    case SYS_write: return "write";
    case SYS_openat: return "openat";
    case SYS_close: return "close";
    case SYS_lseek: return "lseek";
    case SYS_futex: return "futex";
    case SYS_getrandom: return "getrandom";
    case SYS_clock_gettime: return "clock_gettime";
    case SYS_gettimeofday: return "gettimeofday";
    default: return NULL;
  }
}

static int is_replayable_syscall(long no) {
  return no == SYS_read || no == SYS_getrandom || no == SYS_clock_gettime || no == SYS_gettimeofday;
}

static int should_load_replay_event(const replay_event_t *ev) {
  if (ev->syscall_no != SYS_read) return 1;
  if (ev->fd < 3 || ev->length == 0 || !ev->path[0]) return 0;
  if (strncmp(ev->path, "/lib/", 5) == 0) return 0;
  if (strncmp(ev->path, "/lib64/", 7) == 0) return 0;
  if (strncmp(ev->path, "/usr/lib/", 9) == 0) return 0;
  if (strcmp(ev->path, "/etc/ld.so.cache") == 0) return 0;
  if (strcmp(ev->path, "/etc/ld.so.preload") == 0) return 0;
  if (strstr(ev->path, "libadr_sync_hook.so")) return 0;
  return strstr(ev->path, "tests/fixtures/") != NULL;
}

static unsigned long payload_addr(const syscall_entry_t *entry) {
  switch (entry->syscall_no) {
    case SYS_read: return entry->args[1];
    case SYS_getrandom: return entry->args[0];
    case SYS_clock_gettime: return entry->args[1];
    case SYS_gettimeofday: return entry->args[0];
    default: return 0;
  }
}

static ssize_t write_child_mem(pid_t pid, unsigned long addr, const void *buf, size_t len) {
  struct iovec local = {.iov_base = (void *)buf, .iov_len = len};
  struct iovec remote = {.iov_base = (void *)addr, .iov_len = len};
  ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
  if (n >= 0 || len == 0) return n;

  size_t copied = 0;
  while (copied < len) {
    long word = 0;
    size_t chunk = sizeof(long);
    if (copied + chunk > len) {
      errno = 0;
      word = ptrace(PTRACE_PEEKDATA, pid, (void *)(addr + copied), NULL);
      if (word == -1 && errno != 0) return copied ? (ssize_t)copied : -1;
      chunk = len - copied;
    }
    memcpy(&word, (const char *)buf + copied, chunk);
    if (ptrace(PTRACE_POKEDATA, pid, (void *)(addr + copied), (void *)word) != 0) {
      return copied ? (ssize_t)copied : -1;
    }
    copied += chunk;
  }
  return (ssize_t)copied;
}

static ssize_t read_child_mem(pid_t pid, unsigned long addr, void *buf, size_t len) {
  struct iovec local = {.iov_base = buf, .iov_len = len};
  struct iovec remote = {.iov_base = (void *)addr, .iov_len = len};
  ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
  if (n >= 0 || len == 0) return n;

  size_t copied = 0;
  while (copied < len) {
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, pid, (void *)(addr + copied), NULL);
    if (word == -1 && errno != 0) return copied ? (ssize_t)copied : -1;
    size_t chunk = sizeof(long);
    if (copied + chunk > len) chunk = len - copied;
    memcpy((char *)buf + copied, &word, chunk);
    copied += chunk;
  }
  return (ssize_t)copied;
}

static int read_child_string(pid_t pid, unsigned long addr, char *out, size_t out_len) {
  if (!addr || out_len == 0) return -1;
  size_t max = out_len - 1;
  ssize_t n = read_child_mem(pid, addr, out, max);
  if (n <= 0) return -1;
  size_t limit = (size_t)n;
  for (size_t i = 0; i < limit; ++i) {
    if (out[i] == '\0') return 0;
  }
  out[limit] = '\0';
  return 0;
}

static void emit_line(trace_writer_t *tw, const char *body) {
  char event_hash[ADR_SHA256_HEX_LEN];
  char material[8192];
  snprintf(material, sizeof(material), "%s|%s", tw->prev_hash, body);
  adr_sha256_bytes_hex(material, strlen(material), event_hash);
  fprintf(tw->events, "%s,\"prev_hash\":\"%s\",\"event_hash\":\"%s\"}\n", body, tw->prev_hash, event_hash);
  fflush(tw->events);
  snprintf(tw->prev_hash, sizeof(tw->prev_hash), "%s", event_hash);
}

static void emit_process(trace_writer_t *tw, pid_t pid, const char *name, const char *target) {
  char body[4096];
  snprintf(body, sizeof(body),
           "{\"schema_version\":\"adr.trace.v1\",\"run_id\":\"%s\",\"seq\":%llu,"
           "\"time_ns\":%llu,\"pid\":%d,\"tid\":%d,\"etype\":\"PROCESS\","
           "\"name\":\"%s\",\"phase\":\"instant\",\"args\":{\"target\":\"%s\"}",
           tw->run_id, (unsigned long long)++tw->seq, (unsigned long long)adr_now_ns(),
           pid, pid, name, target ? target : "");
  emit_line(tw, body);
}

static void emit_syscall(trace_writer_t *tw, pid_t pid, const syscall_entry_t *entry, long ret,
                         const replay_event_t *source) {
  const char *name = syscall_name(entry->syscall_no);
  char generated_name[64];
  char payload_json[512] = {0};
  if (!name) {
    snprintf(generated_name, sizeof(generated_name), "sys_%ld", entry->syscall_no);
    name = generated_name;
  }
  if (source && source->length > 0) {
    long offset = ftell(tw->payload);
    snprintf(payload_json, sizeof(payload_json),
             "\"payload_ref\":{\"file\":\"payload.bin\",\"offset\":%ld,\"length\":%zu,"
             "\"sha256\":\"%s\",\"encoding\":\"raw\"}",
             offset, source->length, source->sha256);
  }

  char body[8192];
  snprintf(body, sizeof(body),
           "{\"schema_version\":\"adr.trace.v1\",\"run_id\":\"%s\",\"seq\":%llu,"
           "\"time_ns\":%llu,\"pid\":%d,\"tid\":%d,\"etype\":\"SYSCALL\","
           "\"name\":\"%s\",\"phase\":\"exit\","
           "\"args\":{\"syscall_no\":%ld,\"a0\":%lu,\"a1\":%lu,\"a2\":%lu,\"a3\":%lu,\"a4\":%lu,\"a5\":%lu},"
           "\"ret\":%ld,\"errno\":%d%s%s",
           tw->run_id, (unsigned long long)++tw->seq, (unsigned long long)adr_now_ns(),
           pid, pid, name, entry->syscall_no,
           entry->args[0], entry->args[1], entry->args[2], entry->args[3], entry->args[4], entry->args[5],
           ret, ret < 0 ? (int)-ret : 0,
           payload_json[0] ? "," : "", payload_json);
  emit_line(tw, body);
}

static void emit_drift(trace_writer_t *tw, pid_t pid, long expected, long actual) {
  char body[1024];
  snprintf(body, sizeof(body),
           "{\"schema_version\":\"adr.trace.v1\",\"run_id\":\"%s\",\"seq\":%llu,"
           "\"time_ns\":%llu,\"pid\":%d,\"tid\":%d,\"etype\":\"DRIFT\","
           "\"name\":\"syscall_mismatch\",\"phase\":\"instant\","
           "\"args\":{\"expected_syscall\":%ld,\"actual_syscall\":%ld},\"ret\":-1,\"errno\":0",
           tw->run_id, (unsigned long long)++tw->seq, (unsigned long long)adr_now_ns(),
           pid, pid, expected, actual);
  emit_line(tw, body);
}

static const char *find_after(const char *line, const char *needle) {
  const char *p = strstr(line, needle);
  return p ? p + strlen(needle) : NULL;
}

static int parse_long_field(const char *line, const char *needle, long *out) {
  const char *p = find_after(line, needle);
  if (!p) return -1;
  char *end = NULL;
  errno = 0;
  long v = strtol(p, &end, 10);
  if (errno || p == end) return -1;
  *out = v;
  return 0;
}

static int parse_name(const char *line, char *out, size_t out_len) {
  const char *p = find_after(line, "\"name\":\"");
  if (!p) return -1;
  const char *q = strchr(p, '"');
  if (!q) return -1;
  size_t len = (size_t)(q - p);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, p, len);
  out[len] = '\0';
  return 0;
}

static int parse_string_field(const char *line, const char *needle, char *out, size_t out_len) {
  const char *p = find_after(line, needle);
  if (!p) return -1;
  const char *q = strchr(p, '"');
  if (!q) return -1;
  size_t len = (size_t)(q - p);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, p, len);
  out[len] = '\0';
  return 0;
}

static int push_event(replay_events_t *events, replay_event_t ev) {
  if (events->len == events->cap) {
    size_t next = events->cap ? events->cap * 2 : 64;
    replay_event_t *items = realloc(events->items, next * sizeof(*items));
    if (!items) return -1;
    events->items = items;
    events->cap = next;
  }
  events->items[events->len++] = ev;
  return 0;
}

static void remember_fd_path(fd_path_t *fds, size_t *fd_count, long fd, const char *path) {
  if (fd < 0 || !path || !*path) return;
  for (size_t i = 0; i < *fd_count; ++i) {
    if (fds[i].fd == fd) {
      snprintf(fds[i].path, sizeof(fds[i].path), "%s", path);
      return;
    }
  }
  if (*fd_count >= 128) return;
  fds[*fd_count].fd = fd;
  snprintf(fds[*fd_count].path, sizeof(fds[*fd_count].path), "%s", path);
  ++*fd_count;
}

static void forget_fd_path(fd_path_t *fds, size_t *fd_count, long fd) {
  for (size_t i = 0; i < *fd_count; ++i) {
    if (fds[i].fd == fd) {
      fds[i] = fds[*fd_count - 1];
      --*fd_count;
      return;
    }
  }
}

static const char *lookup_fd_path(const fd_path_t *fds, size_t fd_count, long fd) {
  for (size_t i = 0; i < fd_count; ++i) {
    if (fds[i].fd == fd) return fds[i].path;
  }
  return "";
}

static int is_fixture_path(const char *path) {
  return path && strstr(path, "tests/fixtures/") != NULL;
}

static int load_replay_events(const char *trace_dir, replay_events_t *events) {
  char path[PATH_MAX];
  if (adr_join_path(path, sizeof(path), trace_dir, "events.jsonl") != 0) return -1;
  FILE *f = fopen(path, "r");
  if (!f) return -1;

  char *line = NULL;
  size_t cap = 0;
  fd_path_t fds[128];
  size_t fd_count = 0;
  while (getline(&line, &cap, f) > 0) {
    if (!strstr(line, "\"etype\":\"SYSCALL\"")) continue;
    long syscall_no = -1;
    parse_long_field(line, "\"syscall_no\":", &syscall_no);
    if (syscall_no == SYS_openat) {
      long fd = -1;
      char path[PATH_MAX] = {0};
      parse_long_field(line, "\"ret\":", &fd);
      parse_string_field(line, "\"path\":\"", path, sizeof(path));
      remember_fd_path(fds, &fd_count, fd, path);
      continue;
    }
    if (syscall_no == SYS_close) {
      long fd = -1;
      parse_long_field(line, "\"a0\":", &fd);
      forget_fd_path(fds, &fd_count, fd);
      continue;
    }
    replay_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.syscall_no = syscall_no;
    if (!is_replayable_syscall(ev.syscall_no)) continue;
    parse_name(line, ev.name, sizeof(ev.name));
    parse_long_field(line, "\"ret\":", &ev.ret);
    parse_long_field(line, "\"a0\":", &ev.fd);
    snprintf(ev.path, sizeof(ev.path), "%s", lookup_fd_path(fds, fd_count, ev.fd));
    if (strstr(line, "\"payload_ref\"")) {
      long length = 0;
      parse_long_field(line, "\"offset\":", &ev.offset);
      parse_long_field(line, "\"length\":", &length);
      parse_string_field(line, "\"sha256\":\"", ev.sha256, sizeof(ev.sha256));
      ev.length = length > 0 ? (size_t)length : 0;
    }
    if (!should_load_replay_event(&ev)) continue;
    if (push_event(events, ev) != 0) {
      free(line);
      fclose(f);
      return -1;
    }
  }
  free(line);
  fclose(f);
  return 0;
}

static int read_payload(const char *trace_dir, const replay_event_t *ev, void **out_buf) {
  *out_buf = NULL;
  if (ev->length == 0) return 0;
  char path[PATH_MAX];
  if (adr_join_path(path, sizeof(path), trace_dir, "payload.bin") != 0) return -1;
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  void *buf = malloc(ev->length);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (fseek(f, ev->offset, SEEK_SET) != 0 || fread(buf, 1, ev->length, f) != ev->length) {
    free(buf);
    fclose(f);
    return -1;
  }
  fclose(f);
  *out_buf = buf;
  return 0;
}

static int parse_target_from_metadata(const char *trace_dir, char *out, size_t out_len) {
  char path[PATH_MAX];
  if (adr_join_path(path, sizeof(path), trace_dir, "metadata.json") != 0) return -1;
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  const char *p = find_after(buf, "\"target\":\"");
  if (!p) return -1;
  const char *q = strchr(p, '"');
  if (!q) return -1;
  size_t len = (size_t)(q - p);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, p, len);
  out[len] = '\0';
  return 0;
}

static int copy_metadata(const char *trace, const char *out, const char *target) {
  char path[PATH_MAX];
  if (adr_join_path(path, sizeof(path), out, "metadata.json") != 0) return -1;
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
          "{\n  \"schema_version\":\"adr.metadata.v1\",\n  \"run_id\":");
  adr_json_escape(f, adr_basename_const(out));
  fprintf(f,
          ",\n  \"mode\":\"replay\",\n  \"source_trace\":");
  adr_json_escape(f, trace);
  fprintf(f, ",\n  \"target\":");
  adr_json_escape(f, target);
  fprintf(f, ",\n  \"argv\":[");
  adr_json_escape(f, target);
  fprintf(f, "],\n  \"arch\":\"x86_64\",\n  \"adr_version\":\"0.1.0\"\n}\n");
  return fclose(f);
}

static int write_result(const char *out, int exit_code, int signal_no) {
  char stdout_path[PATH_MAX], stderr_path[PATH_MAX], result_path[PATH_MAX];
  char stdout_hash[ADR_SHA256_HEX_LEN] = {0};
  char stderr_hash[ADR_SHA256_HEX_LEN] = {0};
  adr_join_path(stdout_path, sizeof(stdout_path), out, "stdout.log");
  adr_join_path(stderr_path, sizeof(stderr_path), out, "stderr.log");
  adr_join_path(result_path, sizeof(result_path), out, "result.json");
  if (adr_sha256_file_hex(stdout_path, stdout_hash) != 0) snprintf(stdout_hash, sizeof(stdout_hash), "missing");
  if (adr_sha256_file_hex(stderr_path, stderr_hash) != 0) snprintf(stderr_hash, sizeof(stderr_hash), "missing");
  FILE *f = fopen(result_path, "w");
  if (!f) return -1;
  fprintf(f,
          "{\n  \"schema_version\":\"adr.result.v1\",\n  \"mode\":\"replay\",\n"
          "  \"exit_code\":%d,\n  \"signal\":%d,\n"
          "  \"stdout_hash\":\"%s\",\n  \"stderr_hash\":\"%s\"\n}\n",
          exit_code, signal_no, stdout_hash, stderr_hash);
  return fclose(f);
}

static void usage(FILE *out) {
  fprintf(out, "usage: adr-replayer --trace TRACE_DIR --out TRACE_DIR [--target TARGET]\n");
}

int main(int argc, char **argv) {
  const char *trace = NULL;
  const char *out = NULL;
  const char *target_arg = NULL;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) trace = argv[++i];
    else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
    else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) target_arg = argv[++i];
  }
  if (!trace || !out) {
    usage(stderr);
    return 2;
  }
  if (adr_mkdir_p(out) != 0) {
    perror("mkdir trace dir");
    return 1;
  }

  char target[PATH_MAX];
  if (target_arg) snprintf(target, sizeof(target), "%s", target_arg);
  else if (parse_target_from_metadata(trace, target, sizeof(target)) != 0) {
    fprintf(stderr, "failed to read target from metadata; pass --target\n");
    return 1;
  }

  replay_events_t replay_events = {0};
  if (load_replay_events(trace, &replay_events) != 0) {
    perror("load replay events");
    return 1;
  }

  char events_path[PATH_MAX], payload_path[PATH_MAX], stdout_path[PATH_MAX], stderr_path[PATH_MAX];
  adr_join_path(events_path, sizeof(events_path), out, "events.jsonl");
  adr_join_path(payload_path, sizeof(payload_path), out, "payload.bin");
  adr_join_path(stdout_path, sizeof(stdout_path), out, "stdout.log");
  adr_join_path(stderr_path, sizeof(stderr_path), out, "stderr.log");

  FILE *events = fopen(events_path, "a");
  FILE *payload = fopen(payload_path, "wb");
  if (!events || !payload) {
    perror("open output trace");
    return 1;
  }
  copy_metadata(trace, out, target);

  pid_t child = fork();
  if (child < 0) {
    perror("fork");
    return 1;
  }
  if (child == 0) {
    int out_fd = open(stdout_path, O_CREAT | O_TRUNC | O_WRONLY, 0664);
    int err_fd = open(stderr_path, O_CREAT | O_TRUNC | O_WRONLY, 0664);
    if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);
    if (err_fd >= 0) dup2(err_fd, STDERR_FILENO);
    setenv("ADR_TRACE_DIR", out, 1);
    setenv("ADR_RUN_ID", adr_basename_const(out), 1);
    setenv("ADR_RUN_MODE", "replay", 1);
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    raise(SIGSTOP);
    char *child_argv[] = {target, NULL};
    execvp(target, child_argv);
    perror("execvp");
    _exit(127);
  }

  trace_writer_t tw = {
    .run_id = adr_basename_const(out),
    .events = events,
    .payload = payload,
    .seq = 0,
    .prev_hash = "00"
  };

  int status = 0;
  waitpid(child, &status, 0);
  ptrace(PTRACE_SETOPTIONS, child, NULL, PTRACE_O_TRACESYSGOOD);
  emit_process(&tw, child, "program_start", target);

  int in_syscall = 0;
  int drifted = 0;
  syscall_entry_t entry;
  memset(&entry, 0, sizeof(entry));
  size_t cursor = 0;
  const replay_event_t *active = NULL;
  fd_path_t live_fds[128];
  size_t live_fd_count = 0;

  for (;;) {
    if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) != 0) break;
    if (waitpid(child, &status, 0) < 0) break;

    if (WIFEXITED(status)) {
      emit_process(&tw, child, "program_exit", target);
      write_result(out, WEXITSTATUS(status), 0);
      break;
    }
    if (WIFSIGNALED(status)) {
      emit_process(&tw, child, "program_signal", target);
      write_result(out, 128 + WTERMSIG(status), WTERMSIG(status));
      break;
    }
    if (!WIFSTOPPED(status) || (WSTOPSIG(status) & 0x80) == 0) continue;

    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, child, NULL, &regs) != 0) break;

    if (!in_syscall) {
      entry.syscall_no = regs.orig_rax;
      entry.args[0] = regs.rdi;
      entry.args[1] = regs.rsi;
      entry.args[2] = regs.rdx;
      entry.args[3] = regs.r10;
      entry.args[4] = regs.r8;
      entry.args[5] = regs.r9;
      entry.path[0] = '\0';
      if (entry.syscall_no == SYS_openat) {
        read_child_string(child, entry.args[1], entry.path, sizeof(entry.path));
      }
      active = NULL;

      if (is_replayable_syscall(entry.syscall_no)) {
        if (cursor < replay_events.len && replay_events.items[cursor].syscall_no == entry.syscall_no) {
          if (entry.syscall_no != SYS_read ||
              is_fixture_path(lookup_fd_path(live_fds, live_fd_count, (long)entry.args[0]))) {
            active = &replay_events.items[cursor];
          }
        }
      }
      in_syscall = 1;
    } else {
      long ret = (long)regs.rax;
      if (entry.syscall_no == SYS_openat && ret >= 0 && entry.path[0]) {
        remember_fd_path(live_fds, &live_fd_count, ret, entry.path);
      } else if (entry.syscall_no == SYS_close) {
        forget_fd_path(live_fds, &live_fd_count, (long)entry.args[0]);
      }
      if (active) {
        void *buf = NULL;
        if (read_payload(trace, active, &buf) == 0) {
          if (active->length > 0 && buf) {
            write_child_mem(child, payload_addr(&entry), buf, active->length);
            fwrite(buf, 1, active->length, payload);
          }
          regs.rax = (unsigned long long)active->ret;
          ptrace(PTRACE_SETREGS, child, NULL, &regs);
          ret = active->ret;
          free(buf);
        }
        ++cursor;
      }
      emit_syscall(&tw, child, &entry, ret, active);
      in_syscall = 0;
      if (drifted) {
        ptrace(PTRACE_DETACH, child, NULL, NULL);
        write_result(out, 255, 0);
        break;
      }
    }
  }

  fclose(events);
  fclose(payload);
  free(replay_events.items);
  return drifted ? 3 : 0;
}
