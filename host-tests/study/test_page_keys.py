#!/usr/bin/env python3
"""The side keys on a Study card are the reader's page keys (card #761).

StudyActivity needs Arduino and a panel, so its loop cannot be compiled here;
this reads it instead. The card view must read the same two calls Hacker
News's reader reads, in the same order, so the side-button layout in
Settings (swapped, NEXT_NEXT, PREV_PREV, disabled) means the same thing on a
card as on a story. A raw Button::Up/Down there ignores that setting, which
is the mistake this exists to catch.

    python3 host-tests/study/test_page_keys.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
checks = 0
failed = 0


def check(ok, label, detail=""):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f"  FAIL {label}" + (f": {detail}" if detail else ""))


def between(text, start, end, label):
    a = text.find(start)
    b = text.find(end, a + len(start)) if a >= 0 else -1
    check(
        a >= 0 and b > a,
        f"{label}: the block this reads is where it was",
        f"{start!r} .. {end!r}",
    )
    return text[a:b] if a >= 0 and b > a else ""


KEY = r"wasReleased\(MappedInputManager::Button::(\w+)\)"

hn = (ROOT / "src/apps_local/hackernews/HackerNewsActivity.cpp").read_text()
hn_block = between(
    hn, "if (phase_ == Phase::Reading) {", "turnPage(-1)", "Hacker News reader"
)
hn_keys = re.findall(KEY, hn_block)

study_dir = ROOT / "src/apps_local/study"
study = (study_dir / "StudyActivity.cpp").read_text()
loop = between(study, "void StudyActivity::loop() {", "wasScreenTapped", "Study loop")
card = between(loop, "if (view_ == View::Card) {", "SwipeDir::Up", "Study card view")
card_keys = re.findall(KEY, card)
routes = re.findall(KEY + r"\)\s*\{\s*applyCardInput\(study::CardInput::(\w+)\)", card)

check(
    hn_keys == ["PageForward", "PageBack"],
    "Hacker News's reader pages with the page keys",
    str(hn_keys),
)
check(
    card_keys == hn_keys,
    "a Study card reads the same keys as the Hacker News reader",
    f"{card_keys} vs {hn_keys}",
)
check(
    routes == [("PageForward", "KeyNext"), ("PageBack", "KeyPrevious")],
    "page-forward turns on and page-back turns back",
    str(routes),
)

raw = []
for path in sorted(study_dir.rglob("*")):
    if path.suffix in (".cpp", ".h"):
        for n, line in enumerate(path.read_text().splitlines(), 1):
            if re.search(r"Button::(Up|Down)\b", line):
                raw.append(f"{path.name}:{n}")
check(
    not raw,
    "nothing in Study reads the raw Up/Down keys, which ignore the side-button layout",
    ", ".join(raw),
)

print("StudyPageKeys")
print(f"{'FAIL' if failed else 'PASS'} {checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
