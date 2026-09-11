#!/bin/sh
# Builds and runs the Wikipedia tests. No device and no PlatformIO: WikipediaCore
# is freestanding C++17, which is what lets the manifest, the block directory,
# the title index and the block layout be checked without a panel.
#
#   test_core      -- the fold against the vectors the builder emitted, the
#                     manifest and state JSON, a hand-built index and block
#   test_realpack  -- the SAME reader over a pack written by the Python writer
#                     (tools_local/wikipedia/pack_format.py), which is the only
#                     check that the two halves of the format agree
#
#   host-tests/wikipedia/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wikipedia-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wikipedia
TOOLS=../../tools_local/wikipedia

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_core.cpp "$SRC/WikipediaCore.cpp" -o "$BUILD_DIR/test_core"
if [ -f "$TOOLS/fold_vectors.tsv" ]; then
  "$BUILD_DIR/test_core" "$TOOLS/fold_vectors.tsv"
else
  echo "note: $TOOLS/fold_vectors.tsv not present; fold vectors not checked"
  "$BUILD_DIR/test_core"
fi

# A pack written by the real writer, from a few articles with links, headings,
# redirects and one article too big for a block.
if [ -f "$TOOLS/pack_format.py" ]; then
  ZSTD=../../lib/zstd/src
  [ -f "$BUILD_DIR/zstddeclib.o" ] || "${CC:-cc}" -std=c99 -O2 -w -c "$ZSTD/zstddeclib.c" -o "$BUILD_DIR/zstddeclib.o"
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I$ZSTD \
    test_realpack.cpp "$SRC/WikipediaCore.cpp" "$BUILD_DIR/zstddeclib.o" -o "$BUILD_DIR/test_realpack"
  rm -rf "$BUILD_DIR/pack"
  python3 - "$BUILD_DIR/pack" "$TOOLS" > "$BUILD_DIR/pack.tsv" <<'PY'
import sys, os
out, tools = sys.argv[1], sys.argv[2]
sys.path.insert(0, tools)
import pack_format as pf

# Forty articles with the shapes that matter: links, headings, accents, a
# redirect alias each for some, and one article too big for a block. Two
# tiers and a tiny shard size so the boundaries are exercised.
def xhtml(title, n, big=False):
    body = f"<h1>{title}</h1><p><b>{title}</b> is article number {n} with a <a href=\"Article 1\">link</a>.</p>"
    body += f"<h2 id=\"s1\">Quick facts</h2><p><b>Number</b> {n}</p>"
    body += f"<h2 id=\"s2\">History</h2>" + "".join(f"<p>Paragraph {k} of {title}, prose that repeats the way prose does.</p>" for k in range(6 if not big else 3000))
    return ("<html><body>" + body + "</body></html>").encode("utf-8")

titles = [f"Article {i}" for i in range(1, 39)] + ["Emile Zola", "Zzz last"]
articles = []
for n, t in enumerate(titles, 1):
    articles.append((t, ["Quick facts", "History"], xhtml(t, n, big=(t == "Article 20"))))
articles[-2] = ("Émile Zola", ["Quick facts", "History"], xhtml("Émile Zola", 39))

w = pf.PackWriter(out, pack="test", snapshot="2026-05-13", tiers=(("essentials", 10), ("all", len(articles))),
                  block_target=8192, shard_bytes=40000)
w.train([pf.encode_article(t, h, x) for t, h, x in articles])
rows = []
for t, h, x in articles:
    loc = w.add_article(t, h, x)
    rows.append((t, loc, 0, len(x), "|".join(h)))
for i in (1, 5, 20):
    w.add_redirect(f"Alias {i}", f"Article {i}")
w.add_redirect("NYC", "Article 2")
w.finish()
p = pf.Pack(out)
for i in (1, 5, 20):
    e = p.lookup(f"Alias {i}")
    rows.append((f"Alias {i}", e.locator, 1, 0, ""))
e = p.lookup("NYC")
rows.append(("NYC", e.locator, 1, 0, ""))
for r in rows:
    print("\t".join(str(x) for x in r))
PY
  "$BUILD_DIR/test_realpack" "$BUILD_DIR/pack" "$BUILD_DIR/pack.tsv"
else
  echo "note: $TOOLS/pack_format.py not present; cross-language pack check skipped"
fi
