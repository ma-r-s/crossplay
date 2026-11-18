#!/usr/bin/env python3
"""A language that will not open goes to the install screen, which repairs it.

With one pack on the card, a pack that would not open went to the install
screen with "Deutsch on the card is not complete. The page copies what is
missing." Tapping the same damaged language on the chooser showed "SORRY /
Could not open this article" instead: the wrong thing named and no way to
repair it (card #651's review). Both doors must take the same way out.

Reads the source, as test_usb_exclusive.py does: the ui suite cannot run the
activity, and the decision is one line of it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "apps_local" / "wikipedia" / "WikipediaActivity.cpp").read_text(encoding="utf-8")

checks = 0
bad = []


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        bad.append(message)


def case_body(label):
    """The text of one `case <label>:` of routeAction, up to the next case."""
    route = SOURCE[SOURCE.find("void WikipediaActivity::routeAction("):]
    at = route.find("case " + label + ":")
    if at < 0:
        return ""
    rest = route[at + len(label) + 6 :]
    end = re.search(r"\n\s*(case |default:)", rest)
    return rest[: end.start()] if end else rest


chooser = case_body("wikiui::ActionPack")
check(chooser != "", "routeAction has no case for a chooser row")
check(re.search(r"enterInstall\(\s*packs_\[\s*value\s*\]\.pack\s*\)", chooser) is not None,
      "a chooser row whose pack will not open must go to enterInstall(packs_[value].pack)")
check("STR_WIKI_OPEN_FAILED" not in chooser and "showNotice" not in chooser,
      "a chooser row whose pack will not open must not show a notice about an article")
single = SOURCE[SOURCE.find("void WikipediaActivity::showChooser("):]
single = single[: single.find("\n}\n")]
check(re.search(r"enterInstall\(\s*packs_\[\s*0\s*\]\.pack\s*\)", single) is not None,
      "the one pack on the card that will not open must go to enterInstall(packs_[0].pack)")

for b in bad:
    print(f"FAIL failed-pack  {b}")
print(f"{'FAIL' if bad else 'ok  '}  wikipedia failed-pack: {checks} checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
