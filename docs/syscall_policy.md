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
