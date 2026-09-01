#pragma once

// Jaipur, the rules. Freestanding: no renderer, no Activity, no storage.
//
// See docs/apps/jaipur.md for the rulebook this implements and for the two state
// machines. The short version of the shape:
//
//   * `Game` is the whole shared state AND the wire format, 64 bytes. There is
//     one description of a game, so two devices cannot drift.
//   * Everything that can be derived is derived: scores, which tokens each
//     player holds, the value of a drawn bonus, the camel token, the winner,
//     the legal moves. A stored field that disagrees with a computed one is a
//     bug class that does not exist here.
//   * The opponent's hand lives in here the whole time. Nothing outside gets to
//     see it: `observe()` builds what an AI is allowed to know, and the screens
//     take a model that has no per-good array for the other seat.

#include <cstdint>
#include <type_traits>

namespace jaipur {

// The six goods. Camel is deliberately NOT one of these: it never enters a
// hand, never sells, and has no token pile, so making it a seventh Good would
// put a camel branch inside every loop that walks goods.
enum class Good : uint8_t { Diamond = 0, Gold, Silver, Cloth, Spice, Leather };
constexpr int kGoodCount = 6;

// A market slot holds a good or a camel. Camels are cards, never goods.
constexpr uint8_t kCamel = 6;
constexpr uint8_t kEmpty = 7;  // a market slot the deck could not refill

constexpr int kMarketSlots = 5;
constexpr int kHandLimit = 7;
constexpr int kSeats = 2;

// 3 camels open the market, so 52 cards are shuffled.
constexpr int kDeckCards = 52;
constexpr int kOpeningCamels = 3;

// How many of each card are in the game.
constexpr uint8_t kGoodSupply[kGoodCount] = {6, 6, 6, 8, 8, 10};
constexpr uint8_t kCamelSupply = 11;

// Goods tokens, spread face up in descending order so both players can always
// see what is left. Taken from the top, highest first.
constexpr uint8_t kPileDepth[kGoodCount] = {5, 5, 5, 7, 7, 9};
constexpr uint8_t kGoodsTokens[kGoodCount][9] = {
    {7, 7, 5, 5, 5, 0, 0, 0, 0},  // Diamond, 29
    {6, 6, 5, 5, 5, 0, 0, 0, 0},  // Gold,    27
    {5, 5, 5, 5, 5, 0, 0, 0, 0},  // Silver,  25
    {5, 3, 3, 2, 2, 1, 1, 0, 0},  // Cloth,   17
    {5, 3, 3, 2, 2, 1, 1, 0, 0},  // Spice,   17
    {4, 3, 2, 1, 1, 1, 1, 1, 1},  // Leather, 15
};

// Bonus tokens: three stacks, each shuffled separately and never spread. The
// value is printed only on the back, so you learn your own when you draw it and
// your opponent never does.
constexpr int kBonusStacks = 3;  // 3 cards sold, 4 cards sold, 5 or more
constexpr uint8_t kBonusDepth[kBonusStacks] = {7, 6, 5};
constexpr uint8_t kBonusTokens[kBonusStacks][7] = {
    {1, 1, 2, 2, 2, 3, 3},
    {4, 4, 5, 5, 6, 6, 0},
    {8, 8, 9, 10, 10, 0, 0},
};

constexpr uint8_t kCamelTokenValue = 5;
constexpr uint8_t kSealsToWin = 2;

// Diamonds, gold and silver never sell alone, even when one token is left.
constexpr bool sellsInPairs(const Good g) { return g <= Good::Silver; }

enum class Phase : uint8_t { Playing = 0, RoundOver, GameOver };

// Game::lastKind when nothing has been played yet: a fresh deal, so there is
// nothing to narrate.
constexpr uint8_t kNoMove = 0xFF;

// A turn is exactly one of these. Every field a kind does not use is ignored,
// never read, so a partially filled Move cannot mean something by accident.
struct Move {
  enum class Kind : uint8_t { TakeOne = 0, TakeCamels, Exchange, Sell } kind = Kind::TakeOne;

  uint8_t slot = 0;               // TakeOne: which market slot
  uint8_t good = 0;               // Sell: which good
  uint8_t count = 0;              // Sell: how many cards
  uint8_t marketMask = 0;         // Exchange: which market slots, bits 0..4
  uint8_t give[kGoodCount] = {};  // Exchange: goods surrendered, per good
  uint8_t giveCamels = 0;         // Exchange: camels surrendered from the herd

  static Move takeOne(int slot);
  static Move takeCamels();
  static Move sell(Good good, int count);
};

// The whole game. Trivially copyable, so LinkPlay ships it as raw bytes between
// two identical builds.
struct Game {
  uint32_t seed = 0;      // this round's shuffle; both devices rebuild it
  uint8_t deckTaken = 0;  // how far into the shuffled 52 we have read
  uint8_t market[kMarketSlots] = {};
  uint8_t hand[kSeats][kGoodCount] = {};
  uint8_t herd[kSeats] = {};
  uint8_t sold[kGoodCount] = {};  // cards discarded this round, by either seat

  // Which token of each pile went to seat 0, as a bitmask over the pile's
  // depth. Counts alone cannot say *who got which*: two diamonds each is 7+7
  // against 5+5 or the reverse, depending on order. Seat 1 holds the complement
  // inside `goodsDepth`, so one mask describes both.
  uint16_t goodsTakenBy0[kGoodCount] = {};
  uint8_t goodsDepth[kGoodCount] = {};
  uint8_t bonusTakenBy0[kBonusStacks] = {};
  uint8_t bonusDepth[kBonusStacks] = {};

  // What the last move was, so the other device can narrate it. A whole state
  // says nothing about how it was reached, and over a link that is the one
  // thing a player cannot see for themselves: their opponent's hand is hidden,
  // so "THEY SOLD 3 SPICE FOR 11" is the only account of it they get. Same
  // reason Battleship's lastShot rides along in its state.
  //
  // Four bytes, not six, and the two flags ride in spare bits: this struct is
  // memcmp'd and shipped as raw bytes, so a size that is not a multiple of its
  // alignment would put undefined padding on the wire.
  uint8_t lastKind = kNoMove;  // Move::Kind, and the mover's seat in bit 7
  uint8_t lastCard = 0;        // TakeOne: the card taken. Sell: the good.
  uint8_t lastCount = 0;       // camels taken, cards traded, or cards sold
  uint8_t lastValue = 0;       // what a sale fetched, and a bonus token in bit 7

  uint8_t seals[kSeats] = {};
  uint8_t turn = 0;
  uint8_t roundStarter = 0;
  uint8_t phase = static_cast<uint8_t>(Phase::Playing);
  uint8_t round = 1;

  // --- set-up -------------------------------------------------------------

  // Deals a fresh round in the rulebook's order: 3 camels to the market, the
  // remaining 52 shuffled, 5 to each seat, 2 more to the market, then any dealt
  // camels moved into their herds.
  void deal(uint32_t roundSeed, uint8_t starter);

  // A whole new match: seals cleared, round 1 dealt.
  void newGame(uint32_t roundSeed, uint8_t starter);

  // --- playing ------------------------------------------------------------

  bool isLegal(const Move& move) const;

  // Applies a legal move, then tests for the end of the round, then flips the
  // turn. That order is the rulebook's "a round ends immediately": the action
  // completes in full first, which is why a sale that empties the third pile
  // still collects its bonus token. Returns false and changes nothing if the
  // move is not legal.
  //
  // A round that ends here also awards its seal here, and goes straight to
  // Phase::GameOver if that was somebody's second. So Phase::RoundOver always
  // means "scored, seal awarded, match still alive", and any screen drawn from
  // that state is telling the truth.
  bool apply(const Move& move);

  // Deals the next round, loser first. Only valid in Phase::RoundOver, and by
  // then the seal is already banked.
  void startNextRound(uint32_t roundSeed);

  // --- derived, never stored ----------------------------------------------

  Phase currentPhase() const { return static_cast<Phase>(phase); }

  // The last move, unpacked. Meaningless when lastKind is kNoMove.
  bool hasLastMove() const { return (lastKind & 0x7F) != (kNoMove & 0x7F); }
  Move::Kind lastMoveKind() const { return static_cast<Move::Kind>(lastKind & 0x7F); }
  int lastMover() const { return lastKind >> 7; }
  int lastSaleValue() const { return lastValue & 0x7F; }
  bool lastTookBonus() const { return (lastValue & 0x80) != 0; }

  int handSize(int seat) const;
  int deckRemaining() const { return kDeckCards - deckTaken; }
  int marketCamels() const;
  int emptyPiles() const;  // how many goods piles are exhausted

  // Goods tokens only: what is stacked face up in front of a seat, and public.
  int goodsRupees(int seat) const;
  // Just this pile's contribution, which is what a scoring row shows.
  int goodsRupees(int seat, Good good) const;
  int goodsTokenCount(int seat) const;
  // Bonus tokens: the count is public, the value is not. `bonusRupees` is only
  // ever shown for your own seat during play, and for both at scoring.
  int bonusTokenCount(int seat) const;
  int bonusRupees(int seat) const;
  // 5 rupees to strictly more camels; equal camels means nobody takes it.
  int camelTokenSeat() const;
  int score(int seat) const;
  // What `viewer` may honestly total for `seat` mid-round, which is what the
  // running strip prints. Goods tokens and the camel token are face up, so they
  // count for either seat; bonus tokens are face down, so only your own are in
  // here and theirs are shown as a "+N?" count beside this number.
  // `visibleScore(s, s) == score(s)`, and the two seats differ only by that
  // hidden bonus, never by who the viewer is.
  int visibleScore(int viewer, int seat) const;
  // Who takes the seal: rupees, then bonus tokens, then goods tokens, then
  // camels. -1 when all four tie, which is a genuine draw.
  int roundWinner() const;
  int matchWinner() const;  // -1 until somebody holds two seals

  // The token a seat would take next off a pile, for the UI's "-> 11" readout.
  int nextTokenValue(Good good, int fromDepth) const;
  // What a sale of `count` of `good` is worth in goods tokens right now.
  int saleValue(Good good, int count) const;

  // The shuffled deck, rebuilt from `seed`. Position `i` is the i-th card off
  // the 52. Deterministic and identical on both devices.
  uint8_t cardAt(int index) const;
  // The bonus token at `index` of `stack`, from the same seed.
  uint8_t bonusValueAt(int stack, int index) const;
};

static_assert(std::is_trivially_copyable<Game>::value, "Game travels as raw bytes");
static_assert(sizeof(Game) <= 192, "Game must fit one LinkPlay packet");
// Exact, because every byte of this struct is compared, saved and transmitted.
// A layout with padding in it has bytes no field owns, and those are whatever
// the stack left there. Change the fields and this fails, which is the reminder
// to bump both linkplay::GameId and the save version.
static_assert(sizeof(Game) == 64, "Game must have no padding: see the comment above");

// What a player is allowed to know. The opponent's hand composition is not a
// field here, so an AI built on this cannot cheat by construction rather than
// by discipline. See docs/apps/jaipur.md.
struct Observation {
  uint8_t seat = 0;
  uint8_t market[kMarketSlots] = {};
  uint8_t hand[kGoodCount] = {};  // yours
  uint8_t herd[kSeats] = {};      // both, face up
  uint8_t opponentHandSize = 0;   // a number, never a composition
  uint8_t goodsDepth[kGoodCount] = {};
  uint8_t bonusDepth[kBonusStacks] = {};
  uint8_t deckRemaining = 0;
  // Everything you have not seen: the deck plus their hand, as a multiset.
  // total - market - yours - discarded.
  uint8_t unseen[kGoodCount] = {};
  uint8_t unseenCamels = 0;

  // What is banked. Your own bonus tokens you have looked at, so their value is
  // yours to know; theirs are face down, so only the count is.
  uint16_t yourGoodsRupees = 0;
  uint16_t yourBonusRupees = 0;
  uint16_t theirGoodsRupees = 0;
  uint8_t yourBonusCount = 0;
  uint8_t theirBonusCount = 0;
  uint8_t seals[kSeats] = {};
  uint8_t emptyPiles = 0;  // three ends the round
};

// A market slot whose refill this player has not seen. Only ever produced by
// `after()`: the deck is face down, so a take leaves a slot the AI must reason
// about without knowing.
constexpr uint8_t kUnknown = 8;

// The observation you would hold after playing `move` yourself. Refilled market
// slots come back as kUnknown, because that is the honest answer -- nothing
// here may consult the deck.
Observation after(const Observation& obs, const Move& move);

Observation observe(const Game& game, int seat);

}  // namespace jaipur
