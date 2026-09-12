"""Symbols the reader's serif lacks, in a spelling it has.

Dropping an undrawable code point silently changed meaning: "where a != 0"
read "where a 0" in Quadratic equation, "the angle theta" lost its subject,
"10 micrometres" became "10 m". A symbol gets the spelling a reader would
say aloud; a lone Greek letter (one not touching another Greek letter, so a
symbol rather than a word) gets its name; Greek words stay for the run
rules, which remove them whole. The minus sign is the commonest of all.
"""

import re

SYMBOLS = {
    "−": "-",            # minus sign
    "≠": " != ",
    "≤": " <= ",
    "≥": " >= ",
    "≈": " ~ ",
    "≡": " == ",
    "∼": " ~ ",
    "∞": "infinity",
    "√": "sqrt",
    "∑": "sum",
    "∏": "product",
    "∫": "integral",
    "∂": "d",
    "∆": "Delta",
    "∇": "nabla",
    "∈": " in ",
    "∉": " not in ",
    "⊂": " subset of ",
    "⊆": " subset of ",
    "∪": " union ",
    "∩": " intersect ",
    "→": " -> ",
    "←": " <- ",
    "↔": " <-> ",
    "⇒": " => ",
    "⇔": " <=> ",
    "′": "'",            # prime
    "″": "''",
    "‴": "'''",
    "∗": "*",
    "∘": "o",
    "⋅": "·",       # dot operator -> middle dot (Latin-1)
    "⟨": "<",
    "⟩": ">",
    "ℝ": "R",
    "ℕ": "N",
    "ℤ": "Z",
    "ℚ": "Q",
    "ℂ": "C",
    "ℏ": "h-bar",
    "ℓ": "l",
    "⅓": "1/3",
    "⅔": "2/3",
    "⅛": "1/8",
    "∕": "/",
    "⁄": "/",   # fraction slash
}
GREEK = {
    "α": "alpha", "β": "beta", "γ": "gamma", "δ": "delta", "ε": "epsilon",
    "ζ": "zeta", "η": "eta", "θ": "theta", "ι": "iota", "κ": "kappa",
    "λ": "lambda", "μ": "mu", "ν": "nu", "ξ": "xi", "ο": "omicron",
    "π": "pi", "ρ": "rho", "σ": "sigma", "ς": "sigma", "τ": "tau",
    "υ": "upsilon", "φ": "phi", "χ": "chi", "ψ": "psi", "ω": "omega",
    "ϕ": "phi", "ϵ": "epsilon", "ϑ": "theta",
}
GREEK.update({k.upper(): v.capitalize() for k, v in list(GREEK.items()) if k.upper() != k and k.upper() not in GREEK})

_SYMBOL_RE = re.compile("[" + "".join(re.escape(c) for c in SYMBOLS) + "]")
_GREEK_BLOCK = "Ͱ-Ͽἀ-῿"
_LONE_GREEK_RE = re.compile(
    "(?<![" + _GREEK_BLOCK + "])([" + "".join(re.escape(c) for c in GREEK) + "])(?![" + _GREEK_BLOCK + "])(?=([A-Za-z])?)"
)


def _greek(m):
    ch = m.group(1)
    if ch == "μ" and m.group(2):
        return "µ"  # "10 micrometres" keeps the micro sign the serif has
    name = GREEK.get(ch, "")
    return name + " " if m.group(2) else name  # "Delta x", not "Deltax"


def translate(text):
    """Returns (text, symbols replaced)."""
    text, a = _SYMBOL_RE.subn(lambda m: SYMBOLS[m.group(0)], text)
    text, b = _LONE_GREEK_RE.subn(_greek, text)
    if a or b:
        text = re.sub(r"  +", " ", text)
    return text, a + b
