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
- `sqlite_smoke` verifies as `pass` when `sqlite3` is installed.
- `drift_demo` intentionally produces `verify_status: fail` with a stdout drift summary.
- `race_counter` may pass or fail depending on scheduling.
- `deadlock_demo` verifies replayability of its observable output.

## Key Reports

Generated reports are written under `reports/`.

- `reports/random.md`: random payload replay
- `reports/read.md`: file input replay
- `reports/mutex.md`: pthread sync hook trace
- `reports/sqlite.md`: external SQLite workload
- `reports/drift.md`: deterministic high-level output drift
- `reports/race.md`: race-oriented output drift sample
- `reports/deadlock.md`: deadlock-oriented synchronization sample

Runtime traces and reports are intentionally ignored by Git. Regenerate them for each environment instead of committing machine-specific output.
