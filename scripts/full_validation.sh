#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cmake -S "$ROOT" -B "$ROOT/build" -G Ninja
cmake --build "$ROOT/build"
ctest --test-dir "$ROOT/build" --output-on-failure

"$ROOT/build/adr" run --target "$ROOT/build/tests/random_demo" \
  --trace-root "$ROOT/traces" --report "$ROOT/reports/random.md" --stop-on-fail

"$ROOT/build/adr" run --target "$ROOT/build/tests/read_file_demo" \
  --trace-root "$ROOT/traces" --report "$ROOT/reports/read.md" --stop-on-fail

"$ROOT/build/adr" run --target "$ROOT/build/tests/mutex_counter" \
  --trace-root "$ROOT/traces" --report "$ROOT/reports/mutex.md" --stop-on-fail

if command -v sqlite3 >/dev/null 2>&1; then
  "$ROOT/build/adr" run --target "$ROOT/scripts/sqlite_smoke.sh" \
    --trace-root "$ROOT/traces" --report "$ROOT/reports/sqlite.md" --stop-on-fail
fi

"$ROOT/scripts/problem_demos.sh"
