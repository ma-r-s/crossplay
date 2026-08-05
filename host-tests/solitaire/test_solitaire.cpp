// Solitaire rules tests. Freestanding: no device, no PlatformIO.
//
// The soak at the bottom is this game's answer to chess's perft. There is no
// published node count to compare against, so instead it deals a thousand
// games, plays random legal moves in each, and asserts after every single one
// that the board is still a board: fifty-two distinct cards present, every
// foundation ascending in one suit from the ace, every face-up tableau run
// descending and alternating, nothing past the end of a pile. A rules bug that
// corrupts state has to survive a hundred thousand moves of that to reach the
// device.

#include <cstdio>
#include <vector>

#include "../../src/apps_local/solitaire/SolitaireCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_solitaire.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace solitaire;

// Rebuilds a board card by card. The rules deal a shuffled game, which is the
// wrong starting point for testing one specific rule, so these tests place
// exactly the cards they mean to reason about.
class Board {
 public:
  Board() {
    game_.deal(1, false);
    clear();
  }

  // Every card has to be somewhere for a save to validate, and no pile holds
  // fifty-two. The deck is parked face down in the stock and then in the
  // right-hand tableau columns, so columns 0-4 and the waste start genuinely
  // empty and a test can compose exactly the position it means to reason about.
  static constexpr int kSpareColumn = kFirstTableau + kTableauPiles - 1;

  void clear() {
    Game::Save save;
    game_.save(save);
    for (int i = 0; i < kPileCount; ++i) save.counts[i] = 0;

    int pile = kStockPile;
    for (int suit = 0; suit < kSuits; ++suit) {
      for (int rank = 0; rank < kRanks; ++rank) {
        while (save.counts[pile] == kPileCapacity) pile = pile == kStockPile ? kSpareColumn : pile - 1;
        save.piles[pile][save.counts[pile]++] = makeCard(static_cast<Suit>(suit), rank);
      }
    }
    CHECK(game_.restore(save));
  }

  // Empties `pile` first, so a probe lands on a known board instead of on
  // whatever the previous probe left behind.
  void placeOnly(const int pile, const uint8_t card, const bool up = true) {
    clearPile(pile);
    place(pile, card, up);
  }

  // Moves `card` out of wherever it is and onto the top of `pile`.
  void place(const int pile, const uint8_t card, const bool up = true) {
    Game::Save save;
    game_.save(save);
    for (int p = 0; p < kPileCount; ++p) {
      for (int c = 0; c < save.counts[p]; ++c) {
        if (faceDown(save.piles[p][c]) != faceDown(card)) continue;
        for (int k = c; k + 1 < save.counts[p]; ++k) save.piles[p][k] = save.piles[p][k + 1];
        save.counts[p]--;
        break;
      }
    }
    save.piles[pile][save.counts[pile]++] = up ? faceUp(card) : faceDown(card);
    CHECK(game_.restore(save));
  }

  Game& game() { return game_; }

 private:
  // Sends everything on `pile` back to the spare column, face down, so no card
  // goes missing and the save still validates.
  void clearPile(const int pile) {
    Game::Save save;
    game_.save(save);
    while (save.counts[pile] > 0) {
      save.piles[kSpareColumn][save.counts[kSpareColumn]++] = faceDown(save.piles[pile][--save.counts[pile]]);
    }
    CHECK(game_.restore(save));
  }

  Game game_;
};

uint8_t card(const Suit suit, const int rank) { return makeCard(suit, rank); }

// Every invariant a board must satisfy no matter what has been played.
const char* inconsistency(const Game& game) {
  int seen[kDeck] = {};
  int total = 0;
  for (int p = 0; p < kPileCount; ++p) {
    const Pile& pile = game.pile(p);
    if (pile.count > kPileCapacity) return "pile past capacity";
    for (int c = 0; c < pile.count; ++c) {
      const uint8_t value = faceDown(pile.cards[c]);
      const int rank = rankOf(value);
      if (rank >= kRanks) return "rank out of range";
      seen[static_cast<int>(suitOf(value)) * kRanks + rank]++;
      total++;
    }
  }
  if (total != kDeck) return "not 52 cards";
  for (const int count : seen) {
    if (count != 1) return "card duplicated or lost";
  }

  for (int f = 0; f < kFoundationPiles; ++f) {
    const Pile& pile = game.pile(kFirstFoundation + f);
    for (int c = 0; c < pile.count; ++c) {
      if (rankOf(pile.cards[c]) != c) return "foundation does not ascend from the ace";
      if (suitOf(pile.cards[c]) != suitOf(pile.cards[0])) return "foundation mixes suits";
    }
  }

  for (int t = 0; t < kTableauPiles; ++t) {
    const Pile& pile = game.pile(kFirstTableau + t);
    bool sawFaceUp = false;
    for (int c = 0; c < pile.count; ++c) {
      const bool up = isFaceUp(pile.cards[c]);
      // Face-down cards can only ever sit below face-up ones.
      if (up) sawFaceUp = true;
      if (sawFaceUp && !up) return "face-down card above a face-up one";
      if (c == 0 || !up || !isFaceUp(pile.cards[c - 1])) continue;
      if (rankOf(pile.cards[c]) != rankOf(pile.cards[c - 1]) - 1) return "tableau run does not descend";
      if (isRed(pile.cards[c]) == isRed(pile.cards[c - 1])) return "tableau run does not alternate";
    }
  }

  for (int c = 0; c < game.pile(kStockPile).count; ++c) {
    if (isFaceUp(game.pile(kStockPile).cards[c])) return "stock card face up";
  }
  for (int c = 0; c < game.pile(kWastePile).count; ++c) {
    if (!isFaceUp(game.pile(kWastePile).cards[c])) return "waste card face down";
  }
  return nullptr;
}

// One assertion per sweep rather than sixty, because the soak calls this after
// every one of a couple of hundred thousand moves and a check count in the
// tens of millions tells you nothing.
void checkConsistent(const Game& game, const int line) {
  const char* problem = inconsistency(game);
  check(problem == nullptr, problem == nullptr ? "board consistent" : problem, line);
}

#define CHECK_CONSISTENT(game) checkConsistent((game), __LINE__)

void testDeal() {
  Game game;
  game.deal(12345, false);
  CHECK_CONSISTENT(game);

  for (int column = 0; column < kTableauPiles; ++column) {
    const Pile& pile = game.pile(kFirstTableau + column);
    CHECK(pile.count == column + 1);
    for (int c = 0; c < pile.count; ++c) {
      // Exactly the last card of each column starts face up.
      CHECK(isFaceUp(pile.cards[c]) == (c == pile.count - 1));
    }
  }
  CHECK(game.pile(kStockPile).count == 24);
  CHECK(game.pile(kWastePile).count == 0);
  for (int f = 0; f < kFoundationPiles; ++f) CHECK(game.pile(kFirstFoundation + f).empty());
  CHECK(!game.won());
  CHECK(!game.canUndo());
}

void testSeedIsStable() {
  // Resuming a saved game re-deals from the seed in some designs, and a shuffle
  // that drifts would hand back a different game under the same name.
  uint8_t first[kDeck];
  uint8_t second[kDeck];
  shuffledDeck(777, first);
  shuffledDeck(777, second);
  for (int i = 0; i < kDeck; ++i) CHECK(first[i] == second[i]);

  uint8_t other[kDeck];
  shuffledDeck(778, other);
  int differences = 0;
  for (int i = 0; i < kDeck; ++i) {
    if (other[i] != first[i]) differences++;
  }
  CHECK(differences > 20);

  // Seed zero must still shuffle rather than dealing the deck in order.
  uint8_t zero[kDeck];
  shuffledDeck(0, zero);
  int inOrder = 0;
  for (int i = 0; i < kDeck; ++i) {
    if (zero[i] == makeCard(static_cast<Suit>(i / kRanks), i % kRanks)) inOrder++;
  }
  CHECK(inOrder < kDeck);
}

void testTableauBuilding() {
  Board board;
  Game& game = board.game();
  const int columnA = kFirstTableau;
  const int columnB = kFirstTableau + 1;

  board.place(columnA, card(Suit::Spades, 6));  // black seven
  board.place(columnB, card(Suit::Hearts, 5));  // red six
  CHECK(game.canMove(columnB, columnA, 1));
  CHECK(game.move(columnB, columnA, 1));
  CHECK_CONSISTENT(game);

  // columnA now reads black seven, red six. A red five is the right rank and
  // the wrong colour.
  board.placeOnly(columnB, card(Suit::Diamonds, 4));
  CHECK(!game.canMove(columnB, columnA, 1));
  // Right colour, wrong rank.
  board.placeOnly(columnB, card(Suit::Spades, 2));
  CHECK(!game.canMove(columnB, columnA, 1));
  // Right colour, right rank.
  board.placeOnly(columnB, card(Suit::Spades, 4));
  CHECK(game.canMove(columnB, columnA, 1));
}

void testEmptyColumnTakesOnlyAKing() {
  Board board;
  Game& game = board.game();
  const int empty = kFirstTableau + 3;
  const int source = kFirstTableau;

  for (int rank = 0; rank < kRanks; ++rank) {
    board.place(source, card(Suit::Hearts, rank));
    const bool allowed = game.canMove(source, empty, 1);
    CHECK(allowed == (rank == kRanks - 1));
  }
}

void testFoundationBuilding() {
  Board board;
  Game& game = board.game();
  const int foundation = kFirstFoundation;
  const int column = kFirstTableau;

  // Only an ace opens a foundation. placeOnly keeps the column to a single card
  // so each probe is about the foundation rule and not about what it landed on.
  board.placeOnly(column, card(Suit::Hearts, 1));
  CHECK(!game.canMove(column, foundation, 1));
  board.placeOnly(column, card(Suit::Hearts, 0));
  CHECK(game.move(column, foundation, 1));

  // Then the same suit in order, and nothing else.
  board.placeOnly(column, card(Suit::Diamonds, 1));
  CHECK(!game.canMove(column, foundation, 1));
  board.placeOnly(column, card(Suit::Hearts, 2));
  CHECK(!game.canMove(column, foundation, 1));
  board.placeOnly(column, card(Suit::Hearts, 1));
  CHECK(game.move(column, foundation, 1));
  CHECK(game.pile(foundation).count == 2);
  CHECK_CONSISTENT(game);

  // A foundation never takes more than one card at a time, even from a legal
  // tableau run sitting right on top of it.
  board.placeOnly(column, card(Suit::Hearts, 3));
  board.place(column, card(Suit::Spades, 2));
  CHECK(game.runLength(column, 0) == 2);
  CHECK(!game.canMove(column, foundation, 2));
  CHECK_CONSISTENT(game);
}

void testRunLength() {
  Board board;
  Game& game = board.game();
  const int column = kFirstTableau;

  board.place(column, card(Suit::Clubs, 12), false);  // buried king, face down
  board.place(column, card(Suit::Hearts, 8));         // red nine
  board.place(column, card(Suit::Spades, 7));         // black eight
  board.place(column, card(Suit::Diamonds, 6));       // red seven

  CHECK(game.runLength(column, 0) == 0);  // face down moves nothing
  CHECK(game.runLength(column, 1) == 3);
  CHECK(game.runLength(column, 2) == 2);
  CHECK(game.runLength(column, 3) == 1);
  CHECK(game.runLength(column, 4) == 0);  // past the end

  // Break the alternation and the run stops being a run: a red six on a red
  // seven is the right rank and the wrong colour.
  board.place(column, card(Suit::Hearts, 5));
  CHECK(game.runLength(column, 1) == 0);
  CHECK(game.runLength(column, 4) == 1);

  // The waste never yields more than its top card.
  board.place(kWastePile, card(Suit::Hearts, 4));
  board.place(kWastePile, card(Suit::Spades, 3));
  CHECK(game.runLength(kWastePile, 1) == 1);
  CHECK(game.runLength(kWastePile, 0) == 0);
}

void testMovingARun() {
  Board board;
  Game& game = board.game();
  const int from = kFirstTableau;
  const int to = kFirstTableau + 1;

  board.place(from, card(Suit::Hearts, 8));    // red nine
  board.place(from, card(Suit::Spades, 7));    // black eight
  board.place(from, card(Suit::Diamonds, 6));  // red seven
  board.place(to, card(Suit::Clubs, 9));       // black ten

  CHECK(game.canMove(from, to, 3));
  CHECK(!game.canMove(from, to, 2));  // the eight does not sit on the ten
  CHECK(game.move(from, to, 3));
  CHECK(game.pile(from).empty());
  CHECK(game.pile(to).count == 4);
  CHECK_CONSISTENT(game);
}

void testFlipAndUndo() {
  Board board;
  Game& game = board.game();
  const int from = kFirstTableau;
  const int to = kFirstTableau + 1;

  board.place(from, card(Suit::Clubs, 4), false);  // hidden
  board.place(from, card(Suit::Hearts, 5));        // red six
  board.place(to, card(Suit::Spades, 6));          // black seven

  CHECK(!isFaceUp(game.pile(from).at(0)));
  CHECK(game.move(from, to, 1));
  CHECK(isFaceUp(game.pile(from).at(0)));  // the move turned it over

  CHECK(game.canUndo());
  CHECK(game.undo());
  // Undo must hide it again. A card you were shown for free is information you
  // should not get to keep after taking the move back.
  CHECK(!isFaceUp(game.pile(from).at(0)));
  CHECK(game.pile(from).count == 2);
  CHECK(game.pile(to).count == 1);
  CHECK(game.moveCount() == 0);
  CHECK(!game.canUndo());
  CHECK(!game.undo());
  CHECK_CONSISTENT(game);
}

void testStockDrawOne() {
  Game game;
  game.deal(4242, false);
  const uint8_t topOfStock = game.pile(kStockPile).top();

  CHECK(game.drawFromStock());
  CHECK(game.pile(kStockPile).count == 23);
  CHECK(game.pile(kWastePile).count == 1);
  CHECK(faceDown(game.pile(kWastePile).top()) == faceDown(topOfStock));
  CHECK(isFaceUp(game.pile(kWastePile).top()));
  CHECK_CONSISTENT(game);

  CHECK(game.undo());
  CHECK(game.pile(kStockPile).count == 24);
  CHECK(game.pile(kWastePile).empty());
  CHECK(!isFaceUp(game.pile(kStockPile).top()));
  CHECK_CONSISTENT(game);
}

void testStockDrawThree() {
  Game game;
  game.deal(99, true);
  CHECK(game.drawThree());
  CHECK(game.drawFromStock());
  CHECK(game.pile(kWastePile).count == 3);
  CHECK(game.pile(kStockPile).count == 21);
  CHECK(game.undo());
  CHECK(game.pile(kWastePile).empty());
  CHECK(game.pile(kStockPile).count == 24);

  // Twenty-four does not divide by three evenly at the end of every deal, so
  // the last draw must hand over what is left rather than nothing.
  for (int i = 0; i < 8; ++i) CHECK(game.drawFromStock());
  CHECK(game.pile(kStockPile).empty());
  CHECK(game.pile(kWastePile).count == 24);
  CHECK_CONSISTENT(game);
}

void testRecycle() {
  Game game;
  game.deal(31337, false);
  std::vector<uint8_t> order;
  for (int i = 0; i < 24; ++i) {
    CHECK(game.drawFromStock());
    order.push_back(faceDown(game.pile(kWastePile).top()));
  }
  CHECK(game.pile(kStockPile).empty());

  // Recycling reverses the waste back into the stock, so the cards come round
  // again in the same rotation. Reshuffling here would make a draw-three game
  // unsolvable in principle.
  CHECK(game.drawFromStock());
  CHECK(game.pile(kStockPile).count == 24);
  CHECK(game.pile(kWastePile).empty());
  CHECK_CONSISTENT(game);
  for (int i = 0; i < 24; ++i) {
    CHECK(game.drawFromStock());
    CHECK(faceDown(game.pile(kWastePile).top()) == order[static_cast<size_t>(i)]);
  }

  // Undoing the recycle puts the whole waste back, face up.
  Game other;
  other.deal(31337, false);
  for (int i = 0; i < 24; ++i) other.drawFromStock();
  other.drawFromStock();
  CHECK(other.undo());
  CHECK(other.pile(kWastePile).count == 24);
  CHECK(other.pile(kStockPile).empty());
  CHECK_CONSISTENT(other);

  // A stock and waste that are both empty is a dead stock, not a crash.
  Game dead;
  dead.deal(6, false);
  Game::Save save;
  dead.save(save);
  // Move the stock into the foundations, which is the only place 24 cards can
  // legally go without disturbing what the tableau already holds.
  for (int i = 0; i < kFoundationPiles; ++i) save.counts[kFirstFoundation + i] = 0;
  int next = 0;
  for (int i = 0; i < save.counts[kStockPile]; ++i) {
    const uint8_t card = faceDown(save.piles[kStockPile][i]);
    save.piles[kFirstFoundation + (next % kFoundationPiles)][next / kFoundationPiles] = card;
    save.counts[kFirstFoundation + (next % kFoundationPiles)]++;
    next++;
  }
  save.counts[kStockPile] = 0;
  save.counts[kWastePile] = 0;
  CHECK(dead.restore(save));
  CHECK(!dead.drawFromStock());
}

void testAutoFinish() {
  Board board;
  Game& game = board.game();
  // Every card face up in the tableau and the stock empty: nothing is hidden,
  // so the game is already won whatever order it is finished in.
  Game::Save save;
  game.save(save);
  for (int i = 0; i < kPileCount; ++i) save.counts[i] = 0;
  for (int suit = 0; suit < kSuits; ++suit) {
    const int column = kFirstTableau + suit;
    for (int rank = kRanks - 1; rank >= 0; --rank) {
      save.piles[column][save.counts[column]++] = faceUp(makeCard(static_cast<Suit>(suit), rank));
    }
  }
  CHECK(game.restore(save));
  CHECK(game.canAutoFinish());
  CHECK(!game.won());

  const int moved = game.autoPlay();
  CHECK(moved == kDeck);
  CHECK(game.won());
  CHECK(!game.canAutoFinish());  // nothing left to finish
  CHECK_CONSISTENT(game);

  // A fresh deal has face-down cards, so the button must not be offered.
  Game fresh;
  fresh.deal(5, false);
  CHECK(!fresh.canAutoFinish());
}

void testSaveRestore() {
  Game game;
  game.deal(2024, true);
  for (int i = 0; i < 5; ++i) game.drawFromStock();
  game.autoPlay();

  Game::Save save;
  game.save(save);

  Game restored;
  CHECK(restored.restore(save));
  CHECK(restored.seed() == game.seed());
  CHECK(restored.drawThree() == game.drawThree());
  CHECK(restored.moveCount() == game.moveCount());
  for (int p = 0; p < kPileCount; ++p) {
    CHECK(restored.pile(p).count == game.pile(p).count);
    for (int c = 0; c < restored.pile(p).count; ++c) {
      CHECK(restored.pile(p).cards[c] == game.pile(p).cards[c]);
    }
  }
  CHECK_CONSISTENT(restored);
  // Undo does not survive the save: replaying a move stack against a board that
  // could have been edited underneath it is how a valid game gets corrupted.
  CHECK(!restored.canUndo());
}

void testRestoreRejectsRubbish() {
  Game game;
  game.deal(1, false);
  Game::Save good;
  game.save(good);

  // A count past the end of a pile would be read out of bounds on frame one.
  Game::Save tooLong = good;
  tooLong.counts[kFirstTableau] = kPileCapacity + 1;
  CHECK(!Game().restore(tooLong));

  // A duplicated card means a missing one somewhere else.
  Game::Save duplicate = good;
  duplicate.piles[kFirstTableau][0] = duplicate.piles[kStockPile][0];
  CHECK(!Game().restore(duplicate));

  // A rank that does not exist.
  Game::Save badRank = good;
  badRank.piles[kStockPile][0] = 0x0E;
  CHECK(!Game().restore(badRank));

  // A save short of the full deck.
  Game::Save short_ = good;
  short_.counts[kStockPile] = 0;
  CHECK(!Game().restore(short_));

  // The good one still loads, so the checks above are rejecting the damage and
  // not the format.
  CHECK(Game().restore(good));
}

void testUndoDepthOverflow() {
  Game game;
  game.deal(8, false);
  // Far past the ring's capacity. The oldest steps are dropped rather than the
  // move being refused, and the board has to stay coherent through all of it.
  for (int i = 0; i < kUndoDepth * 3; ++i) {
    CHECK(game.drawFromStock());
  }
  CHECK_CONSISTENT(game);
  int undone = 0;
  while (game.undo()) undone++;
  CHECK(undone == kUndoDepth);
  CHECK_CONSISTENT(game);
}

// Random legal play across many deals, checking every invariant after every
// move. This is the closest thing this game has to a perft.
void testSoak() {
  uint32_t random = 0xC0FFEE;
  const auto next = [&random]() {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };

  int wins = 0;
  int totalMoves = 0;
  for (int deal = 0; deal < 1000; ++deal) {
    Game game;
    game.deal(static_cast<uint32_t>(deal) * 2654435761u + 1u, deal % 2 == 0);

    for (int step = 0; step < 220; ++step) {
      // Collect every legal move, then take one at random. Enumerating rather
      // than guessing is what makes this cover the rules instead of the
      // happy path.
      std::vector<std::pair<int, std::pair<int, int>>> options;
      for (int from = kWastePile; from < kPileCount; ++from) {
        const Pile& source = game.pile(from);
        for (int index = 0; index < source.count; ++index) {
          const int run = game.runLength(from, index);
          if (run == 0) continue;
          for (int to = kFirstFoundation; to < kPileCount; ++to) {
            if (game.canMove(from, to, run)) options.push_back({from, {to, run}});
          }
        }
      }
      const bool canDraw = !game.pile(kStockPile).empty() || !game.pile(kWastePile).empty();
      if (options.empty() && !canDraw) break;

      if (options.empty() || (canDraw && next() % 4 == 0)) {
        CHECK(game.drawFromStock());
      } else {
        const auto& pick = options[next() % options.size()];
        CHECK(game.move(pick.first, pick.second.first, pick.second.second));
      }
      totalMoves++;
      CHECK_CONSISTENT(game);
      if (game.won()) {
        wins++;
        break;
      }
    }

    // Unwinding the whole undo ring has to leave a coherent board too, which is
    // where an undo that forgets to re-hide a flipped card shows up.
    while (game.undo()) CHECK_CONSISTENT(game);
  }
  std::printf("  soak: %d moves across 1000 deals, %d won by random play\n", totalMoves, wins);
  CHECK(totalMoves > 50000);
}

}  // namespace

int main() {
  testDeal();
  testSeedIsStable();
  testTableauBuilding();
  testEmptyColumnTakesOnlyAKing();
  testFoundationBuilding();
  testRunLength();
  testMovingARun();
  testFlipAndUndo();
  testStockDrawOne();
  testStockDrawThree();
  testRecycle();
  testAutoFinish();
  testSaveRestore();
  testRestoreRejectsRubbish();
  testUndoDepthOverflow();
  testSoak();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
