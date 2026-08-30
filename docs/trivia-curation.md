# Keeping the good questions and losing the bad ones

The corpus is 366k questions. **Do not try to curate that by hand or by LLM.** In
a year of bar nights you will see perhaps 5,000 of them. Curating the other
361,000 is work spent on questions nobody will ever read.

So: **rank, ship a slice, and let play find the rest.** Three layers, cheapest
first.

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
