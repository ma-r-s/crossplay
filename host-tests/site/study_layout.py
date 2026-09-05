"""The study wizard's steps must stay in normal flow, and the page must be
allowed to grow.

WHAT WENT WRONG. `.wiz-step` was `position: absolute` inside a `min-height: 0`
row and `.study-body` clamped the page to `100vh` with `overflow: hidden`, so a
step taller than the window was cropped by the row instead of growing the page.
At 1440x900 the step-2 "Next" button hung 29 of its 45 pixels below the cut and
`document.elementFromPoint` at the button's own centre returned the footer; at
1280x720 none of it was on screen while `scrollHeight` equalled `innerHeight`,
so the page reported nothing more to see and read as finished.

WHAT THIS FILE CANNOT SEE, said plainly: whether a control is reachable. That
is a browser question and it was answered in one -- elementFromPoint at the
centre, then a real click, at both window heights, in two browsers. Nothing
here can do that, and a static assertion pretending to would be a check that
cannot fail.

WHAT IT DOES SEE is the mechanism, and it checks PROPERTIES rather than
spellings, because the first version of this check forbade three exact
declarations and a reviewer restored the identical breakage through three it
did not name (`overflow: clip` on the body, `height: calc(...)` on the wizard,
`overflow: hidden` on the step container) with zero failures reported. Any
`overflow` at all on these three boxes clips; any `height` on the outer two
stops the page growing. So the whole property is refused, not a wording of it.

The class names are read out of study/index.html rather than written here, and
a class whose rule block is MISSING is a failure rather than an empty range
that passes: renaming `.wiz-step` used to empty the awk range and satisfy all
three checks at once.

Prints one line per problem and nothing when there are none.

    python3 host-tests/site/study_layout.py <repo-root>
"""

import pathlib
import re
import sys

if len(sys.argv) < 2:
    sys.exit("usage: study_layout.py <repo-root>")

root = pathlib.Path(sys.argv[1])
html = (root / "site/study/index.html").read_text()
css = (root / "site/study/study.css").read_text()
problems = []


def say(msg):
    problems.append(msg)


# -- the names, taken from the markup ----------------------------------------

m = re.search(r"<body[^>]*\bclass=\"([^\"]+)\"", html)
body_class = m.group(1).split()[0] if m else None
if not body_class:
    say("study/index.html's <body> carries no class, so its page rules cannot be found")

m = re.search(r"<main[^>]*\bclass=\"([^\"]+)\"", html)
wizard_class = m.group(1).split()[-1] if m else None
if not wizard_class:
    say("study/index.html has no <main> with a class, so the wizard cannot be found")

# The steps are the <section>s that share a class. Take the class every one of
# them carries, minus the state class only the current one has.
step_classes = [set(c.split()) for c in re.findall(r"<section[^>]*\bclass=\"([^\"]+)\"", html)]
step_class = None
if len(step_classes) >= 2:
    shared = set.intersection(*step_classes)
    if len(shared) == 1:
        step_class = shared.pop()
if not step_class:
    say("study/index.html's step <section>s share no single class, so the step rule cannot be found")

# Their common parent's class is the step container.
container_class = None
if step_class:
    m = re.search(r"<div[^>]*\bclass=\"([^\"]*)\"[^>]*>\s*(?:<!--.*?-->\s*)*<section[^>]*\bclass=\"[^\"]*\b"
                  + re.escape(step_class) + r"\b", html, flags=re.S)
    if m:
        container_class = m.group(1).split()[0]
if not container_class:
    say("study/index.html's steps have no classed <div> around them, so the step container cannot be found")


# -- their rule blocks -------------------------------------------------------


def block(cls):
    """The declarations of the top-level `.cls { ... }` rule, or None."""
    m = re.search(r"(?m)^\." + re.escape(cls) + r"\s*\{(.*?)^\}", css, flags=re.S)
    if not m:
        return None
    return re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)


def props(decls):
    return {p.strip().lower() for p in re.findall(r"(?m)^\s*([a-z-]+)\s*:", decls)}


def check(cls, must, must_not_exact, must_not_prefix, label):
    if not cls:
        return
    decls = block(cls)
    if decls is None:
        say(f"study.css has no `.{cls}` rule, and study/index.html says that is the {label}")
        return
    have = props(decls)
    for p in must:
        if p not in have:
            say(f".{cls} ({label}) no longer sets {p}")
    for p in must_not_exact:
        if p in have:
            say(f".{cls} ({label}) sets {p}, which is what cropped the step instead of growing the page")
    for prefix in must_not_prefix:
        hit = sorted(p for p in have if p == prefix or p.startswith(prefix + "-"))
        if hit:
            say(f".{cls} ({label}) sets {', '.join(hit)}; any of them clips a step that does not fit")
    return decls


check(body_class, {"min-height"}, {"height"}, {"overflow"}, "page body")
check(wizard_class, {"min-height"}, {"height"}, set(), "wizard")
cont = check(container_class, {"display", "grid-template-rows"}, set(), {"overflow"}, "step container")
check(step_class, {"grid-area"}, {"position"}, {"overflow"}, "step")

if cont is not None:
    if not re.search(r"display\s*:\s*grid", cont):
        say(f".{container_class} (step container) is not display:grid, so the steps cannot share one cell")
    rows = re.search(r"grid-template-rows\s*:([^;]*)", cont)
    if rows and "minmax(0" in rows.group(1).replace(" ", ""):
        say(f".{container_class} (step container) sizes its row with minmax(0, ...), which lets it be "
            "shorter than the step again -- the clip under another name")

for line in problems:
    print(line)
