# ADR Analysis Report: drift_demo

This is a checked-in sample report. Runtime-specific reports are generated under `reports/` by `./scripts/problem_demos.sh` and uploaded as CI artifacts.

- workload: `drift_demo`
- expected verify status: `fail`
- cause of intentional drift: `getpid()` output differs between record and replay
- important limitation: `getpid` is not a v0.1 replay-injected syscall

## Drift Summary

No replayable syscall cursor mismatch is expected for this demo. The supported replayable syscall sequence can align while the stdout hash still differs.

Typical verifier evidence:

```json
{
  "status": "fail",
  "checks": {
    "exit_code": true,
    "stdout_hash": false,
    "stderr_hash": true,
    "event_sequence": true
  },
  "first_mismatch": null,
  "first_output_mismatch": {
    "stream": "stdout",
    "kind": "stdout_line_diff"
  }
}
```

## Top Signal

1. `R-OUTPUT` score around `0.92`
   - stdout changed even though replayable syscall alignment passed
   - evidence: `stdout_hash=false`, `first_output_mismatch=true`, stdout diff path present
   - interpretation: inspect output-producing code and non-replayed process/environment values

Analyzer output is an investigation priority, not a confirmed root cause.
