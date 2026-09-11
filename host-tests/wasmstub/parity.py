#!/usr/bin/env python3
"""Every method the browser build's canned HttpDownloader defines must match a
declaration in src/network/HttpDownloader.h, parameter types exactly.

The gate builds the simulator and both devices, none of which compile
tools_local/wasm/src/http_canned.cpp: only the emulator workflow does, after
a merge. So on 2026-09-11 the upstream sync went green everywhere and the
first emulator rebuild after it died at http_canned.cpp:112 ("out-of-line
definition of 'downloadToFile' does not match any declaration"): upstream
had added a parameter to downloadToFile, and the live emulator stayed at the
previous merge until somebody read the failed run. This compares the two
files' parameter type lists so the gate says it first.

    parity.py <repo root> [--stub <path to http_canned.cpp>]
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
stub_path = root / "tools_local/wasm/src/http_canned.cpp"
if len(sys.argv) > 3 and sys.argv[2] == "--stub":
    stub_path = Path(sys.argv[3])
header = (root / "src/network/HttpDownloader.h").read_text()
stub = stub_path.read_text()


def types_of(params: str) -> tuple:
    """Parameter TYPES of a parameter list: names and defaults dropped,
    whitespace collapsed, so `const std::string& username = ""` and
    `const std::string&` are the same thing."""
    out = []
    depth = 0
    cur = ""
    for ch in params:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    types = []
    for p in out:
        p = p.split("=", 1)[0].strip()
        p = re.sub(r"\s+", " ", p)
        # drop a trailing identifier (the name) when the parameter has one
        m = re.match(r"^(.*?)(?:\s+|\*|&)([A-Za-z_]\w*)$", p)
        if m and not p.endswith(("&", "*")):
            p = (m.group(1) + ("&" if "&" in p[len(m.group(1)):] else "") + ("*" if "*" in p[len(m.group(1)):] else "")).strip()
            p = re.sub(r"\s+([&*])", r"\1", p)
        types.append(p)
    return tuple(types)


def declared(name: str):
    """Every declaration of `name` in the header, as parameter-type tuples."""
    found = []
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", header):
        # skip mentions inside comments
        line_start = header.rfind("\n", 0, m.start()) + 1
        if header[line_start:m.start()].lstrip().startswith(("//", "*", "/*")):
            continue
        depth, i = 1, m.end()
        while i < len(header) and depth:
            depth += header[i] == "("
            depth -= header[i] == ")"
            i += 1
        found.append(types_of(header[m.end():i - 1]))
    return found


checks = failed = 0
for m in re.finditer(r"HttpDownloader::(\w+)\s*\(", stub):
    name = m.group(1)
    depth, i = 1, m.end()
    while i < len(stub) and depth:
        depth += stub[i] == "("
        depth -= stub[i] == ")"
        i += 1
    defined = types_of(stub[m.end():i - 1])
    checks += 1
    options = declared(name)
    if defined in options:
        print(f"  ok   {name}({', '.join(defined)}) matches a declaration")
    else:
        failed += 1
        print(f"  FAIL {name}({', '.join(defined)}) matches no declaration in HttpDownloader.h;")
        for o in options:
            print(f"       declared: {name}({', '.join(o)})")
print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed or checks == 0 else 0)
