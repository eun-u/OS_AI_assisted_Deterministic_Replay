# Syscall Policy

| syscall | policy | reason |
| --- | --- | --- |
| `read` | record + replay inject | 외부 입력 buffer를 payload로 저장하고 재주입 |
| `getrandom` | record + replay inject | 난수 buffer를 payload로 저장하고 재주입 |
| `clock_gettime` | record + replay inject | 반환 timespec을 payload로 저장하고 재주입 |
| `gettimeofday` | record + replay inject | 반환 timeval을 payload로 저장하고 재주입 |
| `write` | record + compare | stdout/stderr log와 hash 비교 |
| `openat`, `close`, `lseek` | record only | FD lifecycle 관찰 |
| `futex` | observe | pthread hook event의 보조 관찰 |
| network, `io_uring` | unsupported | v0.1 제외 |

## Event Count and Replay Comparison

Record and replay event counts are intentionally allowed to differ. The recorder observes a broad execution surface, including loader/runtime syscalls and pthread hook events. The verifier compares the supported replayable subset instead of requiring raw event-count equality.

The current replayable subset is `read`, `getrandom`, `clock_gettime`, and `gettimeofday`. For `read`, the replayer only injects payloads for known demo fixture inputs such as `tests/fixtures/...`. Startup reads from the dynamic loader, libc, SQLite, shell scripts, and the ADR sync hook are observed but not forced through payload replay. This prevents corrupting ELF/library loading while keeping the MVP focused on controlled external input demos.

This is a v0.1 boundary, not a claim of full-system replay. Unsupported events remain useful as analyzer evidence.
