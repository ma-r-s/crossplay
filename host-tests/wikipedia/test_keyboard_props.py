#!/usr/bin/env python3
"""The search keyboard passes no Shift word, and every app that types draws it.

The keyboard draws its own arrow on a letter page's Shift key and its own
"#+=" / "123" on the symbol pages, the same in every theme and language. The
word "Shift" fitted English at no key gap only (edge to edge in Classic) and
gave way to the arrow wherever it did not, so one key read three ways (card
#651's review). Weather's copy of the keyboard code had the same word (card
#763's second review), so the keyboard is now one implementation,
src/apps_local/ui/ToyboxKeyboard.cpp, and this holds:

  * the shared drawSearchKeyboard() draws fui::keyboard and sets no shiftLabel;
  * Wikipedia's and Weather's drawKeyboard() call it, and build no keyboard
    of their own (no fui::keyboard, no shiftLabel).

host-tests/ui pins what the component draws without the word.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
APPS = ROOT / "src" / "apps_local"


def body(path, signature):
    source = path.read_text(encoding="utf-8")
    at = source.find(signature)
    if at < 0:
        return ""
    rest = source[at:]
    return rest[: rest.find("\n}\n")]


bad = []
checks = 0


def check(ok, what):
    global checks
    checks += 1
    if not ok:
        bad.append(what)


shared = body(APPS / "ui" / "ToyboxKeyboard.cpp", "void drawSearchKeyboard(")
check(bool(shared), "ui/ToyboxKeyboard.cpp has no drawSearchKeyboard()")
check("fui::keyboard(" in shared, "drawSearchKeyboard() no longer draws fui::keyboard; this check reads the wrong function")
check(not re.search(r"\bshiftLabel\s*=", shared), "drawSearchKeyboard() sets props.shiftLabel; Shift must keep the keyboard's own arrow and labels")

for app, path, signature in (
    ("Wikipedia", APPS / "wikipedia" / "WikipediaActivity.cpp", "void WikipediaActivity::drawKeyboard("),
    ("Weather", APPS / "weather" / "WeatherActivity.cpp", "void WeatherActivity::drawKeyboard("),
):
    own = body(path, signature)
    check(bool(own), f"{app}: drawKeyboard() not found")
    check("toybox::drawSearchKeyboard(" in own, f"{app}: drawKeyboard() does not draw the shared search keyboard")
    check(
        "fui::keyboard(" not in own and not re.search(r"\bshiftLabel\s*=", own),
        f"{app}: drawKeyboard() builds a keyboard of its own",
    )

for b in bad:
    print(f"FAIL keyboard-props  {b}")
print(f"{'FAIL' if bad else 'ok  '}  search keyboard-props: {checks} checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
