#!/usr/bin/env python3
"""Every snprintf into a fixed char buffer in src/apps_local/, checked by hand.

WHY THIS EXISTS AND NOT JUST THE COMPILER FLAG
==============================================
Card 256 removed -Wno-format-truncation from three suites so GCC would catch
this class. It does not, at the level the suites run:

    -O0 (what every host suite compiles at)   0 warnings
    -O1                                       1
    -O2                                       3
    -O3                                       6

-Wformat-truncation is a middle-end warning. At -O0 GCC has almost no
value-range information and reports only buffers too small for ANY argument.
Forty-one undersized buffers sat in the very files card 256 edited and produced
not one warning. A flag whose effectiveness depends on an optimisation level
nobody set is not a gate.

So this reads the source instead. It is deterministic, needs no optimiser, no
device and no GCC, and it fails on arithmetic rather than on whether some pass
happened to run.

THE RULE
========
A buffer must hold what its format can PRINT, not what today's values happen to
be. `%d` is eleven characters ("-2147483648"). snprintf does not overrun, it
CUTS, so one byte short is a silently shortened string on the panel -- and a
tile shows the whole text, no exceptions.

WHAT IT DELIBERATELY DOES NOT CHECK
===================================
A `%s` whose argument is not a fixed buffer visible at the call site: its length
is a runtime fact this cannot see. Those are listed as UNCHECKED at the end
rather than passed silently, because a clean list that hides absence is how the
last audit reported clean.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2] / "src" / "apps_local"

# The only sites allowed to be short, and only because somebody else is already
# fixing them: ConnectionsScreens.cpp is card 236, fixed on app/tilesize, and
# this branch must not touch that file. Each entry must STILL BE FAILING -- if
# one starts passing the gate says so and fails, so this list cannot quietly
# outlive the work it is waiting on.
KNOWN = {
    "src/apps_local/connections/ConnectionsScreens.cpp:380": "card 236, app/tilesize",
    "src/apps_local/connections/ConnectionsScreens.cpp:598": "card 236, app/tilesize",
    "src/apps_local/connections/ConnectionsScreens.cpp:615": "card 236, app/tilesize",
    "src/apps_local/connections/ConnectionsScreens.cpp:720": "card 236, app/tilesize",
}
FORMAT_H = ROOT / "ui" / "ToyboxFormat.h"

# The widest text each conversion can produce ON THE TARGET, which is a 32-bit
# ESP32-S3: `long` is four bytes there, so "%lu" is ten digits and "%lx" eight.
# A buffer sized from toybox::kULongChars adapts to whichever machine compiles
# it and is right on both; a buffer sized with a literal is judged against the
# device, because the device is what ships.
WIDTH = {
    "d": 11, "i": 11, "u": 10, "x": 8, "X": 8, "o": 11, "c": 1,
    "ld": 11, "li": 11, "lu": 10, "lx": 8, "lld": 20, "llu": 20, "zu": 20,
}

DECL = re.compile(r"\bchar\s+(\w+)\s*\[\s*([^\];]+?)\s*\]\s*(?:\[[^\]]*\]\s*)?[=;]")
CALL = re.compile(r"\bsn?printf\s*\(\s*([\w.\->\[\]]+?)\s*,\s*([^,]+?)\s*,\s*\"((?:[^\"\\]|\\.)*)\"")
CONST = re.compile(r"constexpr\s+(?:int|size_t|std::size_t|uint\d+_t)\s+(\w+)\s*=\s*([^;]+);")


def shared_constants():
    """kIntChars and friends, evaluated once from the header that defines them."""
    text = FORMAT_H.read_text()
    env = {}
    env["kIntChars"] = 11
    env["kUIntChars"] = 10
    env["kULongChars"] = 10  # the device's 32-bit unsigned long
    for name, expr in CONST.findall(text):
        if name in env:
            continue
        value = evaluate(expr, env, text)
        if value is not None:
            env[name] = value
    return env


LITERAL_CHARS = re.compile(r"(?:toybox::)?literalChars\(\s*\"((?:[^\"\\]|\\.)*)\"\s*\)")


def unescape(s):
    return s.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\")


def evaluate(expr, env, _text=""):
    """Fold a constexpr size expression down to an int, or None if it cannot."""
    expr = expr.strip()
    expr = LITERAL_CHARS.sub(lambda m: str(len(unescape(m.group(1)))), expr)
    expr = expr.replace("toybox::", "").replace("murdletext::", "")
    expr = re.sub(r"//[^\n]*", "", expr)
    expr = re.sub(r"/\*.*?\*/", "", expr, flags=re.S)
    for name, value in env.items():
        expr = re.sub(rf"\b{re.escape(name)}\b", str(value), expr)
    if not re.fullmatch(r"[\d\s+\-*/()]+", expr or "x"):
        return None
    try:
        return int(eval(expr))  # noqa: S307 -- the regex above admits arithmetic only
    except (SyntaxError, ZeroDivisionError, TypeError, ValueError):
        return None


def format_need(fmt, buffers, args):
    """(bytes the format can produce including the terminator, unchecked %s count)"""
    i, need, unchecked = 0, 1, 0
    arg_index = 0
    while i < len(fmt):
        ch = fmt[i]
        if ch == "\\":
            need += 1
            i += 2
            continue
        if ch != "%":
            need += 1
            i += 1
            continue
        j = i + 1
        while j < len(fmt) and fmt[j] in "-+ #0123456789.*":
            j += 1
        while j < len(fmt) and fmt[j] in "lhzjt":
            j += 1
        if j >= len(fmt):
            break
        conv = fmt[j]
        if conv == "%":
            need += 1
            i = j + 1
            continue
        if conv == "s":
            arg = args[arg_index] if arg_index < len(args) else ""
            bound = buffers.get(arg.strip())
            if bound is None:
                unchecked += 1
            else:
                need += bound - 1
        elif conv in "fFeEgG":
            unchecked += 1
        else:
            key = (fmt[i + 1:j] + conv).lstrip("-+ #0123456789.*")
            need += WIDTH.get(key, WIDTH.get(conv, 11))
        arg_index += 1
        i = j + 1
    return need, unchecked


def main():
    env = shared_constants()
    failures = []
    collisions = []
    waived = set()
    unchecked = []
    checked = 0

    for path in sorted(ROOT.rglob("*.cpp")) + sorted(ROOT.rglob("*.h")):
        src = path.read_text()
        local = dict(env)
        # Only constants that actually size a buffer here matter; an unrelated
        # kCols defined twice in two scopes is not this gate's business.
        sizing = set(re.findall(r"\bchar\s+\w+\s*\[\s*(\w+)\s*\]", src))
        seen = {}
        for name, expr in CONST.findall(src):
            value = evaluate(expr, local, src)
            if value is None:
                continue
            # Two constants of the same name in one file, in different scopes,
            # would leave this resolving buffers against the wrong one and
            # reporting a confident wrong number. Refuse instead: an instrument
            # that answers from the wrong scope is worse than one that stops.
            if name in sizing and name in seen and seen[name] != value:
                collisions.append(f"{path.relative_to(ROOT.parents[1])}: two different `{name}` "
                                  f"({seen[name]} and {value}) -- rename one; this cannot tell them apart")
            seen[name] = value
            local[name] = value
        sizes = {}
        for m in DECL.finditer(src):
            value = evaluate(m.group(2), local, src)
            if value is not None:
                sizes.setdefault(m.group(1), []).append((m.start(), value))
        for m in CALL.finditer(src):
            buf, fmt = m.group(1), m.group(3)
            decls = [d for d in sizes.get(buf, []) if d[0] < m.start()]
            if not decls:
                continue
            size = decls[-1][1]
            tail = src[m.end():m.end() + 400]
            args = [a.strip() for a in tail.lstrip().lstrip(",").split(")")[0].split(",")]
            visible = {name: pair[-1][1] for name, pair in sizes.items() if pair}
            need, unk = format_need(fmt, visible, args)
            line = src[:m.start()].count("\n") + 1
            where = f"{path.relative_to(ROOT.parents[1])}:{line}"
            if unk:
                unchecked.append(f"{where}  {buf}[{size}]  \"{fmt}\"  ({unk} unbounded argument(s))")
                continue
            checked += 1
            if need > size:
                if where in KNOWN:
                    waived.add(where)
                else:
                    failures.append(f"{where}  {buf}[{size}] but \"{fmt}\" can print {need}")

    for site, why in sorted(KNOWN.items()):
        if site not in waived:
            failures.append(f"{site} is in KNOWN ({why}) but is not short any more -- delete the entry")
    for c in collisions:
        print(f"FAIL  {c}")
    for f in failures:
        print(f"FAIL  {f}")
    print(f"\n{checked} snprintf-into-fixed-buffer sites checked, {len(failures)} too small, "
          f"{len(collisions)} unresolvable, {len(waived)} waived to another branch")
    print(f"{len(unchecked)} not checkable from the source (a %s or %f whose width is a runtime fact):")
    for u in unchecked:
        print(f"  {u}")
    return 1 if (failures or collisions) else 0


if __name__ == "__main__":
    sys.exit(main())
