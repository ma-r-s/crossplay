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
    | UNDERHAND                    DECK 10 |  header; cards left before a reshuffle
    | ORGAN HARVEST                        |  the card's name
    | The most important organ is the      |  its text, two lines (three when long)
    | organization                         |
    | +----------------------------------+ |
    | | We need the cash                 | |  an option: its text,
    | | [-1 P] OR [-1 C] OR [-1 R] +2$ +1P| |  what it takes (left), gives (right),
    | | ADDS ...                          | |  and what else it does
    | +----------------------------------+ |
    | | ...                              | |  up to three options
    | ! RAID 50%   LAST -2$ +1F        (?) |  dangers and their odds, the last turn
    | R 1  $ 3  C 2  F 4  P 2  S 2         |  what is held; tap either for how to play
    +--------------------------------------+

The panel of options gives its place, and only its place, to three others:
what a choice did, foresight, and the ways to pay.

**Symbols.** One Lucide symbol per resource (gem, coins, user-round, wheat,
user-lock, eye), a count before it: `-2` takes, `+2` gives, a cultist and a
prisoner joined by a slash mean either,
`AT RANDOM` is Greed's random loss. The bar shows each count beside its symbol,
the whole bar in one size: the large symbol, the smaller one when a count has
two digits, and the small face when even that does not fit. Food and suspicion
invert while they invite a punishment.

**Taking an option.** Fewest taps was the brief:

- an option that can be paid one way is taken by tapping anywhere on it;
- an option that can be paid several ways shows them as chips joined by `OR`,
  the first filled black. Tapping a chip pays that way; tapping the rest of the
  option pays the black one, so the common case is still one tap. The first is
  `suggest()`'s: fewest relics, prisoners before cultists. The ways offered
  are `view::choices()`: every exact payment except those that leave held
  suspicion unspent, paying a relic in its place, which is never better
  (suspicion invites a raid and counts toward Greed). With 2 suspicion held
  and 3 asked, a relic and the 2 is the way; with 3 held, the relic ways are
  not offered at all;
- an option that asks for suspicion when none is held, and does nothing else,
  with Greed out of reach (`view::buysNothing()`), would spend relics and
  change nothing; at 16 or more held a relic spent lowers Greed's chance, so
  there it is an ordinary option. It says
  `NO SUSPICION: RELICS BUY NOTHING` and a tap opens the list rather than
  paying, which says why again above its one row;
- each chip answers over the band around it and half of each OR beside it,
  because a finger is wider than a 36px chip is tall: up to 20px above it
  (never over the option's own words) and down to the note, and the last chip
  20px past its right edge, short of what the option gives, so a near miss
  there does not pay the black way;
- when the chips do not all fit, the last one is `ALL n`, which lists every way
  a page at a time (`PAY WHICH WAY?`, `NEXT PAGE`, `BACK`). The screen pages by
  the room it has. Up to 64 ways are kept; no option of the real cards has more
  than 56 even from a hand of 9 relics and 25 of everything (a test holds it);
- an option that cannot be taken is dithered, still shows what it asks, gives
  and does (a summons out of reach is what the player is saving for), says why
  in capitals (`SUMMONS UHL'UHT'C. SHORT: 1 RELIC`, `ONLY WITH NO CULTISTS`,
  `ENDS THE RUN. LOCKED: ANOTHER CHOICE IS OPEN`, and with a spare relic
  `SHORT: 1 MONEY, 1 FOOD (A RELIC COVERS 1)`) and takes no tap. When the
  option's own words need the room, only the why is shown, without the
  relics' share.

**A tap is for the card it was made on.** Every tap that pays carries the
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
last turn shares the line when there is room. Under an outcome panel the
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
them) and what the next run is dealt, or the card that ended it and how long it
lasted, with `PLAY AGAIN` and `MENU`.

**How to play** is two pages: the seven symbols and what each danger is called,
then the goal, the tap rule, grey choices, the warning line and its black
counts, `LAST` and `DECK`. It opens from the menu and from a tap on the bar or the line above it,
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
| the list of ways to pay       | the card                    |
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
  every page of every list of ways; every option's
  outcome with every card it can add, a reshuffle and five kinds lost at
  random; the last turn of every option on the status line; foresight over
  every card; every win, every losing option, every stuck card; the menu in
  its five states (the fifth after a save was set aside); both pages of how to
  play; the one-row list a guarded option opens. About 1,840 screens. The builders report any
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
and a `CardModel` of a few KB, both allocated once in
`onEnter` (the model holds every way to pay for the list, too much for the
render task's stack; it is rebuilt in place for each frame).

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
