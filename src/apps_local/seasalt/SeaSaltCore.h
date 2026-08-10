#pragma once

// Sea Salt & Paper, the rules. Freestanding: no renderer, no Activity, no
// storage. See docs/seasalt.md for the rulebook this implements.
//
// The shape, and why:
//
//   * `Game` is the whole shared state AND the wire format. There is one
//     description of a game, so two devices cannot drift.
//   * Cards have identity. Card `c` is always the same card: `kindOf(c)` and
//     `colourOf(c)` are constants, and the state says only *where* it is. That
//     is what makes a discard pile a real pile you can dig through, which the
//     crab requires, rather than a count.
//   * Everything derivable is derived: scores, hand sizes, pile tops, the
//     legal moves, who won the bet. A stored field that can disagree with a
//     computed one is a bug class that does not exist here.
//   * The opponent's hand lives in here the whole time. Nothing outside gets to
//     see it: `observe()` builds what a player is allowed to know, and it has
//     no per-card array for the other seat.

#include <cstdint>
#include <type_traits>

namespace seasalt {

// The fourteen card faces. Order matters: duos first so `isDuo` is a compare,
// then collectors, then the singletons. Every table below is indexed by this.
enum class Kind : uint8_t {
  Crab = 0,
  Boat,
  Fish,
  Swimmer,
  Shark,
  Shell,
  Octopus,
  Penguin,
  Sailor,
  Mermaid,
  Lighthouse,
  ShoalOfFish,
  PenguinColony,
  Captain,
};
constexpr int kKindCount = 14;

// How many of each face are in the deck. 34 duos + 16 collectors + 4 mermaids
// + 4 multipliers = 58.
constexpr uint8_t kKindSupply[kKindCount] = {9, 8, 7, 5, 5, 6, 5, 3, 2, 4, 1, 1, 1, 1};
constexpr int kCards = 58;

constexpr bool isDuo(const Kind k) { return k <= Kind::Shark; }
constexpr bool isCollector(const Kind k) { return k >= Kind::Shell && k <= Kind::Sailor; }
constexpr bool isMultiplier(const Kind k) { return k >= Kind::Lighthouse; }

// Collector payouts, indexed by how many you hold. Index 0 is holding none.
// Straight from the rulebook; the shape differs per collector, so these are
// tables rather than a formula.
constexpr uint8_t kShellScore[7] = {0, 0, 2, 4, 6, 8, 10};
constexpr uint8_t kOctopusScore[6] = {0, 0, 3, 6, 9, 12};
constexpr uint8_t kPenguinScore[4] = {0, 1, 3, 5};
constexpr uint8_t kSailorScore[3] = {0, 0, 5};

// Two players play to 40.
constexpr int kTargetScore = 40;
constexpr int kMinToEndRound = 7;
constexpr int kSeats = 2;
constexpr int kPiles = 2;
constexpr int kMermaidsToWin = 4;

constexpr uint8_t kNoCard = 0xFF;
constexpr uint8_t kNoSeat = 0xFF;

// Where a card is. A card is in exactly one of these at all times -- including
// the two off the top of the deck that a player is still choosing between,
// which is what `Drawn` is for. Without it those two are past the deal cursor
// while still claiming to be in the deck, and the count no longer adds up.
enum class Place : uint8_t {
  Deck = 0,
  Hand0,
  Hand1,
  Table0,
  Table1,
  PileA,
  PileB,
  Drawn,
};
constexpr int kPlaceCount = 8;
constexpr Place handOf(const int seat) { return static_cast<Place>(static_cast<int>(Place::Hand0) + seat); }
constexpr Place tableOf(const int seat) { return static_cast<Place>(static_cast<int>(Place::Table0) + seat); }
constexpr Place pileAt(const int pile) { return static_cast<Place>(static_cast<int>(Place::PileA) + pile); }

// Which colour a card is printed in, and how many of each exist. Filled from
// the physical deck; see docs/seasalt.md. Two rules read this and nothing else
// does: the mermaid, and the end-of-round colour bonus.
enum class Colour : uint8_t;  // defined in SeaSaltCards.h

Kind kindOf(uint8_t card);
Colour colourOf(uint8_t card);

// Where a round is. Distinct from the step inside a turn: this one says whether
// anybody is still playing.
enum class Phase : uint8_t {
  Playing = 0,
  LastChance,  // somebody bet; every opponent owes one more turn
  RoundOver,   // scored and banked, match still alive
  GameOver,
};

// Where a turn is. A turn is a sequence of decisions, not one move, and both
// devices have to agree which decision is outstanding -- so it lives in the
// shared state rather than in the Activity.
enum class Step : uint8_t {
  Take = 0,    // draw two, or take the top of a pile
  ChooseKeep,  // two are drawn: which one goes to hand
  ChoosePile,  // and which pile takes the other (skipped when one is empty)
  Play,        // lay duos, then end the turn or end the round
  CrabPile,    // a crab pair fired: which pile to dig through
  CrabPick,    // and which card out of it
};

// The whole game. Trivially copyable, so LinkPlay ships it as raw bytes between
// two identical builds.
struct Game {
  uint32_t seed = 0;

  // Where every card is, and the order it entered a discard pile. `seq` is what
  // makes a pile a pile: the top is the highest `seq` in it, so taking the top
  // exposes the one below without storing either pile as a list.
  uint8_t place[kCards] = {};
  uint8_t seq[kCards] = {};
  uint8_t seqNext = 1;   // 0 means "never discarded"
  uint8_t deckNext = 0;  // how far into the shuffled order we have dealt

  uint8_t score[kSeats] = {};  // banked across rounds, not this round's cards

  uint8_t turn = 0;
  uint8_t roundStarter = 0;
  uint8_t phase = static_cast<uint8_t>(Phase::Playing);
  uint8_t step = static_cast<uint8_t>(Step::Take);
  uint8_t round = 1;

  // The bet. `ender` is who said the word, `betWasLastChance` which word.
  uint8_t ender = kNoSeat;
  uint8_t betWasLastChance = 0;
  // Whose final turns are still owed after LAST CHANCE, as a seat bitmask.
  uint8_t owedFinalTurns = 0;

  uint8_t drawn[2] = {kNoCard, kNoCard};  // the two off the deck, mid-decision
  uint8_t pendingDiscard = kNoCard;       // the rejected one, awaiting a pile
  uint8_t crabPile = 0;                   // which pile the crab is digging

  // Hands revealed by ending the round. A revealed hand cannot be stolen from,
  // which is the only thing the rulebook promises the final turns.
  uint8_t revealedMask = 0;

  // A boat pair grants another turn. Counted, not flagged: two boat pairs in
  // one turn grant two.
  uint8_t extraTurns = 0;

  // --- set-up -------------------------------------------------------------

  // Shuffles all 58, deals nothing to hands (the rulebook deals no opening
  // hand), and turns the first two cards up as the two discard piles.
  void deal(uint32_t roundSeed, uint8_t starter);
  void newGame(uint32_t roundSeed, uint8_t starter);

  // --- playing ------------------------------------------------------------
  //
  // Each of these is legal in exactly one Step and returns false otherwise,
  // changing nothing.

  bool takeFromDeck();                 // Step::Take   -> ChooseKeep
  bool takeFromPile(int pile);         // Step::Take   -> Play
  bool keepDrawn(int which);           // ChooseKeep   -> ChoosePile, or Play
  bool discardTo(int pile);            // ChoosePile   -> Play
  bool playDuo(uint8_t a, uint8_t b);  // Play, stays in Play (or CrabPile)
  bool chooseCrabPile(int pile);       // CrabPile     -> CrabPick
  bool takeCrabCard(uint8_t card);     // CrabPick     -> Play
  // A turn with nothing to take is over. Reachable two ways: a boat pair grants
  // an extra turn after the deck has run out, and a LAST CHANCE final turn is
  // owed whatever is left. Both piles can also be empty, at which point there
  // is no legal take and the turn simply passes.
  bool canTake() const;
  bool endTurn();                  // Play (or a Take with nothing to take)
  bool endRound(bool lastChance);  // Play, only at kMinToEndRound or more

  // --- derived, never stored ----------------------------------------------

  Phase currentPhase() const { return static_cast<Phase>(phase); }
  Step currentStep() const { return static_cast<Step>(step); }

  int handSize(int seat) const;
  int tableSize(int seat) const;
  int pileSize(int pile) const;
  uint8_t pileTop(int pile) const;  // kNoCard when empty
  int deckRemaining() const { return kCards - deckNext; }

  int countIn(Place where, Kind kind) const;
  // Cards a seat holds anywhere that counts for scoring: hand plus table.
  int countHeld(int seat, Kind kind) const;
  int countHeldColour(int seat, Colour colour) const;

  // What a seat's cards are worth right now, hand plus table, exactly as the
  // rulebook's "the points on their cards" means it. The colour bonus is NOT
  // in here: it is a property of how the round ended, not of the cards.
  int cardPoints(int seat) const;
  // 1 point per card of the colour held most. Only ever paid in a LAST CHANCE
  // round, to one side or the other.
  int colourBonus(int seat) const;
  // What the seat actually banks, given how the round ended. Valid once the
  // phase is RoundOver or GameOver.
  int roundScore(int seat) const;

  // Can this seat say STOP or LAST CHANCE right now?
  bool mayEndRound(int seat) const { return cardPoints(seat) >= kMinToEndRound; }
  // Did the ender's bet come off? Meaningless unless a bet was made.
  bool betWon() const;

  // Mermaids never leave the hand -- they are not duos and there is no way to
  // lay one down -- so holding all four is the win, and it fires the moment the
  // fourth arrives, whether it was drawn, taken off a pile, dug out by a crab
  // or stolen with a shark.
  int mermaidsHeld(int seat) const { return countIn(handOf(seat), Kind::Mermaid); }
  int matchWinner() const;  // -1 until somebody has won

  // The duo pairs a seat could lay from hand right now, as the count of
  // playable pairs per duo kind. Swimmer and shark pair with each other, so
  // both entries carry the same number.
  int playablePairs(int seat, Kind kind) const;

  // The shuffled deck rebuilt from `seed`: position i is the i-th card dealt.
  uint8_t cardAt(int index) const;
};

static_assert(std::is_trivially_copyable<Game>::value, "Game travels as raw bytes");
static_assert(sizeof(Game) <= 192, "Game must fit one LinkPlay packet");

}  // namespace seasalt
