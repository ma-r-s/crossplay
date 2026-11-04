#!/usr/bin/env python3
"""Wikipedia opened from the reader's selection bar comes back to the book.

The selection bar's Wiki button starts WikipediaActivity on top of the open
book (wikipedia_lookup::open). These must hold, and none of them can be
exercised on the host, so this reads the source the way test_usb_exclusive.py
does:

1. The bar always offers Wiki, and opens the app through wikipedia_lookup.
2. Every way out of the app returns to the book. On the shelf the app leaves
   through shelf::leave(); opened from the reader it must finish() instead, or
   Back drops the reader on the Apps folder with the book closed.
3. With no pack the app never hands the card to USB under the book. The
   install screen calls Storage.beginUsbDrive(); doing that over an open book
   detaches the card the reader is reading from. Instead the no-pack notice
   offers GET WIKIPEDIA, which records the app on the shelf and restarts into
   it, so the install screen opens with nothing beneath it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
WIKI = ROOT / "src" / "apps_local" / "wikipedia"
CLIP = ROOT / "src" / "activities" / "reader" / "ClipSelectionActivity.cpp"

checks = 0
bad = []


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        bad.append(message)


def body_of(source, signature):
    """The brace-balanced body of the first function whose definition starts with `signature`."""
    start = source.find(signature)
    if start < 0:
        return ""
    open_brace = source.find("{", start)
    depth = 0
    for i in range(open_brace, len(source)):
        depth += {"{": 1, "}": -1}.get(source[i], 0)
        if depth == 0:
            return source[open_brace : i + 1]
    return ""


lookup = (WIKI / "WikipediaLookup.cpp").read_text(encoding="utf-8") if (WIKI / "WikipediaLookup.cpp").exists() else ""
activity = (WIKI / "WikipediaActivity.cpp").read_text(encoding="utf-8")
clip = CLIP.read_text(encoding="utf-8")
reader = (CLIP.parent / "EpubReaderActivity.cpp").read_text(encoding="utf-8")

# 1. Always offered, through wikipedia_lookup.
check("createForTitle(" in body_of(lookup, "std::unique_ptr<Activity> open("),
      "wikipedia_lookup::open() must start the app on the selected title")
check(re.search(r"addAction\(\s*ClippingResult::Action::Wikipedia", clip) is not None,
      "ClipSelectionActivity never offers the Wiki action")

# 2. Every exit goes back to the book.
leaves = [m.start() for m in re.finditer(r"shelf::leave\(", activity)]
check(len(leaves) == 1, f"shelf::leave() must have exactly one caller (leaveApp), found {len(leaves)}")
leave_app = body_of(activity, "void WikipediaActivity::leaveApp()")
check("returnToCaller_" in leave_app and "finish()" in leave_app and "shelf::leave(" in leave_app,
      "leaveApp() must finish() back to the reader when opened from it, and use shelf::leave() otherwise")
check("returnToCaller_" in body_of(activity, "void WikipediaActivity::popHistory()"),
      "an empty trail opened from the reader must return to the book, not the search screen")

# 3. Never a USB handoff over a book.
on_enter = body_of(activity, "void WikipediaActivity::onEnter()")
install_call = on_enter.find("enterInstall()")
check(install_call < 0 or "returnToCaller_" in on_enter[:install_call],
      "onEnter() must not reach enterInstall() when opened from the reader")
check("pendingTitle_" in on_enter, "onEnter() must open the title it was started with")
get = body_of(activity, "void WikipediaActivity::getFromReader()")
check("reopenAfterRestart(" in get and "restartToAppAfterStorageHandoff()" in get,
      "GET WIKIPEDIA from the reader must record the app on the shelf and restart into it")
check("beginUsbDrive" not in get and "enterInstall" not in get,
      "GET WIKIPEDIA from the reader must never reach the install screen in place")
install = activity[activity.find("case wikiui::ActionInstall:"):]
install = install[: install.find("return;", install.find("enterInstall();")) + 7]
check(install.find("getFromReader()") >= 0 and install.find("returnToCaller_") < install.find("enterInstall()"),
      "the install action must restart from the reader before it can reach enterInstall()")

# 4. The window opened from a book is not the shelf's Wikipedia. The shelf
#    decides what to resume on wake by the activity name; with the same name,
#    a sleep over the article woke into Wikipedia with no book under it.
create_for = body_of(activity, "std::unique_ptr<Activity> WikipediaActivity::createForTitle(")
name = re.search(r'makeUniqueNoThrow<WikipediaActivity>\(renderer, mappedInput, "([^"]+)"\)', create_for)
check(name is not None and name.group(1) != "Wikipedia",
      "createForTitle() must give its window an activity name other than the shelf's \"Wikipedia\"")

# 5. GET WIKIPEDIA finds the shelf item by its factory, not by a typed title.
check("reopenAfterRestart(&WikipediaActivity::create)" in get,
      "getFromReader() must name the shelf item by WikipediaActivity::create")

# 6. Back in the book, the shared hyphenator is the book's language again: an
#    article sets it to English, and the book's build may still be running.
branch = reader[reader.find("ClippingResult::Action::Wikipedia"):]
branch = branch[: branch.find("ClippingResult::Action::Lookup")]
check("Hyphenator::setPreferredLanguage(epub->getLanguage())" in branch,
      "the reader must restore the book's hyphenation language when Wikipedia returns")

for b in bad:
    print(f"FAIL lookup-entry  {b}")
print(f"{'FAIL' if bad else 'ok  '}  wikipedia lookup-entry: {checks} checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
