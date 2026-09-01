#!/usr/bin/env python3
"""Measure every unwrapped UI string against the panel width."""

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools_local" / "i18n"))
from measure_string import load_font, measure  # noqa: E402

PANEL_WIDTH = 480
# "Close enough that a reword moves it." Shared by the survey and the report so
# the two cannot disagree about what counts as a near miss.
NEAR_MARGIN = 15
FONTS = REPO / "lib/EpdFont/builtinFonts"

# Slot -> face files. From src/main.cpp, where UI_10_FONT_ID binds to
# ui10FontFamily and UI_12_FONT_ID to ui12FontFamily. Verified, not assumed.
FACES = {
    ("10", False): FONTS / "ubuntu_10_regular.h",
    ("10", True): FONTS / "ubuntu_10_bold.h",
    ("12", False): FONTS / "ubuntu_12_regular.h",
    ("12", True): FONTS / "ubuntu_12_bold.h",
}

# drawCenteredText(<font id>, ..., tr(STR_X) [, ..., EpdFontFamily::BOLD])
# The style argument is captured because BOLD is about 5% wider: a gate that
# measured only the regular face would under-measure twelve settings call sites,
# which is exactly enough to license a string that then overflows.
CALL = re.compile(
    r"drawCenteredText\((?P<args>[^;]*?UI_(?P<slot>\d+)_FONT_ID[^;]*?tr\((?P<key>STR_[A-Z0-9_]+)\)[^;]*?)\)",
    re.S,
)


def translations(path=None):
    out = {}
    path = path or REPO / "lib/I18n/translations/english.yaml"
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r'^(STR_[A-Z0-9_]+):\s*"(.*)"\s*$', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def sites():
    found = []
    for path in sorted((REPO / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in CALL.finditer(text):
            bold = "EpdFontFamily::BOLD" in m.group("args")
            line = text[: m.start()].count("\n") + 1
            found.append(
                (
                    m.group("slot"),
                    bold,
                    m.group("key"),
                    "%s:%d" % (path.relative_to(REPO), line),
                )
            )
    return found


def survey():
    """Every language, reported and NOT gated. Most of them have at least one
    overflowing unwrapped string -- a live shipping defect on the recovery and
    sync screens -- and English is one of the few that fit, which is exactly why
    a green English gate must not be read as "it fits everywhere".

    No count is written here on purpose. This docstring said "26 of 32" while
    the code below measured 25, written hours apart, which is precisely the
    mistake the printed output warns about: quote the measurement, not a number
    someone typed. Run it.

    Fixing it is a translator pass, not a release task, so this reports rather
    than fails. docs/translators.md carries the constraint.
    """
    faces = {k: load_font(v) for k, v in FACES.items() if v.exists()}
    keys = {(slot, bold, key) for slot, bold, key, _ in sites()}
    rows = []
    for path in sorted((REPO / "lib/I18n/translations").glob("*.yaml")):
        strings = translations(path)
        worst = None
        near = None
        for slot, bold, key in keys:
            face = faces.get((slot, bold))
            if face is None or key not in strings:
                continue
            width, _ = measure(strings[key], *face)
            if width > PANEL_WIDTH and (worst is None or width > worst[1]):
                worst = (key, width)
            elif width <= PANEL_WIDTH and (near is None or width > near):
                near = width
        rows.append((path.stem, worst, near))
    over = [r for r in rows if r[1]]
    print("all-language survey (reported, not gated)")
    for name, worst, _ in sorted(over, key=lambda r: -r[1][1]):
        print(
            "  %-12s worst %7.1fpx > %dpx  %s" % (name, worst[1], PANEL_WIDTH, worst[0])
        )
    print(
        "  %d of %d languages have at least one overflowing unwrapped string"
        % (len(over), len(rows))
    )
    # This count is a READING, not a property of the codebase. Several languages
    # sit within a few pixels of the limit, so rewording one string in one
    # language moves it. Quote the measurement, never the number.
    margin = 15
    edge = sorted(
        (PANEL_WIDTH - r[2], r[0])
        for r in rows
        if not r[1] and r[2] and PANEL_WIDTH - r[2] <= margin
    )
    if edge:
        print(
            "  %d more are within %dpx of the limit, so this count MOVES when any"
            % (len(edge), margin)
        )
        print("  string in any language is reworded -- re-measure, do not quote:")
        for gap, name in edge:
            print("    %-12s %5.1fpx of margin" % (name, gap))
    return 0


def report():
    """Emit docs/i18n-overflow.md: every overflowing key, per language.

    The survey above names only each language's WORST key, which is the right
    shape for a one-line status and useless to the person who has to fix it --
    a translator needs the list, the measurement and the call site.

    THREE STATES, NOT TWO, and the third is why this cannot just print widths.
    A character the face has no glyph for draws as NOTHING and costs ZERO
    width, so a string in a script the face does not carry measures narrow and
    reads as a comfortable fit. Arabic is that case here: 52 of its 54 keys
    contain characters with no glyph, STR_POWER_ON_HINT measures 38.5px
    against English's 421.2px, and every existing count therefore lists Arabic
    among the languages that fit. It does not fit; it does not render. Folding
    that into "fits" is a measurement that is correct and about something else.
    """
    faces = {k: load_font(v) for k, v in FACES.items() if v.exists()}
    if not faces:
        print("no font headers found under %s" % FONTS)
        return 1
    # A key can be drawn from more than one place; keep every site so the
    # report names them all rather than an arbitrary first.
    where = {}
    for slot, bold, key, site in sites():
        where.setdefault((slot, bold, key), []).append(site)

    langs = []
    for path in sorted((REPO / "lib/I18n/translations").glob("*.yaml")):
        strings = translations(path)
        over, near, blind = [], [], []
        for (slot, bold, key), sites_ in sorted(where.items()):
            face = faces.get((slot, bold))
            if face is None or key not in strings:
                continue
            width, gaps = measure(strings[key], *face)
            row = (
                key,
                width,
                slot,
                bold,
                sites_,
                strings[key],
                "".join(sorted(set(gaps))),
            )
            if gaps:
                blind.append(row)
            elif width > PANEL_WIDTH:
                over.append(row)
            elif PANEL_WIDTH - width <= NEAR_MARGIN:
                near.append(row)
        langs.append((path.stem, over, near, blind))

    worst = lambda L: max([r[1] for r in L[1]], default=0)
    langs.sort(key=lambda L: (-worst(L), L[0]))
    over_langs = [L for L in langs if L[1]]
    blind_langs = [L for L in langs if L[3]]

    out = []
    w = out.append
    w("# Unwrapped strings that do not fit the panel")
    w("")
    w("GENERATED -- do not hand-edit. Regenerate with:")
    w("")
    w("    host-tests/i18nwidth/run.sh --report > docs/i18n-overflow.md")
    w("")
    w("`renderer.drawCenteredText` draws ONE line and does not wrap, so a string")
    w(
        "wider than the %dpx panel runs off the edge. Widths below are real advance"
        % PANEL_WIDTH
    )
    w("widths read from the generated font headers, measured in the face and weight")
    w(
        "each call site actually uses -- %d of the %d sites pass BOLD, which is about"
        % (sum(1 for k in where if k[1]), len(where))
    )
    w("5% wider, and measuring those in regular would under-report them.")
    w("")
    w("**Every number here is a reading, not a property of the project.** Rewording")
    w("one string moves it. Re-measure rather than quoting these figures later.")
    w("")
    w("Fixing them is a translator pass and needs judgement about meaning: see")
    w("`docs/translators.md`. The budget is %dpx, not a character count." % PANEL_WIDTH)
    w("")
    w("## Summary")
    w("")
    w(
        "| language | overflowing | worst | within %dpx of the edge | unmeasurable |"
        % NEAR_MARGIN
    )
    w("| --- | ---: | ---: | ---: | ---: |")
    for name, over, near, blind in langs:
        if not (over or near or blind):
            continue
        w(
            "| %s | %d | %s | %d | %s |"
            % (
                name,
                len(over),
                "%.1fpx" % max([r[1] for r in over], default=0) if over else "-",
                len(near),
                len(blind) or "-",
            )
        )
    w("")
    w(
        "%d of %d languages have at least one overflowing unwrapped string."
        % (len(over_langs), len(langs))
    )
    if blind_langs:
        w("")
        w("## Not measurable, and worse than overflowing")
        w("")
        w("These keys contain characters the face has no glyph for. Such characters")
        w("draw as nothing and cost zero width, so the string measures NARROW and")
        w("would otherwise be reported as fitting. It is not too long -- it does not")
        w("render. A width is not meaningful for these and none is given.")
        w("")
        w("The first count below is the WHOLE translation, not the unwrapped subset")
        w('this report otherwise measures. "Does this language render at all" is not')
        w("a question about one screen, and the subset understates it badly.")
        w("")
        regular = faces.get(("10", False))
        for name, _, _, blind in blind_langs:
            allstr = translations(REPO / "lib/I18n/translations" / (name + ".yaml"))
            whole = sum(
                1 for v in allstr.values() if regular and measure(v, *regular)[1]
            )
            w(
                "- **%s**: %d of %d keys in the whole translation; %d of the %d unwrapped"
                % (name, whole, len(allstr), len(blind), len(where))
            )
            w(
                "  keys measured here. Characters with no glyph include: `%s`"
                % "".join(sorted({c for r in blind for c in r[6]}))[:40]
            )
        w("")
        w("This is not a property of non-Latin scripts. Hebrew is measured in the same")
        w("face and renders, which is what makes this a font problem with a name.")
        w("")
        w("**It is reachable, not latent.** `scripts/gen_i18n.py` builds the language")
        w("table from a glob of `lib/I18n/translations/*.yaml` and requires every file")
        w("to declare `_language_name`, so any translation present is offered in the")
        w("picker. Selecting one whose script the face lacks gives a near-blank")
        w("interface -- including the menu needed to change it back.")
        w("")
        w("A font carrying the script is the fix. No translator can shorten a string")
        w("out of this, and asking them to try wastes their time.")
    w("")
    w("## Every overflowing string, worst first")
    for name, over, near, _ in langs:
        if not over:
            continue
        w("")
        w("### %s" % name)
        w("")
        w("| key | width | over by | face | call site |")
        w("| --- | ---: | ---: | --- | --- |")
        for key, width, slot, bold, sites_, text, _g in sorted(
            over, key=lambda r: -r[1]
        ):
            w(
                "| `%s` | %.1fpx | +%.1f | UI_%s%s | %s |"
                % (
                    key,
                    width,
                    width - PANEL_WIDTH,
                    slot,
                    " BOLD" if bold else "",
                    ", ".join(sites_),
                )
            )
        for key, width, _s, _b, _si, text, _g in sorted(over, key=lambda r: -r[1]):
            w("")
            w("`%s` (%.1fpx):" % (key, width))
            w("")
            w("> %s" % text)
    edge = [(n, near) for n, _o, near, _b in langs if near]
    if edge:
        w("")
        w("## Within %dpx of the edge" % NEAR_MARGIN)
        w("")
        w("Not broken today. Listed because the set above is one reword away from")
        w("including them, which is the reason not to treat any count here as settled.")
        w("")
        w("| language | key | width | margin |")
        w("| --- | --- | ---: | ---: |")
        for name, near in edge:
            for key, width, _s, _b, _si, _t, _g in sorted(near, key=lambda r: -r[1]):
                w(
                    "| %s | `%s` | %.1fpx | %.1fpx |"
                    % (name, key, width, PANEL_WIDTH - width)
                )
    print("\n".join(out))
    return 0


def main():
    if "--all-languages" in sys.argv:
        return survey()
    if "--report" in sys.argv:
        return report()
    strings = translations()
    faces = {k: load_font(v) for k, v in FACES.items() if v.exists()}
    if not faces:
        print("FAIL no font headers found under %s" % FONTS)
        return 1

    seen, failures, missing = set(), [], []
    print("unwrapped screen strings")
    for slot, bold, key, where in sites():
        if (slot, bold, key) in seen:
            continue
        seen.add((slot, bold, key))
        face = faces.get((slot, bold))
        if face is None or key not in strings:
            continue
        glyphs, intervals = face
        # measure() reports characters the face has no glyph for. They draw as
        # NOTHING AT ALL, so without this they would cost zero width and a
        # broken string would measure as a comfortable fit.
        width, gaps = measure(strings[key], glyphs, intervals)
        if gaps:
            missing.append((key, where, "".join(sorted(set(gaps)))))
        if width > PANEL_WIDTH:
            failures.append((key, where, width, slot, bold))

    for key, where, width, slot, bold in sorted(failures, key=lambda r: -r[2]):
        print(
            "  FAIL %-32s %7.1fpx > %dpx  UI_%s%s  %s"
            % (key, width, PANEL_WIDTH, slot, " BOLD" if bold else "", where)
        )
    for key, where, chars in missing:
        print("  FAIL %-32s has no glyph for %r  %s" % (key, chars, where))

    total = len(seen)
    bad = len(failures) + len(missing)
    if not bad:
        # Say what this does NOT cover. The gate reads english.yaml and most
        # translations have an overflowing unwrapped string, so a bare "all fit"
        # would certify a defect rather than catch it. Deliberately no count:
        # --all-languages measures it, and a literal written here rots. This
        # comment said 26 while the tool measured 25, hours apart.
        print(
            "  ok   %d unwrapped strings all fit %dpx -- ENGLISH ONLY"
            % (total, PANEL_WIDTH)
        )
        print("       other languages are NOT gated: run with --all-languages")
    print("%d checks, %d failed" % (total, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
