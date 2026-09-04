"""Composite ARIA roles that promise a choice the page cannot deliver.

A BROWSER NEVER REPORTS THIS, and neither does a screenshot. It is the one
class of site bug where the markup is well formed, the page renders perfectly,
every link resolves and the control is simply a lie.

The instance this was written for: site/study/index.html carried

    <div class="wiz-mode" role="tablist" aria-label="What are you here to do">
      <button type="button" id="modeInstall" role="tab" aria-selected="true">Put a deck on it</button>
    </div>

-- a tablist holding exactly ONE tab, already selected. The second tab ("Bring
reviews back") went in b9d43710 when the website's sync half retired; this one
was left behind. It kept the filled aria-selected styling, so it sat in the
top-right corner looking like the page's primary call to action, and clicking
it re-rendered the step you were already on. To a screen reader it announced an
interactive tab set with nowhere to move. A cold tester reported it as looking
primary and being dead.

A composite role is a PROMISE about siblings: tablist means "there are tabs to
move between", radiogroup means "there are options to pick from". One child
breaks the promise while satisfying every syntax check there is, so the promise
is what gets counted here -- per container and by real nesting, because a
file-level count would pass a page holding one honest tablist and one leftover.

The table is deliberately short. Both rows are exercised by markup on this site
(study had the tablist, the report form has a radiogroup), so neither is a rule
nobody has ever seen run. Add a row when a page grows a new composite, not
before.

The report form is not in any .html file: assets/report.js draws it from a
template literal into the front page's dialog and into /report/. A sweep of
.html alone would have quietly stopped checking the one form this site has, so
the backtick literals in that script are parsed as markup too.

Children are counted by their EFFECTIVE role, implicit included: an
<input type="radio"> is a radio to every screen reader without saying so, and a
check that demanded the attribute would have called both of report's honest
radiogroups broken. That was this script's own first output.

Prints one line per problem and nothing when there are none; run.sh counts the
lines and checks the exit status.

    python3 host-tests/site/aria_roles.py <repo-root>
"""

import html.parser
import pathlib
import re
import sys

# A role that promises siblings -> the role those siblings carry, and how many
# the promise needs to be true.
COMPOSITES = {
    "tablist": ("tab", 2),
    "radiogroup": ("radio", 2),
}

# Roles an element has without an attribute saying so. Only the ones the roles
# above ask about; this is not a general ARIA mapping and should not grow into
# one.
IMPLICIT = {("input", "radio"): "radio"}

# Never closed, so they must not go on the open-element stack -- otherwise the
# first <img> makes every container after it close on somebody else's end tag.
VOID = {
    "area",
    "base",
    "br",
    "col",
    "embed",
    "hr",
    "img",
    "input",
    "link",
    "meta",
    "param",
    "source",
    "track",
    "wbr",
}

# Generated or vendored; not ours to hold to this.
SKIP = ("emulator", "pyodide")


def effective_role(tag, attrs):
    if "role" in attrs:
        return attrs["role"]
    return IMPLICIT.get((tag, attrs.get("type")))


class Roles(html.parser.HTMLParser):
    """Count each composite container's own descendants, by real nesting."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []  # open elements: (tag, container-or-None)
        self.containers = []  # the ones currently open
        self.findings = []

    def opened(self, tag, attrs):
        attrs = dict(attrs)
        role = effective_role(tag, attrs)
        container = None
        if attrs.get("role") in COMPOSITES:
            wanted, least = COMPOSITES[attrs["role"]]
            container = [attrs["role"], wanted, least, self.getpos()[0], 0]
            self.containers.append(container)
        if role:
            # A child counts for every container it sits inside, which is what
            # nesting means; in practice these never nest.
            for open_container in self.containers:
                if role == open_container[1]:
                    open_container[4] += 1
        return container

    def handle_starttag(self, tag, attrs):
        container = self.opened(tag, attrs)
        if tag not in VOID:
            self.stack.append((tag, container))

    def handle_startendtag(self, tag, attrs):
        # <div role="tab" /> -- opens and shuts in one tag.
        container = self.opened(tag, attrs)
        if container is not None:
            self.shut(container)
            self.containers.remove(container)

    def handle_endtag(self, tag):
        # Unwind to the matching open tag, so one stray </div> cannot desync
        # every container after it.
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                for _, container in self.stack[i:]:
                    if container is not None:
                        self.shut(container)
                        self.containers.remove(container)
                del self.stack[i:]
                return

    def finish(self):
        """Shut whatever the document left open, so an unclosed tablist is
        still counted rather than silently dropped."""
        for _, container in self.stack:
            if container is not None:
                self.shut(container)
        self.stack = []
        self.containers = []

    def shut(self, container):
        role, wanted, least, line, count = container
        if count < least:
            self.findings.append(
                f'line {line}: role="{role}" holds {count} {wanted}(s), not '
                f"{least} or more -- it announces a choice to a screen reader "
                f"and there is nothing to move to"
            )


# Scripts that carry markup in template literals. Listed, not discovered: the
# vendored bundles under site/assets hold backticks that are not HTML, and a
# parse of those would be noise passing as coverage.
TEMPLATED = ["site/assets/report.js"]


def pages(root):
    for page in sorted(root.glob("site/**/*.html")):
        if any(part in SKIP for part in page.relative_to(root).parts):
            continue
        yield page, page.read_text()
    for name in TEMPLATED:
        script = root / name
        if not script.exists():
            print(f"{name} is listed as carrying markup and does not exist")
            continue
        literals = re.findall(r"`([^`]*)`", script.read_text())
        if not literals:
            print(f"{name} is listed as carrying markup and has no template literal")
            continue
        yield script, "\n".join(literals)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: aria_roles.py <repo-root>")
    root = pathlib.Path(sys.argv[1])

    seen = 0
    for page, text in pages(root):
        seen += 1
        parser = Roles()
        parser.feed(text)
        parser.finish()
        rel = page.relative_to(root)
        for finding in parser.findings:
            print(f"{rel} {finding}")

    # A sweep that matched no files reads exactly like a clean sweep.
    if seen == 0:
        print("aria_roles.py found no pages under site/ to check at all")


main()
