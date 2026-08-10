# Sea Salt & Paper

The rules this fork implements, and the decisions taken where a two-player
handheld cannot do what the table does.

Sea Salt & Paper is designed by Bruno Cathala and Théo Rivière, published by
Bombyx. This is an unofficial implementation for a personal device; the game and
its name belong to them.

Everything below marked **rulebook** is from the official rulebook and is not
ours to change. Everything marked **ours** is a decision this port had to make.

---

## The deck: 58 cards

| Kind            | Cards | Scores                                      |
| --------------- | ----- | ------------------------------------------- |
| Crab (duo)      | 9     | 1 per **pair**                              |
| Boat (duo)      | 8     | 1 per **pair**                              |
| Fish (duo)      | 7     | 1 per **pair**                              |
| Swimmer (duo)   | 5     | 1 per swimmer+shark **combination**         |
| Shark (duo)     | 5     | (same pairing as swimmer)                   |
| Shell (coll.)   | 6     | 0, 2, 4, 6, 8, 10 for 1..6                  |
| Octopus (coll.) | 5     | 0, 3, 6, 9, 12 for 1..5                     |
| Penguin (coll.) | 3     | 1, 3, 5 for 1..3                            |
| Sailor (coll.)  | 2     | 0, 5 for 1..2                               |
| Mermaid         | 4     | 1 per card of your most-held colour         |
| Lighthouse      | 1     | 1 per boat card; is not itself a boat       |
| Shoal of fish   | 1     | 1 per fish card; is not itself a fish       |
| Penguin colony  | 1     | 2 per penguin card; is not itself a penguin |
| Captain         | 1     | 3 per sailor card; is not itself a sailor   |

**rulebook.** 34 duo + 16 collector + 4 mermaid + 4 multiplier = 58.

Two things about duos that are easy to get backwards, and both are stated
explicitly in the rulebook's Card Details:

- **Duo points count whether the pair was played or not.** Only the _effect_
  requires placing the two cards in front of you.
- **"Points on your cards" always means hand plus table.** Every scoring
  sentence in the rulebook means both.

Each mermaid must claim a _different_ colour: with two mermaids, the second
counts your second-most-held colour. A mermaid never counts a colour another
mermaid already took.

## A turn

**rulebook.** In order:

1. **Take exactly one card into hand**, one of two ways:
   - the top two cards of the deck: keep one, discard the other face up onto
     either discard pile (if a pile is empty, the discard must go there); or
   - the top card of either discard pile.

   You may never look through a discard pile. (The crab's effect is the one
   exception, and it is the whole point of the crab.)

2. **Optionally play duo pairs** face up in front of you, triggering each
   effect. More than one pair may be played in a turn.

3. **Optionally end the round**, but only if your cards (hand plus table) are
   worth **7 or more**.

### Duo effects

| Pair            | Effect                                                                                         |
| --------------- | ---------------------------------------------------------------------------------------------- |
| Crab + crab     | Look through a discard pile without shuffling it and take one card. It is not shown to anyone. |
| Boat + boat     | Take another turn immediately.                                                                 |
| Fish + fish     | Draw the top card of the deck into hand.                                                       |
| Swimmer + shark | Steal a random card from another player's hand.                                                |

## Ending a round

**rulebook.** The ender reveals their hand and says one of two words.

**STOP** — everybody scores the points on their cards. Nothing else happens.

**LAST CHANCE** — a bet that the ender has the most points. Every opponent takes
one more full turn (take a card, play cards), after which their hand is revealed
and can no longer be stolen from. Then:

- **Bet won** (the ender's card points are **greater than or equal to** every
  opponent's): the ender scores their cards **plus the colour bonus**; each
  opponent scores **only their own colour bonus**.
- **Bet lost** (some opponent has strictly more): the ender scores **only their
  colour bonus**; the opponents score **their cards**, with no bonus.

The **colour bonus** is 1 point per card of the colour you hold most of. It
exists only in a LAST CHANCE round: a STOP round never pays it, to either side.

**Deck empty.** If the deck runs out at the end of a turn, the round ends
immediately and **nobody scores**.

## Ending the game

**rulebook.** Two players play to **40**. A round's points are added to the
previous rounds'; the game ends as soon as somebody reaches or exceeds the
target, and the highest total wins. A tie goes to whoever went last in the final
round.

A new round reshuffles everything, including the discard piles. The player after
the one who ended the previous round starts the new one.

**Holding all four mermaids wins the game outright**, immediately, whatever the
scores are. See the note below: the rulebook's wording here is misleading.

---

## Decisions this port made

### Mermaids stay in your hand

The rulebook says "if they **place** 4 mermaid cards, the player immediately
wins", and step 2 of a turn only ever authorises placing **duo pairs**. Read
together those two sentences describe a card you can win with by doing something
the rules never let you do, which is why the question keeps coming up.

The answer is that mermaids are never laid down: they stay in hand until
scoring, and the instant win is for **holding** all four. That is what this port
implements, and the win fires the moment the fourth mermaid arrives, however it
arrived: drawn, taken off a pile, dug out by a crab, or stolen with a shark.

The consequence worth knowing is that three mermaids in hand is a real target
for a swimmer+shark steal, and that is intended.
