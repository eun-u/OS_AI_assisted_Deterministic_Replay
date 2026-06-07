# Event Count Interpretation

Record and replay event counts are not expected to be equal.

The record trace stores broad observation events: syscall exits, payload references, stdout/stderr metadata, process lifecycle events, and pthread synchronization hook entries. The replay trace focuses on replay execution and checked/replayable events. This means a smaller replay event count is not automatically a replay failure.

## Current Examples

| workload | record events | replay events | interpretation |
| --- | ---: | ---: | --- |
| `random_demo` | 46 | 37 | random payload replay path is small; loader/runtime observation still differs |
| `read_file_demo` | 48 | 39 | file read payload is replayed while non-replayable startup reads are observed only |
| `mutex_counter` | 8108 | 49 | pthread sync hook records many lock/unlock events during record; replay does not force scheduler equivalence |
| `race_counter` | 106 | 51 | output drift can appear even when replayable event sequence comparison passes |
| `deadlock_demo` | 89 | 50 | current demo is a sync-including replayability sample, not a confirmed deadlock detector |
| `sqlite_smoke` | 2878 | 376 | SQLite CLI workload causes many internal syscalls; replay checks the supported subset |

The important checks are not raw event-count equality. The verifier compares exit code, stdout/stderr hashes, and the configured replayable event sequence. Analyzer reports use event counts as context, not as a pass/fail criterion.
