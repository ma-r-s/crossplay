#!/usr/bin/env python3
"""The HTML -> flat text conversion, rule by rule.

Every assertion here is something the panel would show wrongly if it broke,
which is the bar for being in this file: this is the only module whose output
a reader sees verbatim.

Run: .venv/bin/python tests/test_article.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from bridge import article as art  # noqa: E402

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


def eq(got, want, what):
    ok(got == want, f"{what}\n    got      {got!r}\n    expected {want!r}")


def main():
    # --- blocks and inlines
    eq(
        art.flatten("<p>One.</p><p>Two.</p>"),
        "One.\n\nTwo.",
        "paragraphs separate with a blank line",
    )
    eq(
        art.flatten("<p>A <em>strong</em> <a href=x>link</a> here.</p>"),
        "A strong link here.",
        "inline tags do not split a paragraph",
    )
    eq(
        art.flatten("<p>Kept <span>together</span></p>"),
        "Kept together",
        "an unknown-but-inline tag stays inline",
    )
    eq(
        art.flatten("<h2>Title</h2><p>Body.</p>"),
        "Title\n\nBody.",
        "a heading is its own paragraph",
    )
    eq(
        art.flatten("<ul><li>One</li><li>Two</li></ul>"),
        "- One\n\n- Two",
        "list items are prefixed",
    )
    eq(
        art.flatten("<p>Line one<br>line two</p>"),
        "Line one line two",
        "a br in prose is a space, because the panel wraps anyway",
    )
    eq(
        art.flatten("<pre>def f():\n    return 1</pre>"),
        "def f():\n    return 1",
        "pre keeps its own line breaks",
    )
    eq(
        art.flatten("<p>Text</p><script>var x=1;</script><style>p{}</style>"),
        "Text",
        "script and style contents are dropped, not flattened",
    )
    eq(
        art.flatten("<p>Text</p><figure><img src=x><figcaption>Cap</figcaption></figure>"),
        "Text",
        "a figure goes entirely, caption included",
    )
    eq(
        art.flatten("<p>Spaced\n   out\t words</p>"),
        "Spaced out words",
        "whitespace inside a paragraph collapses",
    )
    eq(art.flatten("<p></p><p>Only</p><p>  </p>"), "Only", "empty blocks vanish")

    # --- typography
    eq(
        art.flatten("<p>It&rsquo;s a &ldquo;quote&rdquo; &mdash; really&hellip;</p>"),
        "It's a \"quote\" -- really...",
        "curly punctuation folds to what the reading cut can draw",
    )
    eq(
        art.flatten("<p>a&nbsp;b</p>"),
        "a b",
        "a non-breaking space becomes an ordinary one",
    )
    eq(
        art.flatten("<p>caf&eacute; na&iuml;ve</p>"),
        "café naïve",
        "accented Latin is LEFT ALONE -- the cut has Latin-1",
    )
    eq(art.fold_typography("a​b"), "ab", "zero-width characters are removed")
    eq(art.fold_typography("x\x01y"), "xy", "control characters are removed")

    # --- the renderable verdict
    english = "The quick brown fox jumps over the lazy dog. " * 10
    ok(art.exotic_ratio(english) == 0.0, "plain English has no exotic letters")
    ok(art.exotic_ratio("café") == 0.0, "Latin-1 accents are not exotic")
    mostly_english = english + "The word for one is 一 and two is 二."
    ok(
        art.exotic_ratio(mostly_english) <= art.MAX_EXOTIC_RATIO,
        "an English article quoting a little Chinese stays renderable",
    )
    chinese = "我有一百块钱。" * 30
    ok(
        art.exotic_ratio(chinese) > art.MAX_EXOTIC_RATIO,
        "an article written in Chinese is marked unrenderable",
    )
    ok(
        art.exotic_ratio("1234 5678 !!! ...") == 0.0,
        "digits and punctuation do not count as letters either way",
    )

    # --- convert(), the whole gate
    got = art.convert("<p>" + english + "</p>")
    ok(got["renderable"] is True, "a normal article is renderable")
    ok(got["words"] == 90, f"word count ({got['words']})")
    ok(got["minutes"] >= 1, "reading time is never zero")
    ok(art.reading_minutes(0) == 1, "even an empty article claims a minute")
    ok(art.reading_minutes(2200) == 10, "reading time is words over 220")

    try:
        art.convert("<p>Nope.</p>")
        ok(False, "a near-empty extraction is refused")
    except art.Unconvertible as e:
        ok("readable" in str(e), "the refusal is a sentence, not a code")

    try:
        art.convert("<html><body></body></html>")
        ok(False, "an empty document is refused")
    except art.Unconvertible:
        ok(True, "an empty document is refused")

    # Malformed markup must degrade, never raise: it arrives from the
    # internet through a third party and there is no version of this that
    # gets to be strict.
    for bad in ("<p>unclosed", "<<>><p>x</p>", "<p>a</div></p>", "&#xZZZZ; <p>" + english):
        try:
            art.flatten(bad)
            ok(True, f"malformed markup survives: {bad[:20]!r}")
        except Exception as e:
            ok(False, f"malformed markup raised {e!r}")

    # --- the row's subtitle
    eq(art.domain_of("https://www.example.com/a/b?c=d"), "example.com", "domain strips www and path")
    eq(art.domain_of("http://sub.example.co.uk/x"), "sub.example.co.uk", "subdomains are kept")
    eq(art.domain_of("instapaper://private-content/abc"), "saved by email", "private bookmarks say so")
    eq(art.domain_of(""), "", "no url, no domain")

    eq(art.clean_title("A\tB"), "A B", "a tab in a title cannot reach a tab-separated index")
    eq(art.clean_title("Tom &amp; Jerry"), "Tom & Jerry", "titles are unescaped")
    eq(art.clean_title("It&rsquo;s"), "It's", "titles are folded too")
    eq(art.clean_title(None), "", "a missing title is empty, not a crash")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
