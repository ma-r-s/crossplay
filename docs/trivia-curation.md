# Keeping the good questions and losing the bad ones

The corpus is 366k questions. **Do not try to curate that by hand or by LLM.** In
a year of bar nights you will see perhaps 5,000 of them. Curating the other
361,000 is work spent on questions nobody will ever read.

So: **rank, ship a slice, and let play find the rest.** Three layers, cheapest
first.

## Layer 0 -- the options, which are not curation but read like it

A question can be perfectly curated and still be free. On the pack shipped as
`trivia-pack` in August, a cold reader given 42 four-option sets answered **30
of them without knowing the fact**, from the options alone. That is not a
question-quality problem and no verdict file can fix it.

Measured with `tools_local/trivia/audit_options.py`, which deals 400 sets the
way the device does (the answer plus three of the six stored distractors) and
counts what could be used:

| | shipped pack | rebuilt |
| --- | --- | --- |
| an option of another kind | 12.5% | **1.2%** |
| options not capitalised alike | 8.0% | **0.0%** |
| an option out of its own time | 11.2% | **2.5%** |
| a region named, one option in it | 15.5% | **4.5%** |
| two options that are one thing | 2.0% | **0.0%** |
| answer is the longest option (chance 25%) | 13.5% | 14.2% |
| playable as multiple choice | 14,388 | **15,959** |

Three samples, not one lucky draw. Seeds 1/2/3: kind 12.5/8.8/10.5 ->
1.2/0.5/1.2, case 8.0/6.5/4.8 -> 0.0/0.0/0.0, period 11.2/11.8/11.2 ->
2.5/1.2/1.0, region 15.5/14.8/12.0 -> 4.5/7.0/5.5, twins 2.0/1.5/1.0 ->
0.0/0.0/0.0.

Over the subset each check can actually score, the two that dominate move
21.9% -> 2.2% (kind) and 38.5% -> 7.5% (period). Coverage went UP, not down:
the questions lost to stricter typing are outnumbered by the ones a correct
head noun made typeable for the first time.

**Read what the sampler cannot see.** It prints the list on every run and the
list is longer than the numbers: it knows about 90 families of thing and
nothing else, it cannot tell a hard wrong option from an unfair one, two thirds
of clues name no year for it to check, and half its period rule is the same
hand-written table the picker uses. The 42-set human read is still the
instrument that found the problem, and nothing here replaces it.

How the options are built is in
[apps/trivia-pack-format.md](apps/trivia-pack-format.md); the code is
`tools_local/trivia/distractors.py`, imported by both `build_pack.py` and
`redistract.py`.

## Layer 1 -- automatic, at build time

Deterministic and re-runnable, so a corpus refresh (the dataset ships a new
season every July) costs one command.

Already dropped, with counts from the 544,110-clue source:

| Dropped | Why |
| --- | --- |
| 72,867 | wordplay category -- the category IS the puzzle (`WO"RR"DS`, `____UM`) |
| 60,845 | category-dependent -- no demonstrative, meaningless standalone |
| 14,057 | too long or too short for the screen |
| 10,745 | media clue or presenter aside (`(Hi, I'm Marvin Hamlisch...)`) |
| 9,079 | **the show itself declares a constraint** (see below) |
| 6,981 | no usable difficulty (daily doubles, unscored) |

The 9,079 are worth calling out. The dataset's `comments` column carries Alex's
spoken category explainers -- "Each correct response will begin with that letter
of the alphabet", "You have to identify the country", "Notice the spelling".
**That is the show telling you the clue does not stand alone.** It is a far better
signal than any heuristic, and I only found it by opening a column I had been
ignoring.

### Special editions, from the `notes` column

`notes` identifies which broadcast a clue came from, and 87,578 clues (16.1%) are
from special events. These are exactly the "special edition I don't want to play"
category:

| Event | Clues | Why it matters |
| --- | --- | --- |
| Tournament of Champions | 20,969 | much harder than a normal game |
| Teen | 15,995 | easier, and skewed to teen pop culture |
| College | 15,218 | skewed to campus/current topics |
| Champions Wildcard | 6,220 | hard |
| Teachers | 5,937 | skewed academic |
| Celebrity | 4,012 | much easier, celebrity-focused |
| Kids | 3,665 | easiest |

Every question in the pack carries its event tag (`ev`, empty for a regular
game), so any of these can be included or excluded with a switch rather than a
rebuild. Default: exclude all of them, because their difficulty is calibrated for
a different audience than four people in a bar.

### The generality score

The thing you asked for -- "eliminate questions that are really specific" -- has a
clean data answer that does not depend on my taste.

**How often has Jeopardy itself used this answer in 42 years?** Recurring answers
are general knowledge; answers used once are the specific tail.

```
score = 2*log(1 + answer_frequency) + log(1 + category_frequency) - 0.6 if special_event
```

Distinct answers: 132,069. **74,175 of them (56.2%) appear exactly once across 42
years.** The most recurrent are `australia, china, chicago, japan, france, spain,
india, california, mexico, canada` -- which is what general knowledge looks like.

It sorts convincingly. Top of the ranking:

- *To prevent Portugal from claiming the Spice Islands, he set out to sail around the world* = **Magellan**
- *Prometheus' brother, cursed to bear the sky upon his back* = **Atlas**
- *Lausanne in this country is home to the International Olympic Committee* = **Switzerland**

Bottom:

- *Contrary to Whittier's poem, Mary Quantrill, not this title woman, waved the Union flag* = **Barbara Frietchie**
- *As indicated in Daniel 9:3, they're what a penitent person is said to be wearing* = **sackcloth and ashes**

**The score ranks, it does not delete.** Nothing is thrown away; the pack is
sorted and you ship the top slice. Recommended slice: **top 50,000**, which is
800+ bar nights of the general-knowledge core.

## Layer 2 -- the verdicts file

Every question carries a stable `id`: the first 12 hex of a SHA-1 of its
normalised clue text. **Content-addressed, so it survives every rebuild** -- add a
season, change a filter, re-rank, and the ids of surviving questions do not move.

`verdicts.tsv` is the permanent record:

```
id            verdict  reason
8f2a1c4e9b03  bad      answer is wrong, it was Ceres not Vesta
c07d55a1e2f8  bad      needs the category
1a9e3f70b8cc  good     keep even though score is low
```

Hand-editable, diffable, and applied at pack time after ranking. A `bad` verdict
removes the question permanently; a `good` verdict pins it in regardless of score.

## Layer 3 -- flag it while playing

This is the only layer that scales, and the cheapest to build.

While a question is on screen you are already reading it. One button marks it
bad; the device appends the id to `flags.txt` on the SD card. At the next pack
build those ids merge into `verdicts.tsv`.

**Why this is the important one**: you will only ever flag questions you actually
saw, which is precisely the set worth judging. It turns curation from a 366,000-
question project into a byproduct of playing. And a question flagged at a bar,
with four people disagreeing about it, is judged better than anything a filter or
a model would decide alone.

## What this does NOT solve

Nobody fact-checks this corpus and neither does any of the above. The generality
score measures how *often* an answer recurs, not whether the clue is *correct*.
Old clues also age: "this Yugoslavian republic", "the current world record". The
`y` field carries the air year so a recency filter is available, but no automatic
check will catch a clue that was true in 1994.

Layer 3 is what catches those, which is another reason it matters most.

---

## The pack, as built

`tools_local/trivia/build_pack.py` produces it; `test_pack.py` guards it.

```bash
python3 tools_local/trivia/build_pack.py \
    --src <combined_season1-42.tsv> --out pack.jsonl \
    --limit 50000 --verdicts tools_local/trivia/verdicts.tsv
python3 tools_local/trivia/test_pack.py pack.jsonl
```

544,110 clues in, **303,516 survive the filters**, and the shipped slice is the
**top 50,000 by generality score** -- 7.9 MB, difficulty 13,385 / 11,166 / 9,875 /
8,536 / 7,038 across tiers 1-5. The slice's score floor (12.6) sits well above the
full pack's median (8.8), so it really is the general-knowledge core rather than
an arbitrary cut.

5,507 questions carry alternate accepted answers, parsed out of Jeopardy's own
notation: `(Luther) Burbank` yields *Burbank* plus *Luther Burbank*, `spores (or
seeds)` yields both. That recovers the alias capability I had said only TriviaQA
had.

### How "playable" was actually established

Filters cannot prove playability, so I read 70 questions by hand: 40 sampled
uniformly, then 30 from the **bottom** of the shipping slice, on the argument that
if the worst decile reads well the whole slice does.

That found exactly one systematic defect -- **Jeopardy's on-screen shorthand**
(`ran off to Switz.`, `int'l trade route`). The clues were written to be read on a
screen while a host spoke them; at a bar somebody reads them aloud, and "Switz."
is not a sentence. 5,175 clues are now expanded rather than dropped. It also found
one bad question (an expired present-tense claim about Ocalan), now the first
entry in `verdicts.tsv`.

A second suspected defect -- clues opening on a bare possessive -- turned out to
be a **false alarm**. "While fighting for his patent for the cotton gin, he made
arms for the U.S. government" is perfectly answerable. Dropping that pattern would
have cost 7,866 good questions for nothing.

### What the tests caught that reading did not

Writing the invariants found three defects 70 careful reads had missed: 65
shouted answers, 2 duplicates my normalisation was too loose to see, and later a
regression I introduced myself when a normalisation change silently broke the
answer-leak check.

One of the test's own assertions was also wrong: it flagged `E.T.`, `NAACP`,
`AT&T` and `R.E.M.` as shouting. They are acronyms and must stay upper. The rule
is now "an all-caps word of six or more letters", which catches `SWISS CHEESE
PLANT` and exempts the acronyms.

### The honest residual

**2.5% of the shipped pack makes a present-tense superlative or exclusive claim**
("is the only", "is the largest"), and 79% of those aired before 2005. Most are
still true -- Vancouver is still Canada's third-largest urban area, Hamburg still
Germany's second city -- so dropping 1,235 questions to catch perhaps a hundred
stale ones is a bad trade. They stay, and Layer 3 flagging is what will find the
ones that have gone off.

Nothing here fact-checks the corpus. The generality score measures how often an
answer recurs, not whether the clue is right.
