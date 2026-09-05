#!/bin/bash
# Where a screen is allowed to learn where its content starts.
#
# Card #248 is the third time Mario has reported content sitting on the header,
# and the first two fixes were both correct and both fixed the HEADER. The
# header was never the wrong half. Every screen decides for itself where its
# content begins, and a dozen of them decided it by adding a gutter to
# toybox::kHeaderHeight -- a constant that names the black band and knows
# nothing about the rule drawn under it. The result reads as a screen whose
# content is five pixels under a line, on some screens and not others, which is
# exactly what he described twice.
#
# The fix was structural: headerBand() now reserves the band, the gap AND the
# rule, so screen.body().y is the first row a screen owns and the obvious way of
# laying out a screen is also the correct one. This gate is the half that keeps
# it that way, because "nobody does it any more" is a fact about today.
#
# Three rules, all of them zero-hit as this lands:
#
#   1. Outside src/apps_local/ui/, a band-height name (kHeaderHeight,
#      kHeaderBand, .headerHeight) may not appear in an additive expression.
#      Using it AS a height is fine -- band decorations ride on it -- but adding
#      to it is the bug, every time. A content top comes from screen.body(),
#      from toybox::kChromeHeight, or from toybox::chromeBelow(band).
#   2. Nothing calls toybox::headerRule(). The rule is drawn by headerBand() for
#      every screen; the call is a no-op kept only so a branch written before
#      the change still compiles, and it lands here rather than as a build
#      error that would not say why.
#   3. No app draws its own rule under a band. Solitaire did, on the same pixels
#      headerBand() uses, so it was invisible -- a second copy of the chrome's
#      geometry sitting in an app file waiting for the first one to move.
#
# The clearance itself is measured, not grepped: host-tests/ui renders every
# screen and ~Rendered asserts the first content pixel clears the chrome by a
# gutter. This file is about where the NUMBER comes from; that one is about
# where the pixel lands. Neither catches the other's half.
#
#   host-tests/chromeguard/run.sh
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


def check(label, ok, detail=''):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f'FAIL chromeguard  {label}' + (f': {detail}' if detail else ''))


def strip_comments(text):
    """Comments say kHeaderHeight all the time, and should. Only code counts."""
    out = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    out = re.sub(r'//[^\n]*', '', out)
    return out


# The TOYBOX band, by its own names. Deliberately not a bare `.headerHeight`:
# CrossPoint's own chrome has a metrics.headerHeight of its own, drawn by
# GUI.drawHeader with no toybox band and no rule under it, and the reader,
# keyboard and settings screens are not this fork's to re-layout. Only
# `theme().headerHeight`, which is the toybox Screen's own token, counts.
BAND = r'(?:kHeaderHeight|kHeaderBand|(?:theme\(\)|tokens)\.headerHeight)'
# The band's name with a + or - touching it on either side. `x = kHeaderHeight;`
# and `makeRect(0, 0, w, kHeaderHeight)` are heights and stay legal; anything
# measured FROM the band is not.
ADDS = re.compile(BAND + r'\s*[+-]|[+-]\s*' + BAND)
RULE_CALL = re.compile(r'(?<![A-Za-z0-9_])headerRule\s*\(')
# A full-bleed fill of kRule height placed a gap below something: an app drawing
# the chrome's own line.
HAND_RULE = re.compile(r'fill\(\s*fui::makeRect\(\s*0\s*,[^;]*?' + BAND + r'[^;]*?kRule', re.S)


# A file-local rename of the band, e.g. Solitaire's `constexpr int kHeader =
# kHeaderBand;`. Without this the gate is one alias away from blind, and that is
# not hypothetical: Solitaire had TWO live additive uses through `kHeader` while
# this gate reported all three rules zero-hit. An alias is followed one hop,
# which is the depth real code uses.
ALIAS = re.compile(r'\b(?:constexpr|const)\s+(?:int|int16_t|auto)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*'
                   + BAND + r'\s*;')


def additive_hits(text):
    code = strip_comments(text)
    names = BAND
    for m in ALIAS.finditer(code):
        names += '|' + re.escape(m.group(1))
    adds = re.compile(r'(?:' + names + r')\s*[+-]|[+-]\s*(?:' + names + r')')

    # Statements, not lines. The tree is clang-formatted at 120 columns, so
    # `kHeaderHeight\n    + kGutter * 3` is one expression on two lines and a
    # per-line scan sees an additive use on neither of them.
    found = []
    line_of, buf, start = 1, [], 1
    for n, line in enumerate(code.splitlines(), 1):
        if not buf:
            start = n
        buf.append(line)
        if ';' not in line and '{' not in line and '}' not in line:
            continue
        stmt = ' '.join(buf)
        if adds.search(stmt):
            found.append((start, ' '.join(x.strip() for x in buf).strip()))
        buf = []
        line_of = n
    if buf and adds.search(' '.join(buf)):
        found.append((start, ' '.join(x.strip() for x in buf).strip()))
    return found


def scan(top, fn):
    out = []
    for base, _, names in os.walk(top):
        rel_base = os.path.relpath(base, top).replace(os.sep, '/').strip('./')
        # ToyboxMetrics/Tokens/Screen/Theme ARE the chrome; they define these
        # numbers and must be able to add them together. Written to hold whether
        # the walk starts at src/apps_local (the real run) or a directory with
        # apps_local inside it (the fixture).
        parts = [p for p in rel_base.split('/') if p]
        if 'ui' in parts:
            continue
        for n in names:
            if not n.endswith(('.cpp', '.h', '.hpp', '.cc')):
                continue
            p = os.path.join(base, n)
            text = open(p, encoding='utf-8', errors='replace').read()
            for ln, line in fn(text):
                out.append((os.path.relpath(p, top), ln, line))
    return out


def rule_call_hits(text):
    return [(n, line.strip()) for n, line in enumerate(strip_comments(text).splitlines(), 1)
            if RULE_CALL.search(line)]


def hand_rule_hits(text):
    stripped = strip_comments(text)
    found = []
    for m in HAND_RULE.finditer(stripped):
        found.append((stripped.count('\n', 0, m.start()) + 1, m.group(0).split('\n')[0].strip()))
    return found


# --- the scanners on fixtures, first ---------------------------------------
#
# A scanner that matches nothing looks exactly like a tree with nothing to
# match, and this fork has paid for that difference more than once. Each rule is
# shown its own violation before it is trusted with a clean verdict.
with tempfile.TemporaryDirectory() as fx:
    app = os.path.join(fx, 'apps_local', 'someapp')
    os.makedirs(app)
    open(os.path.join(app, 'bad.cpp'), 'w').write(
        'constexpr int kBodyTop = toybox::kHeaderHeight + toybox::kGutter * 3;\n'
        'int top() { return screen.theme().headerHeight + 8; }\n'
        'void chrome(Screen& s) {\n'
        '  toybox::headerBand(s, props);\n'
        '  toybox::headerRule(s);\n'
        '  target.fill(fui::makeRect(0, kHeaderBand + 4, band.width, toybox::kRule), ink);\n'
        '}\n')
    open(os.path.join(app, 'aliased.cpp'), 'w').write(
        'constexpr int kHeader = kHeaderBand;\n'
        'const int top = kHeader + 34;\n')
    open(os.path.join(app, 'wrapped.cpp'), 'w').write(
        'constexpr int kBodyTop =\n'
        '    toybox::kHeaderHeight + toybox::kGutter * 3;\n')
    open(os.path.join(app, 'fine.cpp'), 'w').write(
        '// kHeaderHeight + kGutter is what this used to say.\n'
        'constexpr int kBodyTop = toybox::kChromeHeight + toybox::kGutter * 3;\n'
        'constexpr int kTopRowY = toybox::chromeBelow(kHeader) + toybox::kGutter;\n'
        'void doors(Screen& s) {\n'
        '  const fui::Rect band = toybox::headerBandRect(s);\n'
        '  s.frame().hit(fui::makeRect(0, 0, w, toybox::kHeaderHeight), Action, 0);\n'
        '  tokens.headerHeight = xkcdui::kHeaderBand;\n'
        '}\n')
    # And the exempt directory really is exempt.
    ui = os.path.join(fx, 'apps_local', 'ui')
    os.makedirs(ui)
    open(os.path.join(ui, 'ToyboxMetrics.h'), 'w').write(
        'constexpr int kChromeHeight = kHeaderHeight + kBandRuleGap + kRule;\n')

    got = sorted((f, ln) for f, ln, _ in scan(fx, additive_hits))
    # bad.cpp line 6 too: a hand-drawn rule is also an additive use of the band,
    # and it is caught by both rules rather than by neither. aliased.cpp is the
    # rename Solitaire actually used, and wrapped.cpp is the same expression
    # split by the formatter -- both passed the first version of this gate.
    want = sorted([('apps_local/someapp/bad.cpp', 1), ('apps_local/someapp/bad.cpp', 2),
                   ('apps_local/someapp/bad.cpp', 6), ('apps_local/someapp/aliased.cpp', 2),
                   ('apps_local/someapp/wrapped.cpp', 1)])
    check('the scanner sees a body top measured from the band, aliased or wrapped',
          got == want, repr(got))
    got = [(f, ln) for f, ln, _ in scan(fx, rule_call_hits)]
    check('the scanner sees a headerRule() call', got == [('apps_local/someapp/bad.cpp', 5)], repr(got))
    got = [(f, ln) for f, ln, _ in scan(fx, hand_rule_hits)]
    check('the scanner sees a hand-drawn rule', got == [('apps_local/someapp/bad.cpp', 6)], repr(got))

# --- the real tree ----------------------------------------------------------
#
# src/apps_local/ only: these are the screens built on toybox chrome. Everything
# else under src/ is CrossPoint's, drawn by its own GUI with its own header and
# no rule, and this fork does not re-layout the reader.
src = os.path.join(root, 'src', 'apps_local')

adds = scan(src, additive_hits)
check('no screen measures its content top from the header band', not adds,
      '\n  ' + '\n  '.join(f'src/apps_local/{f}:{ln}: {line}' for f, ln, line in adds) +
      '\n  the first row a screen owns is screen.body().y (headerBand reserves the whole chrome),'
      '\n  or toybox::kChromeHeight / toybox::chromeBelow(band) where there is no Screen to ask')

calls = scan(src, rule_call_hits)
check('nothing calls headerRule(); headerBand draws the rule', not calls,
      '\n  ' + '\n  '.join(f'src/apps_local/{f}:{ln}: {line}' for f, ln, line in calls) +
      '\n  delete the call: toybox::headerBand() has drawn the rule for every screen since card #248')

hand = scan(src, hand_rule_hits)
check('no app draws its own rule under the band', not hand,
      '\n  ' + '\n  '.join(f'src/apps_local/{f}:{ln}: {line}' for f, ln, line in hand) +
      '\n  headerBand() draws it, at exactly those pixels')

# --- the scanner walked real code -------------------------------------------
#
# A clean verdict over an empty read is the failure this fork names most often.
# The fork's own answer has to be present in numbers, or the walk found nothing
# at all and said so as a pass.
chrome_uses = 0
band_uses = 0
for base, _, names in os.walk(src):
    for n in names:
        if not n.endswith(('.cpp', '.h')):
            continue
        text = strip_comments(open(os.path.join(base, n), encoding='utf-8', errors='replace').read())
        chrome_uses += text.count('kChromeHeight') + text.count('chromeBelow(')
        band_uses += len(re.findall(BAND, text))
check('the tree measures from the chrome (the scanner walked real code)', chrome_uses >= 8,
      f'{chrome_uses} uses of kChromeHeight/chromeBelow')
check('the tree still names the band somewhere (the scanner is not reading an empty tree)',
      band_uses >= 5, f'{band_uses} mentions')

print(f'{checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
PY
