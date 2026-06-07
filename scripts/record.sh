#!/usr/bin/env bash
set -euo pipefail
python3 "$(dirname "$0")/../src/adr_cli/adr.py" record "$@"
