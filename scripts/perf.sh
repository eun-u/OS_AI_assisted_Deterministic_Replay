#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/reports/perf"
TRACE_DIR="$ROOT/traces/perf"
mkdir -p "$OUT_DIR" "$TRACE_DIR"

measure() {
  local name="$1"
  shift
  /usr/bin/time -f "elapsed_sec=%e max_rss_kb=%M" -o "$OUT_DIR/${name}.txt" "$@"
}

rm -rf "$TRACE_DIR/random_record" "$TRACE_DIR/random_replay"

measure random_native "$ROOT/build/tests/random_demo"
measure random_record "$ROOT/build/adr" record --target "$ROOT/build/tests/random_demo" --out "$TRACE_DIR/random_record"
measure random_replay "$ROOT/build/adr" replay --trace "$TRACE_DIR/random_record" --out "$TRACE_DIR/random_replay" --target "$ROOT/build/tests/random_demo"

{
  echo "# ADR Performance Measurements"
  echo
  echo "These numbers are measurements only. v0.1 does not claim an overhead target."
  echo
  echo "| case | elapsed_sec | max_rss_kb |"
  echo "| --- | ---: | ---: |"
  for file in "$OUT_DIR"/*.txt; do
    case_name="$(basename "$file" .txt)"
    elapsed="$(awk -F'[ =]' '/elapsed_sec/ {print $2}' "$file")"
    rss="$(awk -F'[ =]' '/max_rss_kb/ {print $4}' "$file")"
    echo "| $case_name | $elapsed | $rss |"
  done
} > "$ROOT/reports/perf.md"
