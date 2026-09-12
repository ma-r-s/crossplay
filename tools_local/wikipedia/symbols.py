"""Symbols the reader's serif lacks, in a spelling it has.

Dropping an undrawable code point silently changed meaning: "where a != 0"
read "where a 0" in Quadratic equation, "D-flat major" read "D major" in
every Brahms article, "10 micrometres" became "10 m". A symbol gets the
spelling a reader would say aloud; a lone Greek letter (one not touching
another Greek letter, so a symbol rather than a word) gets its name; Greek
words stay for the run rules, which remove them whole. Nothing here is a
code point the serif draws: the minus sign, the primes, the bullet and the
fraction slash are in the font and stay as they are.

The table was written from the character census of the essentials
(tools_local/wikipedia/census.py), most frequent first; a code point not in
it and not drawable is folded by article_html (compatibility form, then
base letter plus combining mark, then base letter) or removed and counted.
Keys are written as escapes because the source is ASCII; the comment says
what each one is.
"""

import re
import unicodedata

SYMBOLS = {
    # --- comparison and equality
    "\u2260": " != ",  # not equal  ≠
    "\u2264": " <= ",  # less or equal  ≤
    "\u2265": " >= ",  # greater or equal  ≥
    "\u2a7d": " <= ",  # less or slanted equal  ⩽
    "\u2a7e": " >= ",  # greater or slanted equal  ⩾
    "\u2248": " ~ ",  # almost equal  ≈
    "\u2245": " ~= ",  # approximately equal  ≅
    "\u2243": " ~= ",  # asymptotically equal  ≃
    "\u2261": " == ",  # identical  ≡
    "\u2262": " !== ",  # not identical  ≢
    "\u223c": " ~ ",  # tilde operator  ∼
    "\u226a": " << ",  # much less  ≪
    "\u226b": " >> ",  # much greater  ≫
    "\u2272": " <~ ",  # less or equivalent  ≲
    "\u2273": " >~ ",  # greater or equivalent  ≳
    "\u226e": " !< ",  # not less  ≮
    "\u226f": " !> ",  # not greater  ≯
    "\u221d": " proportional to ",  # ∝
    "\u2258": " corresponds to ",  # ≘
    "\u225c": " := ",  # delta equal  ≜
    "\u2254": " := ",  # colon equals  ≔
    # --- operators
    "\u221e": "infinity",  # ∞
    "\u221a": "sqrt",  # √
    "\u2211": "sum",  # ∑
    "\u220f": "product",  # ∏
    "\u222b": "integral",  # ∫
    "\u222c": "double integral ",  # ∬
    "\u222d": "triple integral ",  # ∭
    "\u222e": "contour integral ",  # ∮
    "\u2202": "d",  # partial  ∂
    "\u2206": "Delta",  # increment  ∆
    "\u2207": "nabla",  # ∇
    "\u2213": " -/+ ",  # minus or plus  ∓
    "\u2217": "*",  # asterisk operator  ∗
    "\u2218": "o",  # ring operator  ∘
    "\u2219": "\u00b7",  # bullet operator -> middle dot  ∙·
    "\u22c5": "\u00b7",  # dot operator -> middle dot  ⋅·
    "\u22c6": "*",  # star operator  ⋆
    "\u2297": " (x) ",  # circled times  ⊗
    "\u2295": " (+) ",  # circled plus  ⊕
    "\u2296": " (-) ",  # circled minus  ⊖
    "\u2299": " (.) ",  # circled dot  ⊙
    "\u2a01": " (+) ",  # n-ary circled plus  ⨁
    "\u2a02": " (x) ",  # n-ary circled times  ⨂
    "\u22ca": " x ",  # semidirect product  ⋊
    "\u22c9": " x ",  # ⋉
    "\u2294": " U ",  # square cup  ⊔
    "\u2223": "|",  # divides  ∣
    "\u2224": " does not divide ",  # ∤
    "\u2225": " || ",  # parallel  ∥
    "\u2226": " not parallel to ",  # ∦
    "\u22a5": " _|_ ",  # up tack, perpendicular, falsum  ⊥
    "\u22a4": " T ",  # down tack  ⊤
    "\u22a2": " |- ",  # right tack  ⊢
    "\u22a8": " |= ",  # true  ⊨
    "\u2220": "angle ",  # ∠
    "\u221f": " > ",  # right angle, used as a breadcrumb separator  ∟
    "\u2234": " therefore ",  # ∴
    "\u2235": " because ",  # ∵
    "\u2236": ":",  # ratio  ∶
    "\u230a": "floor(",  # ⌊
    "\u230b": ")",  # ⌋
    "\u2308": "ceil(",  # ⌈
    "\u2309": ")",  # ⌉
    "\u23df": "",  # bottom curly bracket (a brace under a formula)  ⏟
    "\u23de": "",  # top curly bracket  ⏞
    # --- sets and logic
    "\u2208": " in ",  # ∈
    "\u2209": " not in ",  # ∉
    "\u220b": " contains ",  # ∋
    "\u2282": " subset of ",  # ⊂
    "\u2286": " subset of ",  # ⊆
    "\u228a": " proper subset of ",  # ⊊
    "\u2acb": " proper subset of ",  # ⫋
    "\u2283": " superset of ",  # ⊃
    "\u2287": " superset of ",  # ⊇
    "\u2284": " not a subset of ",  # ⊄
    "\u222a": " union ",  # ∪
    "\u2229": " intersect ",  # ∩
    "\u22c3": " union ",  # n-ary union  ⋃
    "\u22c2": " intersection ",  # n-ary intersection  ⋂
    "\u2216": " \\ ",  # set minus  ∖
    "\u2205": " empty set ",  # ∅
    "\u2200": "for all ",  # ∀
    "\u2203": "there exists ",  # ∃
    "\u2204": "there is no ",  # ∄
    "\u2227": " and ",  # logical and  ∧
    "\u2228": " or ",  # logical or  ∨
    "\u22c0": " and ",  # n-ary and  ⋀
    "\u22c1": " or ",  # n-ary or  ⋁
    "\u2135": "aleph",  # ℵ
    "\u25fb": "[]",  # white medium square, modal box  ◻
    "\u25a1": "[]",  # white square  □
    "\u25ca": "<>",  # lozenge, modal diamond  ◊
    # --- ellipses
    "\u22ef": "...",  # midline horizontal ellipsis  ⋯
    "\u22ee": "...",  # vertical ellipsis  ⋮
    "\u22f1": "...",  # down right diagonal ellipsis  ⋱
    # --- arrows
    "\u2192": " -> ",  # →
    "\u2190": " <- ",  # ←
    "\u2194": " <-> ",  # ↔
    "\u2191": " up ",  # ↑
    "\u2193": " down ",  # ↓
    "\u2195": " up/down ",  # ↕
    "\u2196": " <- ",  # ↖
    "\u2197": " -> ",  # ↗
    "\u2198": " -> ",  # ↘
    "\u2199": " <- ",  # ↙
    "\u21a6": " -> ",  # maps to  ↦
    "\u21aa": " -> ",  # hook arrow  ↪
    "\u21b3": " -> ",  # downwards arrow with tip rightwards  ↳
    "\u21c0": " -> ",  # harpoon  ⇀
    "\u21bd": " <- ",  # harpoon  ↽
    "\u21c4": " <-> ",  # ⇄
    "\u21c6": " <-> ",  # ⇆
    "\u21cc": " <=> ",  # equilibrium harpoons  ⇌
    "\u21d2": " => ",  # ⇒
    "\u21d0": " <== ",  # ⇐
    "\u21d4": " <=> ",  # ⇔
    "\u21d1": " up ",  # ⇑
    "\u21d3": " down ",  # ⇓
    "\u27f5": " <- ",  # ⟵
    "\u27f6": " -> ",  # ⟶
    "\u27f8": " <== ",  # ⟸
    "\u27f9": " => ",  # ⟹
    "\u27fa": " <=> ",  # ⟺
    # --- brackets
    "\u27e8": "<",  # mathematical left angle bracket  ⟨
    "\u27e9": ">",  # ⟩
    "\u27e6": "[[",  # ⟦
    "\u27e7": "]]",  # ⟧
    "\u27ea": "<<",  # ⟪
    "\u27eb": ">>",  # ⟫
    # --- letterlike, units
    "\u210f": "h-bar",  # ℏ
    "\u2126": "ohm",  # Ω
    "\u211e": "Rx",  # prescription take  ℞
    "\u2116": "No.",  # numero  №
    "\u2122": "(TM)",  # ™
    "\u211c": "Re",  # black-letter R, real part  ℜ
    "\u2111": "Im",  # black-letter I, imaginary part  ℑ
    "\u2609": "(sun)",  # solar mass, solar luminosity  ☉
    "\u29b5": "(standard)",  # plimsoll, standard state in thermochemistry  ⦵
    "\u2640": "female",  # ♀
    "\u2642": "male",  # ♂
    "\u26b3": "",  # Ceres symbol: the sentence names it  ⚳
    # --- music: "D \u266d major" is D-flat major
    "\u266d": "-flat",  # ♭
    "\u266f": "-sharp",  # ♯
    "\u266e": "-natural",  # ♮
    # --- transliteration letters: an okina or ayn is a letter of the name
    # ("Hawai\u02bbi", "\u02bfAl\u012b"), typeset as the quote it resembles
    "\u02bb": "\u2018",  # modifier letter turned comma (okina)  \u02bb
    "\u02bf": "\u2018",  # modifier letter left half ring (ayn)  \u02bf
    "\u02be": "\u2019",  # modifier letter right half ring (hamza)  \u02be
    "\u02bc": "\u2019",  # modifier letter apostrophe  \u02bc
    "\u02bd": "\u2018",  # modifier letter reversed comma  \u02bd
    # --- marks people write in tables and titles
    "\u2713": "yes",  # check mark  ✓
    "\u2714": "yes",  # ✔
    "\u2717": "no",  # ballot x  ✗
    "\u2718": "no",  # ✘
    "\u2605": "*",  # black star  ★
    "\u2606": "*",  # white star  ☆
    "\u25cf": "*",  # black circle  ●
    "\u25a0": "*",  # black square  ■
    "\u25cb": "o",  # white circle  ○
    "\u25b3": "triangle ",  # △
    "\u25b5": "triangle ",  # ▵
    "\u2500": "-",  # box drawing horizontal  ─
    "\u2764": "heart",  # heavy black heart  ❤
    "\u2660": "spades",  # ♠
    "\u2665": "hearts",  # ♥
    "\u2666": "diamonds",  # ♦
    "\u2663": "clubs",  # ♣
    "\u24d8": "",  # circled i: the "listen" icon after a pronunciation  ⓘ
    "\u25cc": "",  # dotted circle: a placeholder base for a lone mark  ◌
    # --- fractions the serif lacks (Latin-1 has 1/4, 1/2, 3/4)
    "\u2153": "1/3",  # ⅓
    "\u2154": "2/3",  # ⅔
    "\u2155": "1/5",  # ⅕
    "\u2156": "2/5",  # ⅖
    "\u2157": "3/5",  # ⅗
    "\u2158": "4/5",  # ⅘
    "\u2159": "1/6",  # ⅙
    "\u215a": "5/6",  # ⅚
    "\u215b": "1/8",  # ⅛
    "\u215c": "3/8",  # ⅜
    "\u215d": "5/8",  # ⅝
    "\u215e": "7/8",  # ⅞
    "\u2215": "/",  # division slash  ∕
}
# Letters of an orthography the serif lacks, in the letter they stand in for
# when a word must be written in plain Latin: Azerbaijani schwa, Fula and
# Hausa hooked letters, Khoisan clicks, Egyptological aleph and ayin, the
# IPA letters some languages spell with. Lossy, so article_html applies
# them only inside a word (a token with letters and no hyphen) and only
# after pronunciation spans are gone, and counts them; a lone IPA symbol in
# a respelling or a phonology table never reaches this table.
LOOKALIKES = {
    "\u0259": "e", "\u018f": "E",  # schwa: "Bemidbar", "Memmed" are the plain spellings
    "\u0261": "g", "\u0262": "G",  # script g, small capital G
    "\u0263": "\u011f",  # gamma: Turkic \u011f
    "\u0268": "i", "\u0197": "I", "\u0289": "u", "\u0244": "U",  # barred i, u
    "\u0275": "o", "\u019f": "O",  # barred o
    "\u0254": "o", "\u0186": "O", "\u025b": "e", "\u0190": "E",  # open o, open e (African orthographies)
    "\u025c": "e", "\u026a": "i", "\u028a": "u", "\u0251": "a", "\u0250": "a", "\u0252": "o",
    "\u0253": "b", "\u0181": "B", "\u0257": "d", "\u018a": "D",  # hooked b, d
    "\u0199": "k", "\u0198": "K", "\u0260": "g", "\u0193": "G",  # hooked k, g
    "\u01b4": "y", "\u01b3": "Y", "\u01ad": "t", "\u01ac": "T",  # hooked y, t
    "\u0288": "t", "\u0256": "d", "\u0273": "n", "\u026d": "l", "\u027d": "r", "\u0282": "s", "\u0290": "z",  # retroflex
    "\u0271": "m", "\u0272": "\u00f1", "\u026b": "\u0142", "\u026c": "l",  # m with hook, n with left hook, l with tilde, l with belt
    "\u027e": "r", "\u0279": "r", "\u0281": "r", "\u0280": "r", "\u027b": "r",  # r variants
    "\u0294": "\u2019", "\u0295": "\u2018", "\u02c0": "\u2019",  # glottal stop, pharyngeal, modifier glottal
    "\u01c0": "|", "\u01c1": "||", "\u01c3": "!", "\u01c2": "=",  # clicks
    "\u0192": "f", "\u0191": "F",  # f with hook
    "\ua723": "A", "\ua722": "A", "\ua725": "a", "\ua724": "A",  # Egyptological aleph, ayin (Manuel de Codage)
    "\ua78c": "\u2019", "\ua78b": "\u2019",  # saltillo
    "\u02b9": "\u2032",  # modifier prime -> prime
    "\u1e9e": "SS",  # capital sharp s
}

GREEK = {
    "\u03b1": "alpha",  # α
    "\u03b2": "beta",  # β
    "\u03b3": "gamma",  # γ
    "\u03b4": "delta",  # δ
    "\u03b5": "epsilon",  # ε
    "\u03b6": "zeta",  # ζ
    "\u03b7": "eta",  # η
    "\u03b8": "theta",  # θ
    "\u03b9": "iota",  # ι
    "\u03ba": "kappa",  # κ
    "\u03bb": "lambda",  # λ
    "\u03bc": "mu",  # μ
    "\u03bd": "nu",  # ν
    "\u03be": "xi",  # ξ
    "\u03bf": "omicron",  # ο
    "\u03c0": "pi",  # π
    "\u03c1": "rho",  # ρ
    "\u03c3": "sigma",  # σ
    "\u03c2": "sigma",  # ς
    "\u03c4": "tau",  # τ
    "\u03c5": "upsilon",  # υ
    "\u03c6": "phi",  # φ
    "\u03c7": "chi",  # χ
    "\u03c8": "psi",  # ψ
    "\u03c9": "omega",  # ω
    "\u03d5": "phi",  # ϕ
    "\u03f5": "epsilon",  # ϵ
    "\u03d1": "theta",  # ϑ
    "\u03f1": "rho",  # ϱ
    "\u03d6": "pi",  # ϖ
}
GREEK.update(
    {
        k.upper(): v.capitalize()
        for k, v in list(GREEK.items())
        if k.upper() != k and k.upper() not in GREEK
    }
)

_SYMBOL_RE = re.compile("[" + "".join(re.escape(c) for c in SYMBOLS) + "]")
_GREEK_BLOCK = "\u0370-\u03ff\u1f00-\u1fff"
_LONE_GREEK_RE = re.compile(
    "(?<!["
    + _GREEK_BLOCK
    + "])(["
    + "".join(re.escape(c) for c in GREEK)
    + "])(?!["
    + _GREEK_BLOCK
    + "])(?=([A-Za-z])?)"
)
# "D -flat major": the source spaces the accidental from its note.
_ACCIDENTAL_RE = re.compile(r"(?<=[A-G]) (-flat|-sharp|-natural)\b")


def _greek(m):
    ch = m.group(1)
    if ch == "\u03bc" and m.group(2):
        return "\u00b5"  # "10 micrometres" keeps the micro sign the serif has  µ
    name = GREEK.get(ch, "")
    return name + " " if m.group(2) else name  # "Delta x", not "Deltax"


def translate(text):
    """Returns (text, symbols replaced)."""
    text, a = _SYMBOL_RE.subn(lambda m: SYMBOLS[m.group(0)], text)
    text, b = _LONE_GREEK_RE.subn(_greek, text)
    if a or b:
        text = _ACCIDENTAL_RE.sub(r"\1", text)
        text = re.sub(r"  +", " ", text)
    return text, a + b


# ---------------------------------------------------------------- romanisation
#
# Mario, 2026-09-11: Greek in an English article is defensible; Cyrillic and
# the rest are not, "if I can't even read them why would I want them here",
# but the removal must not butcher the sentence. A Greek or Cyrillic word
# has a deterministic Latin spelling (ALA-LC classical Greek, BGN/PCGN
# Russian with the Serbian, Ukrainian and Belarusian letters), so a word
# that stands in running prose with no romanisation beside it is written
# that way instead of leaving a hole: "the Greek word hybos or hybos
# meaning hump". Scripts without a deterministic romanisation (Arabic,
# Hebrew, Devanagari, Han) are removed with their labels as before.

GREEK_ROMAN = {
    "\u03b1": "a", "\u03b2": "b", "\u03b3": "g", "\u03b4": "d", "\u03b5": "e", "\u03b6": "z",
    "\u03b7": "e", "\u03b8": "th", "\u03b9": "i", "\u03ba": "k", "\u03bb": "l", "\u03bc": "m",
    "\u03bd": "n", "\u03be": "x", "\u03bf": "o", "\u03c0": "p", "\u03c1": "r", "\u03c3": "s",
    "\u03c2": "s", "\u03c4": "t", "\u03c5": "y", "\u03c6": "ph", "\u03c7": "ch", "\u03c8": "ps",
    "\u03c9": "o", "\u03dd": "w", "\u03f2": "s",  # digamma, lunate sigma
}
_GREEK_VOWELS = "\u03b1\u03b5\u03b7\u03bf\u03c5\u03c9\u03b9"
_GREEK_LETTER = re.compile("[\u0370-\u03ff\u1f00-\u1fff]")
_ROUGH = "\u0314"

CYRILLIC_ROMAN = {
    "\u0430": "a", "\u0431": "b", "\u0432": "v", "\u0433": "g", "\u0434": "d", "\u0435": "e",
    "\u0451": "yo", "\u0436": "zh", "\u0437": "z", "\u0438": "i", "\u0439": "y", "\u043a": "k",
    "\u043b": "l", "\u043c": "m", "\u043d": "n", "\u043e": "o", "\u043f": "p", "\u0440": "r",
    "\u0441": "s", "\u0442": "t", "\u0443": "u", "\u0444": "f", "\u0445": "kh", "\u0446": "ts",
    "\u0447": "ch", "\u0448": "sh", "\u0449": "shch", "\u044a": "", "\u044b": "y", "\u044c": "",
    "\u044d": "e", "\u044e": "yu", "\u044f": "ya",
    "\u0456": "i", "\u0457": "yi", "\u0454": "ye", "\u0491": "g",  # Ukrainian
    "\u0458": "j", "\u0459": "lj", "\u045a": "nj", "\u0452": "\u0111", "\u045b": "\u0107", "\u045f": "d\u017e",  # Serbian
    "\u045e": "w", "\u0455": "dz", "\u0453": "gj", "\u045c": "kj", "\u0450": "e", "\u045d": "i",  # Belarusian, Macedonian
    "\u04d9": "a", "\u0493": "gh", "\u049b": "q", "\u04a3": "ng", "\u04e9": "o", "\u04b1": "u", "\u04af": "u", "\u04bb": "h",  # Kazakh
}


def _cap(latin, upper):
    return latin[:1].upper() + latin[1:] if upper and latin else latin


def romanize_greek(word):
    """Classical transliteration of one Greek word: accents off, rough
    breathing an h, gamma before a velar an n, upsilon after a vowel a u."""
    out = []
    prev = ""
    first = True
    for ch in word:
        upper = ch.isupper()
        nfd = unicodedata.normalize("NFD", ch)
        base = nfd[0].lower()
        latin = GREEK_ROMAN.get(base)
        if latin is None:
            out.append(ch if not _GREEK_LETTER.match(ch) else "")
            prev = ""
            continue
        if base == "\u03b3" and prev == "\u03b3":
            out[-1] = "n"  # gg -> ng, then this gamma is g
        elif base in "\u03ba\u03be\u03c7" and prev == "\u03b3":
            out[-1] = "n"
        if base == "\u03c5" and prev and prev in "\u03b1\u03b5\u03b7\u03bf":
            latin = "u"
        if first and _ROUGH in nfd:
            latin = "h" + latin if base != "\u03c1" else "rh"
        out.append(_cap(latin, upper))
        prev = base
        first = False
    return "".join(out)


def romanize_cyrillic(word):
    out = []
    for ch in word:
        upper = ch.isupper()
        latin = CYRILLIC_ROMAN.get(ch.lower())
        if latin is None:
            out.append(ch if not ("\u0400" <= ch <= "\u052f") else "")
            continue
        out.append(_cap(latin, upper))
    return "".join(out)


def romanize(run):
    """A run of Greek or Cyrillic text (with spaces and punctuation) in
    Latin letters; anything else in it stays."""
    out = []
    for token in re.split(r"(\s+)", run):
        if not token or token.isspace():
            out.append(token)
        elif _GREEK_LETTER.search(token):
            out.append(romanize_greek(token))
        else:
            out.append(romanize_cyrillic(token))
    return "".join(out)
