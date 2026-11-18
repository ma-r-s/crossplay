#!/usr/bin/env python3
"""The search keyboard passes no Shift word.

The keyboard draws its own arrow on a letter page's Shift key and its own
"#+=" / "123" on the symbol pages, the same in every theme and language. The
word "Shift" fitted English at no key gap only (edge to edge in Classic) and
gave way to the arrow wherever it did not, so one key read three ways (card
#651's review). host-tests/ui pins what the component draws without the
word; this pins that the activity does not hand it one.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "apps_local" / "wikipedia" / "WikipediaActivity.cpp").read_text(encoding="utf-8")

body = SOURCE[SOURCE.find("void WikipediaActivity::drawKeyboard("):]
body = body[: body.find("\n}\n")]
bad = []
if not body:
    bad.append("WikipediaActivity::drawKeyboard() not found")
if re.search(r"\bshiftLabel\s*=", body):
    bad.append("drawKeyboard() sets props.shiftLabel; the Shift key must keep the keyboard's own arrow and labels")
if "fui::keyboard(" not in body:
    bad.append("drawKeyboard() no longer draws fui::keyboard; this check reads the wrong function")

for b in bad:
    print(f"FAIL keyboard-props  {b}")
print(f"{'FAIL' if bad else 'ok  '}  wikipedia keyboard-props: 3 checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
