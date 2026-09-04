#!/usr/bin/env python3
"""The option-picker's invariants, as the clues that broke it.

    python3 tools_local/trivia/test_distractors.py

Every case below is a real clue from the shipped pack and the wrong option it
produced. Standard library only, no corpus needed: the head rules are learned
from whatever clues they are given, so a dozen made-up "this river" clues teach
them the same thing 50,000 real ones do.
"""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distractors as D  # noqa: E402

FAILED = []


def check(name, ok, detail=""):
    print(
        f"  [{'ok  ' if ok else 'FAIL'}] {name}{('  -- ' + detail) if detail and not ok else ''}"
    )
    if not ok:
        FAILED.append(name)


def corpus():
    """Enough bare uses to teach the heads, plus the clues that went wrong."""
    items = []

    def add(q, a):
        items.append({"q": q, "a": a, "d": 3, "y": 2000, "alt": [], "w": []})

    rivers = [
        "Danube",
        "Rhine",
        "Thames",
        "Seine",
        "Volga",
        "Loire",
        "Elbe",
        "Tiber",
        "Ganges",
        "Mekong",
        "Zambezi",
        "Yangtze",
        "Douro",
        "Ebro",
        "Oder",
        "Vistula",
        "Shannon",
        "Severn",
    ]
    for i, r in enumerate(rivers):
        for j in range(4):
            add(
                f"Clue number {i}{j} about a waterway that reaches the sea, this river",
                r,
            )
    countries = [
        "Egypt",
        "Spain",
        "Peru",
        "Chad",
        "Cuba",
        "Iran",
        "Mali",
        "Togo",
        "Nepal",
        "Kenya",
        "Sudan",
        "Ghana",
        "Japan",
        "Italy",
        "Nauru",
        "Qatar",
        "Libya",
        "Benin",
    ]
    for i, c in enumerate(countries):
        for j in range(4):
            add(f"Clue number {i}{j} naming a place somewhere abroad, this country", c)
    for i, c in enumerate(
        [
            "Cairo",
            "Madrid",
            "Lima",
            "Havana",
            "Tehran",
            "Bamako",
            "Lisbon",
            "Athens",
            "Dublin",
            "Prague",
            "Vienna",
            "Warsaw",
            "Ankara",
            "Bogota",
            "Manila",
            "Nassau",
        ]
    ):
        for j in range(4):
            add(f"Clue number {i}{j} about a place people live in, this city", c)
    for i, k in enumerate(["Piano", "Violin", "Cello", "Flute"]):
        add(f"Clue number {i} about a thing you play in an orchestra, this musical", k)
    return items


def main():
    items = corpus()
    extra = [
        ("Budapest's Vigado concert hall faces this musical river", "Danube", "river"),
        (
            "The name of this large rodent is from middle French for pig with spines",
            "Porcupine",
            None,
        ),
        (
            "In 1819 this country ceded eastern Florida to the United States",
            "Spain",
            "country",
        ),
        (
            "A depiction of Mount Rushmore adorns this country's flag",
            "Spain",
            "country",
        ),
        (
            "Newsweek called the British strike in this industry the country's longest",
            "Coal",
            None,
        ),
        (
            "Under pressure this state of matter becomes something else entirely",
            "Plasma",
            None,
        ),
        (
            "Riding to hounds was the pastime of this country squire from Hyde Park",
            "FDR",
            None,
        ),
        (
            "During her months at Gombe this primatologist observed chimps eating meat",
            "Goodall",
            None,
        ),
    ]
    for q, a, _ in extra:
        items.append({"q": q, "a": a, "d": 3, "y": 2000, "alt": [], "w": []})

    index = D.TypeIndex(items)
    keys = {x["q"]: k for x, k in zip(items, index.keys)}

    print("head noun, not the first word after 'this'")
    for q, _a, want in extra:
        key = keys[q]
        got = key[1] if key else None
        check(f"{q[:52]!r} -> {want}", got == want, f"got {got}")
    check(
        "a proper modifier makes its own pool",
        keys["A depiction of Mount Rushmore adorns this country's flag"]
        == (frozenset(), "country"),
    )

    print("\nnothing gives the answer away by how it is written")
    check("lowercase answers are capitalised", D.display_case("ear") == "Ear")
    check("already capitalised is left alone", D.display_case("Nose") == "Nose")
    check("iPod keeps its own spelling", D.display_case("iPod") == "iPod")
    check(
        "e.e. cummings keeps its own spelling",
        D.display_case("e.e. cummings") == "e.e. cummings",
    )
    check("neBRAska is not offered as an option", D.odd_case("neBRAska"))
    check("McCarthy is", not D.odd_case("McCarthy"))
    check("and so is an acronym", not D.odd_case("NAACP"))

    print("\ntwo options that are one thing")
    check("van Gogh / Van Gogh", D.twins("van Gogh", "Van Gogh"))
    check("Egypt / ancient Egypt", D.twins("Egypt", "ancient Egypt"))
    check("Congo / Congo River", D.twins("Congo", "Congo River"))
    check(
        "Elizabeth I / Queen Elizabeth I",
        D.twins("Elizabeth I", "Queen Elizabeth I"),
    )
    check("New Guinea / Papua New Guinea", D.twins("New Guinea", "Papua New Guinea"))
    check("Spain / France are two things", not D.twins("Spain", "France"))
    # A plural is the same answer, and so is the -ism form of a faith. Both
    # shipped: "Eye" beside "Eyes" and "Hindu" beside "Hinduism", each a set
    # where the option marked wrong is as right as the one marked right.
    check("Eye / Eyes", D.twins("Eye", "Eyes"))
    check("Bat / Bats", D.twins("Bat", "Bats"))
    check("Potato / Potatoes", D.twins("Potato", "Potatoes"))
    check("Bicycle / Bicycles", D.twins("Bicycle", "Bicycles"))
    check("Earthquake / Earthquakes", D.twins("Earthquake", "Earthquakes"))
    check("Hindu / Hinduism", D.twins("Hindu", "Hinduism"))
    check("Shinto / Shintoism", D.twins("Shinto", "Shintoism"))
    check("Islam / Islamic", D.twins("Islam", "Islamic"))
    # and the stemming must not fuse two things a player tells apart
    check("Austria / Australia stay apart", not D.twins("Austria", "Australia"))
    check("Mars / Mercury stay apart", not D.twins("Mars", "Mercury"))
    check("Iran / Iraq stay apart", not D.twins("Iran", "Iraq"))
    check("Glass / Grass stay apart", not D.twins("Glass", "Grass"))
    check("Buddha / Buddhism stay apart", not D.twins("Buddha", "Buddhism"))

    print("\nclue text the abbreviation expander damaged")
    import build_pack as B
    # A: the tail of an initialism. "U.S. Army" became "U.South Army" in 280
    # places, and no legitimate English looks like "U.South", so this half is
    # exactly reversible.
    check("U.S. survives expansion", B.expand_abbrev("the U.S. Army") == "the U.S. Army")
    check("P.W. survives expansion", B.expand_abbrev("P.W. Botha resigned") == "P.W. Botha resigned")
    # B: a personal middle initial. This is the half that rewrote names.
    check("Robert E. Lee keeps his name",
          B.expand_abbrev("Robert E. Lee rode") == "Robert E. Lee rode")
    check("Harry S. Truman keeps his",
          B.expand_abbrev("Harry S. Truman said") == "Harry S. Truman said")
    check("George W. Bush keeps his",
          B.expand_abbrev("George W. Bush won") == "George W. Bush won")
    # and a real compass point still expands, which is why the rule exists
    check("N. Korea still expands",
          B.expand_abbrev("This N. Korean city") == "This North Korean city")
    check("a lowercase lead still expands",
          B.expand_abbrev("way up in N. Dakota") == "way up in North Dakota")

    print("\nrepairing a pack built before that fix")
    r = [{"q": "In 1969 the U.South Destroyer Evans sank", "a": "x"},
         {"q": "Robert East Lee surrendered here", "a": "x"},
         {"q": "Alfred North Whitehead said this", "a": "x"},
         {"q": "Mount Kosciuszko in New South Wales", "a": "x"},
         {"q": "P.West Botha resigned", "a": "x"}]
    n = B.repair_expansions(r)
    check("U.South is put back", r[0]["q"] == "In 1969 the U.S. Destroyer Evans sank", r[0]["q"])
    check("Robert E. Lee is put back", r[1]["q"] == "Robert E. Lee surrendered here", r[1]["q"])
    check("Alfred North Whitehead is LEFT ALONE -- it is his actual name",
          r[2]["q"] == "Alfred North Whitehead said this", r[2]["q"])
    check("New South Wales is left alone", r[3]["q"] == "Mount Kosciuszko in New South Wales", r[3]["q"])
    check("P.West is put back", r[4]["q"] == "P.W. Botha resigned", r[4]["q"])
    check("the repair reports how much it touched", n == 3, f"reported {n}")

    print("\nperiod")
    check(
        "the clue's year is its earliest",
        D.clue_year("From 1919-1960 the kings resided") == 1919,
    )
    check("a century counts as a year", D.clue_year("in the 18th century") == 1701)
    check("no USSR in 1818", not D.existed("Soviet Union", 1818))
    check("no United States in 1499", not D.existed("United States", 1499))
    check("no Zaire in 2010", not D.existed("Zaire", 2010))
    check("no Hawaii as a state in 1900", not D.existed("Hawaii", 1900))
    check("Egypt in 1818 is fine", D.existed("Egypt", 1818))
    check("an undated clue rejects nothing", D.existed("Soviet Union", None))

    print("\none place under two names")
    # A stemmer cannot reach these: the two strings share no letters. Found by
    # reading a set that offered "Holland" and "Netherlands" for the same
    # country, then counted exhaustively -- 42 sets, 22 of them with the answer
    # as half the pair, which makes a wrong option right.
    check("Holland / Netherlands", D.twins("Holland", "Netherlands"))
    check("Persia / Iran", D.twins("Persia", "Iran"))
    check("Burma / Myanmar", D.twins("Burma", "Myanmar"))
    check("Peking / Beijing", D.twins("Peking", "Beijing"))
    check("Constantinople / Istanbul", D.twins("Constantinople", "Istanbul"))
    check("USSR / Soviet Union", D.twins("USSR", "Soviet Union"))
    check("Czechoslovakia / Czech Republic",
          D.twins("Czechoslovakia", "Czech Republic"))
    # neighbours and look-alikes must still be two different answers
    check("Iran / Iraq stay apart still", not D.twins("Iran", "Iraq"))
    check("Austria / Australia stay apart still", not D.twins("Austria", "Australia"))
    check("Norway / Sweden stay apart", not D.twins("Norway", "Sweden"))
    check("Beijing / Shanghai stay apart", not D.twins("Beijing", "Shanghai"))

    print("\na head that means several kinds of thing")
    # Measured on the pack: the bare `body` pool held Moon, Sun and Pluto beside
    # Senate, Supreme Court and the League of Nations, which is how "this dense
    # cosmic body" came to offer the Supreme Court. The MODIFIED pools are fine
    # and must survive -- `body of water` is English Channel, Red Sea, Caspian.
    check("body is refused bare", "body" in D.BARE_TYPE_STOP)
    check("object is refused bare", "object" in D.BARE_TYPE_STOP)
    check("member is refused bare", "member" in D.BARE_TYPE_STOP)
    check("organ is NOT refused -- that pool is coherent (Liver, Heart, Brain)",
          "organ" not in D.BARE_TYPE_STOP)
    check("country is NOT refused", "country" not in D.BARE_TYPE_STOP)
    mini = []
    for i in range(12):
        mini.append({"q": f"Clue {i} about this body", "a": f"A{i}"})
        mini.append({"q": f"Clue {i} about this body of water", "a": f"W{i}"})
        mini.append({"q": f"Clue {i} naming this river", "a": f"R{i}"})
    ti = D.TypeIndex(mini)
    bare = ti._key("Clue about this body")
    withmod = ti._key("Clue about this body of water")
    check("a bare `this body` clue gets NO type", bare is None, f"got {bare}")
    check("`this body of water` still types", withmod is not None, f"got {withmod}")
    check("and it is a body-of-water pool",
          withmod is not None and "of:water" in withmod[0], f"got {withmod}")

    print("\nend to end")
    D.redistract(items, random.Random(1))
    mc = [x for x in items if x["w"]]
    check("questions come out playable", len(mc) > 20, f"only {len(mc)}")
    bad_case = [
        x for x in mc if len({(o[:1].isupper()) for o in [x["a"]] + x["w"]}) > 1
    ]
    check(
        "every option in a set is capitalised alike",
        not bad_case,
        f"{len(bad_case)} mixed, e.g. {bad_case[0] if bad_case else ''}",
    )
    bad_twin = [
        x
        for x in mc
        for i, o in enumerate([x["a"]] + x["w"])
        if any(D.twins(o, p) for p in ([x["a"]] + x["w"])[i + 1 :])
    ]
    check("no set contains the same thing twice", not bad_twin, f"{len(bad_twin)}")
    rivers_expected = {
        "Rhine",
        "Thames",
        "Seine",
        "Volga",
        "Loire",
        "Elbe",
        "Tiber",
        "Ganges",
        "Mekong",
        "Zambezi",
        "Yangtze",
        "Douro",
        "Ebro",
        "Oder",
        "Vistula",
        "Shannon",
        "Severn",
    }
    river = next(x for x in mc if x["q"].startswith("Budapest"))
    check(
        "the Danube is offered against other rivers",
        all(w in rivers_expected for w in river["w"]),
        str(river["w"]),
    )
    coal = next(x for x in items if x["a"] == "Coal")
    check("a mis-typed clue gets no options at all", not coal["w"], str(coal["w"]))

    print(f"\n{len(FAILED)} failed")
    return 1 if FAILED else 0


if __name__ == "__main__":
    raise SystemExit(main())
