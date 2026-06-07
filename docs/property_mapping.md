# Property Mapping

| ID | property | checked by | output |
| --- | --- | --- | --- |
| D1 | 동일 출력/종료코드 | `result.json`의 `exit_code`, `stdout_hash`, `stderr_hash` | pass/fail |
| D2 | 주요 이벤트 순서 동일 | `events.jsonl` event sequence | first drift |
| S1 | syscall 결과 동일 | syscall `name`, `ret`, `errno`, `payload_ref.sha256` | mismatch list |
| T1 | 시간/난수 재현 | replay 결과 hash 및 payload hash | pass/fail |
| C1 | deadlock 의심 | sync wait/lock events | candidate |
| C2 | race 의심 | output drift 직전 sync 부재 또는 순서 차이 | candidate |
