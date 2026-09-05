#!/bin/bash
# A folded margin handed to plain setContentMargin() applies the safe area
# twice. docs/bezel-insets.md says never to do it, and the rule did not stop
# it: upstream's convention is the opposite (their contentMargin is measured
# FROM the safe rect), so every sync imports screens written their way, and
# upstream's new KeyboardLayoutsActivity arrived built from
# getScreenSafeArea() and passed to plain setContentMargin(): 10px low and
# 1px narrow on the X4 Pro (card 145, fixed on app/upsync). The ui host suite
# cannot see it (it constructs a DeviceContext with an empty safeArea, so
# twice nothing is nothing) and the simulator shows the X4's smaller insets.
#
# So this reads the source. A plain setContentMargin( call whose argument
# list mentions safe., getScreenSafeArea or safeRect is the folded-margin
# signature exactly; it is zero-hit today and fires only on something newly
# imported. The scanner is run on a fixture first, so a scanner that matches
# nothing cannot pass as a tree with nothing to match.
#
#   host-tests/marginguard/run.sh
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
failed = 0

CALL = re.compile(r'(?<![A-Za-z0-9_])setContentMargin\(')
FOLDED = re.compile(r'safe\.|getScreenSafeArea|safeRect')
DECL = re.compile(r'\b(void|auto|int|bool)\s+setContentMargin\(|::setContentMargin\(')


def arguments(text, start):
    """The argument text of the call whose '(' is at text[start - 1]."""
    depth, i = 1, start
    while i < len(text) and depth:
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        i += 1
    return text[start:i - 1]


def hits(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    found = []
    for m in CALL.finditer(text):
        line_start = text.rfind('\n', 0, m.start()) + 1
        line = text[line_start:text.find('\n', m.start())]
        if DECL.search(line):
            continue
        args = arguments(text, m.end())
        if FOLDED.search(args):
            found.append((text.count('\n', 0, m.start()) + 1, line.strip()))
    return found


def scan(top):
    out = []
    for base, _, names in os.walk(top):
        for n in names:
            if n.endswith(('.cpp', '.h', '.hpp', '.cc')):
                p = os.path.join(base, n)
                for ln, line in hits(p):
                    out.append((os.path.relpath(p, top), ln, line))
    return out


def check(label, ok, detail=''):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f'FAIL marginguard  {label}' + (f': {detail}' if detail else ''))


# The scanner on a fixture: it must see the fold, and only the fold.
with tempfile.TemporaryDirectory() as fx:
    open(os.path.join(fx, 'folded.cpp'), 'w').write(
        'void A::onEnter() {\n'
        '  const auto safe = getScreenSafeArea();\n'
        '  setContentMargin(safe.top + 4,\n'
        '                   safe.right, safe.bottom, safe.left);\n'
        '}\n')
    open(os.path.join(fx, 'fine.cpp'), 'w').write(
        'void B::onEnter() {\n'
        '  const auto safe = getScreenSafeArea();\n'
        '  setContentMarginFromScreen(safe.top, safe.right, safe.bottom, safe.left);\n'
        '  setContentMarginAbsolute(safeRect().top, 1, 0, 1);\n'
        '  setContentMargin(10, 1, 0, 1);\n'
        '}\n'
        'void Screen::setContentMargin(int t, int r, int b, int l) { safe_ = t; }\n')
    got = scan(fx)
    check('the scanner sees a folded margin split over two lines',
          [(f, ln) for f, ln, _ in got] == [('folded.cpp', 3)], repr(got))

# The real tree. Zero today; anything here is a screen newly written or
# imported to upstream's convention, and its fix is setContentMarginFromScreen.
real = []
for top in ('src', 'lib'):
    real += [(f'{top}/{f}', ln, line) for f, ln, line in scan(os.path.join(root, top))]
check('no plain setContentMargin() call is handed a folded margin', not real,
      '\n  ' + '\n  '.join(f'{f}:{ln}: {line}' for f, ln, line in real) +
      '\n  a margin built from the safe area goes to setContentMarginFromScreen() (docs/bezel-insets.md)')

# The scanner reads real code, not an empty tree: the fork's own convention
# is present in numbers.
from_screen = 0
for top in ('src', 'lib'):
    for base, _, names in os.walk(os.path.join(root, top)):
        for n in names:
            if n.endswith(('.cpp', '.h')):
                from_screen += open(os.path.join(base, n), encoding='utf-8', errors='replace').read().count('setContentMarginFromScreen(')
check('the tree still uses setContentMarginFromScreen (the scanner walked real code)', from_screen >= 10, f'{from_screen} call sites')

print(f'{checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
PY
