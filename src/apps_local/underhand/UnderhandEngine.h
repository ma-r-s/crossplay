#pragma once

// Underhand's rules. Freestanding: no renderer, no storage, no clock.
//
// Reimplemented from the original's compiled logic (DeckController, DeckModel,
// OptionModel, ResourceController, StartupController in libmain.so). Random
// numbers are drawn in the same order the original draws them, with one
// exception: the original also draws one to time its radio clips, depending on
// the clock, and a port without the radio does not.
//
// A Game is trivially copyable: saving one is writing its bytes.

#include <cstdint>

#include "UnderhandCards.h"

namespace underhand {

// Card ids the original's engine names in code rather than in its data.
namespace ids {
constexpr uint8_t kTutorial = 71;  // on top of the first deck until a tutorial run is won
constexpr uint8_t kRumors = 29;    // dealt into the top five cards of every run
constexpr int kRumorsDepth = 5;
constexpr uint8_t kGreed = 56;      // punishment: too many resources
constexpr uint8_t kPoliceRaid = 2;  // punishment: too much suspicion
constexpr uint8_t kDesperate = 27;  // punishment: no food
// Initial cards that are always dealt instead of on a coin flip. 27 and 29 are
// never initial; the set is the original's, whole.
constexpr uint8_t kAlwaysDealt[] = {27, 29, 57, 79, 85, 106};
// Quest openers that become initial once enough gods have been summoned.
struct Tier {
  uint8_t card;
  uint8_t gods;
};
constexpr Tier kTiers[] = {{85, 3}, {106, 3}, {79, 5}, {57, 5}};
}  // namespace ids

// Uniform integers. The device uses Rng; tests script their own.
class Random {
 public:
  virtual int below(int n) = 0;  // in [0, n); 0 when n <= 0

 protected:
  ~Random() = default;
};

class Rng final : public Random {
 public:
  explicit Rng(uint64_t seed = 1) : state_(seed ? seed : 1) {}
  int below(int n) override;
  uint64_t state() const { return state_; }

 private:
  uint64_t state_;
};

// What outlives a run: which gods have been summoned, the last of them, and
// whether the tutorial has been won.
struct Profile {
  uint8_t summoned = 0;  // bit per god
  int8_t previous = -1;  // the god summoned most recently
  bool tutorialDone = false;
  int summonedCount() const;
};

// Draw pile and discard pile. The top of the draw pile is its last card.
// The real cards never put more than 40 in play; a full pile drops the card
// and says so in `overflowed`.
struct Pile {
  static constexpr int kCapacity = 200;
  uint8_t size = 0;
  bool overflowed = false;
  uint8_t card[kCapacity] = {};

  bool push(uint8_t id) { return insert(size, id); }
  bool insert(int at, uint8_t id);
  uint8_t erase(int at);
  uint8_t top() const { return size ? card[size - 1] : 0; }
};

enum class Phase : uint8_t {
  Choosing,   // a card is on the table
  Foresight,  // looking at the next cards after a foresight option
  Won,
  Lost,
};

enum class LossReason : uint8_t {
  None,
  Choice,   // an option that ends the run
  Stuck,    // no option on the table can be paid
  NoCards,  // both piles are empty
  GaveUp,   // the player ended it from the menu (the original's Forfeit)
};

struct Game {
  Counts held{};
  Pile draw;
  Pile discard;

  // The card on the table, with its costs and random shuffles resolved as it
  // was drawn: a "half" cost is half of what was held then.
  uint8_t card = 0;
  Counts cost[kMaxOptions]{};
  Counts gain[kMaxOptions]{};
  uint8_t rolled[kMaxOptions][kMaxRoll]{};
  // Options that end the run, locked because another option can be paid.
  uint8_t lockedMask = 0;

  Phase phase = Phase::Choosing;
  LossReason loss = LossReason::None;
  int8_t god = -1;        // summoned, when Won
  bool tutorial = false;  // this run is the tutorial

  // Foresight: the next cards, top first, and which to discard.
  uint8_t seen[3] = {};
  uint8_t seenCount = 0;
  uint8_t discardMask = 0;
  bool mayDiscard = false;

  // What the last choice did, for the screen to report: the card and option it
  // was made on, what it took, gave and lost at random, what it shuffled in.
  uint8_t played = 0;
  int8_t playedOption = -1;
  Counts paid{};
  Counts lost{};
  Counts gained{};
  struct Added {
    uint8_t card;
    uint8_t copies;
  } added[kMaxAdds + kMaxRoll]{};
  uint8_t addedCount = 0;
  bool reshuffled = false;
  uint16_t turn = 0;
};

// A new run: the starting hand, the deck dealt, the first card drawn. The last
// god's unlock cards are dealt into this run only, so `previous` is spent.
void start(Game& game, const Cards& cards, Profile& profile, Random& random);

// The chance, in percent, of each punishment being rolled before the next
// draw if the hand stood as it is now (DeckController::resolvePunishments):
// Greed at 16 or more held, a Police Raid at 5 or more suspicion, Desperate
// Measures with no food. Each is rolled only if the one before it missed. All
// 0 in the tutorial and on a punishment card, after which nothing is rolled.
struct Odds {
  int greed = 0;
  int police = 0;
  int desperate = 0;
};
Odds punishmentOdds(const Game& game);
// The same three chances on this card for another hand, such as the one a
// payment would leave.
Odds punishmentOdds(const Game& game, const Counts& hand);

// Whether option `k` of the card on the table can be paid from what is held.
bool affordable(const Game& game, const Cards& cards, int k);
// Whether `offer` pays option `k` exactly, as the original requires: relics
// stand in for anything and, where the option allows, cultists and prisoners
// for each other.
bool exact(const Game& game, const Cards& cards, int k, const Counts& offer);
// A payment for option `k`, keeping relics and cultists where it can: the
// named resources first, prisoners before cultists, relics for what is short.
bool suggest(const Game& game, const Cards& cards, int k, Counts& out);

// Takes option `k`, paid with `offer`, which must be held and exact.
// Afterwards the run has ended, is showing foresight, or has the next card on
// the table.
bool choose(Game& game, const Cards& cards, int k, const Counts& offer, Random& random);
// During foresight: mark or unmark the i-th seen card for discard.
void toggleDiscard(Game& game, int i);
// Leaves foresight and draws the next card.
void endForesight(Game& game, const Cards& cards, Random& random);

// Ends the run in progress as the player's own choice, a loss like any
// other: nothing is summoned, and what was summoned before stays.
void giveUp(Game& game);

// Records a finished run.
void finish(Profile& profile, const Game& game);

// Whether a Game read back from storage is one these cards can continue.
bool valid(const Game& game, const Cards& cards);

// Checks the card data holds every card the rules name. Call once after loading.
bool rulesFit(const Cards& cards, const char** why = nullptr);

// The steps the calls above are made of, exposed for tests.
namespace detail {
void reshuffle(Game& game, const Cards& cards, Random& random);
void punish(Game& game, Random& random);
void resolve(Game& game, const Cards& cards, Random& random);
void nextCard(Game& game, const Cards& cards, Random& random);
// OptionModel::satisfiability: 0 when `offer` pays `cost` exactly, below 0
// when it pays too much, 1 when it falls short.
int satisfiability(const Counts& cost, bool swap, Counts offer);
}  // namespace detail

}  // namespace underhand
