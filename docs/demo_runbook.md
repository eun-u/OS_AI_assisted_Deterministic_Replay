# Demo Runbook

This runbook describes the repeatable demo flow for the ADR prototype.

## Environment

- Linux x86-64
- CMake, Ninja, GCC, Python 3
- Optional: `sqlite3` for the external workload smoke test

On Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build sqlite3
```

## Full Validation

```bash
./scripts/full_validation.sh
```

Expected result:

- CMake configure and build complete.
- CTest passes.
- `random_demo`, `read_file_demo`, and `mutex_counter` verify as `pass`.
- `sqlite_smoke` is an output-equivalence smoke when `sqlite3` is installed. Exit/stdout/stderr should match; event sequence drift is kept as analysis evidence.
- `drift_demo` intentionally produces `verify_status: fail` with a stdout drift summary.
- `race_counter` may pass or fail depending on scheduling.
- `deadlock_demo` verifies replayability of its observable output.
- `deadlock_timeout_demo` is executed through `scripts/deadlock_timeout.sh` and should produce a timeout/no-progress signal.
- `reports/*_llm_analysis.md` and `.json` are generated for selected problem demos in mock/offline mode.
- `reports/perf.md` contains measured native/record/replay timing values.

## Key Reports

Generated reports are written under `reports/`.

- `reports/random.md`: random payload replay
- `reports/read.md`: file input replay
- `reports/mutex.md`: pthread sync hook trace
- `reports/sqlite.md`: external SQLite workload
- `reports/drift.md`: deterministic high-level output drift
- `reports/race.md`: race-oriented output drift sample
- `reports/deadlock.md`: deadlock-oriented synchronization sample
- `reports/deadlock_timeout.md`: timeout/no-progress smoke sample
- `reports/perf.md`: measured runtime table
- `reports/*_llm_analysis.md`: evidence-grounded LLM-assisted summaries

Runtime traces and reports are intentionally ignored by Git. Regenerate them for each environment instead of committing machine-specific output.
