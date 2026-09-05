"""Print every selector in a stylesheet, one per line, with comments and
:not() contents removed.

WHY THIS FILE EXISTS. run.sh checks that each class assets/topnav.js writes has
a rule in styles.css to land on. It used to do that by grepping the whole
stylesheet for the class name, and a COMMENT answered yes: styles.css contains
the sentence " * .has-menu is added by assets/topnav.js". A reviewer renamed
.has-menu to .hazmenu in all four of its real rules, left the comment and the
:not(.has-menu) fallback alone, and the suite reported 176 checks and 0
failures -- with the bug fully restored: the toggle stays display:none at 320px
and the panel cannot be opened at all. A check a comment can satisfy is not a
check.

:not() contents are stripped for the same reason in miniature. A class named
only inside :not() is a class being EXCLUDED, not styled: the mutation above
left `.topbar:not(.has-menu) .topnav a[href="#shelf"]` in place, so scoping the
search to selectors alone would still have passed on it.

    python3 host-tests/site/css_selectors.py <file.css>
"""

import pathlib
import re
import sys

if len(sys.argv) < 2:
    sys.exit("usage: css_selectors.py <file.css>")

css = pathlib.Path(sys.argv[1]).read_text()
css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)

# Walk the text and take what sits before each "{". At-rules (@media, and the
# prelude of @supports) are preludes rather than selectors, so they are dropped;
# their inner blocks are reached the same way on the next pass.
out = []
buf = []
for ch in css:
    if ch == "{":
        sel = " ".join("".join(buf).split())
        buf = []
        if sel and not sel.startswith("@"):
            out.append(sel)
    elif ch in "};":
        buf = []
    else:
        buf.append(ch)

for sel in out:
    # Drop what :not(...) excludes, one nesting level, which is all this
    # stylesheet uses.
    print(re.sub(r":not\([^)]*\)", "", sel))
