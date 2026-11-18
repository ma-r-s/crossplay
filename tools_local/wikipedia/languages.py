"""What one Wikipedia language needs from the ZIM converter, as data.

A language is a pack, and a new pack is a build run, not new code: the
converter (zim_article.py) is structural (Parsoid's own classes say what a
reference, a navbox or an infobox is), and the few things that are words
live here. A language missing from this table still builds: GENERIC drops
nothing by name, and the structural rules alone take references, navboxes
and external-link lists out. Adding a language is one entry, written from a
count of that language's corpus: a ZIM build's summary lists the forty most
common h2 names it kept ("headings_top"), which is where `skip` comes from.

    name      what the chooser and the install page call it, in its own words
    facts     the heading over the infobox's grid (CONTENTS lists it)
    skip      h2 sections dropped whole, compared folded
    omitted   the line left where a table could not be drawn
    more_rows the line under a long table listed row by row, "%d" the count
    number    what a "#" column header is spelled as
    labels    a regex for the word naming a script ("chinesisch") that goes
              when the script after it went and nothing replaced it
    ns        link namespaces that are not articles, lowercase, with colon
"""

from fold import fold

_COMMON_NS = (
    "file:",
    "image:",
    "media:",
    "category:",
    "help:",
    "wikipedia:",
    "template:",
    "portal:",
    "special:",
    "user:",
    "talk:",
    "module:",
    "draft:",
    "mediawiki:",
    "timedtext:",
)

GENERIC = {
    "code": "",
    "name": "",
    "facts": "Quick facts",
    "skip": (),
    "omitted": "(a table was omitted)",
    "more_rows": "(%d more rows)",
    "number": "Number",
    "labels": r"(?!)",
    "ns": _COMMON_NS,
    "symbols": {"\u2300": "\u00d8", "\u0192": "f"},
}

PROFILES = {
    "en": {
        "code": "en",
        "name": "English",
        "facts": "Quick facts",
        "skip": (
            "References",
            "External links",
            "See also",
            "Notes",
            "Further reading",
            "Bibliography",
            "Sources",
            "Notes and references",
            "References and notes",
            "Footnotes",
            "Citations",
            "Gallery",
        ),
        "omitted": "(a table was omitted)",
        "more_rows": "(%d more rows)",
        "number": "Number",
        "labels": r"(?!)",
        "ns": _COMMON_NS,
        "symbols": {"\u2300": "\u00d8", "\u0192": "f"},
    },
    # Counted on wikipedia_de_top_nopic_2026-10 (1,670 sampled articles): the
    # first five h2 names are Weblinks, Einzelnachweise, Literatur,
    # Geschichte, Siehe auch, and every one but Geschichte is apparatus.
    "de": {
        "code": "de",
        "name": "Deutsch",
        "facts": "Kurzinfo",
        "skip": (
            "Weblinks",
            "Einzelnachweise",
            "Literatur",
            "Siehe auch",
            "Anmerkungen",
            "Quellen",
            "Fußnoten",
            "Belege",
            "Galerie",
            "Bildergalerie",
            "Einzelnachweise und Anmerkungen",
            "Anmerkungen und Einzelnachweise",
            "Quellen und Einzelnachweise",
            "Literatur und Weblinks",
            "Weblinks und Quellen",
            "Weiterführende Literatur",
            "Belege und Anmerkungen",
            "Fußnoten und Einzelnachweise",
        ),
        "omitted": "(eine Tabelle wurde ausgelassen)",
        "more_rows": "(%d weitere Zeilen)",
        "number": "Nr.",
        # "chinesisch", "altgriechisch", "russischer": the language adjective
        "labels": r"[a-zäöü]+isch(?:e[nrs]?)?",
        # "(34 Jahre)" after a birth date in an infobox: true the day the
        # dump was made, wrong from the next birthday
        "age": r"\s*\(\d{1,3}\s+Jahre\)",
        # the serif lacks both and the German page writes them constantly
        # ("⚭ 1774 mit", "⌀ 12 mm"); these are how print spells them
        "symbols": {
            "\u26ad": "verh.",
            "\u2300": "\u00d8", "\u0192": "f",
            "\u221e": "unendlich",
            "\u221d": " proportional zu ",
            "\u2258": " entspricht ",
            "\u2224": " teilt nicht ",
        },
        # what tex_text spells in English, in a rendered formula only
        "math": {
            " proper subset of ": " echte Teilmenge von ",
            " not a subset of ": " keine Teilmenge von ",
            " subset of ": " Teilmenge von ",
            " superset of ": " Obermenge von ",
            " not in ": " nicht in ",
            " union ": " vereinigt ",
            " intersection ": " geschnitten ",
            " intersect ": " geschnitten ",
            " empty set ": " leere Menge ",
            "for all ": "f\u00fcr alle ",
            "there exists ": "es gibt ",
            "there is no ": "es gibt kein ",
            " therefore ": " also ",
            " because ": " weil ",
            " otherwise": " sonst",
            " else ": " sonst ",
            " if ": " falls ",
            "sum from ": "Summe von ",
            "product from ": "Produkt von ",
            "integral from ": "Integral von ",
            " to ": " bis ",
            " of ": " \u00fcber ",
            "infinity": "unendlich",
            " proportional to ": " proportional zu ",
            " does not divide ": " teilt nicht ",
            " not parallel to ": " nicht parallel zu ",
            " corresponds to ": " entspricht ",
        },
        "ns": _COMMON_NS
        + (
            "datei:",
            "bild:",
            "kategorie:",
            "hilfe:",
            "vorlage:",
            "spezial:",
            "benutzer:",
            "diskussion:",
            "modul:",
        ),
    },
}

# ZIM metadata says "deu"; the pack says "de".
ISO639_3 = {
    "deu": "de",
    "eng": "en",
    "fra": "fr",
    "spa": "es",
    "ita": "it",
    "nld": "nl",
    "por": "pt",
    "pol": "pl",
}


def profile(code):
    """The profile for a pack code ("de"), GENERIC with the code filled in
    when there is none."""
    p = PROFILES.get(code)
    if p is None:
        p = dict(GENERIC, code=code, name=code.upper())
    p = dict(p)
    p["skip_folded"] = frozenset(fold(s) for s in p["skip"])
    return p


def from_zim_language(lang):
    """ "deu" (or "deu,eng") to "de"; unknown codes pass through."""
    first = (lang or "").split(",")[0].strip().lower()
    return ISO639_3.get(first, first[:2] if len(first) == 3 else first)
