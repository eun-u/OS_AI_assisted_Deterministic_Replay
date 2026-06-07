# Test Plan

## Demo Programs

- `time_demo`: time syscall record/replay
- `random_demo`: random payload record/replay
- `read_file_demo`: file read payload record/replay
- `stdout_demo`: stdout/stderr capture
- `mutex_counter`: pthread sync hook smoke test
- `race_counter`: race drift analysis sample
- `deadlock_demo`: deadlock timeout/candidate sample

## Commands

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

최종 보고서용 결과는 WSL2가 아닌 Ubuntu VM 또는 Native Ubuntu에서 한 번 더 생성합니다.
