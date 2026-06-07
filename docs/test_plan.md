# Test Plan

## Demo Programs

- `time_demo`: time syscall record/replay
- `random_demo`: random payload record/replay
- `read_file_demo`: file read payload record/replay
- `stdout_demo`: stdout/stderr capture
- `drift_demo`: deterministic output drift via non-replayed process id (`getpid`)
- `mutex_counter`: pthread sync hook smoke test
- `race_counter`: race drift analysis sample
- `deadlock_demo`: deadlock timeout/candidate sample
- `deadlock_timeout_demo`: real no-progress process guarded by `timeout`
- `sqlite_smoke`: SQLite CLI external workload smoke test

## Commands

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

전체 검증:

```bash
./scripts/full_validation.sh
```

최종 보고서용 결과는 WSL2가 아닌 Ubuntu VM 또는 Native Ubuntu에서 한 번 더 생성합니다.

`sqlite_smoke_pipeline`은 `sqlite3`가 설치된 환경에서만 CTest에 추가됩니다.

문제 상황 리포트는 `./scripts/problem_demos.sh`로 생성합니다. `drift_demo`는 `getpid()`가 record/replay 사이에서 달라지는 점을 이용한 의도적 stdout hash mismatch입니다. `getpid`는 v0.1 재주입 대상 syscall이 아니므로, 이 데모는 "non-replayed syscall/environment value로 인한 high-level output drift를 verifier/analyzer가 어떻게 보고하는지"를 보여줍니다. `race_counter`는 스케줄링에 따라 pass/fail이 달라질 수 있습니다.

`scripts/full_validation.sh`는 문제 상황 리포트와 `reports/perf.md`까지 생성합니다. LLM-assisted reports are generated in deterministic mock/offline mode for selected problem demos.
