#!/bin/sh
# Every snprintf into a fixed char buffer in src/apps_local/, sized against what
# its own format can print. See check_widths.py for why this exists rather than
# leaning on -Wformat-truncation, which reports nothing at the -O0 every host
# suite compiles at.
set -e
cd "$(dirname "$0")"
python3 check_widths.py
