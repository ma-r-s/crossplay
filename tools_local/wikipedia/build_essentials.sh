#!/usr/bin/env bash
# Build the essentials pack: every Vital Article (levels 1 to 5), full text.
#
#   tools_local/wikipedia/build_essentials.sh <parquet-dir> <out-dir> [rows-cache]
#
# 1. vital.py fetches (or reuses) the Vital Articles titles.
# 2. parquet_rows.py scans the parquet files' name column and pulls only the
#    row groups holding a Vital title into <rows-cache>/essentials.jsonl.gz.
#    This step needs pyarrow, which is not installed on this Mac; it runs
#    through `uv run --with pyarrow`, which downloads pyarrow into uv's cache
#    and installs nothing system-wide. That download is the one dependency
#    this pipeline has, and the reason this script has not been run yet
#    (2026-09-11): the rest of the tool is verified without it.
# 3. build_pack.py writes the pack to <out-dir> (a /wikipedia/ directory).
#
# Every step is skipped when its output already exists, so a rerun resumes.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARQUET="${1:?parquet directory}"
OUT="${2:?output directory (the /wikipedia/ directory of the card)}"
ROWS_DIR="${3:-$HERE/cache}"
ROWS="$ROWS_DIR/essentials.jsonl.gz"
VITAL="$HERE/cache/vital.json"

mkdir -p "$ROWS_DIR" "$OUT"

if [ ! -f "$VITAL" ]; then
  echo "== fetching the Vital Articles data" >&2
  python3 "$HERE/vital.py"
fi

if [ ! -f "$ROWS" ]; then
  n=$(ls "$PARQUET"/*.parquet 2>/dev/null | wc -l | tr -d ' ')
  echo "== extracting Vital rows from $n parquet files (pyarrow through uv)" >&2
  if ! command -v uv >/dev/null; then
    echo "uv is not on PATH; install pyarrow some other way and run parquet_rows.py directly" >&2
    exit 1
  fi
  uv run --with pyarrow python3 "$HERE/parquet_rows.py" \
    --parquet "$PARQUET" --titles "$VITAL" --out "$ROWS"
else
  echo "== reusing $ROWS" >&2
fi

echo "== building the pack into $OUT" >&2
# essentials=all: the rows are already the Vital list, so every article is
# in the essentials tier, whatever the list spells its name.
python3 "$HERE/build_pack.py" --rows "$ROWS" --out "$OUT" --vital "$VITAL" \
  --tier essentials=all --summary-json "$OUT/../essentials-summary.json"
python3 "$HERE/pack_format.py" verify "$OUT"
