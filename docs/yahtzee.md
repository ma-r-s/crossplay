# Yahtzee

The rules, exactly, and the two state machines the app is built on. Read this
before touching `src/apps_local/yahtzee/`.

The rules are Hasbro's, all of them. This file used to carry a **[house]**
marker on the Joker rules; Hasbro turned out to be explicit there, and the
marker was hiding a deviation rather than filling a gap.

---

## 1. The rules

Five dice, three rolls a turn, thirteen boxes, thirteen turns each. Roll, keep
what you want, roll again up to twice more, then put the hand in exactly one
free box. The game ends when both cards are full, and not before: there is no
"no legal move" state and no draw condition to invent.

### The boxes

| Box             | Scores                                  |
| --------------- | --------------------------------------- |
| Ones .. Sixes   | the sum of the dice showing that face   |
| Three of a kind | all five dice, if three match           |
| Four of a kind  | all five dice, if four match            |
| Full house      | 25, for three of one and two of another |
| Small straight  | 30, for four in a row                   |
| Large straight  | 40, for five in a row                   |
| Yahtzee         | 50, for five of a kind                  |
| Chance          | all five dice, always                   |

**Sixty-three in the top half earns thirty-five more.** That is three of each,
and it is a cliff rather than a slope: sixty-two earns nothing.

**A zero is a real score.** Crossing out a box you cannot make is legal and
often correct, so `kUnscored` is -1 and zero cannot double as empty. Getting
that wrong makes a crossed-out Yahtzee look like a free box for the rest of the
game.

### Five of a kind is a full house

Three of them and two of them. The rule that says otherwise does not exist, and
dropping it would make a Yahtzee score zero in Full House, which is the exact
opposite of what the Joker rules are for.

### The Joker rules

Where this game is usually wrong, and where all three of the bugs found while
writing it were. There are **three separate rules** and conflating any two of
them is the bug.

Roll a Yahtzee when the Yahtzee box is already filled and:

1. **If it holds 50, you score a 100 bonus.** Immediately, and whatever box you
   then choose. A zero there earns nothing.
2. **You must use the matching upper box** if it is free. Five threes with
   Threes open means Threes, for 15.
3. **Otherwise you must use a box in the LOWER SECTION**, and there the
   straights and the full house pay in full whatever the dice show. Only when
   every lower box is also filled may you put a zero in an upper one.

Rule 1 does not depend on 2 or 3. The first version made the bonus conditional
on the matching upper box being gone, which silently robs a player of a hundred
points in the common case: five threes with Threes still open.
`yahtzeeBonusDue` and `jokerApplies` are separate functions for exactly this,
and `take()` calls the first.

Rule 3 is a **Lower Section** restriction, not "any free box". The second
version offered free upper boxes while the lower section was still open: 30,240
(card, dice, category) triples Hasbro forbids, reached in 1.25% of real games.
The code was wider than its own comment, which already said "any lower box".

**A Yahtzee box holding zero still triggers the Joker.** This file used to say
Hasbro was silent here and mark the strictest reading as **[house]**. That was
wrong: the rulebook says the Joker applies when the box "has been previously
filled with 50 or zero", and Hasbro's own support says a zero earns no bonus but
still forces the Joker rules. A `[house]` marker on a rule that is written down
is worse than getting the rule wrong, because it stops the next reader looking
it up.

There is now **no [house] rule in this game**. Every rule here is Hasbro's.

---

## 2. The turn state machine

`yahtzee::Stage`, in `YahtzeeFlow.h`. Derived from `rollsUsed`, never stored.

```
Fresh  ---- roll ----> Rolling ---- roll ----> Rolling ---- roll ----> Spent
  |                       |                                              |
  +-- nothing to keep     +-- keep dice, roll again                      +-- a box must be taken
```

Yahtzee needs this second machine where Minesweeper did not, and the reason is
worth writing down. "Whose turn" is not a rules concept a solitaire has, but the
state **within** a turn is: a turn is not one action, it is up to three rolls and
then exactly one box, and the difference between those decides what every control
on the screen does. `canRoll`, `canHold` and `take` each refuse outside their own
window, so the stage and the rules cannot disagree.

In `Fresh` the dice on screen belong to the previous player and mean nothing, so
the card does not draw them at all. Five stale dice above a live scorecard is the
screen saying something untrue.

---

## 3. Multiplayer

`GameId::Yahtzee`. The whole `Game` travels on every move: two cards, five dice,
the held mask, the roll count, the turn and the seed. Trivially copyable and well
inside the 192-byte cap.

**Only the LEADER deals.** Unlike Checkers and Connect Four, Yahtzee has
randomness, so two devices calling `start()` would not agree about the dice. The
follower begins on a zeroed `Game` and waits for the leader's first state.

Starting zeroed is safe here in a way it was not in Checkers: an empty Yahtzee
card is not a finished game, so `over()` is false and nothing announces a winner
before the first roll. That was a real bug in Checkers and it is worth knowing
which games are exposed to it and why.

---

## 4. The opponent

`YahtzeeBrain.h`. An **exact one-step expectation**, not a sample and not a
heuristic.

For each of the 32 ways to keep dice it enumerates every outcome of re-rolling
the rest and takes the expectation. That is only affordable because dice are
unordered: enumerating multisets with multinomial weights turns 7,776 ordered
outcomes into 252. Counted across a whole `chooseHold` call: **1,683 evaluations
against 16,807**, a ten-fold saving. An earlier draft claimed 3,200 against
250,000; the larger figure assumed every mask re-rolls all five dice, and most
re-roll fewer.

It cannot cheat **by construction**: `chooseHold` and `chooseBox` take the card
and the dice, never the `Game`, so they cannot reach `game.rng` and see the dice
they are about to roll.

Boxes are valued as **score minus what that box would usually be worth later**
(`kBoxWorth`), plus credit for upper-section progress while 63 is still
reachable.

An earlier draft justified that by saying the brain would otherwise take Chance
on turn one of every game and leave a zero in Yahtzee at the end of it. Measured
over 200 games each way, both halves were wrong: removing `kBoxWorth` gives
first-turn Chance in 45 games of 200, not all of them, and it makes the Yahtzee
box **better** (122 crossed out against 135). This brain zeroes Yahtzee in two
games of three either way. Solo strength is 219 without it against 220 with, and
the record against a greedy mover is identical.

It is kept because the ordering it encodes is right and it costs nothing, not
because it does what that sentence claimed.

Measured: **average 230 over 40 solo games, worst 153**, and **26-4 against a
greedy strategy** that keeps its commonest face and always takes the highest
score available. Human averages land around 200-250.

### Where it differs from conventional play, on purpose

Holding `2 3 4 5 5` it keeps the **pair**, not the four to an outside straight.

That falls out of the two modelling choices and is not a bug in either.
`handValue` has to assume the hand is scored now, and under opportunity-cost
valuation filling Small Straight at exactly its par value is worth about nothing
-- so the guaranteed fallback that makes a human keep the run scores as almost
zero here. Deeper lookahead, or a valuation that credits keeping a box open,
would change it.

It is recorded rather than tuned away. Bending a constant until one hand agrees
with folklore is how an evaluation stops meaning anything, and the measured
strength does not suffer.

---

## 5. The menu state machine

```
Menu ----> Card ----> Result
  |          |           |
  +-> HowTo  +-- Back ---+---> Menu
```

`back()` is an exhaustive switch with no `default`. `leavesApp()` is true only
for `Menu`.

### The card is a table, not a list

Fifteen lines: six upper, the bonus, seven lower, the total. At 33px each they
all fit under the dice with the capsule still on screen. A `ListProps` row is
62px, and six of those would be the whole card, which is why this is the one
screen in the fork that draws its own rows.

### The preview is the game

An unfilled box shows **what you would score in it**, drawn as a number inside
the printed box. A filled box has no outline. So committed and pencilled never
look alike, and the screen is the physical card: figures written in boxes, with
the empty ones still waiting.

That preview is the entire decision every turn. Without it the player has to do
thirteen scoring calculations in their head, which is not the game, it is
arithmetic homework.

### The capsule is the roll button

One control doing two jobs, because they are never both live: there is nothing
to say while you can still roll, and nothing to roll once you cannot. It reads
`ROLL`, then `ROLL AGAIN (2 LEFT)`, then `TAKE A BOX`.

### The bonus line does the arithmetic

`63 MORE`, then `+35` once it is banked, then `OUT OF REACH` when every remaining
upper box at its maximum can no longer clear 63. That last state matters: a
shortfall counting down toward a number that can no longer arrive is worse than
no shortfall at all.

### The gap between two dice belongs to neither

`dieAt` returns -1 there rather than rounding to the nearest die. Five dice are
five separate switches and a fat-fingered hold is a wasted turn, so a miss is
better than a wrong hit.
