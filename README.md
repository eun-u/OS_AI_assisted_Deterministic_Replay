# AI-assisted Deterministic Replay System

Linux x86-64 사용자 공간 프로그램을 대상으로 하는 record/replay/verify/analyze MVP입니다.
Windows에서 코드를 관리하되, 저수준 실행은 WSL2 Ubuntu 또는 Native Ubuntu에서 수행하는 구조입니다.

## 상태

- `adr record`: ptrace 기반 syscall 기록, stdout/stderr/result 저장
- `adr replay`: read/getrandom/time 계열 payload 재주입 및 syscall drift 감지
- `adr verify`: original/replay trace의 결과와 주요 이벤트 비교
- `adr analyze`: drift 주변 원인 후보 Top-K Markdown 리포트 생성
- `libadr_sync_hook.so`: pthread create/join/mutex/cond 이벤트 JSONL 기록
- `sqlite_smoke`: SQLite CLI 기반 외부 프로그램 workload smoke test

v0.1은 완전한 전시스템 재현이 아니라 관찰 가능한 결과 재현과 drift 분석에 집중합니다.

## Linux 빌드

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 실행 예시

```bash
python3 src/adr_cli/adr.py run --target ./build/tests/time_demo --repeat 1 --report reports/time_demo.md

./build/adr record --target ./build/tests/random_demo --out traces/random_record
./build/adr replay --trace traces/random_record --out traces/random_replay
./build/adr verify --original traces/random_record --replay traces/random_replay --out reports/random_verify.json
./build/adr analyze --original traces/random_record --replay traces/random_replay \
  --verify reports/random_verify.json --out reports/random_analysis.md

# Optional external workload if sqlite3 is installed
./build/adr run --target ./scripts/sqlite_smoke.sh --trace-root traces --report reports/sqlite.md

# Problem-oriented demos
./scripts/problem_demos.sh
```

## 제한사항

- Linux x86-64 전용입니다.
- v0.1은 단일 프로세스 중심이며 fork/exec/network/io_uring 재현은 제외합니다.
- ptrace 기반이라 성능 최적화보다 관찰성, 정확성, 디버깅 편의성을 우선합니다.
- WSL2와 Native Ubuntu의 ptrace/perf 동작 차이가 있을 수 있어 최종 검증은 Ubuntu VM/Native를 권장합니다.
