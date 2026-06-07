# Test Plan

## Demo Programs

- `time_demo`: time syscall record/replay
- `random_demo`: random payload record/replay
- `read_file_demo`: file read payload record/replay
- `stdout_demo`: stdout/stderr capture
- `drift_demo`: deterministic output drift via non-replayed process id
- `mutex_counter`: pthread sync hook smoke test
- `race_counter`: race drift analysis sample
- `deadlock_demo`: deadlock timeout/candidate sample
- `sqlite_smoke`: SQLite CLI external workload smoke test

## Commands

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

최종 보고서용 결과는 WSL2가 아닌 Ubuntu VM 또는 Native Ubuntu에서 한 번 더 생성합니다.

`sqlite_smoke_pipeline`은 `sqlite3`가 설치된 환경에서만 CTest에 추가됩니다.

문제 상황 리포트는 `./scripts/problem_demos.sh`로 생성합니다. `drift_demo`는 의도적으로 stdout hash mismatch를 만들며, `race_counter`는 스케줄링에 따라 pass/fail이 달라질 수 있습니다.
