# MVP Scope

이 저장소의 v0.1 범위는 설계서 `ADR-SDD-001`의 MVP 항목을 따릅니다.

## 포함

- Linux x86-64 동적 링크 C/C++ 사용자 공간 프로그램
- ptrace 기반 syscall record/replay
- `read`, `getrandom`, `clock_gettime`, `gettimeofday` payload 저장 및 재주입
- stdout/stderr/result hash 비교
- LD_PRELOAD 기반 pthread sync event 기록
- rule-based drift analyzer 및 Markdown report

## 제외

- kernel/full-system replay
- Windows native replay
- fork/exec/process tree replay
- network, io_uring, asynchronous signal 정확 재현
- 성능 오버헤드 보장
