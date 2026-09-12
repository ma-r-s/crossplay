"""A formula's TeX, as linear text the panel can draw.

The dump writes every formula twice: the words a screen reader would say
("J z 0 = - J z 1") and the TeX ("J_{z}^{0}=-J_{z}^{1}"). The words lose
the structure; the TeX keeps it. This renders the TeX into a notation the
serif can show: superscript and subscript digits as the real glyphs, a
letter index as "_x", a fraction as "a/b" or "(a)/(b)", a root as
"sqrt(x)", a sum as "sum from i=1 to n of", a matrix as "[a, b; c, d]",
every symbol the serif lacks in the spelling symbols.py uses for prose.

    render(tex) -> (text, complete)     complete is False when a command the
                                        renderer does not know was met; the
                                        caller then keeps the words instead
    leaves(tex) -> str                  the words the dump would have made
                                        from this TeX (the MathML leaf text),
                                        used to find those words in the
                                        paragraph and replace them

Mario, 2026-09-11: maths needs delicate care to be readable; route 3 of the
three offered was chosen (render from TeX, words as fallback, measured by
the share rendered fully).
"""

import re

import symbols

SUPERSCRIPT = str.maketrans("0123456789+-=()n", "\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079\u207a\u207b\u207c\u207d\u207e\u207f")
SUBSCRIPT = str.maketrans("0123456789+-=()", "\u2080\u2081\u2082\u2083\u2084\u2085\u2086\u2087\u2088\u2089\u208a\u208b\u208c\u208d\u208e")

# command -> (rendered text, leaf text). A leaf is what MathML shows as the
# token's text; the dump's words are those leaves in order.
GREEK_TEX = {
    "alpha": "\u03b1",
    "beta": "\u03b2",
    "gamma": "\u03b3",
    "delta": "\u03b4",
    "epsilon": "\u03f5",
    "varepsilon": "\u03b5",
    "zeta": "\u03b6",
    "eta": "\u03b7",
    "theta": "\u03b8",
    "vartheta": "\u03d1",
    "iota": "\u03b9",
    "kappa": "\u03ba",
    "lambda": "\u03bb",
    "mu": "\u03bc",
    "nu": "\u03bd",
    "xi": "\u03be",
    "omicron": "\u03bf",
    "pi": "\u03c0",
    "varpi": "\u03d6",
    "rho": "\u03c1",
    "varrho": "\u03f1",
    "sigma": "\u03c3",
    "varsigma": "\u03c2",
    "tau": "\u03c4",
    "upsilon": "\u03c5",
    "phi": "\u03d5",
    "varphi": "\u03c6",
    "chi": "\u03c7",
    "psi": "\u03c8",
    "omega": "\u03c9",
    "Gamma": "\u0393",
    "Delta": "\u0394",
    "Theta": "\u0398",
    "Lambda": "\u039b",
    "Xi": "\u039e",
    "Pi": "\u03a0",
    "Sigma": "\u03a3",
    "Upsilon": "\u03a5",
    "Phi": "\u03a6",
    "Psi": "\u03a8",
    "Omega": "\u03a9",
}
GREEK_NAME = {
    "alpha": "alpha",
    "beta": "beta",
    "gamma": "gamma",
    "delta": "delta",
    "epsilon": "epsilon",
    "varepsilon": "epsilon",
    "zeta": "zeta",
    "eta": "eta",
    "theta": "theta",
    "vartheta": "theta",
    "iota": "iota",
    "kappa": "kappa",
    "lambda": "lambda",
    "mu": "mu",
    "nu": "nu",
    "xi": "xi",
    "omicron": "omicron",
    "pi": "pi",
    "varpi": "pi",
    "rho": "rho",
    "varrho": "rho",
    "sigma": "sigma",
    "varsigma": "sigma",
    "tau": "tau",
    "upsilon": "upsilon",
    "phi": "phi",
    "varphi": "phi",
    "chi": "chi",
    "psi": "psi",
    "omega": "omega",
    "Gamma": "Gamma",
    "Delta": "Delta",
    "Theta": "Theta",
    "Lambda": "Lambda",
    "Xi": "Xi",
    "Pi": "Pi",
    "Sigma": "Sigma",
    "Upsilon": "Upsilon",
    "Phi": "Phi",
    "Psi": "Psi",
    "Omega": "Omega",
}

SYMBOL = {
    "cdot": ("\u00b7", "\u22c5"),
    "times": ("\u00d7", "\u00d7"),
    "pm": ("\u00b1", "\u00b1"),
    "mp": (" -/+ ", "\u2213"),
    "div": ("\u00f7", "\u00f7"),
    "ast": ("*", "\u2217"),
    "star": ("*", "\u22c6"),
    "bullet": ("\u2022", "\u2219"),
    "circ": ("o", "\u2218"),
    "oplus": (" (+) ", "\u2295"),
    "otimes": (" (x) ", "\u2297"),
    "odot": (" (.) ", "\u2299"),
    "le": (" <= ", "\u2264"),
    "leq": (" <= ", "\u2264"),
    "leqslant": (" <= ", "\u2a7d"),
    "ge": (" >= ", "\u2265"),
    "geq": (" >= ", "\u2265"),
    "geqslant": (" >= ", "\u2a7e"),
    "ne": (" != ", "\u2260"),
    "neq": (" != ", "\u2260"),
    "approx": (" ~ ", "\u2248"),
    "sim": (" ~ ", "\u223c"),
    "simeq": (" ~= ", "\u2243"),
    "cong": (" ~= ", "\u2245"),
    "equiv": (" == ", "\u2261"),
    "propto": (" proportional to ", "\u221d"),
    "ll": (" << ", "\u226a"),
    "gg": (" >> ", "\u226b"),
    "lesssim": (" <~ ", "\u2272"),
    "gtrsim": (" >~ ", "\u2273"),
    "infty": ("infinity", "\u221e"),
    "to": (" -> ", "\u2192"),
    "rightarrow": (" -> ", "\u2192"),
    "longrightarrow": (" -> ", "\u27f6"),
    "leftarrow": (" <- ", "\u2190"),
    "longleftarrow": (" <- ", "\u27f5"),
    "leftrightarrow": (" <-> ", "\u2194"),
    "Rightarrow": (" => ", "\u21d2"),
    "implies": (" => ", "\u27f9"),
    "Leftarrow": (" <== ", "\u21d0"),
    "Leftrightarrow": (" <=> ", "\u21d4"),
    "iff": (" <=> ", "\u27fa"),
    "mapsto": (" -> ", "\u21a6"),
    "hookrightarrow": (" -> ", "\u21aa"),
    "uparrow": (" up ", "\u2191"),
    "downarrow": (" down ", "\u2193"),
    "nearrow": (" -> ", "\u2197"),
    "searrow": (" -> ", "\u2198"),
    "ldots": ("...", "\u2026"),
    "cdots": ("...", "\u22ef"),
    "dots": ("...", "\u2026"),
    "dotsc": ("...", "\u2026"),
    "dotsb": ("...", "\u22ef"),
    "vdots": ("...", "\u22ee"),
    "ddots": ("...", "\u22f1"),
    "partial": ("d", "\u2202"),
    "nabla": ("nabla", "\u2207"),
    "in": (" in ", "\u2208"),
    "notin": (" not in ", "\u2209"),
    "ni": (" contains ", "\u220b"),
    "subset": (" subset of ", "\u2282"),
    "subseteq": (" subset of ", "\u2286"),
    "subsetneq": (" proper subset of ", "\u228a"),
    "supset": (" superset of ", "\u2283"),
    "supseteq": (" superset of ", "\u2287"),
    "cup": (" union ", "\u222a"),
    "cap": (" intersect ", "\u2229"),
    "setminus": (" \\ ", "\u2216"),
    "forall": ("for all ", "\u2200"),
    "exists": ("there exists ", "\u2203"),
    "nexists": ("there is no ", "\u2204"),
    "emptyset": (" empty set ", "\u2205"),
    "varnothing": (" empty set ", "\u2205"),
    "langle": ("<", "\u27e8"),
    "rangle": (">", "\u27e9"),
    "lfloor": ("floor(", "\u230a"),
    "rfloor": (")", "\u230b"),
    "lceil": ("ceil(", "\u2308"),
    "rceil": (")", "\u2309"),
    "|": ("||", "\u2016"),
    "mid": ("|", "\u2223"),
    "nmid": (" does not divide ", "\u2224"),
    "parallel": (" || ", "\u2225"),
    "perp": (" _|_ ", "\u22a5"),
    "angle": ("angle ", "\u2220"),
    "wedge": (" and ", "\u2227"),
    "land": (" and ", "\u2227"),
    "vee": (" or ", "\u2228"),
    "lor": (" or ", "\u2228"),
    "neg": ("\u00ac", "\u00ac"),
    "lnot": ("\u00ac", "\u00ac"),
    "top": (" T ", "\u22a4"),
    "bot": (" _|_ ", "\u22a5"),
    "vdash": (" |- ", "\u22a2"),
    "models": (" |= ", "\u22a8"),
    "therefore": (" therefore ", "\u2234"),
    "because": (" because ", "\u2235"),
    "hbar": ("h-bar", "\u210f"),
    "ell": ("l", "\u2113"),
    "Re": ("Re", "\u211c"),
    "Im": ("Im", "\u2111"),
    "aleph": ("aleph", "\u2135"),
    "prime": ("\u2032", "\u2032"),
    "degree": ("\u00b0", "\u00b0"),
    "%": ("%", "%"),
    "&": ("&", "&"),
    "#": ("#", "#"),
    "_": ("_", "_"),
    "{": ("{", "{"),
    "}": ("}", "}"),
    "$": ("$", "$"),
    "backslash": ("\\", "\\"),
    "colon": (":", ":"),
    "cdotp": ("\u00b7", "\u22c5"),
    "ldotp": (".", "."),
    "dagger": ("\u2020", "\u2020"),
    "ddagger": ("\u2021", "\u2021"),
    "S": ("\u00a7", "\u00a7"),
    "P": ("\u00b6", "\u00b6"),
    "imath": ("i", "\u0131"),
    "jmath": ("j", "\u0237"),
    "wp": ("P", "\u2118"),
    "nabla ": ("nabla", "\u2207"),
    "square": ("[]", "\u25a1"),
    "Box": ("[]", "\u25a1"),
    "diamond": ("<>", "\u22c4"),
    "triangle": ("triangle ", "\u25b3"),
    "sqcup": (" U ", "\u2294"),
    "bigcup": (" union ", "\u22c3"),
    "bigcap": (" intersection ", "\u22c2"),
    "bigoplus": (" (+) ", "\u2a01"),
    "bigotimes": (" (x) ", "\u2a02"),
    "bigwedge": (" and ", "\u22c0"),
    "bigvee": (" or ", "\u22c1"),
    "coprod": (" coproduct ", "\u2210"),
    "amalg": (" U ", "\u2a3f"),
    "triangleq": (" := ", "\u225c"),
    "coloneqq": (" := ", "\u2254"),
    "eqqcolon": (" =: ", "\u2255"),
    "lesseqgtr": (" <=> ", "\u22da"),
    "smile": (" smile ", "\u2323"),
    "frown": (" frown ", "\u2322"),
    "surd": ("sqrt", "\u221a"),
    "Diamond": ("<>", "\u25ca"), "lozenge": ("<>", "\u25ca"), "vert": ("|", "|"), "Vert": ("||", "\u2016"),
    "lvert": ("|", "|"), "rvert": ("|", "|"), "lVert": ("||", "\u2016"), "rVert": ("||", "\u2016"),
    "complement": ("complement", "\u2201"), "lbrace": ("{", "{"), "rbrace": ("}", "}"), "beth": ("beth", "\u2136"),
    "bigtriangleup": ("triangle ", "\u25b3"), "bigtriangledown": ("triangle ", "\u25bd"), "lbrack": ("[", "["), "rbrack": ("]", "]"),
    "lparen": ("(", "("), "rparen": (")", ")"), "vartriangle": ("triangle ", "\u25b3"), "blacksquare": ("*", "\u25a0"),
    "checkmark": ("yes", "\u2713"), "sharp": ("-sharp", "\u266f"), "flat": ("-flat", "\u266d"), "natural": ("-natural", "\u266e"),
    "clubsuit": ("clubs", "\u2663"), "diamondsuit": ("diamonds", "\u2662"), "heartsuit": ("hearts", "\u2661"), "spadesuit": ("spades", "\u2660"),
    "approxeq": (" ~= ", "\u224a"), "asymp": (" ~ ", "\u224d"), "doteq": (" := ", "\u2250"),
    "leftharpoonup": (" <- ", "\u21bc"), "rightharpoonup": (" -> ", "\u21c0"), "rightleftharpoons": (" <=> ", "\u21cc"),
    "longmapsto": (" -> ", "\u27fc"), "Longrightarrow": (" => ", "\u27f9"), "Longleftrightarrow": (" <=> ", "\u27fa"),
    "nsubseteq": (" not a subset of ", "\u2288"), "subsetneqq": (" proper subset of ", "\u2acb"), "sqsubseteq": (" subset of ", "\u2291"),
    "sqcap": (" intersect ", "\u2293"), "uplus": (" union ", "\u228e"), "circledast": (" (*) ", "\u229b"), "ominus": (" (-) ", "\u2296"),
    "oslash": (" (/) ", "\u2298"), "bowtie": (" bowtie ", "\u22c8"), "ltimes": (" x ", "\u22c9"),
    "rtimes": (" x ", "\u22ca"), "triangleleft": (" <| ", "\u25c3"), "triangleright": (" |> ", "\u25b9"), "dashv": (" -| ", "\u22a3"),
    "Vdash": (" ||- ", "\u22a9"), "vDash": (" |= ", "\u22a8"), "succ": (" > ", "\u227b"), "prec": (" < ", "\u227a"),
    "succeq": (" >= ", "\u2ab0"), "preceq": (" <= ", "\u2aaf"), "gtrless": (" <> ", "\u2277"), "lessgtr": (" <> ", "\u2276"),
    "nless": (" !< ", "\u226e"), "ngtr": (" !> ", "\u226f"), "nleq": (" !<= ", "\u2270"), "ngeq": (" !>= ", "\u2271"),
    "nsim": (" !~ ", "\u2241"), "ncong": (" !~= ", "\u2247"), "nparallel": (" not parallel to ", "\u2226"),
    "mho": ("mho", "\u2127"), "wr": (" wr ", "\u2240"), "smallsetminus": (" \\ ", "\u2216"),
}
# spacing and styling: nothing on the panel, nothing in the leaves
NOTHING = {
    ",",
    ";",
    ":",
    "!",
    " ",
    "quad",
    "qquad",
    "left",
    "right",
    "big",
    "Big",
    "bigg",
    "Bigg",
    "bigl",
    "bigr",
    "Bigl",
    "Bigr",
    "biggl",
    "biggr",
    "Biggl",
    "Biggr",
    "displaystyle",
    "textstyle",
    "scriptstyle",
    "scriptscriptstyle",
    "limits",
    "nolimits",
    "mathstrut",
    "strut",
    "nonumber",
    "notag",
    "allowbreak",
    "smallskip",
    "medskip",
    "bigskip",
    "noindent",
    "rm",
    "it",
    "bf",
    "cal",
    "sf",
    "tt",
    "boldmath",
    "unboldmath",
    "textnormal",
    "mathnormal",
    "thinspace",
    "negthinspace",
    "enspace",
    "kern",
    "mkern",
    "mskip",
    "hfill",
    "hfil",
    "hline",
    "vline",
    "cline",
    "unskip",
    "mathinner",
    "mathord",
    "mathop",
    "mathrel",
    "mathbin",
    "mathpunct",
    "mathopen",
    "mathclose",
}
SPACE = {"quad", "qquad", ";", ":", " ", "enspace", "thinspace"}
# a command whose one argument is shown as it is
PLAIN_ARG = {
    "text",
    "mathrm",
    "mathbf",
    "mathit",
    "mathsf",
    "mathtt",
    "operatorname",
    "operatorname*",
    "textrm",
    "textbf",
    "textit",
    "textsf",
    "texttt",
    "boldsymbol",
    "mathcal",
    "mathfrak",
    "mathbb",
    "mathscr",
    "bm",
    "pmb",
    "mbox",
    "hbox",
    "textup",
    "textsc",
    "emph",
    "underline",
    "mathversion",
    "color",
    "textcolor",
    "cancel",
    "bcancel",
    "xcancel",
    "sout",
    "boxed",
    "mathbold",
    "mathsfit",
    "mathbfit",
    "bold",
    "symbf",
    "symrm",
}
# mathbb letters the dump writes as plain capitals, as do we
ACCENT = {
    "hat": "hat",
    "widehat": "hat",
    "bar": "bar",
    "overline": "bar",
    "vec": "vec",
    "overrightarrow": "vec",
    "dot": "dot",
    "ddot": "ddot",
    "tilde": "tilde",
    "widetilde": "tilde",
    "check": "check",
    "breve": "breve",
    "acute": "acute",
    "grave": "grave",
    "overbrace": "",
    "underbrace": "",
    "underset": "",
    "overset": "",
    "stackrel": "",
    "overleftarrow": "vec",
    "mathring": "ring",
}
ACCENT_LEAF = {
    "hat": "^",
    "bar": "\u00af",
    "vec": "\u2192",
    "dot": "\u02d9",
    "ddot": "\u00a8",
    "tilde": "~",
    "check": "\u02c7",
    "breve": "\u02d8",
    "acute": "\u00b4",
    "grave": "`",
    "ring": "\u02da",
    "": "",
}
FUNCTIONS = {
    "sin",
    "cos",
    "tan",
    "cot",
    "sec",
    "csc",
    "arcsin",
    "arccos",
    "arctan",
    "sinh",
    "cosh",
    "tanh",
    "coth",
    "log",
    "ln",
    "lg",
    "exp",
    "det",
    "dim",
    "deg",
    "gcd",
    "ker",
    "hom",
    "arg",
    "sgn",
    "Pr",
    "mod",
    "bmod",
    "pmod",
}
BIG = {
    "sum": "sum",
    "prod": "product",
    "int": "integral",
    "iint": "double integral",
    "iiint": "triple integral",
    "oint": "contour integral",
    "lim": "lim",
    "max": "max",
    "min": "min",
    "sup": "sup",
    "inf": "inf",
    "limsup": "limsup",
    "liminf": "liminf",
    "bigcup": "union",
    "bigcap": "intersection",
    "bigoplus": "(+)",
    "bigotimes": "(x)",
    "coprod": "coproduct",
    "argmax": "argmax",
    "argmin": "argmin",
}
BIG_LEAF = {
    "sum": "\u2211",
    "prod": "\u220f",
    "int": "\u222b",
    "iint": "\u222c",
    "iiint": "\u222d",
    "oint": "\u222e",
    "lim": "lim",
    "max": "max",
    "min": "min",
    "sup": "sup",
    "inf": "inf",
    "limsup": "lim sup",
    "liminf": "lim inf",
    "bigcup": "\u22c3",
    "bigcap": "\u22c2",
    "bigoplus": "\u2a01",
    "bigotimes": "\u2a02",
    "coprod": "\u2210",
    "argmax": "arg max",
    "argmin": "arg min",
}
MATRIX = {
    "matrix": ("[", "]"),
    "pmatrix": ("(", ")"),
    "bmatrix": ("[", "]"),
    "Bmatrix": ("{", "}"),
    "vmatrix": ("|", "|"),
    "Vmatrix": ("||", "||"),
    "smallmatrix": ("[", "]"),
    "array": ("[", "]"),
}
ALIGN = {
    "aligned",
    "align",
    "align*",
    "alignat",
    "alignat*",
    "gathered",
    "gather",
    "gather*",
    "split",
    "eqnarray",
    "eqnarray*",
    "multline",
    "multline*",
    "equation",
    "equation*",
    "alignedat",
}


class Unknown(Exception):
    pass


class Parser:
    def __init__(self, tex):
        self.s = tex
        self.i = 0
        self.complete = True

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def skip_space(self):
        while self.i < len(self.s) and self.s[self.i] in " \t\n\r":
            self.i += 1

    def command(self):
        """At a backslash: the command name."""
        self.i += 1
        if self.i >= len(self.s):
            return ""
        c = self.s[self.i]
        if c.isalpha():
            j = self.i
            while j < len(self.s) and self.s[j].isalpha():
                j += 1
            if j < len(self.s) and self.s[j] == "*":
                j += 1
            name = self.s[self.i : j]
            self.i = j
            return name
        self.i += 1
        return c

    def group(self):
        """At "{": the nodes inside, past the matching "}"."""
        assert self.s[self.i] == "{"
        self.i += 1
        nodes = self.sequence(until="}")
        if self.peek() == "}":
            self.i += 1
        return nodes

    def argument(self):
        """One argument: a group, or the next single token."""
        self.skip_space()
        if self.peek() == "{":
            return self.group()
        if self.peek() == "\\":
            return [self.node()]
        if self.i < len(self.s):
            ch = self.s[self.i]
            self.i += 1
            return [("char", ch)]
        return []

    def optional(self):
        self.skip_space()
        if self.peek() == "[":
            j = self.s.find("]", self.i)
            if j > 0:
                inner = self.s[self.i + 1 : j]
                self.i = j + 1
                return inner
        return None

    def sequence(self, until=None):
        nodes = []
        while self.i < len(self.s):
            c = self.s[self.i]
            if until and c == until:
                break
            if c in " \t\n\r":
                self.i += 1
                continue
            if c == "}":
                self.i += 1  # a stray closer
                continue
            if c in "^_":
                self.i += 1
                arg = self.argument()
                base = nodes.pop() if nodes else ("char", "")
                nodes.append(("script", c, base, arg))
                continue
            nodes.append(self.node())
        return nodes

    def node(self):
        c = self.s[self.i]
        if c == "{":
            return ("group", self.group())
        if c == "\\":
            name = self.command()
            return self.command_node(name)
        if c == "&":
            self.i += 1
            return ("amp",)
        self.i += 1
        return ("char", c)

    def command_node(self, name):
        if name == "\\":
            return ("newline",)
        if name in ("begin",):
            env = "".join(ch for ch in self.argument_text())
            if (
                env in MATRIX
                or env in ALIGN
                or env == "cases"
                or env.endswith("matrix")
            ):
                if env == "array":
                    self.argument_text()  # the column spec
                body = self.sequence_until_end(env)
                return ("env", env, body)
            self.complete = False
            body = self.sequence_until_end(env)
            return ("env", env, body)
        if name == "end":
            self.argument_text()
            return ("group", [])
        if name == "frac" or name == "dfrac" or name == "tfrac" or name == "cfrac":
            return ("frac", self.argument(), self.argument())
        if name == "binom" or name == "tbinom" or name == "dbinom":
            return ("binom", self.argument(), self.argument())
        if name == "sqrt":
            index = self.optional()
            return ("sqrt", index, self.argument())
        if name in PLAIN_ARG:
            if name in ("color", "textcolor"):
                self.argument()  # the colour
            return ("plain", name, self.argument())
        if name in (
            "phantom",
            "hphantom",
            "vphantom",
            "vspace",
            "hspace",
            "label",
            "tag",
            "class",
            "cssId",
        ):
            self.argument()
            return ("group", [])
        if name in ACCENT:
            if name in ("underset", "overset", "stackrel"):
                self.argument()  # the small thing above or below
                return ("group", self.argument())
            return ("accent", ACCENT[name], self.argument())
        if name == "not":
            return ("not", self.argument())
        if name in ("substack",):
            return ("group", self.argument())
        if name in ("xrightarrow", "xleftarrow"):
            self.optional()
            over = self.argument()
            return ("xarrow", name, over)
        if name in ("pmod",):
            return ("pmod", self.argument())
        if name in ("operatorname",):
            return ("plain", name, self.argument())
        if name in ("left", "right"):
            self.skip_space()
            if self.peek() == ".":
                self.i += 1  # an invisible delimiter
            return ("cmd", name)
        if name in ("over", "choose", "atop"):
            self.complete = False
            return ("cmd", name)
        if name in NOTHING or name in SPACE:
            return ("cmd", name)
        if name in SYMBOL or name in GREEK_TEX or name in FUNCTIONS or name in BIG:
            return ("cmd", name)
        if name == "" or not name.isalpha():
            return ("char", name)  # "\(" and the like
        self.complete = False
        return ("cmd", name)

    def argument_text(self):
        nodes = self.argument()
        return "".join(n[1] for n in nodes if n[0] == "char")

    def sequence_until_end(self, env):
        nodes = []
        while self.i < len(self.s):
            j = self.s.find("\\end", self.i)
            if j < 0:
                nodes.extend(self.sequence())
                break
            part = Parser(self.s[self.i : j])
            part.complete = True
            nodes.extend(part.sequence())
            if not part.complete:
                self.complete = False
            self.i = j
            self.command()  # "end"
            self.argument_text()
            break
        return nodes


_WORDY = re.compile(r"[A-Za-z]{2,}$")
_WORDY_START = re.compile(r"^[A-Za-z]{2,}")


def _join(parts):
    """Adjacent single letters stay glued ("xy"); a word next to a letter or
    a word gets a space ("h-bar d", "sin x", "2 lambda")."""
    text = ""
    for part in parts:
        if not part:
            continue
        if text and text[-1].isalnum() and part[0].isalnum():
            if _WORDY.search(text) or _WORDY_START.match(part):
                text += " "
        text += part
    return text


def _index(text):
    return bool(text) and re.fullmatch(r"[A-Za-z0-9*\u2032\u2217']+", text) is not None


def _digits(text):
    return bool(text) and all(c in "0123456789-+" for c in text)


def _simple(text):
    return (
        bool(text)
        and re.fullmatch(r"[A-Za-z0-9\u00b0-\u00ff\u0370-\u03ff']+|\d+(?:\.\d+)?", text) is not None
    )


class Renderer:
    def __init__(self):
        self.complete = True

    def nodes(self, nodes):
        out = []
        i = 0
        while i < len(nodes):
            n = nodes[i]
            # a big operator with its limits: "sum from a to b of"
            if n[0] == "script" and self._big_base(n):
                base, sub, sup = self._collect_limits(nodes, i)
                out.append(self._big(base, sub, sup))
                i += 1
                while (
                    i < len(nodes)
                    and nodes[i][0] == "script"
                    and self._big_base(nodes[i])
                    and nodes[i][2] is base
                ):
                    i += 1
                continue
            out.append(self.node(n))
            i += 1
        text = _join(out)
        return re.sub(r"  +", " ", text)

    def _big_base(self, n):
        base = n[2]
        while base[0] == "script":
            base = base[2]
        return base[0] == "cmd" and base[1] in BIG

    def _collect_limits(self, nodes, i):
        n = nodes[i]
        sub = sup = None
        base = n
        while base[0] == "script":
            if base[1] == "_":
                sub = base[3]
            else:
                sup = base[3]
            base = base[2]
        return base, sub, sup

    def _big(self, base, sub, sup):
        name = BIG[base[1]]
        parts = [name]
        if sub is not None and sup is not None:
            parts.append(
                " from " + self.nodes(sub).strip() + " to " + self.nodes(sup).strip()
            )
        elif sub is not None:
            lower = self.nodes(sub).strip()
            parts.append((" as " if name == "lim" else " over ") + lower)
        elif sup is not None:
            parts.append(" to " + self.nodes(sup).strip())
        parts.append(
            " of "
            if name not in ("max", "min", "sup", "inf", "argmax", "argmin")
            else " "
        )
        return "".join(parts)

    def node(self, n):
        kind = n[0]
        if kind == "char":
            c = n[1]
            if c == "-":
                return "\u2212"
            if c == "'":
                return "\u2032"
            if c == "~":
                return " "
            if c in "=<>+":
                return " " + c + " "
            if c == ",":
                return ", "
            return c
        if kind == "group":
            return self.nodes(n[1])
        if kind == "amp":
            return ""
        if kind == "newline":
            return "; "
        if kind == "cmd":
            name = n[1]
            if name in NOTHING:
                return " " if name in SPACE else ""
            if name in GREEK_TEX:
                return " " + GREEK_NAME[name] + " "
            if name in FUNCTIONS:
                return " " + name + " "
            if name in BIG:
                return " " + BIG[name] + " "
            if name in SYMBOL:
                return SYMBOL[name][0]
            self.complete = False
            return " " + name + " "
        if kind == "plain":
            # words are words: "otherwise", "gain-db", "in"; no token spacing
            text = "".join(c[1] if c[0] == "char" else self.node(c) for c in n[2])
            if n[1] in (
                "operatorname",
                "operatorname*",
                "text",
                "mathrm",
                "textrm",
                "mbox",
                "hbox",
            ):
                return " " + text.strip() + " " if len(text.strip()) > 1 else text
            return text
        if kind == "frac":
            a = self.nodes(n[1]).strip()
            b = self.nodes(n[2]).strip()
            a = (
                a
                if _simple(a) or a.startswith("(") and a.endswith(")")
                else "(" + a + ")"
            )
            b = (
                b
                if _simple(b) or b.startswith("(") and b.endswith(")")
                else "(" + b + ")"
            )
            return a + "/" + b
        if kind == "binom":
            return (
                "C(" + self.nodes(n[1]).strip() + ", " + self.nodes(n[2]).strip() + ")"
            )
        if kind == "sqrt":
            arg = self.nodes(n[2]).strip()
            if n[1]:
                return "root(" + n[1] + ", " + arg + ")"
            return "sqrt(" + arg + ")"
        if kind == "script":
            return self.script(n)
        if kind == "accent":
            text = self.nodes(n[2]).strip()
            return text + ("-" + n[1] if n[1] else "")
        if kind == "not":
            inner = self.nodes(n[1]).strip()
            table = {"in": " not in ", "=": " != ", "subset of": " not a subset of ", "there exists": " there is no ",
                     "==": " !== ", "<": " !< ", ">": " !> ", "<=": " !<= ", ">=": " !>= ", "~": " !~ ", "|": " does not divide "}
            return table.get(inner, " not " + inner)
        if kind == "xarrow":
            over = self.nodes(n[2]).strip()
            arrow = " -> " if n[1] == "xrightarrow" else " <- "
            return arrow + ("(" + over + ") " if over else "")
        if kind == "pmod":
            return " (mod " + self.nodes(n[1]).strip() + ")"
        if kind == "env":
            return self.env(n[1], n[2])
        self.complete = False
        return ""

    def script(self, n):
        base = self.node(n[2]).rstrip()
        arg = self.nodes(n[3]).strip()
        if n[1] == "^":
            if arg in ("o", "\u2218", "\\circ"):
                return base + "\u00b0"
            if arg == "\u2032" or arg == "'":
                return base + "\u2032"
            if _digits(arg):
                return base + arg.translate(SUPERSCRIPT)
            if arg == "T":
                return base + "^T"
            if _index(arg) and len(arg) <= 2:
                return base + "^" + arg
            return base + "^(" + arg + ")"
        if _digits(arg):
            return base + arg.translate(SUBSCRIPT)
        if _index(arg) and len(arg) <= 3:
            return base + "_" + arg
        return base + "_(" + arg + ")"

    def env(self, env, body):
        rows = [[[]]]
        for n in body:
            if n[0] == "newline":
                rows.append([[]])
            elif n[0] == "amp":
                rows[-1].append([])
            else:
                rows[-1][-1].append(n)
        if env in MATRIX or env.endswith("matrix"):
            open_, close = MATRIX.get(env, ("[", "]"))
            cells = [
                ", ".join(self.nodes(c).strip() for c in row)
                for row in rows
                if any(c for c in row)
            ]
            return open_ + "; ".join(cells) + close
        if env == "cases":
            parts = []
            for row in rows:
                if not any(c for c in row):
                    continue
                value = self.nodes(row[0]).strip()
                cond = self.nodes(row[1]).strip() if len(row) > 1 else ""
                cond = re.sub(r"^(?:if|when)\s+", "", cond)
                parts.append(
                    value
                    + (
                        " if " + cond
                        if cond and cond != "otherwise"
                        else (" otherwise" if cond else "")
                    )
                )
            return "{ " + "; ".join(parts) + " }"
        # aligned and the rest: the alignment marks vanish, rows are clauses
        cells = [
            "".join(self.nodes(c) for c in row).strip()
            for row in rows
            if any(c for c in row)
        ]
        return "; ".join(cells)


def render(tex):
    """(text, complete). The text is linear, with the serif's superscript and
    subscript digits; complete is False when a command was not understood."""
    p = Parser(tex)
    nodes = p.sequence()
    r = Renderer()
    text = r.nodes(nodes)
    text = re.sub(r"\s+([,;:)])", r"\1", text)
    text = re.sub(r"\(\s+", "(", text)
    text = re.sub(r"  +", " ", text).strip()
    return text, p.complete and r.complete


# ---------------------------------------------------------------- leaves


class Leaves:
    skip_matrices = False

    def nodes(self, nodes):
        out = []
        for n in nodes:
            out.extend(self.node(n))
        return out

    def node(self, n):
        kind = n[0]
        if kind == "char":
            c = n[1]
            if c == "-":
                return ["\u2212"]
            if c == "'":
                return ["\u2032"]
            if c == "*":
                return ["\u2217"]
            if c in " \t~":
                return []
            return [c]
        if kind == "group":
            return self.nodes(n[1])
        if kind in ("amp", "newline"):
            return []
        if kind == "cmd":
            name = n[1]
            if name in NOTHING:
                return []
            if name in GREEK_TEX:
                return [GREEK_TEX[name]]
            if name in FUNCTIONS:
                return [name]
            if name in BIG:
                return [BIG_LEAF[name]]
            if name in SYMBOL:
                return [SYMBOL[name][1]]
            return [name]
        if kind == "plain":
            if n[1] == "mathbb":
                return self.nodes(n[2])
            return self.nodes(n[2])
        if kind == "frac":
            return self.nodes(n[1]) + self.nodes(n[2])
        if kind == "binom":
            return ["("] + self.nodes(n[1]) + self.nodes(n[2]) + [")"]
        if kind == "sqrt":
            return ([n[1]] if n[1] else []) + self.nodes(n[2])
        if kind == "script":
            return self.node(n[2]) + self.nodes(n[3])
        if kind == "accent":
            return self.nodes(n[2]) + (
                [ACCENT_LEAF[n[1]]] if ACCENT_LEAF.get(n[1]) else []
            )
        if kind == "not":
            inner = self.nodes(n[1])
            composed = {"\u2261": "\u2262", "\u2208": "\u2209", "=": "\u2260", "\u2282": "\u2284", "\u2286": "\u2288",
                        "\u2203": "\u2204", "<": "\u226e", ">": "\u226f", "\u2264": "\u2270", "\u2265": "\u2271", "\u223c": "\u2241",
                        "\u2245": "\u2247", "\u2223": "\u2224", "\u2225": "\u2226"}
            if len(inner) == 1 and inner[0] in composed:
                return [composed[inner[0]]]
            return ["\u00ac"] + inner
        if kind == "xarrow":
            return self.nodes(n[2]) + ["\u2192" if n[1] == "xrightarrow" else "\u2190"]
        if kind == "pmod":
            return ["(", "mod"] + self.nodes(n[1]) + [")"]
        if kind == "env":
            if self.skip_matrices and (n[1] in MATRIX or n[1].endswith("matrix")):
                return []
            open_, close = MATRIX.get(n[1], ("", ""))
            inner = self.nodes(n[2])
            if n[1] in MATRIX or n[1].endswith("matrix"):
                return ([open_] if open_ else []) + inner + ([close] if close else [])
            if n[1] == "cases":
                return ["{"] + inner
            return inner
        return []


def leaves(tex, skip_matrices=False):
    """The dump's words for this TeX, without spaces: what to look for right
    before the "{\\displaystyle" block. The dump writes nothing for a
    matrix's cells; skip_matrices matches the words it does write."""
    p = Parser(tex)
    lv = Leaves()
    lv.skip_matrices = skip_matrices
    return "".join(lv.nodes(p.sequence()))


_LOOSE = str.maketrans({"\u2212": "-", "\u2217": "*", "\u2032": "'", "\u22c5": "\u00b7", "\u2016": "|", "\u00a0": ""})


def loose(text):
    """Both sides of a comparison, made insensitive to the marks that differ
    between the dump's words and the leaves."""
    return (
        "".join(c for c in text if not c.isspace()).translate(_LOOSE).replace("||", "|")
    )
