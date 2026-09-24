# Underhand

A port of **Underhand** (Spoopy Squad, Cornell GDIAC, 2017), the cult card
game. The engine is written from scratch against the original's compiled
rules; the screens come later and are designed fresh for e-ink, with none of
the original's art or animation.

Status: **engine only.** `src/apps_local/underhand/` holds the card reader and
the rules. Nothing is registered on the shelf yet.

## The cards

The cards are the original game's writing and stay out of this repository. The
engine reads the game's own two files, unmodified, as they come out of its APK:

    assets/json/savedatafiletemplate.json   the seven gods and their unlocks
    assets/json/cardwip.json                118 cards, 278 options

`CardsReader` streams them through `lib/JsonParser`'s `StreamingJsonParser`,
a chunk at a time, into fixed arrays and one 20KB text store (the game's text
uses 15.6KB). It accepts the file exactly as shipped, which is not strictly
valid JSON: strings carry raw tabs, one option has `outputtext` twice, and
`numcards` is a number or a list depending on the shuffle. It refuses data the
rules could not play (a weight of 0, a card or god that does not exist, a
random shuffle asking for more distinct cards than its range holds) instead of
hanging on it the way the original would.

## The rules

`UnderhandEngine.h` is the whole game: `start`, `choose`, `endForesight`,
`finish`. A `Game` is a plain struct, saved by writing its bytes and checked
with `valid()` when read back. Random numbers go through `Random::below` in
the order the original draws them, with one exception: the original also
draws one to time its radio clips, depending on the clock.

- **Resources**: relic, money, cultist, food, prisoner, suspicion. A normal run
  starts with 2 money, 2 cultists, 2 food, 2 prisoners; the tutorial run
  starts empty.
- **Deal**: each initial card on a coin flip; quest openers that are marked
  initial always. Quest openers become initial by tier: A Fishing Trip and
  Start a Tea Shop at 3 gods summoned, A Mysterious Egg and The Call of
  Uhl'uht'c at 5. The two unlock cards of a god just summoned are dealt into
  the next run, and only that one. After the shuffle, Rumors of Darkness goes
  into the top five, and during the tutorial the tutorial card goes on top.
- **Reshuffle** (the start of a run, and whenever the deck runs out): the
  discard pile and what is left of the deck are pooled, and the new order is
  picked one card at a time with chance proportional to `weight`. Heavy cards
  (the quest steps, 50 and 100 against 10) come up early.
- **Paying**: any exact payment (`exact()`). Relics stand in for anything;
  on options marked `cultistequalsprisoner` cultists and prisoners pay for
  each other in any split. `suggest()` proposes one: the named resources
  first, prisoners before cultists, relics only for what is short. A cost of
  420 is half of what is held when the card is drawn, rounded up; 840 is
  payable only while none is held. A random cost (Greed's) takes that many
  resources, a random held kind at a time.
- **Losing options are locked** while another option on the card can be
  paid, checked in order, each against the others as they stand by then.
- **After a choice**: shuffled-in cards go to the discard pile (straight onto
  the deck during the tutorial); the played card goes to the discard pile only
  if it is `isrecurring`, otherwise it leaves the run.
- **Foresight**: the next three cards; with discard, any of them can be sent to
  the discard pile, where they come back at the next reshuffle.
- **Punishments**, checked before each draw once the tutorial is done, never
  straight after another one, at most one, placed on top: Greed at 16+
  resources (35%, +15 points each, certain at 21), Police Raid at 5+ suspicion
  (same curve), Desperate Measures with no food (20%).
- **Winning** summons the option's god and remembers it as the last one.
  Winning the tutorial run finishes the tutorial; losing it does not.

### Where it differs from the original, on purpose

- **Foresight with fewer than three cards left** reshuffles and then shows the
  new top three. The original reshuffles and shows nothing, which players of
  the original called unfair.
- **A card nothing can pay for ends the run** (`LossReason::Stuck`), where the
  original would sit there forever. It has never happened in simulation.
- **Running out of cards ends the run** (`LossReason::NoCards`), where the
  original reads past the end of an empty deck. It has never happened either.
- **Unlocks are spent when the next run starts**, as the original's save file
  records it. In the original, abandoning that run and starting another in the
  same session dealt them again.

One assumption is not settled by the decompilation: that a choice's rewards
are in hand before the next card's punishment roll. The original adds them as
an animation finishes, so the two may race.

## Tests

    host-tests/underhand/run.sh
    UNDERHAND_DATA=<folder with the two JSON files> host-tests/underhand/run.sh

The first form builds small card files in the original's JSON shape and
scripts every random number, so each expectation is worked out by hand from
the rule: the exact order of a reshuffle, the punishment thresholds to the
percent, exact payment with relics and swaps, the losing-option lock, the
deal and where Rumors lands, what a win records and when unlocks are spent,
foresight's discard positions, save validation. Twelve deliberate breakages
of the rules were each caught.

The second form also plays the real cards, in about three seconds:

- every card is reachable from what the engine deals, walked exactly;
- 400 careful players, up to 60 runs each, checking at every step that
  nothing goes negative, no pile overflows and no state gets stuck. They win
  about 15% of runs and lose mostly to Police Raid and Wrath of the Gods;
- 2,000 explorers, whose hand is re-dealt every turn and who wander off the
  quests half the time, must summon all seven gods and draw all 118 cards.
  They do, the rarest god 191 times.
