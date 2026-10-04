#!/bin/bash
# Home's shelf rows (GAMES, APPS) open what they say in every theme, with and
# without a book on the card and a library slot (src/activities/home/HomeShelfRows.h).
#
# Report box #647 (2026-10-04): under RoundedRaff with a book on the card,
# GAMES did nothing and APPS opened GAMES. RoundedRaff draws the book as a
# Continue Reading row inside the menu, and the row count counted it twice.
# Every theme's book count and Continue Reading flag are READ from its header
# here, not retyped, so a sync that changes them is tested as changed.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-homeshelf-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
python3 - > "$BUILD_DIR/theme_rows.h" <<'PY'
import re
def read(path):
    t = open(path).read()
    books = re.search(r"homeRecentBooksCount\s*=\s*(\d+)", t)
    cont = re.search(r"homeContinueReadingInMenu\s*=\s*(true|false)", t)
    return (int(books.group(1)) if books else None, (cont.group(1) == "true") if cont else None)
base = read("../../src/components/themes/BaseTheme.h")
lyra = read("../../src/components/themes/lyra/LyraTheme.h")
lyra3 = read("../../src/components/themes/lyra/Lyra3CoversTheme.h")
raff = read("../../src/components/themes/roundedraff/RoundedRaffTheme.h")
themes = [("classic", base), ("lyra", lyra), ("lyra extended", (lyra3[0] or lyra[0], lyra[1])), ("roundedraff", raff)]
print("struct ThemeRows { const char* name; int books; bool continueInMenu; };")
print("constexpr ThemeRows kThemes[] = {")
for name, (books, cont) in themes:
    assert books is not None and cont is not None, name
    print(f'  {{"{name}", {books}, {"true" if cont else "false"}}},')
print("};")
PY
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$BUILD_DIR" -I../../src/activities/home \
  test_homeshelf.cpp -o "$BUILD_DIR/test_homeshelf"
"$BUILD_DIR/test_homeshelf"
