# Trace Schema v1

## Trace Store

```text
traces/run_x/
  metadata.json
  events.jsonl
  payload.bin
  stdout.log
  stderr.log
  result.json
```

## Event Fields

- `schema_version`: `adr.trace.v1`
- `run_id`: trace directory name
- `seq`: monotonic event sequence
- `time_ns`: recorder monotonic timestamp
- `pid`, `tid`: process/thread identifiers
- `etype`: `PROCESS`, `SYSCALL`, `SYNC`, `DRIFT`
- `name`: syscall or sync operation name
- `phase`: `entry`, `exit`, `instant`, `enter`, `leave`
- `args`: syscall or sync arguments
- `ret`: return value
- `errno`: errno-like value
- `payload_ref`: binary payload reference
- `object_id`: mutex/cond/thread object identifier
- `prev_hash`, `event_hash`: append-only hash chain fields

## Payload Reference

```json
{
  "file": "payload.bin",
  "offset": 0,
  "length": 16,
  "sha256": "...",
  "encoding": "raw"
}
```
