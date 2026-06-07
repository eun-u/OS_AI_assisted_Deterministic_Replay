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
  const char *trace_dir;
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

static int is_payload_syscall(long no) {
  return no == SYS_read || no == SYS_getrandom || no == SYS_clock_gettime || no == SYS_gettimeofday;
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

static size_t payload_len(const syscall_entry_t *entry, long ret) {
  if (ret < 0) return 0;
  switch (entry->syscall_no) {
    case SYS_read:
    case SYS_getrandom:
      return (size_t)ret;
    case SYS_clock_gettime:
    case SYS_gettimeofday:
      return ret == 0 ? 16 : 0;
    default:
      return 0;
  }
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

static int json_escape_buf(char *out, size_t out_len, const char *s) {
  size_t used = 0;
  if (out_len < 3) return -1;
  out[used++] = '"';
  for (; s && *s; ++s) {
    unsigned char c = (unsigned char)*s;
    const char *escaped = NULL;
    char unicode[8];
    switch (c) {
      case '\\': escaped = "\\\\"; break;
      case '"': escaped = "\\\""; break;
      case '\n': escaped = "\\n"; break;
      case '\r': escaped = "\\r"; break;
      case '\t': escaped = "\\t"; break;
      default:
        if (c < 0x20) {
          snprintf(unicode, sizeof(unicode), "\\u%04x", c);
          escaped = unicode;
        }
    }
    if (escaped) {
      size_t len = strlen(escaped);
      if (used + len + 2 > out_len) return -1;
      memcpy(out + used, escaped, len);
      used += len;
    } else {
      if (used + 2 > out_len) return -1;
      out[used++] = (char)c;
    }
  }
  if (used + 2 > out_len) return -1;
  out[used++] = '"';
  out[used] = '\0';
  return 0;
}

static int append_payload(trace_writer_t *tw, const void *data, size_t len, long *offset, char hash[ADR_SHA256_HEX_LEN]) {
  *offset = ftell(tw->payload);
  if (*offset < 0) return -1;
  if (len > 0 && fwrite(data, 1, len, tw->payload) != len) return -1;
  fflush(tw->payload);
  adr_sha256_bytes_hex(data, len, hash);
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
                         const char *payload_json, const char *extra_json) {
  const char *name = syscall_name(entry->syscall_no);
  char generated_name[64];
  if (!name) {
    snprintf(generated_name, sizeof(generated_name), "sys_%ld", entry->syscall_no);
    name = generated_name;
  }

  char body[8192];
  snprintf(body, sizeof(body),
           "{\"schema_version\":\"adr.trace.v1\",\"run_id\":\"%s\",\"seq\":%llu,"
           "\"time_ns\":%llu,\"pid\":%d,\"tid\":%d,\"etype\":\"SYSCALL\","
           "\"name\":\"%s\",\"phase\":\"exit\","
           "\"args\":{\"syscall_no\":%ld,\"a0\":%lu,\"a1\":%lu,\"a2\":%lu,\"a3\":%lu,\"a4\":%lu,\"a5\":%lu},"
           "\"ret\":%ld,\"errno\":%d%s%s%s%s",
           tw->run_id, (unsigned long long)++tw->seq, (unsigned long long)adr_now_ns(),
           pid, pid, name, entry->syscall_no,
           entry->args[0], entry->args[1], entry->args[2], entry->args[3], entry->args[4], entry->args[5],
           ret, ret < 0 ? (int)-ret : 0,
           payload_json && payload_json[0] ? "," : "",
           payload_json && payload_json[0] ? payload_json : "",
           extra_json && extra_json[0] ? "," : "",
           extra_json && extra_json[0] ? extra_json : "");
  emit_line(tw, body);
}

static int write_metadata(const char *out, const char *target, char *const child_argv[]) {
  char path[PATH_MAX];
  if (adr_join_path(path, sizeof(path), out, "metadata.json") != 0) return -1;
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "{\n  \"schema_version\":\"adr.metadata.v1\",\n  \"run_id\":");
  adr_json_escape(f, adr_basename_const(out));
  fprintf(f, ",\n  \"mode\":\"record\",\n  \"target\":");
  adr_json_escape(f, target);
  fprintf(f, ",\n  \"argv\":[");
  for (int i = 0; child_argv[i]; ++i) {
    if (i) fputc(',', f);
    adr_json_escape(f, child_argv[i]);
  }
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
          "{\n  \"schema_version\":\"adr.result.v1\",\n  \"mode\":\"record\",\n"
          "  \"exit_code\":%d,\n  \"signal\":%d,\n"
          "  \"stdout_hash\":\"%s\",\n  \"stderr_hash\":\"%s\"\n}\n",
          exit_code, signal_no, stdout_hash, stderr_hash);
  return fclose(f);
}

static void usage(FILE *out) {
  fprintf(out, "usage: adr-recorder --out TRACE_DIR -- TARGET [ARG...]\n");
}

int main(int argc, char **argv) {
  const char *out = NULL;
  int target_index = -1;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      out = argv[++i];
    } else if (strcmp(argv[i], "--") == 0 && i + 1 < argc) {
      target_index = i + 1;
      break;
    }
  }
  if (!out || target_index < 0) {
    usage(stderr);
    return 2;
  }

  if (adr_mkdir_p(out) != 0) {
    perror("mkdir trace dir");
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
    perror("open trace files");
    return 1;
  }

  char **child_argv = &argv[target_index];
  if (write_metadata(out, child_argv[0], child_argv) != 0) {
    perror("write metadata");
    return 1;
  }

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
    setenv("ADR_RUN_MODE", "record", 1);
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    raise(SIGSTOP);
    execvp(child_argv[0], child_argv);
    perror("execvp");
    _exit(127);
  }

  trace_writer_t tw = {
    .trace_dir = out,
    .run_id = adr_basename_const(out),
    .events = events,
    .payload = payload,
    .seq = 0,
    .prev_hash = "00"
  };

  int status = 0;
  waitpid(child, &status, 0);
  ptrace(PTRACE_SETOPTIONS, child, NULL, PTRACE_O_TRACESYSGOOD);
  emit_process(&tw, child, "program_start", child_argv[0]);

  int in_syscall = 0;
  syscall_entry_t entry;
  memset(&entry, 0, sizeof(entry));

  for (;;) {
    if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) != 0) break;
    if (waitpid(child, &status, 0) < 0) break;

    if (WIFEXITED(status)) {
      emit_process(&tw, child, "program_exit", child_argv[0]);
      write_result(out, WEXITSTATUS(status), 0);
      break;
    }
    if (WIFSIGNALED(status)) {
      emit_process(&tw, child, "program_signal", child_argv[0]);
      write_result(out, 128 + WTERMSIG(status), WTERMSIG(status));
      break;
    }
    if (!WIFSTOPPED(status) || (WSTOPSIG(status) & 0x80) == 0) {
      continue;
    }

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
      in_syscall = 1;
    } else {
      long ret = (long)regs.rax;
      char payload_json[512] = {0};
      char extra_json[PATH_MAX + 32] = {0};
      size_t len = payload_len(&entry, ret);
      if (is_payload_syscall(entry.syscall_no) && len > 0) {
        void *buf = malloc(len);
        if (buf && read_child_mem(child, payload_addr(&entry), buf, len) == (ssize_t)len) {
          long offset = 0;
          char hash[ADR_SHA256_HEX_LEN];
          if (append_payload(&tw, buf, len, &offset, hash) == 0) {
            snprintf(payload_json, sizeof(payload_json),
                     "\"payload_ref\":{\"file\":\"payload.bin\",\"offset\":%ld,\"length\":%zu,"
                     "\"sha256\":\"%s\",\"encoding\":\"raw\"}",
                     offset, len, hash);
          }
        }
        free(buf);
      }
      if (entry.syscall_no == SYS_openat && ret >= 0 && entry.path[0]) {
        char escaped_path[PATH_MAX + 16];
        if (json_escape_buf(escaped_path, sizeof(escaped_path), entry.path) == 0) {
          snprintf(extra_json, sizeof(extra_json), "\"path\":%s", escaped_path);
        }
      }
      emit_syscall(&tw, child, &entry, ret, payload_json, extra_json);
      in_syscall = 0;
    }
  }

  fclose(events);
  fclose(payload);
  return 0;
}
