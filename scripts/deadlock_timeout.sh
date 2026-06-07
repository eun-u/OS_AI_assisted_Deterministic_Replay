#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${ADR_DEADLOCK_TARGET:-$ROOT/build/tests/deadlock_timeout_demo}"
TIMEOUT_SEC="${ADR_DEADLOCK_TIMEOUT_SEC:-3}"

if timeout "${TIMEOUT_SEC}s" "$TARGET"; then
  echo "deadlock_timeout_demo_completed_unexpectedly"
else
  status=$?
  if [ "$status" -eq 124 ]; then
    echo "timeout_or_deadlock_detected"
  else
    echo "deadlock_timeout_demo_failed status=$status"
    exit "$status"
  fi
fi
