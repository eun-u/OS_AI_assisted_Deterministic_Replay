#!/usr/bin/env bash
set -euo pipefail

DB="${ADR_SQLITE_DB:-/tmp/adr_sqlite_smoke.db}"
SQL="${ADR_SQLITE_SQL:-tests/sqlite/workload.sql}"

rm -f "$DB"
sqlite3 "$DB" ".read $SQL"
