#!/usr/bin/env python3
"""Wrong options that are the same KIND of thing as the right one.

WHY A SECOND ATTEMPT. The first pass (`redistract.py`, 2026-08-31) fixed the
POOL: distractors used to be the nearest 40 answers by length, so a five-letter
country always drew the same five-letter countries and the answer became the
option you had not seen before. Widening the pool and spreading it fixed the
recurrence and nothing else. A cold player then read 42 four-option sets on the
shipped pack and answered 30 of them without knowing the fact, because the
options were not the same kind of thing:

    Budapest's Vigado concert hall faces this musical river
        Lion King / Danube / Piano / Beatles
    The name of this large rodent is from middle French for "pig with spines"
        Great Salt Lake / Lake Okeechobee / Spanish Armada / Porcupine

Both come from ONE line: the old answer_type() took the FIRST lowercase word
after "this", not the head noun. "this musical river" typed as `musical`, whose
pool holds musicals, instruments and bands; "this large rodent" typed as
`large`, whose pool holds anything at all; "this country squire" typed as
`country`, which is how FDR came to be offered against Egypt. It also stripped a
trailing "s" unconditionally, so "this was" became type `wa` and "this gas"
became type `ga` -- two pools of pure noise.

WHAT THIS MODULE DOES INSTEAD, in the order the failures were measured:

  head noun, not first word    the noun phrase after "this" is scanned to its
                               HEAD, and the head must be a word the corpus
                               actually uses as a bare type ("this river"), so
                               adjectives can never become types
  proper modifiers are kept    "this African country" draws African countries,
                               not Italy and Malta; the sub-pool is a real key,
                               not a discarded word
  period                       an option whose name did not exist in the year
                               the clue names is dropped -- no more USSR for
                               1818, Zaire for 2010, United States for 1499
  place                        when the clue names the region the answer is in,
                               every option must be in it, or the question
                               becomes quizmaster-only. "the financial hub of
                               Switzerland" may not be Zurich against Stockholm
  one case for every option    the answer used to be the only capitalised
                               option, or the only lowercase one, which told you
                               the answer without reading the question. Every
                               option is now first-letter capitalised, so the
                               tell has nowhere to live
  no near-duplicates           "van Gogh" and "Van Gogh" shipped in the same
                               option set 486 times; so did "Egypt"/"ancient
                               Egypt". Two options that are one thing make a
                               four-option question into a three-option one
  two clues to join a pool     an answer that reached a big pool on ONE clue is
                               usually the one clue that typed wrongly, and
                               least-used-first then PREFERRED it: that is how
                               FDR kept being offered against Egypt
  a near-miss beats a stranger the best wrong option in the shipped pack was
                               the Thames against the Rhine, for a clue about
                               reaching the North Sea. Salient names shared with
                               the clue are preferred, so those come back

Types whose pool is too small still lose their distractors and become
quizmaster-only. A bad option set is worse than no solo question: it teaches
the player the pool rather than the answer.
"""

import collections
import re

# --- the noun phrase after "this" --------------------------------------------
# A Jeopardy clue names its answer's type in a demonstrative phrase: "this
# country", "this African country", "this large rodent". The type is the HEAD of
# that phrase, and everything before it is a modifier.

# Words that cannot be inside the phrase, so they end it. Function words plus
# the irregular past tenses, because "this country ceded eastern Florida" must
# stop at "ceded" rather than walking on to "eastern".
FUNC_STOP = set(
    """
a an the this that these those
is was were are be been being am has have had do does did of in on at to from for with
by that which which whom whose who when where and or but as its his her their it he she
they we you i not no than then so if while after before during since until about into
over under between against through per via upon out up down off near would will can
could should may might must also both each such very more most less least other another
same only just even still yet ever never here there than once again toward towards among
along across behind beyond within without despite unlike like plus versus
one two three four five six seven eight nine ten
""".split()
)

# Present-tense verbs a clue puts straight after the head noun ("this country
# DRIVE ...", "this state OBSERVE ..."). -ed/-ing/-s morphology cannot see them,
# and _key gives up at the first lowercase non-head rather than walking past it,
# so each one silently costs a question its options: 847 of them.
BARE_VERBS = set(
    """
drive include observe contain feature offer produce provide serve mean cover span border
house hold run lie sit stand link connect flow rise form name call use make take give get
put say see know find come go want need seem look appear become remain stay keep let help
show tell ask work play live die grow open close start begin end win lose beat meet join
leave arrive reach cross pass follow lead carry bring send receive accept allow require
mark celebrate honor honour separate divide surround face front adjoin rank measure weigh
stretch extend total number range vary differ consist comprise
""".split()
)

IRREGULAR_PAST = set(
    """
became began brought bought built came chose cut drew drove fell felt flew found gave
got grew held hit kept knew laid led left lent let lost made meant met paid put ran read
rode rose said sang sank sat saw sent set shot showed shut sold sought spent spoke spread
stood stole struck swam taught thought threw told took wore won wrote lay dealt bore beat
""".split()
)

# The phrase never runs long. Five tokens is already "this Nobel Prize winning
# American author"; past that the match is noise.
MAX_PHRASE = 5

_TOKEN = re.compile(r"[A-Za-z][A-Za-z'’-]*|\d+[a-z]*")
# Case-insensitive: 6,067 clues OPEN with "This"/"These", and only 6 of them
# were playable while this pattern was not.
_DEMO = re.compile(r"\b(?:this|these)\s+", re.I)


def phrases(clue):
    """Every "this ..." noun phrase in a clue.

    Yields (tokens, closed, of_) where tokens is a list of (word, capitalised,
    possessive), closed says the phrase ended for a reason -- a function word,
    punctuation, a possessive, the end of the clue -- rather than by running
    out of room, and of_ is the noun after a trailing "of".

    That last one carries a surprising amount. `state` is three different kinds
    of thing in this corpus: a US state, a sovereign state, and a state OF
    MATTER, and the third put plasma and nirvana in the pool that questions
    about Nebraska drew from. `land of 6-inch people` did the same for
    Lilliput. Only a closed phrase is evidence about its last word.
    """
    for m in _DEMO.finditer(clue):
        pos = m.end()
        toks = []
        closed = False
        of_ = None
        while len(toks) < MAX_PHRASE:
            tm = _TOKEN.match(clue, pos)
            if not tm:
                closed = True
                break
            raw = tm.group(0)
            low = raw.lower()
            if low in FUNC_STOP or low in IRREGULAR_PAST or low in BARE_VERBS:
                closed = True
                if low == "of" and toks:
                    nxt = _TOKEN.match(clue, tm.end() + 1)
                    while nxt and nxt.group(0).lower() in ("the", "a", "an"):
                        nxt = _TOKEN.match(clue, nxt.end() + 1)
                    if nxt and not nxt.group(0)[0].isupper():
                        of_ = nxt.group(0).lower()
                break
            possessive = low.endswith("'s") or low.endswith("’s")
            word = low[:-2] if possessive else low
            toks.append((word, raw[0].isupper(), possessive))
            pos = tm.end()
            if possessive:
                closed = True  # "this country's kings" -- head is country
                break
            if clue[pos : pos + 1] != " ":
                closed = True  # punctuation, or the clue ended
                break
            pos += 1
        yield toks, closed, of_


# Heads that name no kind of thing: the pool would be everything.
TYPE_STOP = {
    "many",
    "one",
    "the",
    "a",
    "an",
    "future",
    "largest",
    "smallest",
    "other",
    "same",
    "type",
    "kind",
    "word",
    "term",
    "name",
    "number",
    "first",
    "last",
    "group",
    "form",
    "part",
    "way",
    "thing",
    "item",
    "list",
    "phrase",
    "title",
    "little",
    "new",
    "old",
    "famous",
    "great",
    "well",
    "only",
    "sort",
    "next",
    "work",
    "piece",
    "letter",
    "pair",
    "role",
    "kinda",
    "stuff",
    "field",
    "area",
    "place",
    "subject",
    "topic",
    "figure",
    "category",
    # A celestial body and a celebrity share this word and no "of" tail
    # separates them: the pool offered Sirius and the North Star against
    # Michael Jordan.
    "star",
    "answer",
    "question",
    "clue",
    "response",
}

# Heads that mean the same kind of thing. Merging them is what makes a pool big
# enough to draw from; merging any two that are NOT the same kind is exactly the
# defect this module exists to remove, so the map is short and literal.
TYPE_SYN = {
    "nation": "country",
    "kingdom": "country",
    "town": "city",
    "metropolis": "city",
    "municipality": "city",
    "burg": "city",
    "author": "writer",
    "novelist": "writer",
    "poet": "writer",
    "playwright": "writer",
    "dramatist": "writer",
    "essayist": "writer",
    "satirist": "writer",
    "composer": "musician",
    "singer": "musician",
    "songwriter": "musician",
    "guitarist": "musician",
    "pianist": "musician",
    "violinist": "musician",
    "drummer": "musician",
    "rapper": "musician",
    "crooner": "musician",
    "actress": "actor",
    "comedian": "actor",
    "comic": "actor",
    "flick": "film",
    "movie": "film",
    "picture": "film",
    "tune": "song",
    "ditty": "song",
    "veggie": "vegetable",
    "waterway": "river",
    "strait": "channel",
    "gem": "gemstone",
    "gemstone": "gemstone",
    "tsar": "czar",
    "monarch": "king",
    "sovereign": "king",
    "pontiff": "pope",
    "deity": "god",
    "diety": "god",
    "critter": "animal",
    "creature": "animal",
    "beast": "animal",
    "auto": "car",
    "automobile": "car",
    "airplane": "plane",
    "aircraft": "plane",
    "conflict": "war",
    "battle": "battle",
    "college": "university",
    "firm": "company",
    "corporation": "company",
    "business": "company",
    "illness": "disease",
    "ailment": "disease",
    "malady": "disease",
    "booze": "liquor",
    "spirit": "liquor",
}

# Lowercase modifiers that change WHAT KIND of thing is being asked for, so they
# are kept even though the corpus never uses them as a bare type. Without
# `vice`, "this vice president" and "this president" are one pool -- which is
# how Agnew, Quayle and Aaron Burr came to be offered as presidents.
MODIFIER_KEEP = {
    "vice", "deputy", "assistant", "acting", "prime", "chief", "grand",
    "former", "future", "fictional", "mythical", "imaginary", "legendary",
    "ancient", "medieval", "modern", "female", "male",
}

IRREGULAR_PLURAL = {
    "men": "man",
    "women": "woman",
    "people": "person",
    "children": "child",
    "mice": "mouse",
    "geese": "goose",
    "teeth": "tooth",
    "feet": "foot",
    "oxen": "ox",
    "wolves": "wolf",
    "leaves": "leaf",
    "knives": "knife",
    "lives": "life",
    "wives": "wife",
    "halves": "half",
    "thieves": "thief",
}

# A word is allowed to be a head only if the corpus uses it BARE ("this river"),
# which is the one thing an adjective is never used as. Measured on the shipped
# pack: river 215 bare uses, gas 54, composer 96 -- against large 2, famous 2,
# ancient 0, national 0, largest 3. The ratio guard removes the words that are
# usually modifiers even though they can stand alone ("this title role" would
# otherwise type as `title`).
MIN_BARE = 5
MIN_BARE_RATIO = 0.25


class TypeIndex:
    """Head types, sub-pools and the corpus facts the filters need.

    Everything here is derived from the pack itself. The Jeopardy source is not
    in this repo, and does not need to be: a clue names its answer's type, its
    period and often its region, so the pack can be re-distracted from itself.
    """

    def __init__(self, items):
        self._learn_heads(items)
        self.keys = [self._key(x["q"]) for x in items]

        pools = collections.defaultdict(set)
        # How many clues put each answer in each pool. Least-used-first spreads
        # the pool, but on its own it PREFERS whatever is rarest, which is
        # exactly the one clue that typed wrongly: Lilliput and plasma were
        # picked first precisely because nothing had picked them yet.
        self.attest = collections.defaultdict(collections.Counter)
        for x, key in zip(items, self.keys):
            if key is None:
                continue
            self.attest[key][x["a"]] += 1
            pools[key].add(x["a"])
        # Deduplicated: one answer serves many clues, and a pool counted with
        # duplicates looks large while offering few distinct options.
        #
        # And in a big pool, an answer that got there on ONE clue is usually the
        # one clue that typed wrongly. FDR reached the country pool through a
        # quoted speech ("I would dedicate this nation to..."), coal through
        # "this industry the country's...", Israel and Franklin reached the US
        # state pool through a sovereign state and a lost one. Every one of
        # them was then PREFERRED, because least-used-first hands the next slot
        # to whatever nothing has used yet. Requiring a second clue costs a
        # handful of genuine rarities and removes the whole family.
        self.pools = {}
        for k, v in pools.items():
            if len(v) >= 2 * MIN_TYPE_POOL:
                v = {a for a in v if self.attest[k][a] >= 2} or v
            self.pools[k] = sorted(v)

        self._learn_places(items)
        self._learn_years(items)
        self._learn_topics(items)

    # -- heads ---------------------------------------------------------------
    def _learn_heads(self, items):
        bare = collections.Counter()
        inner = collections.Counter()
        for x in items:
            for toks, closed, _of in phrases(x["q"]):
                if not toks:
                    continue
                if len(toks) == 1 and closed and not toks[0][1]:
                    bare[toks[0][0]] += 1
                for w, _cap, _p in toks[:-1]:
                    inner[w] += 1
        self.bare = bare
        # Singular and plural of the same head are the same type. Fold the
        # plural in only when the singular is itself an established head, so
        # "gas" is not folded to "ga" and "was" is not folded to "wa" -- the
        # unconditional strip that produced two pools of pure noise.
        self.singular = {}
        for w in list(bare):
            s = self._singularise(w, bare)
            if s != w:
                self.singular[w] = s
                bare[s] += bare[w]
        self.heads = {
            w
            for w, n in bare.items()
            if n >= MIN_BARE and n >= MIN_BARE_RATIO * inner[w] and w not in TYPE_STOP
        }

    @staticmethod
    def _singularise(w, bare):
        if w in IRREGULAR_PLURAL:
            return IRREGULAR_PLURAL[w]
        if w.endswith("ies") and len(w) > 4 and bare.get(w[:-3] + "y"):
            return w[:-3] + "y"
        if w.endswith("es") and len(w) > 3 and bare.get(w[:-2]):
            return w[:-2]
        if w.endswith("s") and not w.endswith("ss") and len(w) > 3 and bare.get(w[:-1]):
            return w[:-1]
        return w

    def head(self, word):
        w = self.singular.get(word, word)
        if w not in self.heads:
            w2 = self._singularise(w, self.bare)
            if w2 in self.heads:
                w = w2
            else:
                return None
        return TYPE_SYN.get(w, w)

    def _is_verb(self, word):
        """A word the phrase has to stop at because it is doing, not naming.

        Morphology only, checked against the heads the corpus taught us, so
        "this king" survives -ing and "this river flows to the North Sea" loses
        a verb rather than its head. Without it the scan walks straight through
        the sentence: "this primatologist observed chimps eating meat" typed as
        `meat`, and Jane Goodall joined the pool a beef question drew from.
        """
        if word in self.heads:
            return False
        if word in BARE_VERBS:
            return True
        if len(word) >= 5 and (word.endswith("ed") or word.endswith("ing")):
            return True
        if len(word) >= 4 and word.endswith("s") and not word.endswith("ss"):
            stem = word[:-2] if word.endswith("es") else word[:-1]
            return stem not in self.heads and word[:-1] not in self.heads
        return False

    def _key(self, clue):
        """(modifiers, head) for a clue, or None.

        The head must be the LAST lowercase word of the phrase. "this country
        squire" therefore has no type at all rather than typing as `country` --
        which is how a president came to be offered as a wrong answer to a
        question about Egypt. Capitalised words after the head are the rest of
        the sentence ("this country Ronald Reagan visited") and are ignored.

        Words BEFORE the head are the modifiers, and they make a sub-pool
        rather than being thrown away: `African country` draws African
        countries, `state capital` draws state capitals, `country singer`
        draws country singers. A lowercase modifier counts only when the corpus
        uses it as a type in its own right, which is what separates "island
        nation" from "large rodent".

        A clue with two different demonstrative types answers to neither, so it
        gets none.
        """
        found = set()
        for toks, _closed, of_ in phrases(clue):
            cut = []
            for t in toks:
                if not t[1] and self._is_verb(t[0]):
                    break
                cut.append(t)
            for i in range(len(cut) - 1, -1, -1):
                word, cap, _poss = cut[i]
                if cap:
                    continue
                h = self.head(word)
                if h is None:
                    break  # a lowercase non-head ends the search
                if any(not c for _w, c, _p in cut[i + 1 :]):
                    break  # a lowercase word follows: the head is later
                mods = {
                    w
                    for w, c, _p in cut[:i]
                    if c or w in MODIFIER_KEEP or (self.head(w) and self.head(w) != h)
                }
                if of_ and of_ not in ("which", "whom", "course"):
                    mods = mods | {"of:" + of_}
                found.add((frozenset(mods), h))
                break
        if len(found) != 1:
            return None
        return found.pop()

    # -- places --------------------------------------------------------------
    def _learn_places(self, items):
        """Which region each answer belongs to, from the clues about it.

        A city's clues name its country ("Interlaken in this country", "the
        largest city in Ecuador"), so an option can be required to be in the
        region the question named. The country list is the pack's own answers to
        plain "this country" clues, not a gazetteer typed in here, so it covers
        whatever the corpus covers and no more -- see audit_options.py, which
        says how much that is.
        """
        countries = set()
        for (mods, head), pool in self.pools.items():
            if head == "country" and not mods:
                countries.update(pool)
        self.countries = {c for c in countries if len(c) > 3}
        self._country_rx = _names_regex(self.countries)

        assoc = collections.defaultdict(collections.Counter)
        for x in items:
            for name in self.names_in(x["q"]):
                if name.lower() != x["a"].lower():
                    assoc[x["a"]][name] += 1
        self.assoc = assoc

    def _learn_topics(self, items):
        """What each answer keeps being mentioned alongside.

        The best distractor in the shipped pack was the Thames, offered against
        the Rhine for a clue about a river reaching the NORTH SEA -- wrong, but
        wrong in a way you have to know something to reject. Spreading the pool
        loses those by construction, because the good near-miss is always a
        popular answer and least-used-first puts popular last.

        So salient names are counted: any capitalised run of one to three words
        that the corpus uses often enough to be a subject rather than a
        coincidence. A candidate that shares one with the clue is preferred,
        which is how "North Sea" pulls the Thames back in front of the Volga.
        """
        counts = collections.Counter()
        lower = collections.Counter()
        per = []
        for x in items:
            found = set(_SALIENT.findall(x["q"]))
            per.append(found)
            counts.update(found)
            lower.update(re.findall(r"\b[a-z]{3,}\b", x["q"]))
        # A clue opens with a capital, so "From", "It", "The" all look like
        # names and all of them match nearly every answer -- which turns the
        # preference into noise and cost the Rhine its Thames. A one-word name
        # counts only if the corpus does not also use the word in lower case.
        keep = {
            p
            for p, n in counts.items()
            if n >= SALIENT_MIN and (" " in p or n > 3 * lower[p.lower()])
        }
        self.topic = collections.defaultdict(set)
        for x, found in zip(items, per):
            self.topic[x["a"]] |= found & keep
        self.salient = keep

    def topics_in(self, clue):
        return {p for p in _SALIENT.findall(clue) if p in self.salient}

    def _learn_years(self, items):
        """When each answer was, from the years its own clues name.

        The hand-written ENTITY_SPAN table below can only know what somebody
        wrote into it, and the anachronisms a cold player actually hit were
        mostly people and works: Caligula and Michelangelo offered for a 1916
        drowning, Oliver Twist for a 1927 novel. Nobody is going to tabulate
        every person in the corpus, but the corpus dates them itself -- every
        clue about Rasputin names a year near 1916, every clue about
        Michelangelo names one near 1500.

        Two dated clues are enough to place an answer; one is a coincidence.
        The margin is a working lifetime, so the window rejects the wrong
        CENTURY and nothing finer.
        """
        seen = collections.defaultdict(set)
        for x in items:
            for y in years_in(x["q"]):
                seen[x["a"]].add(y)
        self.window = {}
        for a, ys in seen.items():
            if len(ys) >= 2:
                self.window[a] = (min(ys) - YEAR_MARGIN, max(ys) + YEAR_MARGIN)

    def names_in(self, clue):
        """The places a clue actually names.

        A match inside a longer proper name is not one -- "New Guinea" does not
        name Guinea -- but a clue opens with a capital, so the word in front is
        capitalised about as often as not and a bare "is it preceded by a
        capital" test hides every place a sentence starts in front of.
        """
        if self._country_rx is None:
            return []
        out = set()
        for m in self._country_rx.finditer(clue):
            before = _PRECEDING_WORD.search(clue[max(0, m.start() - 24) : m.start()])
            if before and before.group(1) not in SENTENCE_WORDS:
                continue
            out.add(m.group(1))
        return sorted(out)


_PRECEDING_WORD = re.compile(r"([A-Z][A-Za-z.]*)\s+$")
SENTENCE_WORDS = {
    "In", "On", "At", "To", "From", "By", "For", "With", "Of", "The", "A", "An",
    "This", "These", "It", "He", "She", "They", "And", "But", "Or", "If", "As",
    "After", "Before", "During", "Since", "Until", "When", "While", "Its", "His",
    "Her", "Their", "Both", "Near", "Between", "Under", "Over", "Through",
}


def _names_regex(names):
    names = sorted(
        (n for n in names if re.fullmatch(r"[A-Z][A-Za-z .'-]+", n)),
        key=len,
        reverse=True,
    )
    if not names:
        return None
    return re.compile(r"\b(" + "|".join(re.escape(n) for n in names) + r")\b")


# --- period ------------------------------------------------------------------
# Names with a birthday. An option is dropped when the clue is set outside the
# window, which is what stops the USSR being offered for 1818 and the United
# States for a treaty signed in 1499. The list is what a person knows, so it is
# the honest limit of the check: an entity nobody wrote down reads clean.
ENTITY_SPAN = {
    "soviet union": (1922, 1991),
    "ussr": (1922, 1991),
    "u.s.s.r.": (1922, 1991),
    "russia": (1547, 9999),
    "russian empire": (1721, 1917),
    "czechoslovakia": (1918, 1992),
    "czech republic": (1993, 9999),
    "slovakia": (1993, 9999),
    "yugoslavia": (1918, 2003),
    "serbia": (1817, 9999),
    "croatia": (1991, 9999),
    "slovenia": (1991, 9999),
    "bosnia": (1992, 9999),
    "montenegro": (2006, 9999),
    "macedonia": (1991, 9999),
    "kosovo": (2008, 9999),
    "east germany": (1949, 1990),
    "west germany": (1949, 1990),
    "germany": (1871, 9999),
    "prussia": (1701, 1918),
    "italy": (1861, 9999),
    "belgium": (1830, 9999),
    "greece": (1830, 9999),
    "norway": (1905, 9999),
    "finland": (1917, 9999),
    "iceland": (1944, 9999),
    "ireland": (1922, 9999),
    "poland": (1918, 9999),
    "hungary": (1918, 9999),
    "austria": (1918, 9999),
    "romania": (1878, 9999),
    "bulgaria": (1878, 9999),
    "albania": (1912, 9999),
    "estonia": (1918, 9999),
    "latvia": (1918, 9999),
    "lithuania": (1918, 9999),
    "ukraine": (1991, 9999),
    "belarus": (1991, 9999),
    "moldova": (1991, 9999),
    "georgia the country": (1991, 9999),
    "armenia": (1991, 9999),
    "azerbaijan": (1991, 9999),
    "kazakhstan": (1991, 9999),
    "uzbekistan": (1991, 9999),
    "turkmenistan": (1991, 9999),
    "kyrgyzstan": (1991, 9999),
    "tajikistan": (1991, 9999),
    "turkey": (1923, 9999),
    "ottoman empire": (1299, 1922),
    "byzantine empire": (395, 1453),
    "holy roman empire": (800, 1806),
    "iran": (1935, 9999),
    "persia": (-600, 1935),
    "iraq": (1932, 9999),
    "syria": (1946, 9999),
    "lebanon": (1943, 9999),
    "jordan": (1946, 9999),
    "israel": (1948, 9999),
    "saudi arabia": (1932, 9999),
    "yemen": (1918, 9999),
    "kuwait": (1961, 9999),
    "qatar": (1971, 9999),
    "bahrain": (1971, 9999),
    "oman": (1970, 9999),
    "united arab emirates": (1971, 9999),
    "pakistan": (1947, 9999),
    "bangladesh": (1971, 9999),
    "sri lanka": (1972, 9999),
    "ceylon": (1505, 1972),
    "nepal": (1768, 9999),
    "myanmar": (1989, 9999),
    "burma": (1824, 1989),
    "thailand": (1939, 9999),
    "siam": (1350, 1939),
    "malaysia": (1963, 9999),
    "singapore": (1965, 9999),
    "indonesia": (1945, 9999),
    "philippines": (1946, 9999),
    "vietnam": (1976, 9999),
    "south vietnam": (1955, 1975),
    "north vietnam": (1945, 1976),
    "cambodia": (1953, 9999),
    "laos": (1953, 9999),
    "south korea": (1948, 9999),
    "north korea": (1948, 9999),
    "india": (1947, 9999),
    "china": (-1600, 9999),
    "zaire": (1971, 1997),
    "congo": (1960, 9999),
    "democratic republic of the congo": (1997, 9999),
    "rhodesia": (1965, 1979),
    "zimbabwe": (1980, 9999),
    "zambia": (1964, 9999),
    "malawi": (1964, 9999),
    "tanzania": (1964, 9999),
    "kenya": (1963, 9999),
    "uganda": (1962, 9999),
    "ghana": (1957, 9999),
    "nigeria": (1960, 9999),
    "namibia": (1990, 9999),
    "botswana": (1966, 9999),
    "lesotho": (1966, 9999),
    "eritrea": (1993, 9999),
    "south sudan": (2011, 9999),
    "sudan": (1956, 9999),
    "somalia": (1960, 9999),
    "djibouti": (1977, 9999),
    "algeria": (1962, 9999),
    "tunisia": (1956, 9999),
    "morocco": (1956, 9999),
    "libya": (1951, 9999),
    "south africa": (1910, 9999),
    "cameroon": (1960, 9999),
    "ivory coast": (1960, 9999),
    "senegal": (1960, 9999),
    "mali": (1960, 9999),
    "united states": (1776, 9999),
    "the united states": (1776, 9999),
    "u.s.": (1776, 9999),
    "usa": (1776, 9999),
    "america": (1776, 9999),
    "canada": (1867, 9999),
    "mexico": (1821, 9999),
    "brazil": (1822, 9999),
    "argentina": (1816, 9999),
    "chile": (1818, 9999),
    "peru": (1821, 9999),
    "colombia": (1810, 9999),
    "venezuela": (1811, 9999),
    "bolivia": (1825, 9999),
    "ecuador": (1830, 9999),
    "uruguay": (1828, 9999),
    "paraguay": (1811, 9999),
    "panama": (1903, 9999),
    "costa rica": (1821, 9999),
    "cuba": (1902, 9999),
    "jamaica": (1962, 9999),
    "haiti": (1804, 9999),
    "dominican republic": (1844, 9999),
    "bahamas": (1973, 9999),
    "belize": (1981, 9999),
    "guyana": (1966, 9999),
    "suriname": (1975, 9999),
    "australia": (1901, 9999),
    "new zealand": (1907, 9999),
    "papua new guinea": (1975, 9999),
    "fiji": (1970, 9999),
    "confederacy": (1861, 1865),
    "confederate states": (1861, 1865),
    "united nations": (1945, 9999),
    "nato": (1949, 9999),
    "european union": (1993, 9999),
    "soviet russia": (1917, 1991),
}

# Statehood, for the same reason and with the same limits.
STATEHOOD = {
    "delaware": 1787,
    "pennsylvania": 1787,
    "new jersey": 1787,
    "georgia": 1788,
    "connecticut": 1788,
    "massachusetts": 1788,
    "maryland": 1788,
    "south carolina": 1788,
    "new hampshire": 1788,
    "virginia": 1788,
    "new york": 1788,
    "north carolina": 1789,
    "rhode island": 1790,
    "vermont": 1791,
    "kentucky": 1792,
    "tennessee": 1796,
    "ohio": 1803,
    "louisiana": 1812,
    "indiana": 1816,
    "mississippi": 1817,
    "illinois": 1818,
    "alabama": 1819,
    "maine": 1820,
    "missouri": 1821,
    "arkansas": 1836,
    "michigan": 1837,
    "florida": 1845,
    "texas": 1845,
    "iowa": 1846,
    "wisconsin": 1848,
    "california": 1850,
    "minnesota": 1858,
    "oregon": 1859,
    "kansas": 1861,
    "west virginia": 1863,
    "nevada": 1864,
    "nebraska": 1867,
    "colorado": 1876,
    "north dakota": 1889,
    "south dakota": 1889,
    "montana": 1889,
    "washington": 1889,
    "idaho": 1890,
    "wyoming": 1890,
    "utah": 1896,
    "oklahoma": 1907,
    "new mexico": 1912,
    "arizona": 1912,
    "alaska": 1959,
    "hawaii": 1959,
}

_YEAR = re.compile(r"\b(1[0-9]{3}|20[0-2][0-9])\b")
_CENTURY = re.compile(r"\b(\d{1,2})(?:st|nd|rd|th)[- ]century\b", re.I)
_BC = re.compile(r"\bB\.?C\.?(?:E\.?)?\b")


# A capitalised run of one to three words. Kept only when the corpus uses it
# often enough to be a subject rather than a coincidence of one clue.
_SALIENT = re.compile(r"\b[A-Z][a-z]+(?:\s+(?:of\s+|the\s+)?[A-Z][a-z]+){0,2}\b")
SALIENT_MIN = 15

YEAR_MARGIN = 60


def years_in(clue):
    """Every year a clue names. B.C. is one bucket; nothing here needs finer."""
    if _bc_is_a_date(clue):
        return [-1000]
    out = [int(y) for y in _YEAR.findall(clue)]
    for c in _CENTURY.findall(clue):
        n = int(c)
        if 1 <= n <= 21:
            out.append((n - 1) * 100 + 50)
    return out


def _bc_is_a_date(clue):
    """B.C. is also British Columbia. Twelve clues about Victoria and Vancouver
    dated themselves to 1000 BC and lost every option they had."""
    m = _BC.search(clue)
    if not m:
        return False
    before = clue[max(0, m.start() - 24) : m.start()]
    return bool(re.search(r"\d\s*$|\bcentury\s*$", before))


def clue_year(clue):
    """The earliest year the clue is set in, or None.

    The earliest, not the latest: a clue spanning 1919-1960 needs an option
    that already existed in 1919. B.C. collapses to a single very old year,
    which is all any of these checks can use it for.
    """
    if _bc_is_a_date(clue):
        return -1000
    years = [int(y) for y in _YEAR.findall(clue)]
    for c in _CENTURY.findall(clue):
        n = int(c)
        if 1 <= n <= 21:
            years.append((n - 1) * 100 + 1)
    return min(years) if years else None


def _span_key(name):
    n = name.lower().strip()
    n = re.sub(r"^(the)\s+", "", n)
    return n


def existed(name, year):
    """False only when something is KNOWN not to have existed then."""
    if year is None:
        return True
    key = _span_key(name)
    span = ENTITY_SPAN.get(key)
    if span and not (span[0] <= year <= span[1]):
        return False
    admitted = STATEHOOD.get(key)
    if admitted and year < admitted:
        return False
    return True


# --- shape -------------------------------------------------------------------
_STRIP = re.compile(r"[^a-z0-9 ]")
# Titles come off too: "Queen Elizabeth I" and "Elizabeth I" shipped in the
# same option set, which is a four-option question with three answers in it.
_LEAD = re.compile(
    r"^(the|a|an|mount|mt|saint|st|lake|river|ancient|modern"
    r"|king|queen|emperor|empress|prince|princess|pope|president|general"
    r"|sir|lord|lady|dr|mr|mrs|ms)\s+"
)


_FOLD_CACHE = {}


def fold(s):
    """The form two options are compared in: case, punctuation, the little
    leading words and the titles gone. "van Gogh"/"Van Gogh",
    "Egypt"/"ancient Egypt" and "Elizabeth I"/"Queen Elizabeth I" are each one
    option twice, and a four-option question with a twin in it is a
    three-option question that looks like four."""
    t = _FOLD_CACHE.get(s)
    if t is not None:
        return t
    t = _STRIP.sub(" ", s.lower())
    t = re.sub(r"\s+", " ", t).strip()
    prev = None
    while t != prev:
        prev = t
        t = _LEAD.sub("", t)
    _FOLD_CACHE[s] = t
    return t


def twins(a, b):
    """ONE implementation. There used to be a cached private copy for the hot
    loop, the public one got the suffix rule, and New Guinea shipped beside
    Papua New Guinea because the copy the picker calls never got it."""
    fa, fb = fold(a), fold(b)
    if not fa or not fb:
        return a.lower() == b.lower()
    if fa == fb:
        return True
    # one inside the other on word boundaries, at either end: "Congo" /
    # "Congo River", and "Elizabeth I" / "Queen Elizabeth I"
    wa, wb = fa.split(), fb.split()
    return (
        wa == wb[: len(wa)]
        or wb == wa[: len(wb)]
        or wa == wb[-len(wa) :]
        or wb == wa[-len(wb) :]
    )


def display_case(s):
    """Every option starts with a capital, so none of them can be marked out by
    starting without one.

    The tell this removes worked in BOTH directions on the shipped pack:
    `ear / sun / Nose / head` (the capitalised one is the answer) and
    `Casey at the Bat / Fencing / bullfighting / Softball` (the lowercase one
    is). Neither needs the question to be read.

    Names that are deliberately lower-initial keep their own spelling: iPod is
    not IPod, and e.e. cummings is not E.e. cummings.
    """
    if not s or not s[0].isalpha() or not s[0].islower():
        return s
    if " " not in s[:4] and any(c.isupper() for c in s[1:4]):
        return s  # iPod, eBay, iMac -- but not "de Gaulle" or "du Maurier"
    if s[1:2] == ".":
        return s  # e.e. cummings
    return s[0].upper() + s[1:]


# --- selection ---------------------------------------------------------------
MIN_TYPE_POOL = 14  # below this a type cannot supply plausible distractors
DISTRACTORS = 6  # stored; the device draws 3, so a replay differs

# A band around the answer's length, so the answer never stands out by being the
# longest or shortest option -- a tell that needs no knowledge at all.
LEN_RATIO = 0.55
LEN_FLOOR = 2


def length_ok(answer, candidate):
    la, lc = len(answer), len(candidate)
    return max(LEN_FLOOR, la * LEN_RATIO) <= lc <= la / LEN_RATIO


def _words(s):
    return len(s.split())


def _plural(s):
    """"Thyroid" against "Gills", "Eyes" and "Ears" is a tell that survives
    every other filter: three options are plural and the answer is not."""
    w = s.rsplit(" ", 1)[-1].lower()
    return w.endswith("s") and not w.endswith("ss") and not w.endswith("us")


_INNER_SHOUT = re.compile(r"[a-z][A-Z]{2}")


def odd_case(s):
    """`neBRAska` is in the pack as an answer to a spelling clue, and as a wrong
    option it is the only thing on the screen shaped like that. Acronyms are
    entirely upper and survive; McCartney and DiMaggio have one inner capital
    and survive."""
    return bool(_INNER_SHOUT.search(s)) and not s.isupper()


def kind_word(s):
    """The last word of a place name usually says what kind of place it is:
    Hudson BAY, Baltic SEA, Lake CHAMPLAIN. An option set that mixes them makes
    the odd one out visible without reading the question."""
    w = s.rsplit(" ", 1)[-1].lower().strip(".,'\"")
    return w if w in KIND_WORDS else None


KIND_WORDS = {
    "bay", "sea", "ocean", "gulf", "lake", "river", "sound", "strait", "channel",
    "canal", "island", "islands", "isle", "mountain", "mountains", "peak", "range",
    "desert", "valley", "falls", "cape", "peninsula", "volcano", "glacier",
    "war", "revolution", "rebellion", "battle", "treaty", "act", "amendment",
    "university", "college", "school", "institute", "museum", "cathedral",
    "sea.", "county", "province", "republic", "kingdom", "empire", "dynasty",
}


def choose(index, item, key, used, rng):
    """Six wrong options for one question, or [] for quizmaster-only.

    Every filter here answers one thing a cold player could say about an option
    on the shipped pack without knowing the fact: it is not that kind of thing,
    it did not exist yet, it is not in the place the question named, it is the
    only one written in lower case, it is another option said twice.
    """
    if key is None:
        return []
    pool = index.pools.get(key, ())
    if len(pool) < MIN_TYPE_POOL:
        # No backing off to a wider kind. Every hypernym worth the name merges
        # things a player tells apart on sight -- a composer for "this
        # novelist", an octopus for "this reptile", gills for "this gland" --
        # so a type too small to fill its own options gets none.
        return []

    if odd_case(item['a']) or display_case(item['a'])[:1].islower():
        # The spelling clues ("A 'bra' is supporting this state") want the
        # shout -- it IS the question. They just cannot be multiple choice,
        # because the answer is then the only option shaped like that, and
        # display_case deliberately preserves it. neBRAska, monTANa and
        # virGINia each shipped against six normally-spelled states: the tell
        # this module claims to remove, in its worst direction. The same goes
        # for a name that keeps its own lower-initial spelling -- iPod, or
        # e.e. cummings against six capitalised poets.
        return []

    clue = item['q']
    low = clue.lower()
    year = clue_year(clue)
    answer = item['a']
    alts = tuple(item.get('alt', ()))
    banned = {answer.lower()} | {a.lower() for a in alts}

    # The place rule. It only bites when the clue names the region the ANSWER is
    # in, because that is the only arrangement a player can exploit: "the
    # financial hub of Switzerland" against Stockholm, Charlotte and Boulder.
    named = index.names_in(clue)
    need_place = None
    for name in named:
        if index.assoc.get(answer, {}).get(name, 0) >= 2:
            need_place = name
            break

    cands = []
    for c in pool:
        if c.lower() in banned or not length_ok(answer, c):
            continue
        if odd_case(c) or c[:1].isalpha() != answer[:1].isalpha():
            continue
        if not existed(c, year):
            continue
        if year is not None:
            span = index.window.get(c)
            if span and not (span[0] <= year <= span[1]):
                continue
        if need_place and not index.assoc.get(c, {}).get(need_place):
            continue
        cands.append(c)
    if len(cands) < 3:
        return []

    # "Hudson Bay" against "Baltic Sea" is the same tell in miniature: the
    # answer is the only Bay on the screen. Applied only when it leaves enough
    # to draw from, because a rule that empties the pool ships no question.
    want_kind = kind_word(answer)
    if want_kind:
        same = [c for c in cands if kind_word(c) == want_kind]
        if len(same) >= 3:
            cands = same

    # Word count is a shape a player reads before the words: a one-word answer
    # among three two-word options is visible from across the table. Preferred,
    # not required, because requiring it empties the small pools.
    want = _words(answer)
    plural = _plural(answer)
    attest = index.attest.get(key, {})
    topics = index.topics_in(clue) - {answer}
    rng.shuffle(cands)
    cands.sort(
        key=lambda c: (
            abs(_words(c) - want) > 0,
            _plural(c) != plural,
            not (topics & index.topic.get(c, frozenset())),
            used[c.lower()],
            -attest.get(c, 0),
        )
    )

    picks = []
    for c in cands:
        # Against the alternates too: the answer "Davis" also accepts
        # "Jefferson Davis", so offering "Jefferson" offers half of it.
        if twins(c, answer) or any(twins(c, a) for a in alts):
            continue
        if fold(c) and fold(c) in low:
            continue          # the clue says it, so it is visibly not the answer
        if any(twins(c, p) for p in picks):
            continue
        picks.append(c)
        if len(picks) == DISTRACTORS:
            break
    if len(picks) < 3:
        return []
    for c in picks:
        used[c.lower()] += 1
    return picks


def redistract(items, rng):
    """Rewrite every option set in place. Returns (index, kept, dropped, used)."""
    index = TypeIndex(items)
    used = collections.Counter()
    kept = dropped = 0
    # Least-used-first spreads a pool across its whole type, so the order the
    # questions happen to sit in must not decide who gets the popular options.
    order = sorted(range(len(items)), key=lambda i: rng.random())
    for i in order:
        x = items[i]
        picks = choose(index, x, index.keys[i], used, rng)
        if len(picks) >= 3:
            x["w"] = picks
            kept += 1
        else:
            x["w"] = []
            dropped += 1
    for x in items:
        x["a"] = display_case(x["a"])
        x["w"] = [display_case(w) for w in x["w"]]
        if x.get("alt"):
            x["alt"] = [display_case(a) for a in x["alt"]]
    return index, kept, dropped, used
