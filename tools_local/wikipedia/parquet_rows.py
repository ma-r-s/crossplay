#!/usr/bin/env python3
"""Pull selected rows out of the structured-wikipedia parquet files.

    parquet_rows.py --parquet DIR --titles vital.json --out rows.jsonl.gz

Reads only the `name` column of every file first, then fetches just the
row groups that hold a wanted title, and writes those rows as jsonl.gz in
the shape build_pack.py reads (sections, infoboxes and tables as JSON
strings). Without --titles every row is written.

Needs pyarrow, which is NOT installed on this machine and is the one
dependency the pack tool has; run it through uv so nothing is installed
system-wide:

    uv run --with pyarrow python3 tools_local/wikipedia/parquet_rows.py ...

Untested here for that reason (2026-09-11): build_essentials.sh is the
first run.
"""

import argparse
import gzip
import json
import os
import sys
import time

try:
    import pyarrow.parquet as pq
except ImportError:  # pragma: no cover
    pq = None

STRING_COLUMNS = ("sections", "infoboxes", "tables", "references")


def wanted_titles(path):
    if not path:
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if isinstance(data, list):
        return set(data)
    return set(data.keys())


def as_row(record):
    row = {}
    for k, v in record.items():
        if k in STRING_COLUMNS and v is not None and not isinstance(v, str):
            v = json.dumps(v, ensure_ascii=False, separators=(",", ":"))
        if hasattr(v, "isoformat"):
            v = v.isoformat()
        row[k] = v
    return row


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--parquet", required=True, help="directory of .parquet files")
    ap.add_argument("--titles", help="JSON list or {title: ...} of titles to keep")
    ap.add_argument("--out", required=True, help="rows.jsonl.gz to write")
    ap.add_argument(
        "--columns",
        default="name,abstract,sections,infoboxes,tables,identifier,version,url,date_modified",
    )
    args = ap.parse_args(argv)
    if pq is None:
        sys.exit(
            "pyarrow is not importable; run through: uv run --with pyarrow python3 "
            + __file__
        )
    want = wanted_titles(args.titles)
    columns = [c.strip() for c in args.columns.split(",") if c.strip()]
    files = sorted(f for f in os.listdir(args.parquet) if f.endswith(".parquet"))
    if not files:
        sys.exit(f"no .parquet files in {args.parquet}")
    t0 = time.time()
    written = 0
    scanned = 0
    seen = set()
    with gzip.open(args.out + ".part", "wt", encoding="utf-8") as out:
        for i, name in enumerate(files):
            pf = pq.ParquetFile(os.path.join(args.parquet, name))
            groups = []
            for g in range(pf.num_row_groups):
                names = (
                    pf.read_row_group(g, columns=["name"]).column("name").to_pylist()
                )
                scanned += len(names)
                if want is None or any(n in want for n in names):
                    groups.append(g)
            for g in groups:
                table = pf.read_row_group(g, columns=columns)
                for record in table.to_pylist():
                    n = record.get("name")
                    if want is not None and n not in want:
                        continue
                    out.write(json.dumps(as_row(record), ensure_ascii=False, default=str) + "\n")
                    written += 1
                    seen.add(n)
            print(
                f"[{i + 1}/{len(files)}] {name}: {len(groups)}/{pf.num_row_groups} row groups, {written:,} rows so far, {time.time() - t0:.0f}s",
                file=sys.stderr,
                flush=True,
            )
    os.replace(args.out + ".part", args.out)
    missing = (want - seen) if want is not None else set()
    print(
        json.dumps(
            {
                "files": len(files),
                "rows_scanned": scanned,
                "rows_written": written,
                "titles_wanted": len(want) if want else None,
                "titles_found": len(seen),
                "titles_missing": len(missing),
                "seconds": round(time.time() - t0, 1),
            },
            indent=2,
        )
    )
    if missing:
        with open(args.out + ".missing.txt", "w", encoding="utf-8") as f:
            for t in sorted(missing):
                f.write(t + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
