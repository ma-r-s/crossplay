#!/usr/bin/env python3
"""Count what a cold player could answer without knowing the fact.

    tools_local/trivia/audit_options.py pack.dat [--n 400] [--seed 1]
    tools_local/trivia/audit_options.py before.dat after.dat        # side by side

The finding this exists to measure was made by a person, not a filter: someone
read 42 four-option sets off the shipped pack and answered 30 of them from the
options alone. This samples the same way the device deals -- the answer plus
THREE of the six stored distractors, drawn uniformly, exactly as
`buildChoices()` does -- and counts four things they could use:

  type     an option that is not the kind of thing the question asks for
  case     the options are not all capitalised the same way
  period   an option whose name did not exist when the clue is set
  region   the clue names a place and only one option is in it

DELIBERATELY NOT THE SAME CODE AS THE FIX. A checker that classifies options
the way the picker classified them agrees with the picker by construction, and
this repo has shipped that mistake more than once. So the type check here uses
the naive rule instead: the literal two words "this country", "this river",
"this novel" wherever they appear, with no phrase scan, no head noun, no
modifiers, no verb rule and no synonym table -- none of the machinery in
distractors.py -- plus two hand-written gazetteers for the one distinction the
naive rule cannot make (a sovereign state is not a US state).

WHAT IT CANNOT SEE is printed at the bottom of every run, and it is not a
footnote. Read it before quoting any number above it.
"""

import argparse
import collections
import json
import pathlib
import random
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import distractors  # noqa: E402
import pack_format  # noqa: E402

# --- the naive kind rule -----------------------------------------------------
# The two literal words, and which family of things they name. Families are
# disjoint on purpose: two options in different families are two different kinds
# of thing, and a player can see that without knowing the answer.
FAMILY = {
    "country": "country",
    "nation": "country",
    "state": "state",
    "city": "city",
    "capital": "city",
    "town": "city",
    "river": "water",
    "lake": "water",
    "sea": "water",
    "ocean": "water",
    "bay": "water",
    "gulf": "water",
    "canal": "water",
    "island": "land",
    "mountain": "land",
    "desert": "land",
    "continent": "land",
    "peninsula": "land",
    "volcano": "land",
    "man": "person",
    "woman": "person",
    "president": "person",
    "king": "person",
    "queen": "person",
    "emperor": "person",
    "author": "person",
    "writer": "person",
    "poet": "person",
    "composer": "person",
    "singer": "person",
    "actor": "person",
    "actress": "person",
    "artist": "person",
    "painter": "person",
    "novelist": "person",
    "playwright": "person",
    "scientist": "person",
    "inventor": "person",
    "explorer": "person",
    "general": "person",
    "philosopher": "person",
    "physicist": "person",
    "leader": "person",
    "star": "person",
    "novel": "work",
    "book": "work",
    "film": "work",
    "movie": "work",
    "play": "work",
    "song": "work",
    "opera": "work",
    "musical": "work",
    "poem": "work",
    "album": "work",
    "series": "work",
    "sitcom": "work",
    "animal": "animal",
    "bird": "animal",
    "fish": "animal",
    "mammal": "animal",
    "insect": "animal",
    "reptile": "animal",
    "rodent": "animal",
    "dog": "animal",
    "plant": "plant",
    "tree": "plant",
    "flower": "plant",
    "fruit": "food",
    "vegetable": "food",
    "food": "food",
    "dish": "food",
    "drink": "food",
    "liquor": "food",
    "cheese": "food",
    "spice": "food",
    "meat": "food",
    "candy": "food",
    "element": "matter",
    "metal": "matter",
    "gas": "matter",
    "mineral": "matter",
    "acid": "matter",
    "gem": "matter",
    "gemstone": "matter",
    "organ": "body",
    "bone": "body",
    "muscle": "body",
    "gland": "body",
    "war": "event",
    "battle": "event",
    "treaty": "event",
    "holiday": "event",
    "revolution": "event",
    "sport": "sport",
    "game": "sport",
    "company": "org",
    "university": "org",
    "college": "org",
    "team": "org",
    "band": "org",
    "religion": "religion",
    "language": "language",
    "color": "color",
    "month": "month",
    "planet": "planet",
}

# What the QUESTION asks for: the last word of the demonstrative phrase.
# One regex, and it shares exactly one assumption with the fix -- that the head
# noun is the last word before the phrase ends. Everything else in
# distractors.py (the bare-use statistics, the verb rule, possessives,
# modifiers, synonyms) is absent here.
_QKIND = re.compile(r"\b(?:this|these)\s+(?:[a-z][a-z-]*\s+){0,3}([a-z][a-z-]*)\b(?!\s+[a-z])")
# "this country's judicial capital" asks for a country, not a capital: the
# possessive marks the head, and reading past it scored South Africa as the
# wrong kind of thing for its own question.
_QPOSS = re.compile(r"\b(?:this|these)\s+(?:[a-z][a-z-]*\s+){0,3}?([a-z][a-z-]*)['’]s\b")

# What an OPTION is: only the phrases where the kind word stands alone. "this
# state capital" says nothing about whether Providence is a state, and counting
# it as one is how a state question with Providence in it read clean.
_AKIND = re.compile(r"\b(?:this|these)\s+([a-z][a-z'-]*)\b(?!\s+[a-z])")


def _family(word):
    if word in FAMILY:
        return FAMILY[word]
    if word.endswith("s") and word[:-1] in FAMILY:
        return FAMILY[word[:-1]]
    if word.endswith("es") and word[:-2] in FAMILY:
        return FAMILY[word[:-2]]
    return None

# The one distinction the naive rule gets wrong often enough to matter: the
# corpus says "this state" for Nebraska, for Israel and for plasma.
COUNTRIES = set(
    """
Afghanistan Albania Algeria Andorra Angola Argentina Armenia Australia Austria Azerbaijan
Bahamas Bahrain Bangladesh Barbados Belarus Belgium Belize Benin Bhutan Bolivia Bosnia
Botswana Brazil Brunei Bulgaria Burkina Faso Burma Burundi Cambodia Cameroon Canada Chad
Chile China Colombia Congo Croatia Cuba Cyprus Czechoslovakia Denmark Djibouti Ecuador
Egypt England Eritrea Estonia Ethiopia Fiji Finland France Gabon Gambia Georgia Germany
Ghana Greece Grenada Guatemala Guinea Guyana Haiti Holland Honduras Hungary Iceland India
Indonesia Iran Iraq Ireland Israel Italy Jamaica Japan Jordan Kazakhstan Kenya Kuwait Laos
Latvia Lebanon Lesotho Liberia Libya Liechtenstein Lithuania Luxembourg Macedonia
Madagascar Malawi Malaysia Maldives Mali Malta Mauritania Mauritius Mexico Moldova Monaco
Mongolia Montenegro Morocco Mozambique Myanmar Namibia Nepal Netherlands Nicaragua Niger
Nigeria Norway Oman Pakistan Panama Paraguay Peru Philippines Poland Portugal Qatar
Romania Russia Rwanda Samoa Scotland Senegal Serbia Seychelles Singapore Slovakia Slovenia
Somalia Spain Sudan Suriname Swaziland Sweden Switzerland Syria Taiwan Tajikistan Tanzania
Thailand Togo Tonga Tunisia Turkey Turkmenistan Uganda Ukraine Uruguay Uzbekistan Vanuatu
Venezuela Vietnam Wales Yemen Yugoslavia Zaire Zambia Zimbabwe
""".split()
)
COUNTRIES |= {
    "United States",
    "the United States",
    "U.S.",
    "USA",
    "America",
    "Great Britain",
    "United Kingdom",
    "New Zealand",
    "South Africa",
    "South Korea",
    "North Korea",
    "Costa Rica",
    "Dominican Republic",
    "El Salvador",
    "Ivory Coast",
    "Papua New Guinea",
    "Saudi Arabia",
    "Sierra Leone",
    "Sri Lanka",
    "Soviet Union",
    "Czech Republic",
    "East Germany",
    "West Germany",
    "South Vietnam",
    "North Vietnam",
    "Burkina Faso",
    "United Arab Emirates",
    "Trinidad and Tobago",
    "San Marino",
    "Vatican City",
}
US_STATES = set(
    """
Alabama Alaska Arizona Arkansas California Colorado Connecticut Delaware Florida Hawaii
Idaho Illinois Indiana Iowa Kansas Kentucky Louisiana Maine Maryland Massachusetts
Michigan Minnesota Mississippi Missouri Montana Nebraska Nevada Ohio Oklahoma Oregon
Pennsylvania Tennessee Texas Utah Vermont Virginia Washington Wisconsin Wyoming
""".split()
)
US_STATES |= {
    "New Hampshire",
    "New Jersey",
    "New Mexico",
    "New York",
    "North Carolina",
    "North Dakota",
    "Rhode Island",
    "South Carolina",
    "South Dakota",
    "West Virginia",
}


def clue_family(clue):
    """What kind of thing THIS clue asks for, or None if it names two."""
    words = _QPOSS.findall(clue) or _QKIND.findall(clue)
    fams = {f for f in (_family(w) for w in words) if f}
    return fams.pop() if len(fams) == 1 else None


def naive_kinds(items):
    """answer -> the families its clues literally ask for.

    One pass, one regex, no parsing. An answer that the corpus calls two
    different kinds of thing ("Georgia" is a state and a country) keeps both,
    and a set containing it is never flagged on its account.
    """
    kinds = collections.defaultdict(set)
    for x in items:
        fams = {f for f in (_family(w) for w in _AKIND.findall(x["q"])) if f}
        if len(fams) == 1:
            kinds[x["a"]] |= fams
    for a in list(kinds):
        if a in COUNTRIES:
            kinds[a].add("country")
        if a in US_STATES:
            kinds[a].add("state")
    for a in COUNTRIES:
        kinds[a].add("country")
    for a in US_STATES:
        kinds[a].add("state")
    return kinds


# --- period ------------------------------------------------------------------
# Half of this is the same hand-written table the picker uses, and a table
# cannot audit itself. The other half is derived from the corpus by a different
# statistic than the picker's (a median and a 150-year tolerance, against the
# picker's min/max and 60), so it can flag entities nobody tabulated.
DATED_KINDS = {"person", "work", "event"}


def year_medians(items):
    seen = collections.defaultdict(list)
    for x in items:
        for y in distractors.years_in(x["q"]):
            seen[x["a"]].append(y)
    out = {}
    for a, ys in seen.items():
        if len(ys) >= 3:
            ys = sorted(ys)
            out[a] = ys[len(ys) // 2]
    return out


# --- region ------------------------------------------------------------------
def _place_regex():
    names = sorted(
        (n for n in COUNTRIES | US_STATES if len(n) > 3), key=len, reverse=True
    )
    return re.compile(r"\b(" + "|".join(re.escape(n) for n in names) + r")\b")


PLACE_RX = _place_regex()
_PRECEDED = re.compile(r"([A-Z][A-Za-z.]*)\s+$")
# A clue opens with a capital, so the word before a place name is capitalised
# about as often as not. Treating those as part of a longer proper name hid
# every place a sentence happened to start in front of.
_SENTENCE_WORDS = {
    "In", "On", "At", "To", "From", "By", "For", "With", "Of", "The", "A", "An",
    "This", "These", "It", "He", "She", "They", "And", "But", "Or", "If", "As",
    "After", "Before", "During", "Since", "Until", "When", "While", "Its", "His",
    "Her", "Their", "Both", "Near", "Between", "Under", "Over", "Through",
}


def places_in(text):
    """Place names the text actually names.

    A match inside a longer proper name is not one: "George Washington" is not
    the state, and "New Guinea" is not Guinea. Both scored as region leaks
    until this was here.
    """
    out = set()
    for m in PLACE_RX.finditer(text):
        before = _PRECEDED.search(text[max(0, m.start() - 24) : m.start()])
        if before and before.group(1) not in _SENTENCE_WORDS:
            continue
        out.add(m.group(1))
    return out


def place_assoc(items):
    """answer -> the places its OWN clues name. Zurich's clues say Switzerland."""
    assoc = collections.defaultdict(set)
    for x in items:
        for name in places_in(x["q"]):
            if name != x["a"]:
                assoc[x["a"]].add(name)
    return assoc


# --- the audit ---------------------------------------------------------------
def load(path):
    if path.endswith(".jsonl"):
        rows = [json.loads(line) for line in open(path, encoding="utf-8")]
        for r in rows:
            r.setdefault("w", [])
            r.setdefault("alt", [])
        return rows
    pack = pack_format.open_pack(path)
    return [pack_format.read_one(pack, i) for i in range(pack["count"])]


def case_class(s):
    return "upper" if s[:1].isupper() else ("lower" if s[:1].islower() else "other")


def audit(path, n, seed):
    items = load(path)
    kinds = naive_kinds(items)
    medians = year_medians(items)
    assoc = place_assoc(items)

    mc = [x for x in items if len(x.get("w", [])) >= 3]
    rng = random.Random(seed)
    sample = mc if len(mc) <= n else rng.sample(mc, n)

    counts = collections.Counter()
    examples = collections.defaultdict(list)
    kind_cover = [0, 0]

    for x in sample:
        opts = [x["a"]] + rng.sample(x["w"], 3)
        counts["sets"] += 1

        # (a) kind. The question says what kind it wants; an option is wrong
        # in kind when the corpus never calls it that, and right in kind when
        # it does. Nothing here consults the picker's idea of the type.
        qfam = clue_family(x["q"])
        known = [o for o in opts if kinds.get(o)]
        kind_cover[0] += len(known)
        kind_cover[1] += len(opts)
        if qfam is None or len(known) < 2:
            counts["type_unknown"] += 1
        else:
            bad = [o for o in known[1:] if qfam not in kinds[o]]
            if bad:
                counts["type"] += 1
                examples["type"].append((x, opts, bad))

        # (b) case
        classes = {case_class(o) for o in opts}
        if len(classes) > 1:
            counts["case"] += 1
            examples["case"].append((x, opts, []))

        # (c) period
        year = distractors.clue_year(x["q"])
        if year is None:
            counts["period_undated"] += 1
        else:
            off = []
            for o in opts[1:]:
                if not distractors.existed(o, year):
                    off.append(o)
                    continue
                # Only things that stop existing. A planet, an element, a city
                # and a country are all "wrong" by a median-year test whenever
                # the clue is not from their most-written-about decade, and
                # scoring Uranus as an anachronism for a 2015 clue inflates the
                # count in every pack equally while measuring nothing.
                if kinds.get(o, set()) & DATED_KINDS:
                    med = medians.get(o)
                    if med is not None and abs(med - year) > 150:
                        off.append(o)
            if off:
                counts["period"] += 1
                examples["period"].append((x, opts, off))

        # (d) region
        named = places_in(x["q"])
        for r in named:
            inside = [o for o in opts if o == r or r in assoc.get(o, ())]
            if len(inside) == 1 and inside[0] == opts[0]:
                counts["region"] += 1
                examples["region"].append((x, opts, [r]))
                break

        # surface tells kept for regression cover
        if len(opts[0]) > max(len(o) for o in opts[1:]):
            counts["longest"] += 1
        if any(
            distractors.twins(a, b) for i, a in enumerate(opts) for b in opts[i + 1 :]
        ):
            counts["twin"] += 1
            examples["twin"].append((x, opts, []))

    return counts, examples, kind_cover, len(mc), len(items)


def report(path, counts, cover, n_mc, n_all, examples, show):
    sets = counts["sets"]

    def pct(k):
        return f"{counts[k]:5d}  {100 * counts[k] / sets:5.1f}%"

    print(f"\n{path}")
    print(
        f"  {n_all:,} questions, {n_mc:,} playable as multiple choice, {sets} sampled\n"
    )
    measurable = sets - counts["type_unknown"]
    share = f"{100 * counts['type'] / measurable:.1f}%" if measurable else "n/a"
    print(f"  type mismatch      {pct('type')}   an option of another kind")
    print(f"                              {share:>6} of the {measurable} it could score")
    print(f"  case mismatch      {pct('case')}   options not capitalised alike")
    dated = sets - counts["period_undated"]
    dshare = f"{100 * counts['period'] / dated:.1f}%" if dated else "n/a"
    print(f"  anachronism        {pct('period')}   an option out of its own time")
    print(f"                              {dshare:>6} of the {dated} clues that name a year")
    print(f"  region leak        {pct('region')}   one option in the place named")
    print(f"  answer is longest  {pct('longest')}   (chance is 25%)")
    print(f"  a twin in the set  {pct('twin')}   two options that are one thing")
    print()
    print(f"  kind known for     {100 * cover[0] / cover[1]:.1f}% of options")
    print(f"  no kind to compare {pct('type_unknown')}   type unmeasurable in these")
    print(
        f"  clue names no year {pct('period_undated')}   period unmeasurable in these"
    )
    for k in show:
        if examples[k]:
            print(f"\n  -- {k} --")
            for x, opts, why in examples[k][:6]:
                print(f"    {x['q'][:88]}")
                print(f"      = {opts[0]}   vs {opts[1:]}   {why if why else ''}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("packs", nargs="+")
    ap.add_argument("--n", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument(
        "--show", default="", help="comma-separated: type,case,period,region,twin"
    )
    a = ap.parse_args()
    show = [s for s in a.show.split(",") if s]
    for p in a.packs:
        counts, examples, cover, n_mc, n_all = audit(p, a.n, a.seed)
        report(p, counts, cover, n_mc, n_all, examples, show)
    print("""
WHAT THIS SAMPLER CANNOT SEE

  * A kind it has no word for. The type check knows the 90-odd families in
    FAMILY above and nothing else, so an option set of four fabrics, four
    dances or four card games reads clean whatever is in it. It says how much
    it classified ("kind known for N% of options"); the rest is unmeasured, not
    passed.
  * A wrong option that is the right kind. "Which river" answered with four
    real rivers is exactly what this is trying to produce, and no counter here
    can tell a hard one from an unfair one. That still needs a person.
  * A period the clue does not state. Two thirds of clues name no year at all,
    and the anachronism rate is over the dated third only.
  * An entity nobody dated. Half the period check is a hand-written table; the
    other half needs three dated clues about the same answer before it has an
    opinion. A name in neither is never flagged.
  * A region the clue implies rather than names. "the Kremlin" is not Russia to
    this check, and a country named only by its adjective ("the Swiss city") is
    not matched either.
  * Whether the question is any good. Nothing here reads the clue for truth,
    fairness or staleness -- that is what FLAG on the device is for.
""")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
