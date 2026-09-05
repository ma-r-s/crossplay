#!/bin/bash
# Can an app in src/apps_local ship with no way back out of it?
#
# Card #250: "back swipe does not exit Trivia". It was filed as a Trivia bug and
# it was one, but the diagnosis attached to it was wrong in a way worth writing
# down, because the wrong version is the one a grep produces.
#
# THE WRONG VERSION. `MappedInputManager::wasSwipe()` exists, and only three
# things in src/apps_local call it, so nineteen-odd games "never ask whether the
# user swiped back". Every part of that is true and the conclusion does not
# follow: wasSwipe() is the four-direction PAGING swipe. The two apps that call
# it (ShelfFolderActivity, hackernews) compare it against Up and Down to turn
# pages, and a test asserting that apps must not call it would delete paging
# from both.
#
# THE ACTUAL MECHANISM. Back is already unified, and has been all along:
#
#   MappedInputManager::wasPressed/wasReleased(Button::Back)
#     -> if (button == Button::Back && wasBackGesture()) return true;
#
# so the left-edge swipe IS Button::Back. Every app that reads that button
# already gets the gesture, on every board, with no per-app gesture code -- and
# 25 of the 26 activities in apps_local do read it. The one that did not was
# Trivia, whose loop() returned early unless a tap had arrived; a swipe is not a
# tap, so the read never happened and the front door had no exit.
#
# So the fork-wide hole is not a missing gesture. It is that ANY new app can be
# written the way Trivia was -- touch-only, exiting through an on-screen button
# -- and nothing notices until somebody swipes. This suite is what notices.
#
#   host-tests/backgesture/run.sh
#
# TWO CHECKS.
#
# 1. Every *Activity.cpp under src/apps_local must read Button::Back from a
#    function on the PER-FRAME input path. That qualifier is the whole test:
#    Trivia before the fix DID contain a Button::Back read, at line 300, inside
#    runPackDownload() -- the "Back stops the download" affordance, on a path
#    that only exists while a multi-minute fetch has blocked the loop. A
#    file-level grep for Button::Back passes Trivia and finds nothing. The frame
#    path is loop(), gameLoop() (the link-play base calls it) and the route*()
#    handlers the bigger games split their screens into; that list is a
#    WHITELIST, so an app whose handler is named something new fails this suite
#    rather than slipping past it, and the fix is one line here plus a look at
#    whether the new name should exist.
#
# 2. No app may build a back gesture out of a raw horizontal wasSwipe(). One
#    app doing that is a second, competing definition of Back that the release
#    gate, the board profiles and wasBackGesture()'s edge anchoring know nothing
#    about. Zero hits today -- the four live wasSwipe() comparisons are all Up
#    or Down -- so this fires only on something newly written.
#
# THE SCANNER RUNS ON FIXTURES FIRST, for the reason marginguard does it: a
# scanner that has quietly stopped matching anything is indistinguishable from a
# tree with nothing to match. The fixtures are the two shapes that matter -- an
# activity shaped like Minesweeper (Back in loop, must pass) and one shaped like
# Trivia before the fix (Back only in a download worker, must be caught).
#
# What this suite CANNOT do, said plainly: it reads source. It cannot tell you
# the swipe works on the panel -- nothing on the host can, because the simulator
# does not compile lib/hal and never runs InputManager, so its swipes are a
# different mechanism entirely. It tells you every app ASKS. Whether the answer
# arrives is a device question.
#
# Nor can it see a frame on which an app asks and would not have. Several apps
# return before their Back read in some state: Jaipur and Sea Salt while
# `interactionsReady` is false, which is the whole of an e-ink repaint rather
# than one frame (JaipurActivity.cpp:1462, SeaSaltActivity.cpp:712); xkcd on an
# update frame; Instapaper, Murdle and Connections on their deferred-work
# frames. A Back that lands there is dropped. That is LEFT UNPROVEN ON PURPOSE.
#
# The alternative was a runtime gate in ActivityManager -- do the default back
# when a Back edge existed and nothing read it -- and a cold review killed it on
# evidence rather than taste. It has counterexamples that fire on ordinary use,
# and the worst is not in an app at all: ReaderUtils.h:245 refuses the back
# gesture DELIBERATELY, returning before the button read, so that a right swipe
# can turn a page in swipe mode. Under that gate every swipe page turn would
# have closed the book. ButtonRemapActivity, which must not read Button::Back at
# all, would have closed itself halfway through a remap. "Nobody read the edge"
# detects a QUERY, not a handling, and the screens that most need to refuse Back
# are exactly the ones that refuse it by not asking.
#
# So: dropping a Back on a busy frame is benign, and the cure was worse than the
# disease. What is worth catching is an app that can never answer at all, and
# that is what this suite catches.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

python3 - "$ROOT" <<'PY'
import os
import re
import sys
import tempfile

root = sys.argv[1]
checks = 0
failures = 0


def check(cond, message):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL %s" % message)


# A member function definition: "void ChessActivity::gameLoop() {". Deliberately
# anchored at column zero -- a definition at file scope is never indented, and
# not anchoring it matches lambdas and nested declarations.
DEFN = re.compile(
    r'^[A-Za-z_][A-Za-z0-9_:<>,&*\s]*?\b([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)\s*\(',
    re.M)

# The per-frame input path. A whitelist: unknown names fail closed.
def on_frame_path(name):
    return name in ('loop', 'gameLoop') or name.startswith('route')


BACK_READ = re.compile(r'Button::Back')
# A horizontal wasSwipe() comparison -- a hand-rolled second Back.
SIDEWAYS = re.compile(r'SwipeDir::(Left|Right)')


def functions(text):
    """Yield (name, body) for every member function defined at file scope."""
    for m in DEFN.finditer(text):
        name = m.group(2)
        open_brace = text.find('{', m.end())
        if open_brace < 0:
            continue
        # A ';' before the brace means this was a declaration, not a definition.
        if ';' in text[m.end():open_brace]:
            continue
        depth, i = 0, open_brace
        while i < len(text):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield name, text[open_brace:i + 1]


def offenders(tree):
    """Activities under `tree` with no Button::Back read on the frame path."""
    missing, sideways = [], []
    for dirpath, _dirs, files in os.walk(tree):
        if os.sep + 'ui' in dirpath:
            continue
        for f in sorted(files):
            if not f.endswith('Activity.cpp'):
                continue
            path = os.path.join(dirpath, f)
            text = open(path, encoding='utf-8').read()
            rel = os.path.relpath(path, tree)
            if not any(on_frame_path(n) and BACK_READ.search(b)
                       for n, b in functions(text)):
                missing.append(rel)
            for n, b in functions(text):
                if SIDEWAYS.search(b):
                    sideways.append('%s (%s)' % (rel, n))
    return missing, sideways


# --- the scanner is checked against known shapes before it is believed -------

GOOD = '''
#include "Whatever.h"
void GoodActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
}
'''

# Trivia before the fix, reduced: a Back read exists in the file, on a path that
# only runs while a download has blocked the loop.
BAD = '''
#include "Whatever.h"
void BadActivity::runPackDownload() {
  while (downloading) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) downloadCancel_ = true;
  }
}
void BadActivity::loop() {
  if (!input.touchReleased) return;
  interactions_.route(input);
}
'''

# A hand-rolled second Back out of a horizontal swipe.
PARALLEL = '''
void ParallelActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return;
  if (mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Right) leave();
}
'''

with tempfile.TemporaryDirectory() as tmp:
    apps = os.path.join(tmp, 'apps_local', 'sample')
    os.makedirs(apps)
    open(os.path.join(apps, 'GoodActivity.cpp'), 'w').write(GOOD)
    missing, sideways = offenders(tmp)
    check(missing == [], 'fixture: an activity reading Back in loop() was flagged: %s' % missing)
    check(sideways == [], 'fixture: a clean activity was flagged sideways: %s' % sideways)

    open(os.path.join(apps, 'BadActivity.cpp'), 'w').write(BAD)
    missing, _ = offenders(tmp)
    check(any('BadActivity' in m for m in missing),
          'fixture: Back read only in a download worker was NOT caught -- '
          'the scanner cannot see the bug it exists for')
    check(not any('GoodActivity' in m for m in missing),
          'fixture: the good activity was caught alongside the bad one')

    os.remove(os.path.join(apps, 'BadActivity.cpp'))
    open(os.path.join(apps, 'ParallelActivity.cpp'), 'w').write(PARALLEL)
    _missing, sideways = offenders(tmp)
    check(any('ParallelActivity' in s for s in sideways),
          'fixture: a hand-rolled horizontal back-swipe was NOT caught')

# --- and only now, the tree ---------------------------------------------------

# src/apps_local ONLY, which is this fork's own apps. src/activities is
# upstream's and is shaped differently on purpose: most of those screens inherit
# Back from the UiListActivity / UiTabListActivity bases rather than reading the
# button themselves, and two of them (EpubReaderPercentSelection, BmpViewer)
# consume horizontal swipes legitimately, for scrubbing and panning. Pointing
# this scanner at them produces 29 findings and not one of them is a bug.
missing, sideways = offenders(os.path.join(root, 'src', 'apps_local'))

check(missing == [],
      'these activities never read Button::Back on the per-frame input path, so '
      'a back swipe reaches nothing in them: %s' % ', '.join(missing))
check(sideways == [],
      'these build a back gesture out of a horizontal wasSwipe() instead of '
      'Button::Back, which is a second definition of Back: %s' % ', '.join(sideways))

# The scanner must have had something to scan. A walk that found no activities
# at all -- a moved directory, a renamed suffix -- otherwise reports clean.
scanned = sum(1 for dirpath, _d, files in os.walk(os.path.join(root, "src", "apps_local"))
              for f in files if f.endswith('Activity.cpp') and os.sep + 'ui' not in dirpath)
check(scanned >= 20, 'only %d activities scanned; the walk found nothing to check' % scanned)
print('%d checks, %d failed' % (checks, failures))
sys.exit(1 if failures else 0)
PY
