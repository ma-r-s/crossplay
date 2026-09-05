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

|                                           | shipped pack | rebuilt    | how it was counted                    |
| ----------------------------------------- | ------------ | ---------- | ------------------------------------- |
| two options that are one thing            | 5.3%         | **0.0%**   | every stored set                      |
| options not capitalised alike             | 9.9%         | **0.0%**   | every stored set                      |
| a region named, one option in it          | 15.5%        | **5.5%**   | 400 dealt sets                        |
| an option of another kind                 | 12.8%        | **1.0%**   | 400 dealt sets, see caveat            |
| an option out of its own time             | 2.2%         | 1.8%       | 400 dealt sets, independent half only |
| answer is the longest option (chance 25%) | 13.5%        | 13.5%      | 400 dealt sets                        |
| playable as multiple choice               | 14,388       | **18,485** |                                       |

Seeds 1/2/3, so none of it is one lucky draw: kind 12.8/9.0/10.8 -> 1.0/2.2/1.5,
region 15.5/14.8/12.0 -> 5.5/5.2/5.0, period 2.2/1.8/2.8 -> 1.8/2.2/1.5.

**Two of these numbers do not mean what they look like, and a cold review of
the sampler is what established that.**

- **The period row is flat, and that is the honest reading.** The check had two
  halves and one of them was literally the picker's own `existed()` table, so
  it went 36 -> 0 by construction and cannot fail. Adding the halves together
  turned a tautology into a 38.5% -> 5.6% headline. The shared half is now
  printed on its own line and excluded; what is left moved 2.2% -> 1.8%, which
  is noise. **The anachronisms this fix removes are the ones somebody wrote
  into a table.**
- **The kind row is largely circular.** The sampler's rule agrees with the
  picker's head noun 99.8% of the time, and 73% of options are certified "right
  kind" by the same corpus token that put them in the pool. Two deliberately
  wrong merges (queen into king, novel into film), each changing over ten
  thousand option sets, moved this count by ZERO. Read it as "the
  first-word-after-this mis-typing is gone" -- which it does prove, because
  that mutant IS caught -- and not as "the options are the same kind of thing".

The two rows counted exhaustively are the ones to trust without caveat, and the
region row is the most independent of the sampled ones.

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

| Dropped | Why                                                                    |
| ------- | ---------------------------------------------------------------------- |
| 72,867  | wordplay category -- the category IS the puzzle (`WO"RR"DS`, `____UM`) |
| 60,845  | category-dependent -- no demonstrative, meaningless standalone         |
| 14,057  | too long or too short for the screen                                   |
| 10,745  | media clue or presenter aside (`(Hi, I'm Marvin Hamlisch...)`)         |
| 9,079   | **the show itself declares a constraint** (see below)                  |
| 6,981   | no usable difficulty (daily doubles, unscored)                         |

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

| Event                   | Clues  | Why it matters                         |
| ----------------------- | ------ | -------------------------------------- |
| Tournament of Champions | 20,969 | much harder than a normal game         |
| Teen                    | 15,995 | easier, and skewed to teen pop culture |
| College                 | 15,218 | skewed to campus/current topics        |
| Champions Wildcard      | 6,220  | hard                                   |
| Teachers                | 5,937  | skewed academic                        |
| Celebrity               | 4,012  | much easier, celebrity-focused         |
| Kids                    | 3,665  | easiest                                |

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

- _To prevent Portugal from claiming the Spice Islands, he set out to sail around the world_ = **Magellan**
- _Prometheus' brother, cursed to bear the sky upon his back_ = **Atlas**
- _Lausanne in this country is home to the International Olympic Committee_ = **Switzerland**

Bottom:

- _Contrary to Whittier's poem, Mary Quantrill, not this title woman, waved the Union flag_ = **Barbara Frietchie**
- _As indicated in Daniel 9:3, they're what a penitent person is said to be wearing_ = **sackcloth and ashes**

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

While a question is on screen you are already reading it. HIDE marks it bad; the
device sets the question's `FLAGGED` bit in `/trivia/pack.state`, and its reason,
if the player gave one, goes in `/trivia/reports.dat`
(`tools_local/trivia/reports.py` is the format). Read them back off the card
with:

```bash
python3 tools_local/trivia/collect_flags.py /Volumes/<card>/trivia/pack.dat --apply
```

**This paragraph used to describe a `flags.txt` that nothing ever wrote.** No
file of that name has existed anywhere in the tree, no tool read a card, and
`verdicts.tsv` accumulated exactly one verdict in its lifetime -- so the loop
below was described, believed and never run. Board card #257.

`collect_flags.py` REFUSES rather than resolves whenever it cannot prove an
index still names the question it named: a `pack.state` whose length is not the
pack's count, a manifest for another build, a reports queue filed against a
different pack. Downstream a verdict is applied without review, so a wrong id
deletes a question nobody reported and the only symptom is a slightly smaller
pack.

**Why this is the important one**: you will only ever flag questions you actually
saw, which is precisely the set worth judging. It turns curation from a 366,000-
question project into a byproduct of playing. And a question flagged at a bar,
with four people disagreeing about it, is judged better than anything a filter or
a model would decide alone.

## What this does NOT solve

Nobody fact-checks this corpus and neither does any of the above. The generality
score measures how _often_ an answer recurs, not whether the clue is _correct_.
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
notation: `(Luther) Burbank` yields _Burbank_ plus _Luther Burbank_, `spores (or
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

---

## Building the pack from a local rating run

Everything above builds the pack from the Jeopardy TSV, where difficulty is the
clue's dollar value. That source is **not in this repo and not on this machine**,
and the dollar value is a fact about a 1994 television show rather than about
four people in a bar. `tools_local/trivia/enrich_pack.py` rates the corpus
locally instead, and `tools_local/trivia/assemble_pack.py` turns that rating run
into a pack.

Two builds, two files, on purpose. `build_pack.py` still owns the TSV path: it
holds every filter that turns 544k raw clues into 50k shippable ones, and all of
it needs the source columns. `assemble_pack.py` starts from the output of that
work -- the decoded pack -- and only re-decides difficulty and options.

```text
build_pack.py       Jeopardy TSV      -> pack   difficulty from the dollar value
assemble_pack.py    corpus + ratings  -> pack   difficulty from the local rater
calibrate_levels.py corpus + ratings  -> advice which r maps to which level
```

### The command sequence, in order

The rating run appends to `.rate/enriched.jsonl` as it goes and the file is
usable at any point; a partial run just makes a smaller pack. Run this from the
tree holding the rating run.

```bash
# 1. Is the run finished? Rated rows against the worklist it was given.
#    (The rating tree may also hold its own untracked rate_status.py, which
#    prints the same thing with a verdict. This line needs nothing but the run.)
echo "$(cut -d'"' -f4 .rate/enriched.jsonl | sort -u | wc -l) rated of $(wc -l < .rate/rest40383.ids)"

# 2. Assemble. US-centric questions SHIP tagged (us:true); the device toggle
#    hides them by default. Nothing is deleted from the corpus or the ratings.
python3 tools_local/trivia/assemble_pack.py \
    --corpus .rate/corpus_repaired.jsonl \
    --enriched .rate/enriched.jsonl \
    --out .rate/out/pack.jsonl \
    --dat .rate/out/pack.dat

# 3. The gate. All 21 must pass; the pack is not shippable at 20/21.
python3 tools_local/trivia/test_pack.py .rate/out/pack.jsonl

# 4. What a cold player could exploit without knowing anything.
python3 tools_local/trivia/audit_options.py .rate/out/pack.jsonl

# 5. The one difficulty check the ratings cannot satisfy by construction.
#    Reads the run's own output; see "The levels against a blind panel" below
#    before treating a low number as a blocker.
python3 tools_local/trivia/panel_score.py .rate/enriched.jsonl

# 6. On the card, beside the .state file the build wrote next to it.
cp .rate/out/pack.dat  <sd>/trivia/pack.dat
cp .rate/out/pack.state <sd>/trivia/pack.state
```

**`pack.state` must be replaced together with `pack.dat`.** State is fixed-width
and addressed by index, so a state file left over from a different pack marks
the wrong questions seen. `assemble_pack.py --dat` writes a fresh zeroed one
beside the pack every time; copy both or neither.

**A device crossing this rebuild over the air cannot do that**, because the
download replaces `pack.dat` and never fetches a state file. It relies on the
firmware noticing that the state file's length no longer matches the pack's
count, and until 2026-09-04 that check only caught a state file that was too
SHORT. An assembled pack is SMALLER than the 50,000 it replaces, so the state
file left behind is too LONG, which was the case the check could not see.

**Until 2026-09-04 the finished pack would have missed by a handful, not by
thousands**, which was the worst version of it. The run's worklist is
`rest40383.ids` plus the 9,617 already rated, i.e. the whole 50,000 corpus, so
the only questions it dropped were the ones `assemble_pack.py` rejects as
unanswerable -- 9 of the first 25,875. A pack of roughly 49,98x against a
50,000-byte state file is a misalignment nothing on screen can show and nobody
would think to look for.

**Dropping US-centric questions made that gap loud rather than subtle**, and
did not make it go away. They are 22.7% of the rated rows, so the finished pack
lands near 38,600 rather than 49,98x, and a stale 50,000-entry state file now
misses by more than eleven thousand. That is easier for the length check to
catch, not harder -- but the check is still the only thing catching it, and a
device on older firmware is in exactly the same trouble it was before, just
further out of alignment.

Fixed at `PackState::open` and `TriviaActivity::ensureState`; a device on
firmware older than that will reuse stale `FLAGGED` bytes against the new record
order and hide arbitrary questions from every draw.

### The levels against a blind panel

`difficulty_panel.tsv` is 50 pairs that three blind judges unanimously called
(easier, harder), frozen. It is the only difficulty check here that a wrong
rating can fail: everything else -- a stocked tier, monotone means, a wide 1-to-5
gap -- is computed from `r`, and `d` is CUT from `r`, so those are arithmetic.
Shuffle the ratings across questions and they all still pass; the panel drops to
34%.

| ratings                                              | panel agreement |
| ---------------------------------------------------- | --------------- |
| the cloud judge's, which the pairs were sampled from | 50/50 = 100%    |
| the LOCAL run, which builds the shipping pack        | 35/50 = 70%     |
| the local run, raw `r`, thresholds removed           | 38/50 = 76%     |
| the local run, shuffled across questions             | 17/50 = 34%     |

That second row was 36/50 until the levels were recalibrated for the
international-only pack. The interim floors `(9, 8, 7, 5)`, chosen on the
partial 2026-09-04 run, spanned level 5 across r 0-4 and a band that wide stops
separating pairs the old cut told apart at the hard end, doubling the tied pairs
from 5 to 10. When the run finished (49,980 rated, 2026-09-05) the thresholds
were re-derived on the full population to `(9, 8, 6, 4)`, which pulls level 5
back to r 0-3; that narrows the wide bottom band and brings the tied pairs back
down to **7**, while level agreement holds at 35/50 and raw `r` stays 38/50
throughout. All of the movement is the cut and none of it is the rater. See
"Recalibrating the thresholds". Still worth revisiting against a fresh panel
drawn from this rater, since these pairs were sampled under an earlier cut.

**Do not read that first row as quality, and do not read the second as a
verdict on the local rater.** Candidates were sampled as level 1 against level 5
_as the cloud file cut them_, so that file is being scored on its own extremes
and every other rater on somebody else's. No threshold here transfers.

The cloud file is also the noisy one, which is the finding that put PR #19 on
hold: **9,597 of its 9,617 rows are single-judge** (only the 20 anchors carry
more), a fresh judge agrees with it at +0.58 / +0.64, and two fresh judges agree
with **each other** at +0.92 / +0.895. The two files agree with each other at
Spearman +0.34 on the 9,597 questions both cover, and their scales differ --
`local/local_difficulty_intl.tsv` says so in its own header -- so their means are
not comparable either.

What survives is 50 blind pairwise verdicts, re-scorable against any future
rater, and the shape of a check that a wrong rating can fail. **To use it as a
bar, draw a fresh panel from the shipping rater's own extremes.** Until then
`panel_score.py` prints numbers and no verdict, exits 0, and is not in
`check.sh`.

### The fresh panel, drawn from the local rater (2026-09-04)

That last instruction was carried out. `make_local_panel.py` drew 90 pairs from
the local run's own 27,301-row snapshot, one judge decided them blind, and
`local_panel_score.py` scored them. Panel and verdicts are in
`tools_local/trivia/local_panel/`; it is not in `check.sh` either, for the same
reason plus a sharper one: the bar is now **one** judge's opinion, not three.

**The headline is 73/88 = 83%, and the headline is not the evidence.** Sample a
panel from the extremes alone and any ordered rater approaches 100%; sample it
from adjacent levels and every rater alive approaches 50%. So the pairs were
stratified by how far apart the rater put them, because a rater emitting
well-spread NOISE scores ~50% in every band and only a rater carrying signal
falls as the band narrows:

| gap band           | pairs | agreement |
| ------------------ | ----- | --------- |
| wide, \|dr\| >= 6  | 30    | 96.7%     |
| mid, \|dr\| 4-5    | 30    | 90.0%     |
| narrow, \|dr\| 2-3 | 30    | 60.7%     |

Monotone, and the judge's own confidence -- written down before the key was
opened -- tracks it: 97.6% on the calls marked high, 42.1% on the ones marked
low. The disagreements are concentrated in the pairs the judge already knew were
guesses. **The mechanism works. The levels are not noise.**

**One real defect, measured rather than inferred from the panel.** Fifteen
disagreements are far too few to read a pattern off, so each hunch was turned
into a corpus-wide query that could come back negative (`local_panel_probes.py`,
which keeps both outcomes). The one that held: on 156 rows where the clue NAMES
the answer's country and the answer is that country's capital -- Oslo from "this
Norwegian capital" -- **23% still ship at level 3 or worse.** The rater grades
the obscurity of the clue's wrapper (a museum, a librarian, a theme park)
instead of asking whether the answer is reachable from the words the player
sees. It costs a slab of genuinely easy questions to the middle tiers; it does
not reorder the scale.

The hunch that did NOT hold is kept in the same tool: a Pentagon clue rated
easiest in the panel suggested the rater's floor of general knowledge was
American, but across 442 rows whose clue says "U.S." or "American" outright and
whose `us` flag said false, mean `r` is 5.39 against the rest at 6.35. The rater
applies the international correction through `r` even where its own flag missed.
One bad row was one bad row.

### Three rules in that tool, each of which fails silently when undone

### Four rules in that tool, each of which fails silently when undone

**Join on the corpus's stored id. Never re-derive it.** Ids are a sha1 of
normalised clue text and `pack_format.py` re-derives them, which is right when
the text is the text the id was made from. It is wrong here:
`corpus_repaired.jsonl` deliberately carries pre-repair ids beside post-repair
clue text, because the pack is addressed by index and moving an id moves a
question out from under `pack.state`. **349 rows are in that state.** Re-deriving
drops their ratings, an unrated question is simply excluded, and the result is
a slightly smaller pack with no error anywhere. The tool prints what re-deriving
would have cost on every run, which is the only place that number is visible.
Board card #146.

**A question with fewer than three sound options loses the `w` key entirely.**
Not a shortened key: no key. A two-option set fails `test_pack.py`'s "every MC
has at least 3 distractors" and would put two choices on a four-option screen.
Without the key the question is quizmaster-only and completely correct. Topping
these up is board card #172.

**`r` maps to `d` by fixed absolute thresholds, never by quantile.** Each level
is a band on the rater's own 0-10 scale (`LEVELS` in the tool). A quantile
mapping would be perfectly balanced and would redefine every level each time the
run is re-cut, so "level 3" would mean one thing at 20,000 rated rows and
another at 40,000, and a player's difficulty setting would mean nothing across
builds.

Fixed does not mean arbitrary. The constants are still chosen on a population,
and when that population changes they have to be chosen again -- as fixed
numbers, on the new population. See "Recalibrating the thresholds" below.

**US-centric questions ship TAGGED, and the device hides them by default.**
Mario's call on 2026-09-04: they should not show up until a toggle is written;
2026-09-05, the toggle (#191/#223). The pack now carries every us_centric
question with a `us` flag (bit 7 of the difficulty byte), and the
`triviaShowUsCentric` setting decides at runtime whether the chooser deals them
(TRIVIA > SETTINGS > US QUESTIONS since card #311; the stored key is unchanged).
The default is hidden, so the pack is international by default. `enrich_pack.py`
still writes `us` as a field rather than a deletion, every US-centric row keeps
its rating, and no re-rating is ever needed to flip the toggle. Deleting the
rows instead would cost thousands of ratings that took hours of local model time
and cannot be recovered from the pack. The r->d levels stay calibrated on the
international-only population (`calibrate_levels.py`), because a level means
"hard for an international table" -- which is what the default player sees.

`test_assemble.py` exists to make undoing any of the four loud, and
`check.sh --tests` runs it.

### Recalibrating the thresholds

Dropping the US-centric questions broke the difficulty spread, and the reason is
real rather than a bug: **for an international table, the US-centric questions
genuinely are the hard ones.** Measured on the 2026-09-04 run, mean `r` was 1.30
for them against 6.36 for the rest, and under the old floors 86% of them sat in
level 5. Take them out and level 5 goes from 5,926 questions to 845, a 4.2%
tier, and `test_pack.py` fails `difficulty reasonably spread` at 20/21.

The fix is different fixed numbers measured on the international-only
population, so that a level means "hard for an international table" -- which is
what the ratings measure and what the pack now contains. `LEVELS` moved from
floors `(9, 7, 5, 3)` to `(9, 8, 7, 5)`:

| level | r band | before (US kept) | after |
| ----- | ------ | ---------------- | ----- |
| 1     | 9-10   | 3,772            | 3,754 |
| 2     | 8      | 6,883            | 3,849 |
| 3     | 7      | 5,165            | 3,022 |
| 4     | 5-6    | 4,222            | 5,101 |
| 5     | 0-4    | 5,926            | 4,337 |

Bands are narrow at the easy end and wide at the hard end because that is where
the international mass sits. Level 1 still means "9 or 10 groups in 10 get it"
and level 5 still means "at most 4 do"; the numbers are absolute, not quantiles.

**The interim `(9, 8, 7, 5)` cut had a measured cost**, recorded rather than
buried: level 5 spanned r 0-4, which separates fewer of the blind panel's hard
pairs than the old cut did, doubling the tied pairs from 5 to 10. On the partial
run no candidate that kept the hard end sharper also passed the spread check on
every view, so the trade was forced at the time.

**2026-09-05: the run finished (49,980 rated) and the thresholds were
re-derived on the full international population (38,883 rows), moving `LEVELS`
from `(9, 8, 7, 5)` to `(9, 8, 6, 4)`.** On the completed run the old floors
failed the calibrator's "rated last" view at 3.587 (past the 2.5 limit), i.e.
level 5 over-stuffed; `(9, 8, 6, 4)` is the minimax pick (worst spread 2.513)
and pulls level 5 back to r 0-3. Full-population international tiers are now
`{1: 6630, 2: 6511, 3: 10437, 4: 9968, 5: 5337}`, spread 1.96, and the narrower
level 5 brings the blind panel's tied pairs back down from 10 to 7. The
`CALIBRATED ON` comment in `assemble_pack.py` carries the date and population.

**Re-derive the constants whenever the run changes** -- one command, which
prints how the shipped constants stand on the current data and what it would
choose instead:

```bash
python3 tools_local/trivia/calibrate_levels.py \
    --corpus .rate/corpus_repaired.jsonl \
    --enriched .rate/enriched.jsonl
```

It prints rather than edits, deliberately: constants a build step rewrites are
quantiles with extra steps. Adopting a new set means editing `LEVELS`, updating
the `CALIBRATED ON` comment above it with the date and population, rebuilding
and re-running `test_pack.py`.

It scores candidates by their **worst** spread ratio across three views of the
run -- all rated rows, the half rated first, the half rated last -- not by the
best on the pooled data. `enriched.jsonl` is append-ordered and mixes rows rated
under different shot configurations, so its halves genuinely disagree: on
2026-09-04 `r=5` was 20.1% of the first half and 7.7% of the second. A candidate
tuned to the pooled distribution can look best and then fail on the rows still
to come, and the half rated last is the closest proxy for those. On that day
`(9, 8, 7, 5)` was the only candidate that passed on all three views (worst
1.98); the runner-up already failed at 2.70.

### The longest-option defect, and why banding alone did not fix it

Model-generated options failed `test_pack.py`'s guessability check: the answer
was **strictly the longest of the four options 20.6% of the time**, against a bar
of 15% and a chance rate of 25%. An answer visibly longer than every option is a
tell a player uses without knowing the subject at all.

Head to head on the same 7,768 questions, the old rule-based picker was 4.2%
strictly longest and 72.2% tied-longest; the model 20.7% strict and 12.4% tied.
**Mean option length was 7.5 characters against a 7.7-character answer in both.**
The model is not writing short options. It is writing options of scattered
lengths -- stdev 1.63 against the picker's 0.49 -- and scatter is what leaves the
answer alone at the top. The cause is spread, not bias, and that is why raising
the option lengths would not have helped.

`distractors.length_ok` is the band the rule-based picker already uses. Applying
it moved 20.6% to 15.7%: better, and still failing. **Banding cannot finish the
job, because "the answer is the longest" is a property of the SET.** Three
options each individually inside the band can still all be shorter than the
answer.

So the guarantee is constructed rather than sampled for: **every option set
carries at least one option at least as long as the answer**, and the set is
then filled by closeness in length. The answer is never strictly longest, on
every draw the device can make, rather than on average. A question whose
candidates cannot supply a covering option keeps its clue and loses its options.

Rule-based candidates top up the model's three, so a set that cannot reach three
in band on model options alone usually still can.

### Exactly three options are stored, where the TSV build stores six

`build_pack.py` stores six and lets the device draw three, so replaying a
question varies. `assemble_pack.py` stores three. Two reasons, and the trade is
deliberate:

- **Six is not available for half the pack.** 8,582 of 16,680 questions with a
  sound option set have only three candidates that survive the band at all.
- **With three stored, the set on the panel is the set the gate checked.** The
  cover guarantee is then exact rather than a property of one sampled draw of
  three from six. Storing six leaves the answer strictly longest on about 4.6%
  of possible draws while the first three read 0.0%, which is precisely the kind
  of number that passes a test and reaches a player anyway.

The cost is that replaying a question shows the same three options.

Three is the exact boundary of the device's draw, so it is covered rather than
assumed. `buildChoices()` runs a partial Fisher-Yates `kOptions - 1` times over
the stored pool, which at three stored is a COMPLETE shuffle: every stored
distractor is used, and the option set is forced to be the answer plus all
three. One fewer and the loop would index a slot of `pool[]` it never
initialised, so `playableAsChoice()` (`wrongCount_ >= kOptions - 1`) is
load-bearing and not cosmetic -- it is the only thing that keeps a short `w` out
of the draw, which is the second reason rule 2 above drops the key entirely
rather than storing two. `testChoicesAtThreeOptions()` in
`host-tests/trivia/test_trivia.cpp` pins both shapes (three, and `w` absent),
and `host-tests/trivia/run.sh` now round-trips a three-distractor record through
the real writer as well.

### What a run of this looks like

The finished run (49,980 rated, US-centric shipped tagged and hidden by
default):

```text
corpus            : 50,000
ratings           : 49,980
  ids that moved  : 349 rows carry a pre-repair id
  re-derive would : lose 349 ratings SILENTLY (card #146)
  dropped      22  rejected: unanswerable
  dropped      20  unrated (not yet reached by the run)

pack              : 49,958
  solo MC ready   : 37,650 (3 stored options each)
  read-aloud only : 12,308 (no sound option set; card #172)
  us-centric      : 11,075 (tagged; hidden by default, toggle shows)
  difficulty full : {1: 6651, 2: 6513, 3: 10470, 4: 10473, 5: 15851}  (toggle ON)
  difficulty intl : {1: 6630, 2: 6511, 3: 10437, 4: 9968, 5: 5337}  (default)
```

`test_pack.py` passes 21/21 on that pack. The spread gate wants no tier more
than 2.5x the smallest, measured on the international default (US hidden): with
the `(9, 8, 6, 4)` floors that is 10,437 against 5,337, a ratio of 1.96.

This has now happened once, which is what "Recalibrating the thresholds" above
is about. When `difficulty reasonably spread` fails, look at the r histogram and
move `LEVELS`, once, deliberately, and say so here. **It is not to switch to
quantiles, and it is not to weaken the check** -- the check exists because
levels 1 and 5 feeling identical is the complaint that started this work.
`calibrate_levels.py` is the histogram-reading step, written down so the next
person does not have to re-derive the method along with the numbers.
