# Analyzer Score Policy

The analyzer ranks investigation candidates. It does not confirm root cause.

Scores are intentionally simple and evidence-grounded. They are not ML probabilities. Each candidate includes:

- `evidence`: observed facts from `verify.json`, trace windows, stdout/stderr diffs, or sync events
- `score_breakdown`: additive components used to produce the displayed score
- `reason`: human-readable interpretation

## Rules

| rule | typical score | evidence |
| --- | ---: | --- |
| `PASS` | 1.00 | verifier passed; optional replayable I/O or sync events were observed |
| `R-OUTPUT` | 0.82-0.92 | `stdout_hash=false`, optional `first_output_mismatch`, optional stdout diff path |
| `R-IO` | 0.95 | read/getrandom/time event near mismatch window |
| `R-SYNC` | 0.72-0.88 | pthread sync events exist or appear near drift window |
| `R-DEADLOCK` | 0.70-0.90 | mutex ordering changes or timeout/no-progress marker |
| `R-SYSCALL` | 0.65 | exit code or syscall result changed |
| `R-HIGHLEVEL` | 0.50 | high-level checks failed without a replayable syscall mismatch |

## Interpretation Guidance

`event_sequence=true` with `stdout_hash=false` is not contradictory. The event sequence check is scoped to the configured replayable subset. A race or process/environment-dependent output can still cause stdout drift while the checked syscall sequence remains aligned.
