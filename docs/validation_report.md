# Validation Report

This report summarizes the current v0.1.1 validation state and the v0.2 analysis hardening direction.

## Scope

This project is not a full-system deterministic replay implementation. It is a Linux x86-64 user-space record/replay/verify/analyze prototype. The target is observable replay for selected nondeterministic inputs and evidence-grounded drift analysis.

## Current Result Summary

| workload | expected status | meaning |
| --- | --- | --- |
| `random_demo` | pass | getrandom payload replay works for the demo |
| `read_file_demo` | pass | file read payload replay works for the demo |
| `mutex_counter` | pass | pthread sync hook records lock/unlock events while observable output replays |
| `sqlite_smoke` | output-equivalence smoke | SQLite CLI workload should match exit/stdout/stderr; event sequence drift is treated as analysis evidence |
| `drift_demo` | fail | intentional stdout drift caused by non-replayed process id |
| `race_counter` | pass or fail | scheduling-dependent output drift sample; fail is an analysis case, not an infrastructure failure |
| `deadlock_demo` | pass | sync-including observable replay sample, not a confirmed deadlock detector |
| `deadlock_timeout_demo` | pass with timeout marker | timeout/no-progress smoke case reported through analyzer |

## Key Interpretations

Record and replay event counts are not required to match. Record traces include broad observation events, including pthread hook entries. Replay traces focus on replay execution and checked/replayable events.

`event_sequence=true` with `stdout_hash=false` means the configured replayable event comparison passed while the target's observable output still drifted. This can happen for race-like behavior or process/environment-dependent output.

For external shell workloads such as `sqlite_smoke`, output equivalence is the primary smoke condition. The script may report `event_sequence=false` because shell, sqlite, and dynamic loader startup reads are intentionally not all forced through payload replay.

The analyzer ranks investigation candidates. It does not confirm root causes.

## Remaining Limitations

- v0.1 does not control scheduler order.
- fork/exec tree replay, network replay, io_uring, and full-system replay are out of scope.
- LLM assistance is currently deterministic mock/offline summarization.
- GitHub Actions runs full validation on a fresh Ubuntu runner. Manual Ubuntu VM/native runs are still useful for final presentation evidence.
