#!/bin/bash
# tools_local/header_row_check.py, the check sim-shot.sh runs on every
# screenshot, against headers drawn here with known answers: a check that
# reads pictures is only as good as the pictures it has been seen to fail.
#
#   host-tests/headerrow/run.sh
set -euo pipefail
cd "$(dirname "$0")"
uv run --quiet --with pillow python test_header_row.py
