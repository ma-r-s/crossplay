#pragma once

// Klondike solitaire, as pure rules.
//
// Freestanding by the same rule as ChessCore and ConnectionsCore: no renderer,
// no storage, no Arduino. Everything here is decided by looking at the state
// and nothing here can draw, which is what lets host-tests/solitaire/ deal
// thousands of games and check every move without a device.
//
// Card encoding is one byte:
//
//   bits 0-3   rank, 0 = ace .. 12 = king
//   bits 4-5   suit
//   bit 7      face up
//
// so a card is comparable, copyable and fits an array. kNoCard is 0xFF, which
// cannot collide because rank 15 does not exist.

#include <cstdint>

namespace solitaire {

constexpr uint8_t kNoCard = 0xFF;
constexpr int kRanks = 13;
constexpr int kSuits = 4;
constexpr int kDeck = kRanks * kSuits;

constexpr int kTableauPiles = 7;
constexpr int kFoundationPiles = 4;

// Every pile in one array, so a move is a pair of indices and the rules never
// need to know which kind of pile they were handed until they check.
constexpr int kStockPile = 0;
constexpr int kWastePile = 1;
constexpr int kFirstFoundation = 2;
constexpr int kFirstTableau = kFirstFoundation + kFoundationPiles;  // 6
constexpr int kPileCount = kFirstTableau + kTableauPiles;           // 13

// A tableau pile tops out at six face-down cards plus a full king-to-ace run,
// and the stock starts at twenty-four. One size fits every pile.
constexpr int kPileCapacity = 24;

enum class Suit : uint8_t { Clubs = 0, Diamonds = 1, Spades = 2, Hearts = 3 };

inline uint8_t makeCard(const Suit suit, const int rank) {
  return static_cast<uint8_t>((static_cast<int>(suit) << 4) | rank);
}
inline int rankOf(const uint8_t card) { return card & 0x0F; }
inline Suit suitOf(const uint8_t card) { return static_cast<Suit>((card >> 4) & 0x03); }
inline bool isFaceUp(const uint8_t card) { return (card & 0x80) != 0; }
inline uint8_t faceUp(const uint8_t card) { return static_cast<uint8_t>(card | 0x80); }
inline uint8_t faceDown(const uint8_t card) { return static_cast<uint8_t>(card & 0x7F); }

// Diamonds and hearts are the red suits. On a one-bit panel this decides
// outline versus solid rather than colour, but the rule it drives -- tableau
// runs alternate colour -- is the same rule.
inline bool isRed(const uint8_t card) {
  const Suit suit = suitOf(card);
  return suit == Suit::Diamonds || suit == Suit::Hearts;
}

struct Pile {
  uint8_t cards[kPileCapacity] = {};
  uint8_t count = 0;

  bool empty() const { return count == 0; }
  uint8_t top() const { return count == 0 ? kNoCard : cards[count - 1]; }
  uint8_t at(const int index) const { return (index < 0 || index >= count) ? kNoCard : cards[index]; }
};

// One reversible step. `flipped` records whether the move turned a face-down
// tableau card up, because undoing has to turn it back: a card you were shown
// for free is information you should not keep after taking the move back.
struct Move {
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t count = 0;
  bool flipped = false;
  // Set for the stock tap, which is a move like any other so that it undoes
  // like one. `count` then means how many cards went to the waste, and a
  // recycle of the whole waste back into the stock is recorded as count 0.
  bool stock = false;
};

constexpr int kUndoDepth = 96;

class Game {
 public:
  // `drawThree` picks the deal size for a stock tap. Everything else about the
  // game is identical either way.
  void deal(uint32_t seed, bool drawThree);

  const Pile& pile(int index) const;
  bool drawThree() const { return drawThree_; }
  uint32_t seed() const { return seed_; }
  int moveCount() const { return moveCount_; }
  bool won() const;

  // How many cards from `index` up the pile form a legal, movable run. 0 when
  // nothing there can move. Used both by the rules and by the screen, which
  // needs to know how far a tap's selection reaches.
  int runLength(int pileIndex, int cardIndex) const;

  // Whether `count` cards taken from the top of `from` may land on `to`.
  bool canMove(int from, int to, int count) const;

  // Performs the move if legal and returns true. Flips a newly exposed
  // face-down tableau card, which is part of the move rather than a separate
  // step, so undo puts it back.
  bool move(int from, int to, int count);

  // The stock tap: deals to the waste, or recycles the waste when the stock is
  // empty. False only when both are empty, which is a dead stock.
  bool drawFromStock();

  // The single move a tapped card most likely wants: its own foundation if it
  // fits, otherwise nothing. Returns the destination pile or -1.
  int foundationFor(int from) const;

  // True when every card is face up and the game is therefore already won,
  // whatever order you finish it in. This is the only condition under which
  // the UI offers autoPlay(): sending cards up early can genuinely strand you
  // (a two you still needed to build on), and a button that sometimes loses
  // the game for you is worse than no button. Once nothing is hidden there is
  // no decision left to take away.
  bool canAutoFinish() const;

  // Plays every foundation move available, repeatedly, and returns how many
  // cards it moved. On a screen that costs half a second per refresh, this is
  // the difference between a finished game and an abandoned one.
  int autoPlay();

  bool canUndo() const { return undoCount_ > 0; }
  bool undo();

  // Byte-for-byte save, so a game survives leaving the app.
  struct Save {
    uint32_t seed = 0;
    uint8_t piles[kPileCount][kPileCapacity] = {};
    uint8_t counts[kPileCount] = {};
    uint16_t moves = 0;
    bool drawThree = false;
  };
  void save(Save& out) const;
  bool restore(const Save& in);

 private:
  bool acceptsOnFoundation(int foundation, uint8_t card) const;
  bool acceptsOnTableau(int tableau, uint8_t card) const;
  void pushUndo(const Move& record);

  Pile piles_[kPileCount];
  Move undo_[kUndoDepth];
  int undoCount_ = 0;
  int moveCount_ = 0;
  uint32_t seed_ = 0;
  bool drawThree_ = false;
};

// The shuffle, exposed so a test can assert a seed always deals the same game.
// A deck that reshuffles on resume is a different game wearing the same save.
void shuffledDeck(uint32_t seed, uint8_t out[kDeck]);

}  // namespace solitaire
