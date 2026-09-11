#!/usr/bin/env python3
"""No app may read Storage.mkdir()'s false as "the card is not writable".

SdFat's mkdir opens the new entry with O_CREAT | O_EXCL, so it returns false
when the directory ALREADY EXISTS. An app that creates its folder with a bare
`if (!Storage.mkdir(dir))` works once and then, on every later attempt,
tells the user the card is not writable: card #475, xkcd, "folders are
created but it says that sd is not writable or present". The guard is
`!Storage.exists(dir) && !Storage.mkdir(dir)`, which every other app here
already had.

Scans src/apps_local for a negated mkdir that is not preceded on the same
statement by an exists() check. Only the fork's apps: upstream's callers are
upstream's, and the web server's create-folder handler wants the refusal.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
APPS = ROOT / "src" / "apps_local"

bad = []
checks = 0
for path in sorted(APPS.rglob("*.cpp")):
    text = path.read_text(encoding="utf-8")
    for m in re.finditer(r"!\s*Storage\.mkdir\(", text):
        checks += 1
        # The statement this call sits in: back to the previous ';', '{' or '}'.
        start = max(text.rfind(c, 0, m.start()) for c in ";{}")
        statement = text[start + 1 : m.end()]
        if "exists(" not in statement:
            line = text.count("\n", 0, m.start()) + 1
            bad.append(f"{path.relative_to(ROOT)}:{line}")

for b in bad:
    print(f"FAIL mkdir-guard  {b}: !Storage.mkdir() with no exists() guard; "
          "SdFat's mkdir refuses an existing directory")
# The gate counts "N checks, M failed" lines; a suite worded otherwise passes
# unseen (host-tests/checksh).
print(f"{'FAIL' if bad else 'ok  '}  xkcd mkdir-guard: {checks} checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
