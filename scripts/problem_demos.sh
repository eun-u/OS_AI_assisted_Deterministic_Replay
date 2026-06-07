#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"$ROOT/build/adr" run --target "$ROOT/build/tests/drift_demo" \
  --trace-root "$ROOT/traces" --report "$ROOT/reports/drift.md"

"$ROOT/build/adr" run --target "$ROOT/build/tests/race_counter" \
  --trace-root "$ROOT/traces" --report "$ROOT/reports/race.md"

"$ROOT/build/adr" run --target "$ROOT/build/tests/deadlock_demo" \
  --trace-root "$ROOT/traces" --report "$ROOT/reports/deadlock.md"
