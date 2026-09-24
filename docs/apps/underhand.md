# Underhand

A port of **Underhand** (Spoopy Squad, Cornell GDIAC, 2017), the cult card
game. The rules are written from scratch against the original's compiled code;
the screens are designed fresh for e-ink, with none of the original's art,
sound or animation. It sits at the end of the Games shelf.

`src/apps_local/underhand/`:

| File                        | What it is                                             |
| --------------------------- | ------------------------------------------------------ |
| `UnderhandOriginalData.h`   | the game's two card files, verbatim                    |
| `UnderhandCards.{h,cpp}`    | streams those files into fixed arrays                  |
| `UnderhandEngine.{h,cpp}`   | the rules: `start`, `choose`, `endForesight`, `finish` |
| `UnderhandSave.{h,cpp}`     | the save file                                          |
| `UnderhandView.{h,cpp}`     | the words and marks a screen shows for a game          |
| `UnderhandScreens.{h,cpp}`  | freestanding builders over plain models                |
| `UnderhandActivity.{h,cpp}` | the device: renderer, taps, save, shelf, audit         |
| `UnderhandIcons.h`          | generated symbols (see Icons)                          |

## The cards

The cards are the original game's writing, used with its authors' permission:
Mario asked Spoopy Squad, who said the text can be used (2026-09-24). The
permission covers the text only. The original's art and sounds are not used and
must not be.

The engine reads the game's own two files as they come out of its APK:

    assets/json/savedatafiletemplate.json   the seven gods and their unlocks
    assets/json/cardwip.json                118 cards, 278 options

They are embedded in `UnderhandOriginalData.h` as raw string literals, byte for
byte except that the CR of each CRLF is dropped. There is no conversion step
and no derived format: `CardsReader` reads the original text at every launch,
streaming it through `lib/JsonParser`'s `StreamingJsonParser` into fixed arrays
and one 20KB text store (the game's text uses 15.6KB). It accepts the files
exactly as shipped, which are not strictly valid JSON: strings carry raw tabs,
one option has `outputtext` twice, and `numcards` is a number or a list
depending on the shuffle. It refuses data the rules could not play (a weight of
0, a card or god that does not exist, a random shuffle asking for more distinct
cards than its range holds) instead of hanging on it the way the original would.

The APK, the extracted files and the decompilation live outside the repository,
in `~/Projects/Personal/Code/underhand-data/`: `json/` (the two files),
`decompiled/` (Ghidra's C for the deck, startup and option code, the arm64
disassembly, and the Ghidra script that dumped them) and `oracle/` (the answer
key, below).

## The rules

`UnderhandEngine.h` is the whole game. A `Game` is a plain struct, saved by
writing its bytes and checked with `valid()` when read back. Random numbers go
through `Random::below` in the order the original draws them, with one
exception: the original also draws one to time its radio clips, depending on
the clock.

- **Resources**: relic, money, cultist, food, prisoner, suspicion. A normal run
  starts with 2 money, 2 cultists, 2 food, 2 prisoners; the tutorial run
  starts empty.
- **Deal** (`StartupController::init`, `DeckController::compileDeck`): each
  initial card on a coin flip; quest openers that are marked initial always.
  Quest openers become initial by tier: A Fishing Trip and Start a Tea Shop at
  3 gods summoned, A Mysterious Egg and The Call of Uhl'uht'c at 5. The two
  unlock cards of a god just summoned are dealt into the next run, and only
  that one. After the shuffle, Rumors of Darkness goes into the top five, and
  during the tutorial the tutorial card goes on top.
- **Reshuffle** (`DeckModel::reshuffle`; the start of a run, and whenever the
  deck runs out): the discard pile and what is left of the deck are pooled, and
  the new order is picked one card at a time with chance proportional to
  `weight`. Heavy cards (the quest steps, 50 and 100 against 10) come up early.
- **Paying** (`OptionModel::satisfiability`): any exact payment (`exact()`).
  Relics stand in for anything; on options marked `cultistequalsprisoner`
  cultists and prisoners pay for each other in any split. `suggest()` proposes
  one: the named resources first, prisoners before cultists, relics only for
  what is short. A cost of 420 is half of what is held when the card is drawn,
  rounded up; 840 is payable only while none is held. A random cost (Greed's)
  takes that many resources, a random held kind at a time.
- **Losing options are locked** (`createOptionModelFromOption`) while another
  option on the card can be paid, checked in order, each against the others as
  they stand by then.
- **After a choice**: shuffled-in cards go to the discard pile (straight onto
  the deck during the tutorial); the played card goes to the discard pile only
  if it is `isrecurring`, otherwise it leaves the run.
- **Foresight**: the next three cards; with discard, any of them can be sent to
  the discard pile, where they come back at the next reshuffle.
- **Punishments** (`DeckController::resolvePunishments`), checked before each
  draw once the tutorial is done, never straight after another one, at most
  one, placed on top: Greed at 16+ resources (35%, +15 points each, certain at
  21), Police Raid at 5+ suspicion (same curve), Desperate Measures with no
  food (20%).
- **Winning** summons the option's god and remembers it as the last one.
  Winning the tutorial run finishes the tutorial; losing it does not.

### Where it differs from the original, on purpose

- **Foresight with fewer than three cards left** reshuffles and then shows the
  new top three. The original reshuffles and shows nothing, which players of
  the original called unfair.
- **Foresight after a tutorial shuffle-in** discards the card marked. In the
  original the shuffled-in card lands on top after the peek, so it removes the
  card one position up and discards the marked one: one card lost, one in both
  piles. No shipped card has foresight and a shuffle-in together, so this is
  never reached; the engine is right anyway, and a test pins it.
- **A card nothing can pay for ends the run** (`LossReason::Stuck`), where the
  original would sit there forever. It has never happened in simulation.
- **Running out of cards ends the run** (`LossReason::NoCards`), where the
  original reads past the end of an empty deck. It has never happened either.
- **Rumors dealt into a deck of fewer than five** goes to the bottom. The
  original writes before its buffer (about 1 run in 1,900).
- **Unlocks are spent when the next run starts**, as the original's save file
  records it. In the original, abandoning that run and starting another in the
  same session dealt them again.

One assumption is not settled by the decompilation: that a choice's rewards
are in hand before the next card's punishment roll. The original adds them as
an animation finishes, so the two may race.

### The answer key

`underhand-data/oracle/` runs the original's own `libmain.so` (arm64) under
Unicorn and the port side by side on seeded cases, handing both the same random
numbers and comparing the results and the sequence of bounds asked for. On
2026-09-24: 12 rule functions, 156,500 cases, 0 disagreements on anything the
shipped cards can reach; 26 deliberate misreadings of the port, each caught.
`./run.sh`, then grep `ANSWER-KEY: agree` and `MUTANTS: all-caught`. Its
report is `oracle/REPORT.md`. It predates the foresight fix above, which it
will now report as the one latent disagreement.

## The screen

One screen carries a run, and nothing on it moves while a card is played:

    +--------------------------------------+
    | UNDERHAND                    DECK 10 |  header: cards before a reshuffle,
    |                                (h)14 |  and under it everything held
    | ORGAN HARVEST                        |  the card's name
    | The most important organ is the      |  its text, two lines (three when long)
    | organization                         |
    | +----------------------------------+ |
    | | We need the cash                 | |  an option: its text,
    | | -1 C/P [#P#] [C]         +2$ +1P | |  what it costs, a chip per way (the black
    | |                                  | |  one is a tap anywhere), what it gives,
    | | ADDS ...                          | |  and what else it does
    | +----------------------------------+ |
    | | ...                              | |  up to three options
    | ! RAID 50%   LAST -2$ +1F        (?) |  dangers and their odds, the last turn
    | R 1  $ 3  C 2  F 4  P 2  S 2         |  what is held; tap either for how to play
    +--------------------------------------+

The panel of options gives its place, and only its place, to three others:
what a choice did, foresight, and paying.

**The header** shows, at its right, `DECK` (the cards before a reshuffle)
and under it everything held beside a hand symbol, because Greed rolls on
that total from 16 (Mario asked to see it coming, and to have it under the
deck count rather than beside it). While Greed can strike, the count sits in
a white box on the black band, as the bar's counts turn black.

**Symbols.** One Lucide symbol per resource (gem, coins, user-round, wheat,
user-lock, eye), a count before it: `-2` takes, `+2` gives, a cultist and a
prisoner joined by a slash mean either,
`AT RANDOM` is Greed's random loss. The bar shows each count beside its symbol,
the whole bar in one size: the large symbol, the smaller one when a count has
two digits, and the small face when even that does not fit. Food and suspicion
invert while they invite a punishment.

**Taking an option.** Mario's design, over two rounds of playing it
(2026-09-24): an option shows the card's own cost, the same every time the
card comes back, never the ways the hand could meet it; and every option is
one tap, choice or not. (The punishment cards' "half of what you hold" shows
what that half is now, so it is the one cost that changes.) Nothing is said
that the screen already shows: no note that a relic stands in, no list of
what is missing (the cost beside the bar says it), and no parentheses.

- every option draws its cost as the card states it (`-1 C/P`, `-2 $`);
- the ways that count are `view::choices()`: every exact payment, less those
  that leave held suspicion unspent (paying a relic in its place is never
  better: suspicion invites a raid and counts toward Greed), less those that
  use more relics than the fewest any way needs. A relic pays for what the
  hand cannot. The one exception, Mario's: when every fewest-relic way eats
  the last food, the ways that keep exactly one food with the fewest relics
  in its place are offered too (a cultist or a prisoner beside it, whichever
  the player picks), since starving invites Desperate Measures. Not where
  nothing is rolled after: the tutorial, a punishment card, a choice that
  wins or loses the run; and not when the choice gives food back;
- one way: a tap anywhere on the option pays it, relics and all;
- two to four ways (a cultist or a prisoner, a split of them, a relic for
  the last food): after the cost, a chip per way showing what it pays that
  the others do not (`[prisoner] [cultist]`, `[food] [relic]`, `[1 C 2 P]
  [2 C 1 P]`; the count is left out where every way is one of something).
  The first, `suggest()`'s, is filled black, and the rest of the option pays
  it, so the usual choice is a tap anywhere and the other is a tap on its
  chip. Chosen from three rendered layouts (the others: the cost's own
  symbols as the buttons, and chips with the rest of the option opening the
  panel). Each chip answers over its whole column, from under the option's
  words to its bottom (or its note), out to halfway to its neighbour, and the
  last one out to what the option gives: a finger aiming at a white chip
  that lands high or wide still pays that way. Measured over a million
  simulated turns, 88% of choices are cultist or prisoner and 12% relic or
  last food; 91% have two or three ways;
- more ways than fit, or ways differing in two things at once: an outlined
  `CHOOSE`, and a tap opens the paying panel in the options' place, with what
  every offered way pays already picked (`view::common()`). `PAY FOR` and the
  option, its `COST`, and `PAYING` with one outlined chip per symbol picked
  (tapping a chip takes it back), and when a relic may keep the last food,
  `A RELIC CAN PAY INSTEAD OF YOUR LAST FOOD`. The bar counts what would be
  left; each symbol that can still go toward the cost is outlined and takes a
  tap (`view::canAdd()`: one more of it still fits inside one of
  `choices()`), so no pick is a dead end. The rest are dithered, or black
  when the hand the payment leaves, with what the choice gives, invites a
  punishment. `PAY` is black once the picks are exactly one of those ways;
  `BACK`, and the device's Back, return to the options. The status line says
  `TAP A SYMBOL BELOW TO PAY WITH IT`, then `TAP PAY, OR A CHIP TO TAKE IT
  BACK`;
- an option that asks for suspicion when none is held, and does nothing else,
  where paying would not lower Greed's chance either (`view::buysNothing()`:
  Greed counts relics and is rolled on the hand after paying, so a payment
  that brings the total down past Greed's threshold, or lowers its chance, is
  worth something; the check compares the chance before and after), would
  spend relics and change nothing. It says `NO SUSPICION: RELICS BUY NOTHING`
  and a tap opens the paying panel with that line and nothing picked, so it
  is never paid by accident;
- an option that cannot be taken is dithered, still shows what it asks, gives
  and does (a summons out of reach is what the player is saving for), and
  takes no tap. A reason is written only where the screen cannot show it
  (`view::whyNot()`): `ONLY WITH NO` and the symbol, since a cost of none is
  not drawn, and `ONLY WHEN NOTHING ELSE IS OPEN` on a choice that ends the
  run. What it does gives way when the option's own words need the room.

This replaced, in turn, the first build's chips joined by `OR` with an
`ALL n` list (the hand's arithmetic in place of the card's cost), and then
`CHOOSE` on every choice (a second tap Mario found too costly).

**A tap is for the card it was made on.** Every tap that pays, or opens the
paying panel, carries the
card's turn (`ui::stamp`), so two cards with the same buttons in the same
places still build different tap tables, and the fork's tap gate
(`lib/GfxRenderer/RevealedInteractions.h`) drops a tap made while a changed
table is being painted. The render task holds the `RenderLock` until the panel
has changed, so `loop()` also notes the paint counter before taking the lock
and drops a tap that had to wait out a paint: it was made on the screen before.

On a card of one or two options each option takes half the panel: every option
text long enough to need a third line is on such a card.

**What a choice did.** When it added cards, reshuffled the deck or lost
something at random, the options step aside for a panel: `YOU CHOSE`, the
option, what it took and gave, what was lost at random, and one sentence about
the deck ("3 x Reading the Necronomicon join the deck at the next shuffle.",
"The deck was reshuffled and now holds Harvest.", "... goes on top of the
deck." in the tutorial). A tap anywhere on it carries on. It is saved, so
leaving the app or sleeping over it does not lose it. A plain trade does not
stop play; the status line says what it took and gave instead. The choice that
opened foresight does not show an outcome after it.

**Foresight** lists the next three cards, top first. With discard, tapping a
card toggles it between `KEPT` and `OUT` (plain words, a state rather than a
button; a card that is out greys), a line says discards return at a reshuffle,
and `CONTINUE` applies it.

**Danger** is on the status line with the chance of each punishment striking
before the next card if the hand stays as it is (`GREED 35% RAID 23%
DESPERATE 8%`). `punishmentOdds()` gives each roll's own chance, the same
numbers the roll uses; `view::chances()` chains them, since a raid is rolled
only when Greed missed and Desperate Measures only when both did. The bar's
inverted cells follow each roll's own chance: a certain Greed hides a raid from
the warning line, not from the roll after it. None during
the tutorial and none on a punishment card, after which nothing is rolled. The
last turn (`LAST`: what left the hand, the random loss included, and what
came in) shares the line when there is room. Under an outcome panel the
warning and the black counts wait: the next card is already drawn, so the odds
would describe the draw after it. On a card whose only open options end the
run there is no warning at all, since no card follows.

**The tutorial's words.** Five tutorial lines describe the phone's controls
(drag from your hand, the middle of the option box, the `Insert` keyword, "this
symbol"). `view::flavorText` and `view::optionText` show a rewording for those
five, matched on the card id AND the original words, so a changed card file
shows its own text. Everything else on screen is the card data's.

| Card       | Original                                                      | Shown                                                                   |
| ---------- | ------------------------------------------------------------- | ----------------------------------------------------------------------- |
| 91 flavour | Rewards and effects are shown at the bottom of the option box | What an option gives is shown at its right, and what else it does below |
| 92 flavour | Drag these resources from your hand                           | Tap an option to pay for it from what you hold                          |
| 92 option  | The middle of the option box shows these requirements         | What an option takes is shown at its left                               |
| 93 option  | This is denoted by the 'Insert' keyword                       | The option says which card it adds                                      |
| 99 option  | This symbol means a mix of prisoners and cultists can be used | Here prisoners and cultists pay in any mix                              |

**The menu** opens only when there is no run: a run in progress opens straight
onto its card. Headline (`TURN 17`, `FIRST RUN`, `NEW RUN`) over the goal, the
seven gods (a skull by each summoned, a dash by the rest: a record, not
controls), `HOW TO PLAY`, `GIVE UP` during a run
(which asks first; `KEEP PLAYING` goes back to the card), and the primary
button at the bottom (`CONTINUE`, `BEGIN`, `START`).

**The end of a run** names the god in capitals (on two lines when it needs
them) and the two cards the next run's deck adds, or the card that ended it and how long it
lasted, with `PLAY AGAIN` and `MENU`.

**How to play** is two pages: the six symbols, the hand count (Greed at 16)
and the warning sign, then the goal, taking a choice and `CHOOSE`, grey
choices, the warning line and its black counts, `LAST` and `DECK`. It opens from the menu and from a tap on the bar or the line above it,
where a small question mark says so.

**Refresh.** A screen change is a full refresh, and so is every twelfth frame,
so a long run of fast refreshes does not leave ghosts of earlier cards. A tap
that changes nothing repaints nothing.

**Threads.** The render task reads the game while it draws, so every change
from a tap or Back is made under the `RenderLock`, and leaving the app happens
after it is released. `render()` draws nothing until `onEnter()` has finished
(`ready`), in case a render was already requested when the app opened.

No credit is shown in the app (Mario, 2026-09-24).

### Where Back goes

| Screen                        | Back                        |
| ----------------------------- | --------------------------- |
| a card, an outcome, foresight | the menu; the run is kept   |
| paying                        | the card's options          |
| how to play                   | wherever it was opened from |
| the menu, asking to give up   | the menu                    |
| the menu                      | the shelf                   |
| the end of a run              | the menu                    |

## Nothing overflows

Two instruments, both run from the simulator:

- **The audit.** `UNDERHAND_AUDIT=1` makes the activity render, before its
  first real frame, every card with a full hand, a hand that raises all three
  dangers with Greed certain, one with all three odds showing, an empty one,
  one with two digits in every cell of the bar and one of nothing but relics;
  the paying panel of every option that chooses, with nothing picked, with
  what it opens on, and with each way it offers picked; every option's
  outcome with every card it can add, a reshuffle and five kinds lost at
  random; the last turn of every option on the status line; foresight over
  every card; every win, every losing option, every stuck card; the menu in
  its five states (the fifth after a save was set aside); both pages of how to
  play. About 1,890 screens. The builders report any
  label wider than its box, prose needing more lines than its box, tokens
  running into each other, a panel running into its buttons. It logs `AUDIT
<screen> <id>: <problem>` and then `AUDIT: <n> screens, <m> layout
problems`, which must read 0.
- **sim-shot's glyph gate**, which fails on a truncation ellipsis in a face
  that has no ellipsis glyph.

      CROSSPLAY_AUTOSTART=UNDERHAND UNDERHAND_AUDIT=1 SIM_LOG_GREP='AUDIT' \
        ./scripts_local/sim-shot.sh '12000:QUIT' '11000:/tmp/uh.bmp'

The audit is compiled into the simulator only.

## Screenshots of any moment

`tools_local/underhand/seed.cpp` writes a save at a chosen moment; the
simulator then opens on it. Build and usage are in its header, for example:

    /tmp/underhand-seed fs_agent/.crosspoint/underhand.sav card=5 held=2,4,4,4,4,4 turn=14
    CROSSPLAY_AUTOSTART=UNDERHAND ./scripts_local/sim-shot.sh '4000:QUIT' '3500:/tmp/wrath.bmp'
    /tmp/underhand-seed - list held=2,4,4,4,4,4    # every card, option states, ways to pay

## The save

`/.crosspoint/underhand.sav`, written to a `.tmp` and renamed; a `.tmp` found
alone at launch (power lost between the remove and the rename) is taken as the
save. Magic `UHND`, version 1, the size of a `Game`, the profile, one byte of
flags (a run in progress, the outcome panel showing), the `Game` bytes and the
random state. A save that fails `valid()` keeps the profile and drops the run,
and so does one written by a build with a different `Game` (the profile sits
in front of it, where it always was) or with a flags byte no build writes. A
file that cannot be read at all is renamed `underhand.sav.bad` rather than
overwritten, since the gods summoned may still be in it, and the menu says
so until the next run starts; the first such file
is never replaced (a later one is `underhand.sav.bad2`), and a failed read is
tried twice before anything is set aside. The `Game`'s own flags are read back
as bytes, so a damaged one cannot be a bool that is neither true nor false.

## Memory

About 39KB of `Cards` (in PSRAM: allocations over 4KB go there on this board)
and a `CardModel` of about a KB, both allocated once in
`onEnter` (kept off the render task's stack; it is rebuilt in place for each
frame).

## Icons

    uv run --with pillow python freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
      --manifest tools_local/underhand/icons.txt \
      --svgdir freeink-sdk/libs/assets/Icons/lucide/icons \
      --sizes 24,32 --out src/apps_local/underhand/UnderhandIcons.h

The shelf icon (`icon_underhand_32`, Lucide's skull) is in
`tools_local/toybox/icons.txt` with the other shelf icons.

## Tests

    host-tests/underhand/run.sh
    UNDERHAND_DATA=<folder with the two JSON files> host-tests/underhand/run.sh

Small card files in the original's JSON shape, with every random number
scripted, so each expectation is worked out by hand from the rule: the exact
order of a reshuffle, the punishment thresholds to the percent, exact payment
with relics and swaps, the losing-option lock, the deal and where Rumors lands,
what a win records and when unlocks are spent, foresight's discard positions
(the tutorial shuffle-in case included), save validation, every way to pay.
Then the screen's words: added cards counted by title, the deck sentence in
each of its forms, and the tutorial rewording, which must not apply to a card
that says something else.

Then the real, embedded cards, in about three seconds:

- every card is reachable from what the engine deals, walked exactly;
- 400 careful players, up to 60 runs each, checking at every step that
  nothing goes negative, no pile overflows and no state gets stuck. They win
  about 15% of runs and lose mostly to Police Raid and Wrath of the Gods;
- 2,000 explorers, whose hand is re-dealt every turn and who wander off the
  quests half the time, must summon all seven gods and draw all 118 cards.
  They do, the rarest god 191 times.

The second form also checks the embedded text against the APK's files.
