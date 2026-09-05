#!/usr/bin/env python3
"""Emit the corpora this suite walks, DERIVED from where they really live.

Two of the five corpora cannot be reached from the test's own C++: the link
game titles are one string per game Activity header, and the xkcd titles are in
a pack on the card rather than in the repository. Everything else the test
calls directly (forehead::kCategories, toybattle::terrainAt, the dungeon guide
and Connections' own formatDate), because a corpus copied into a test is a
corpus that stops matching the app the day somebody adds a game.

    host-tests/fittedtitle/corpus.py <out.h>

The xkcd pack is looked for in $XKCD_PACK, then the workspace card, then the
emulator's small canned pack. WHICH ONE WAS USED IS COMPILED IN and printed by
the suite: a run over 32 comics and a run over 3279 are not the same evidence,
and a suite that did not say which it had would let the small one pass for the
big one.
"""

import os
import pathlib
import re
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]


def c_string(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 0x20 <= ord(ch) < 0x7F:
            out.append(ch)
        else:
            out.append("\\x%02x" % ord(ch))
    return '"%s"' % "".join(out)


def link_titles():
    """Every game that can show the link screen, from its own Activity header."""
    found = []
    pattern = re.compile(r'linkGameTitle\(\)\s*const\s*override\s*\{\s*return\s*"([^"]*)"')
    for path in sorted((REPO / "src" / "apps_local").rglob("*.h")):
        for m in pattern.finditer(path.read_text(encoding="utf-8", errors="replace")):
            found.append(m.group(1))
    return sorted(set(found))


def pack_format_version():
    """The layout XkcdCore.h reads, so this parser and the device agree."""
    src = (REPO / "src/apps_local/xkcd/XkcdCore.h").read_text()
    m = re.search(r"kFormatVersion\s*=\s*(\d+)", src)
    if not m:
        sys.exit("cannot find kFormatVersion in XkcdCore.h")
    return int(m.group(1))


def xkcd_pack():
    """-> (label, [titles]). Empty list when no pack is reachable."""
    candidates = []
    if os.environ.get("XKCD_PACK"):
        candidates.append(pathlib.Path(os.environ["XKCD_PACK"]))
    # The workspace card. Walking up from this checkout finds it from a normal
    # worktree and NOT from the throwaway one check.sh --committed builds in,
    # which lives under TMPDIR -- so the gate would silently fall through to the
    # 32-comic canned pack while a developer's run walked 3279. Asking git for
    # the common directory finds the real repository from either, and its
    # parents are where the card sits.
    roots = [REPO]
    try:
        common = subprocess.run(
            ["git", "-C", str(REPO), "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        if common:
            roots.append(pathlib.Path(common))
    except Exception:  # no git, no repo, no matter: the in-repo pack still answers
        pass
    for root in roots:
        for up in list(root.parents)[:4]:
            candidates.append(up / "fs_mario" / "xkcd")
            candidates.append(up / "fs_agent" / "xkcd")
    candidates.append(REPO / "tools_local" / "wasm" / "sdcard" / "xkcd")

    for pack in candidates:
        index = pack / "index.dat"
        text = pack / "text.dat"
        if not index.is_file() or not text.is_file():
            continue
        idx = index.read_bytes()
        txt = text.read_bytes()
        if len(idx) < 16:
            continue
        magic, version, _reserved, count, _max = struct.unpack_from("<IHHII", idx, 0)
        # The version is CHECKED, not read for the label. This parser knows one
        # record layout, and a pack of another version is not a smaller corpus,
        # it is garbage that looks like one: the canned v2 pack in the repo has
        # 32 records and this walked it into 2 "titles" and reported them as a
        # corpus. The device rejects a stale pack whole (XkcdCore.h,
        # kFormatVersion) and so does this, and the number it rejects on is read
        # from that header rather than typed here.
        if magic != 0x44434B58 or version != pack_format_version():
            continue
        titles = []
        for i in range(count):
            at = 16 + i * 40
            if at + 40 > len(idx):
                break
            (text_offset,) = struct.unpack_from("<I", idx, at + 16)
            end = txt.find(b"\0", text_offset)
            if end < 0:
                break
            titles.append(txt[text_offset:end].decode("ascii", "replace"))
        if titles:
            return ("%s (format v%d)" % (pack, version), titles)
    return ("NO PACK FOUND (none reachable at format v%d)" % pack_format_version(), [])


def bindings():
    """Which cut each font id really is, and which three each Faces set binds.

    BOTH ARE DERIVED, and that is not fussiness. A test that wrote down
    "kDisplayFontId is toybox_30" would keep passing after somebody rebound it,
    measuring one face while the app drew another -- and the whole value of
    this suite is that it measures the real cut. ToyboxFonts.cpp's
    insertFont() calls and ToyboxTheme.h's Faces functions are the two places
    those facts exist, so they are the two places these are read from.
    """
    fonts_h = (REPO / "src/apps_local/ui/ToyboxFonts.h").read_text()
    fonts_src = (REPO / "src/apps_local/ui/ToyboxFonts.cpp").read_text()
    theme_src = (REPO / "src/apps_local/ui/ToyboxTheme.h").read_text()

    data_of = dict(re.findall(r"^EpdFont\s+(\w+)\(&(\w+)\);", fonts_src, re.M))
    font_of = dict(re.findall(r"^EpdFontFamily\s+(\w+)\(&(\w+)\);", fonts_src, re.M))
    ids = []
    for font_id, family in re.findall(r"insertFont\((k\w+),\s*(\w+)\)", fonts_src):
        ids.append((font_id, data_of[font_of[family]]))

    struct = re.search(r"struct Faces \{([^}]*)\}", theme_src).group(1)
    default = re.findall(r"(\w+)\s*=\s*(k\w+);", struct)
    default = [v for _, v in default]
    faces = []
    for name, args in re.findall(r"inline Faces (\w+)\(\)\s*\{\s*return Faces\{([^}]*)\};", theme_src):
        slots = [a.strip() for a in args.split(",") if a.strip()] or default
        if len(slots) != 3:
            sys.exit("Faces %s does not name three slots: %r" % (name, args))
        faces.append((name, slots))
    # The id VALUES too: ToyboxFonts.h declares them beside a GfxRenderer
    # include, so a freestanding suite cannot include it to learn them.
    values = re.findall(r"constexpr int (k\w+FontId) = (0x[0-9A-Fa-f']+);", fonts_h)
    values = [(name, value.replace("'", "")) for name, value in values]
    return ids, faces, values


def main():
    out = pathlib.Path(sys.argv[1])
    link = link_titles()
    label, xkcd = xkcd_pack()
    ids, faces, values = bindings()

    lines = [
        "// Generated by host-tests/fittedtitle/corpus.py. Do not edit; do not commit.",
        "#pragma once",
        "",
        "// Its own font includes, because it cannot rely on being included after",
        "// them: clang-format sorts an include block alphabetically, so a source",
        "// file's comment promising an order is a comment the formatter deletes the",
        "// meaning of. (It did, and the build said 'undeclared identifier",
        "// instrument_24' with nothing pointing at the formatter.)",
    ]
    for face in sorted({data for _id, data in ids}):
        lines.append('#include "fonts/%s.h"' % face)
    lines += [
        "",
        "namespace fitted {",
        "",
        "inline constexpr const char* kLinkGameTitles[] = {",
    ]
    lines += ["    %s," % c_string(t) for t in link]
    lines += [
        "};",
        "inline constexpr int kLinkGameTitleCount = %d;" % len(link),
        "",
        "inline constexpr const char* kXkcdPackLabel = %s;" % c_string(label),
        "inline constexpr const char* kXkcdTitles[] = {",
    ]
    lines += ["    %s," % c_string(t) for t in xkcd]
    lines += [
        '    "",  // never empty, so the array is well formed with no pack',
        "};",
        "inline constexpr int kXkcdTitleCount = %d;" % len(xkcd),
        "",
        "// The font ids themselves. ToyboxFonts.h declares them next to a",
        "// GfxRenderer include, so a freestanding suite reads them from here.",
    ]
    for name, value in values:
        lines.append("inline constexpr int %s = %s;" % (name, value))
    lines += [
        "",
        "// The cut behind each font id, from ToyboxFonts.cpp's insertFont calls.",
        "inline const EpdFontData* dataForId(const int id) {",
    ]
    for font_id, data in ids:
        lines.append("  if (id == %s) return &%s;" % (font_id, data))
    lines += [
        "  return nullptr;",
        "}",
        "",
        "// The three slots each Faces set binds, from ToyboxTheme.h.",
        "struct FaceSet {",
        "  const char* name;",
        "  int small;",
        "  int body;",
        "  int title;",
        "};",
        "inline constexpr FaceSet kFaceSets[] = {",
    ]
    for name, slots in faces:
        lines.append(
            '    {"%s", %s, %s, %s},' % (name, slots[0], slots[1], slots[2])
        )
    lines += [
        "};",
        "inline constexpr int kFaceSetCount = %d;" % len(faces),
        "",
        "}  // namespace fitted",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")
    print(
        "corpus: %d link game titles, %d xkcd titles from %s; %d font ids, %d face sets"
        % (len(link), len(xkcd), label, len(ids), len(faces)),
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
