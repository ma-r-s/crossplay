# Jaipur

The rules, exactly, and the two state machines the app is built on. Read this
before touching `src/apps_local/jaipur/`.

Everything in "The rules" is taken from the official GameWorks rulebook
(Sebastien Pauchon, illustrations Alexandre Roche). Where the rulebook is silent
the gap is marked **[house]** and says what we chose and why.

---

## 1. The rules

### Material

| Thing        | Count | Detail                                                    |
| ------------ | ----- | --------------------------------------------------------- |
| Goods cards  | 44    | 6 diamond, 6 gold, 6 silver, 8 cloth, 8 spice, 10 leather |
| Camel cards  | 11    |                                                           |
| Goods tokens | 38    | see the table below                                       |
| Bonus tokens | 18    | three stacks, values hidden until drawn                   |
| Camel token  | 1     | 5 rupees                                                  |
| Seals        | 3     | first to 2 wins the game                                  |

55 cards total.

**Goods tokens**, each pile spread face up in descending order, so both players
can always see what is left:

| Good    | Tokens | Values            | Total |
| ------- | ------ | ----------------- | ----- |
| Diamond | 5      | 7 7 5 5 5         | 29    |
| Gold    | 5      | 6 6 5 5 5         | 27    |
| Silver  | 5      | 5 5 5 5 5         | 25    |
| Cloth   | 7      | 5 3 3 2 2 1 1     | 17    |
| Spice   | 7      | 5 3 3 2 2 1 1     | 17    |
| Leather | 9      | 4 3 2 1 1 1 1 1 1 | 15    |

**Bonus tokens**, three stacks, each shuffled separately and _not_ spread out.
The value is printed only on the back, so a drawn bonus stays face down until
scoring.

| Stack         | Tokens | Values        |
| ------------- | ------ | ------------- |
| 3 cards sold  | 7      | 1 1 2 2 2 3 3 |
| 4 cards sold  | 6      | 4 4 5 5 6 6   |
| 5+ cards sold | 5      | 8 8 9 10 10   |

### Set-up, in the rulebook's order

1. Place **3 camel cards face up** between the players. These are the start of
   the market.
2. Shuffle the remaining **52** cards.
3. Deal **5 cards** to each player.
4. The rest becomes the face-down deck.
5. Turn the **top 2 cards** face up next to the camels. The market is now 5
   cards, and 1 or 2 of those 2 may themselves be camels.
6. Each player moves any camels from their dealt hand into their **herd**, face
   up in front of them.
7. Pick a starting player.

After set-up the deck holds **40** cards.

### A turn

Take cards **or** sell cards. Never both.

**TAKE**, one of three:

- **A. Take several goods (exchange).** Take any number of goods cards from the
  market into your hand, then put back the _same number_ of cards from your hand
  or your herd.
  - The cards you give back may be goods, camels, or a mix.
  - **The same goods type may not be both taken and given.**
  - **An exchange is always at least 2 for 2.** One for one is never legal.
  - Camels can be _given_ in an exchange but never _taken_ in one.
  - **No card is drawn from the deck.** The market is refilled by what you gave.
- **B. Take one single good.** Take one goods card from the market, then replace
  it with the top card of the deck.
- **C. Take all the camels.** Take _every_ camel in the market into your herd,
  then replace each with a card from the deck. All or nothing.

You may never hold more than **7 cards in hand at the end of your turn**. Camels
sit in the herd and do not count.

**SELL**, in three steps:

1. Discard any number of cards of **one single** goods type.
2. Take that many tokens off the top of that good's pile, highest first. If the
   pile has fewer tokens than you sold, you take what is there and lose the rest.
3. If you sold **3 or more**, draw the matching bonus token: 3 cards, 4 cards,
   or 5+ cards. You get the bonus even when the goods pile came up short.

**Diamonds, gold and silver must be sold at least 2 at a time.** This holds even
if only one token of that type is left.

### End of a round

A round ends **immediately** when either:

- **3 of the 6 goods token piles are empty**, or
- **the deck cannot refill the market** (only takes B and C draw, so only they
  can trigger this).

The action that triggered it still completes in full, bonus token included.

### Scoring

- Whoever has **strictly more camels** takes the camel token, 5 rupees. Equal
  camels, nobody takes it.
- Add goods tokens plus bonus tokens plus the camel token.
- The richer trader takes a **Seal of Excellence**.
- Tie on rupees: most **bonus tokens** takes the seal.
- Still tied: most **goods tokens** takes the seal.
- **[house]** Still tied: **most camels** takes the seal. The rulebook stops
  after goods tokens; camels are the last public quantity left, and Mario chose
  it. If camels are also equal the round is a genuine draw: no seal, and it is
  replayed with the same starter. That path needs four exact ties at once and
  exists so the game never has to invent a winner.

### A new round, and the end of the game

If neither player has 2 seals, set up again from scratch (full 55-card reshuffle,
all tokens back) and play another round. **The player who lost the previous round
starts it.**

The game ends the instant one player holds **2 seals**. That is a 2 or 3 round
match. **[house]** There is no shorter variant on the menu; Mario chose the full
match only.

### Edge cases the rulebook calls out by name

- Take goods **or** camels from the market, never both in one turn.
- Taking camels always takes **all** of them.
- Equal camels means **nobody** gets the camel token.
- A sale short of tokens still earns the bonus token.
- You are not required to announce your camel count. **[house]** We show it
  anyway: every digital version does, and a stack you have to count is a memory
  chore the box only tolerates because the cards are physically there.
- Camels never count against the 7 card hand limit.

### Edge cases the rulebook does not mention

- **A bonus stack running out.** 7, 6 and 5 tokens deep. Unreachable in a real
  round, but the code awards nothing rather than reading off the end.
- **Selling more than 5.** Legal (7 leather in hand is reachable) and it draws
  from the 5+ stack.
- **Taking camels with a short deck.** Each camel is replaced one at a time; the
  first draw that finds an empty deck ends the round, with the camels already
  taken.

---

## 2. The game state machine

### One struct, and everything else derived

The shared state is the wire format, the same discipline battleship uses: two
descriptions of one game will drift, so there is only ever one. 60 bytes, well
inside LinkPlay's 192.

```
seed             4   the round's shuffle; both devices rebuild the same deck
deckTaken        1   cards drawn so far
market[5]        5   a good, or a camel
hand[2][6]      12   counts per good, per seat
herd[2]          2   camels
sold[6]          6   cards discarded this round, by either seat
goodsTakenBy0[6]12   which token of each pile went to seat 0 (bitmask)
goodsDepth[6]    6   how deep each pile has been dug
bonusTakenBy0[3] 3   same, for the three bonus stacks
bonusDepth[3]    3
seals[2]         2
turn             1
roundStarter     1
phase            1
round            1
```

Derived, never stored: every score, which tokens each player holds, the value of
a drawn bonus, who holds the camel token, the winner, the legal moves, and what
is left unseen. A stored field that disagrees with a derived one is a class of
bug that cannot occur here because the field does not exist.

Two of these are worth explaining.

**`goodsTakenBy0` is a mask, not a count.** Counts do not say _who got which_
token: two diamonds taken by each player is 7+7 against 5+5 or the reverse
depending on order. The mask says exactly, seat 1's holding is the complement
inside `goodsDepth`, and the values come off the const table. Same for bonuses,
where the values come off the seeded shuffle of each stack.

**`sold[]` is stored even though it is derivable**, because the AI's view of the
world is built from it and that view must be constructible without touching the
opponent's hand. Six bytes to keep a secrecy guarantee structural.

### Keeping the hidden hand hidden

Both hands travel inside the state, exactly as both fleets do in battleship. The
trust model is unchanged: two devices in one room running one build. What is
different is that a Jaipur hand changes every turn, so a drawing discipline alone
would leak eventually. So the secret is kept by the **type system** instead:

- `JaipurScreens` never sees `Game`. It sees a `BoardModel` whose only fact
  about the opponent's hand is `opponentHandSize`, an integer. There is no
  per-good array to accidentally draw.
- The AI never sees `Game`. It sees an `Observation` built by
  `observe(game, seat)`, holding your hand, the market, both herds, the pile
  depths, the discards, the deck size, and the **unseen multiset** derived as
  `total - market - yourHand - sold`. The opponent's hand composition is not a
  field, so the AI cannot cheat by construction rather than by discipline.

Jaipur hides much less than it looks like it does. Every take, exchange and sale
is public, camels are face up, and the token piles record what was sold. The only
genuine unknown is which cards came off the deck into the other hand, drawn from
a deck whose composition is known exactly. That makes `Observation` small, exact,
and enough to play well from.

### Phases

```
              new game
                  |
                  v
              [ deal ]  seed -> 3 camels, 5 each, camels to herds, 2 to market
                  |
                  v
      +------> PLAYING ------+
      |           |          | a legal move resolves, turn flips
      |           |<---------+
      |           |
      |           | 3 piles empty, or the deck failed to refill
      |           v
      |      ROUND_OVER   score, camel token, seal
      |           |
      |           +--- someone holds 2 seals ---> GAME_OVER
      |           |
      +-----------+ otherwise: deal again, the loser starts
```

A move resolves completely, _then_ the end-of-round test runs, _then_ the turn
flips. That order is the rulebook's "the round ends immediately" and it is why a
sale that empties the third pile still collects its bonus token.

### Moves

```
TakeOne     slot                      market[slot] is a good, hand < 7
TakeCamels                            at least one camel in the market
Exchange    marketMask, give[6], giveCamels
                                      >= 2 slots, all goods, counts equal,
                                      no type on both sides, hand <= 7 after
Sell        good, count               count <= held; >= 2 for diamond/gold/silver
```

Every one of these is checked by `Game::isLegal` before `Game::apply` touches
anything, and the UI only ever offers a move `isLegal` accepts. One judge, so the
button and the rules cannot disagree.

### The one place the transport turn and the game turn diverge

LinkPlay alternates strictly: send a state, hand over the turn. Jaipur alternates
strictly too, so the two agree on every turn of every round, with exactly one
exception.

The round ends on somebody's move, and **the loser starts the next round**. Those
are independent, so half the time the player who just moved is also the player
who must move next. The transport will not allow that.

The fix needs no new primitive. When a device holds the transport turn but the
game says the other seat moves, it **sends the state back unchanged**. It costs
one packet, it happens behind the round-over screen where both players are
reading their scores anyway, and it is one condition in the loop:

```
if (linkPhase() == YourTurn && game.turn != mySeat) play(game);   // silent pass
```

Stated as an invariant: _the transport turn and the game turn can only disagree
across a round boundary, and a pass is what closes it._

---

## 4. The opponent

`JaipurBrain` takes an `Observation`, never a `Game`. That is the whole design:
an Observation has no field for the other hand, so the opponent cannot cheat by
construction rather than by discipline. It knows what somebody sitting opposite
would: the market, its own hand, both herds, every pile depth, the discards, and
therefore the exact multiset of what it has not seen.

**No search.** One ply, and an evaluation that knows what a Jaipur position is
worth. That is the right trade here: the game's difficulty is valuation, not
lookahead. What the evaluation weighs, in descending order -- banked rupees,
cards in hand at what they would fetch today, a run far enough along to earn a
bonus token, the camel majority, and room under the hand limit.

Two skills, because there is an honest difference to express and no more:

|              |                                                                                              |
| ------------ | -------------------------------------------------------------------------------------------- |
| **Merchant** | plays the board in front of it                                                               |
| **Maharaja** | also races the round's end when ahead, and counts what its move leaves in the market for you |

### What is proven and what is only measured

Three things are proven, and they are the ones where being wrong would be
invisible in play:

- **`legalMoves` accepts exactly what `Game::isLegal` accepts.** The opponent
  needs its own move generator, because it must decide without the hidden hand,
  and two judges that drift is the bug class this whole design avoids. Checked
  move-for-move against a brute-force sweep of the rules engine.
- **`after(observation, move)` predicts what `Game::apply` produces** -- hand,
  herd, pile depth and rupees. If that drifts, every evaluation is scoring a
  position the game will never reach.
- **The rules themselves**, by a soak that plays whole matches and rechecks
  every invariant after every move: card conservation per good, the hand limit,
  tokens partitioning between the players, and a round ending only via one of
  its two legal triggers.

Strength is only **measured**, and the tests say so. There is no closed form for
"is this opponent any good", so `host-tests/jaipur/` asserts two bands instead:
each skill beats a greedy baseline at least 80% of the time, and Maharaja beats
Merchant at least 53% head to head. The second is the one that matters -- if
Maharaja's extra terms stop earning their place it lands at 50% and the test
fails. Beating the greedy baseline 98% of the time says the baseline is weak,
not that the opponent is strong, and that number should not be read as skill.

---

## 5. The menu state machine

Two taps to the game from Home, like everything else on the shelf. The app never
names its own destination; Back calls `shelf::leave()`.

```
GAMES folder
    |
    v
  MENU ---------------------------------------------------------+
    |  CONTINUE      only when a save exists; detail says where  |
    |  NEW GAME                                                  |
    |  MULTIPLAYER   hands off to LinkActivity's own chrome      |
    |  HOW TO PLAY                                               |
    |  Back -> shelf::leave()                                    |
    |                                                            |
    +--> BOARD                                                   |
    |      Back -> MENU, game saved                              |
    |      |                                                     |
    |      +--> ROUND OVER  -> BOARD (next round) or GAME OVER   |
    |      +--> GAME OVER   -> MENU, full refresh as the payoff  |
    |                                                            |
    +--> HELP -------------------------------------------------- +
```

BOARD is one screen. Selling and exchanging are **not** separate views: they are
selection state on the board, so the market and both hands stay visible while you
build the move. A modal sheet on a panel that repaints in a third of a second
costs two refreshes to enter and leave and hides the thing you are deciding
about.

### Selecting, on the board

There is no action menu and no mode. You tap cards, and the capsule at the bottom
names the single action your selection currently means. It is live when that
action is legal and dithered when it is not.

| What is selected                          | What the capsule says |
| ----------------------------------------- | --------------------- |
| nothing                                   | whose turn it is      |
| one market good                           | TAKE DIAMOND          |
| the market camels                         | TAKE 3 CAMELS         |
| two or more market goods, give side short | EXCHANGE 3 (dead)     |
| two or more market goods, give side equal | EXCHANGE 3            |
| n of one good in your hand                | SELL 3 CLOTH -> 11    |

This falls out of the rules rather than being imposed on them: a market selection
of one is a take, of two or more is an exchange, and a hand selection is a sale.
Tapping the camels clears any exchange in progress, because camels can never be
part of one.

Hit-testing is derived from the same geometry that drew the cards, never computed
a second time. That rule has caught more bugs in this fork than any other.

The opponent's move is reported on the line under the rule, and the turn comes
straight back to you. A tap-to-continue beat was built and removed after one
play-test: their move went into the capsule and waited to be acknowledged, which
put a press between the player and every single one of their own turns. What you
lose without it is that a fast player can miss the line; what you gain is that
the game never stops. The board itself shows what changed anyway.
