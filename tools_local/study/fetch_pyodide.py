#!/usr/bin/env python3
"""Vendor Pyodide into site/pyodide/, same-origin, pinned.

The site's rule (site/README.md) is that every subresource is same-origin:
under the COOP/COEP headers a plain CDN script silently fails to load, and
the emulator artifacts are already committed for the same reason. So the
installer page's Python runtime is committed too, fetched once by this
script rather than by hand, so the version and the file list are written
down instead of remembered.

    python3 tools_local/study/fetch_pyodide.py

Downloads the core runtime plus the dependency closure of the packages the
study tools import (sqlite3, zstandard, fonttools, pillow), verifying the
sha256 the lockfile declares for each package. Idempotent: files already
present and correct are skipped.
"""

import hashlib
import json
import pathlib
import sys
import urllib.request

VERSION = "0.29.1"
BASE = f"https://cdn.jsdelivr.net/pyodide/v{VERSION}/full/"
OUT = pathlib.Path(__file__).resolve().parents[2] / "site" / "pyodide"

CORE = [
    "pyodide.js",
    "pyodide.asm.js",
    "pyodide.asm.wasm",
    "python_stdlib.zip",
    "pyodide-lock.json",
]

# What the study tools import beyond the stdlib zip. The lockfile knows the
# transitive dependencies (shared libraries included), so name only the roots.
PACKAGES = ["sqlite3", "zstandard", "fonttools", "pillow"]


def fetch(name, expected_sha=None):
    dest = OUT / name
    if dest.exists():
        if expected_sha is None:
            print(f"  have {name}")
            return
        if hashlib.sha256(dest.read_bytes()).hexdigest() == expected_sha:
            print(f"  have {name} (sha ok)")
            return
        print(f"  {name} is present but wrong; refetching")
    with urllib.request.urlopen(BASE + name, timeout=120) as response:
        data = response.read()
    if expected_sha and hashlib.sha256(data).hexdigest() != expected_sha:
        sys.exit(f"sha256 mismatch for {name}; refusing to write it")
    dest.write_bytes(data)
    print(f"  got  {name}  {len(data) / 1024 / 1024:.1f} MB")


def closure(lock, roots):
    """Package names -> the full set including transitive depends."""
    packages = lock["packages"]
    todo = list(roots)
    seen = set()
    while todo:
        name = todo.pop()
        key = name.lower()
        if key in seen:
            continue
        if key not in packages:
            sys.exit(f"pyodide {VERSION} has no package '{name}'")
        seen.add(key)
        todo.extend(packages[key].get("depends", []))
    return sorted(seen)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    print(f"pyodide {VERSION} -> {OUT}")
    for name in CORE:
        fetch(name)

    lock = json.loads((OUT / "pyodide-lock.json").read_text())
    total = 0
    for name in closure(lock, PACKAGES):
        info = lock["packages"][name]
        fetch(info["file_name"], expected_sha=info["sha256"])
        total += (OUT / info["file_name"]).stat().st_size

    size = sum(f.stat().st_size for f in OUT.iterdir())
    print(f"\n{size / 1024 / 1024:.1f} MB total under site/pyodide/")

    # Served with Content-Encoding: br, so compress before committing.
    import subprocess

    precompress = OUT.parents[1] / "tools_local" / "site" / "precompress.py"
    if precompress.exists():
        subprocess.run([sys.executable, str(precompress)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
