#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/build/adr" run --target "$ROOT/build/tests/time_demo" --trace-root "$ROOT/traces" --report "$ROOT/reports/time_demo.md"
"$ROOT/build/adr" run --target "$ROOT/build/tests/random_demo" --trace-root "$ROOT/traces" --report "$ROOT/reports/random_demo.md"
"$ROOT/build/adr" run --target "$ROOT/build/tests/read_file_demo" --trace-root "$ROOT/traces" --report "$ROOT/reports/read_file_demo.md"
