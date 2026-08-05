#include "SolitaireCore.h"

#include <cstring>

namespace solitaire {

namespace {

// xorshift32. The same generator the other games use: it is seedable, it is
// four lines, and it has no dependency on the standard library's RNG, whose
// implementation is not guaranteed to match between the host tests and the
// device. A deal that differed between the two would make every test a lie.
uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

bool isTableau(const int index) { return index >= kFirstTableau && index < kPileCount; }
bool isFoundation(const int index) { return index >= kFirstFoundation && index < kFirstFoundation + kFoundationPiles; }

}  // namespace

void shuffledDeck(const uint32_t seed, uint8_t out[kDeck]) {
  for (int suit = 0; suit < kSuits; ++suit) {
    for (int rank = 0; rank < kRanks; ++rank) {
      out[suit * kRanks + rank] = makeCard(static_cast<Suit>(suit), rank);
    }
  }
  // A zero seed would leave xorshift stuck at zero forever, dealing the same
  // sorted deck every time. Nudge it rather than rejecting it, so callers can
  // pass any uptime value they like.
  uint32_t state = seed == 0 ? 0x9E3779B9u : seed;
  for (int i = kDeck - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(state) % static_cast<uint32_t>(i + 1));
    const uint8_t swap = out[i];
    out[i] = out[j];
    out[j] = swap;
  }
}

void Game::deal(const uint32_t seed, const bool drawThree) {
  for (auto& pile : piles_) pile.count = 0;
  undoCount_ = 0;
  moveCount_ = 0;
  seed_ = seed;
  drawThree_ = drawThree;

  uint8_t deck[kDeck];
  shuffledDeck(seed, deck);

  int next = 0;
  for (int column = 0; column < kTableauPiles; ++column) {
    Pile& pile = piles_[kFirstTableau + column];
    for (int row = 0; row <= column; ++row) {
      // Only the last card of each column is dealt face up. That is the whole
      // shape of Klondike: 28 cards down, 24 in the stock, one visible per pile.
      const bool last = row == column;
      pile.cards[pile.count++] = last ? faceUp(deck[next]) : faceDown(deck[next]);
      next++;
    }
  }
  Pile& stock = piles_[kStockPile];
  while (next < kDeck) stock.cards[stock.count++] = faceDown(deck[next++]);
}

const Pile& Game::pile(const int index) const {
  static const Pile empty;
  if (index < 0 || index >= kPileCount) return empty;
  return piles_[index];
}

bool Game::won() const {
  for (int i = 0; i < kFoundationPiles; ++i) {
    if (piles_[kFirstFoundation + i].count != kRanks) return false;
  }
  return true;
}

bool Game::acceptsOnFoundation(const int foundation, const uint8_t card) const {
  const Pile& pile = piles_[foundation];
  if (pile.empty()) return rankOf(card) == 0;
  const uint8_t top = pile.top();
  return suitOf(top) == suitOf(card) && rankOf(card) == rankOf(top) + 1;
}

bool Game::acceptsOnTableau(const int tableau, const uint8_t card) const {
  const Pile& pile = piles_[tableau];
  // An empty column takes a king and nothing else. This is the rule that makes
  // the game hard, so it is worth being loud about: without it Klondike is
  // nearly always winnable.
  if (pile.empty()) return rankOf(card) == kRanks - 1;
  const uint8_t top = pile.top();
  if (!isFaceUp(top)) return false;
  return isRed(top) != isRed(card) && rankOf(card) == rankOf(top) - 1;
}

int Game::runLength(const int pileIndex, const int cardIndex) const {
  if (pileIndex < 0 || pileIndex >= kPileCount) return 0;
  const Pile& pile = piles_[pileIndex];
  if (cardIndex < 0 || cardIndex >= pile.count) return 0;
  if (!isFaceUp(pile.cards[cardIndex])) return 0;

  // Only a tableau lets you take more than the top card, and only when what
  // sits above forms a descending alternating-colour run.
  if (!isTableau(pileIndex)) return cardIndex == pile.count - 1 ? 1 : 0;

  for (int i = cardIndex; i + 1 < pile.count; ++i) {
    const uint8_t upper = pile.cards[i];
    const uint8_t lower = pile.cards[i + 1];
    if (!isFaceUp(lower)) return 0;
    if (isRed(upper) == isRed(lower)) return 0;
    if (rankOf(lower) != rankOf(upper) - 1) return 0;
  }
  return pile.count - cardIndex;
}

bool Game::canMove(const int from, const int to, const int count) const {
  if (from < 0 || from >= kPileCount || to < 0 || to >= kPileCount) return false;
  if (from == to || count <= 0) return false;
  if (to == kStockPile || to == kWastePile) return false;

  const Pile& source = piles_[from];
  if (count > source.count) return false;
  // The stock is only ever dealt from, never dragged.
  if (from == kStockPile) return false;

  const int firstIndex = source.count - count;
  if (runLength(from, firstIndex) != count) return false;

  const uint8_t moving = source.cards[firstIndex];
  if (isFoundation(to)) {
    // Foundations take one card at a time, always.
    return count == 1 && acceptsOnFoundation(to, moving);
  }
  return acceptsOnTableau(to, moving);
}

void Game::pushUndo(const Move& record) {
  if (undoCount_ == kUndoDepth) {
    // Drop the oldest rather than refusing the move. A move you cannot make is
    // a worse failure than a move you cannot take back ninety-six steps later.
    std::memmove(&undo_[0], &undo_[1], sizeof(Move) * (kUndoDepth - 1));
    undoCount_--;
  }
  undo_[undoCount_++] = record;
}

bool Game::move(const int from, const int to, const int count) {
  if (!canMove(from, to, count)) return false;

  Pile& source = piles_[from];
  Pile& target = piles_[to];
  const int firstIndex = source.count - count;
  for (int i = 0; i < count; ++i) target.cards[target.count++] = source.cards[firstIndex + i];
  source.count = static_cast<uint8_t>(firstIndex);

  Move record;
  record.from = static_cast<uint8_t>(from);
  record.to = static_cast<uint8_t>(to);
  record.count = static_cast<uint8_t>(count);

  // Exposing a face-down card turns it over. It is part of this move, not a
  // separate one, so undo has to put it back down.
  if (isTableau(from) && !source.empty() && !isFaceUp(source.top())) {
    source.cards[source.count - 1] = faceUp(source.top());
    record.flipped = true;
  }
  pushUndo(record);
  moveCount_++;
  return true;
}

bool Game::drawFromStock() {
  Pile& stock = piles_[kStockPile];
  Pile& waste = piles_[kWastePile];

  if (stock.empty()) {
    if (waste.empty()) return false;
    // Recycling preserves the order by reversing the waste back, which is what
    // makes a draw-three game solvable at all: the cards come round again in
    // the same rotation rather than being reshuffled.
    while (!waste.empty()) stock.cards[stock.count++] = faceDown(waste.cards[--waste.count]);
    Move record;
    record.stock = true;
    record.count = 0;
    pushUndo(record);
    moveCount_++;
    return true;
  }

  const int wanted = drawThree_ ? 3 : 1;
  int dealt = 0;
  while (dealt < wanted && !stock.empty()) {
    waste.cards[waste.count++] = faceUp(stock.cards[--stock.count]);
    dealt++;
  }
  Move record;
  record.stock = true;
  record.count = static_cast<uint8_t>(dealt);
  pushUndo(record);
  moveCount_++;
  return true;
}

int Game::foundationFor(const int from) const {
  if (from < 0 || from >= kPileCount) return -1;
  const Pile& source = piles_[from];
  if (source.empty()) return -1;
  const uint8_t card = source.top();
  if (!isFaceUp(card)) return -1;
  for (int i = 0; i < kFoundationPiles; ++i) {
    const int foundation = kFirstFoundation + i;
    if (foundation == from) continue;
    if (canMove(from, foundation, 1)) return foundation;
  }
  return -1;
}

bool Game::canAutoFinish() const {
  if (won()) return false;
  if (!piles_[kStockPile].empty()) return false;
  for (int i = 0; i < kTableauPiles; ++i) {
    const Pile& pile = piles_[kFirstTableau + i];
    for (int c = 0; c < pile.count; ++c) {
      if (!isFaceUp(pile.cards[c])) return false;
    }
  }
  return true;
}

int Game::autoPlay() {
  int moved = 0;
  bool progress = true;
  while (progress) {
    progress = false;
    // The waste first, then the tableau left to right. Order only affects which
    // equivalent card goes first, never whether the sweep terminates: every
    // iteration that makes progress removes a card from a non-foundation pile,
    // and there are fifty-two of those.
    for (int source = kWastePile; source < kPileCount; ++source) {
      if (source == kStockPile) continue;
      if (isFoundation(source)) continue;
      const int target = foundationFor(source);
      if (target < 0) continue;
      if (!move(source, target, 1)) continue;
      moved++;
      progress = true;
    }
  }
  return moved;
}

bool Game::undo() {
  if (undoCount_ == 0) return false;
  const Move record = undo_[--undoCount_];
  moveCount_--;

  if (record.stock) {
    Pile& stock = piles_[kStockPile];
    Pile& waste = piles_[kWastePile];
    if (record.count == 0) {
      while (!stock.empty()) waste.cards[waste.count++] = faceUp(stock.cards[--stock.count]);
    } else {
      for (int i = 0; i < record.count; ++i) {
        stock.cards[stock.count++] = faceDown(waste.cards[--waste.count]);
      }
    }
    return true;
  }

  Pile& source = piles_[record.from];
  Pile& target = piles_[record.to];
  if (record.flipped && !source.empty()) source.cards[source.count - 1] = faceDown(source.top());
  const int firstIndex = target.count - record.count;
  for (int i = 0; i < record.count; ++i) source.cards[source.count++] = target.cards[firstIndex + i];
  target.count = static_cast<uint8_t>(firstIndex);
  return true;
}

void Game::save(Save& out) const {
  out.seed = seed_;
  out.moves = static_cast<uint16_t>(moveCount_ < 0 ? 0 : moveCount_);
  out.drawThree = drawThree_;
  for (int i = 0; i < kPileCount; ++i) {
    out.counts[i] = piles_[i].count;
    std::memcpy(out.piles[i], piles_[i].cards, kPileCapacity);
  }
}

bool Game::restore(const Save& in) {
  // A save is read off a card that anything could have written, so validate it
  // rather than trusting it: a count past the end of a pile would be read out
  // of bounds on the very first frame.
  int seen[kDeck] = {};
  for (int i = 0; i < kPileCount; ++i) {
    if (in.counts[i] > kPileCapacity) return false;
    for (int c = 0; c < in.counts[i]; ++c) {
      const uint8_t card = faceDown(in.piles[i][c]);
      const int rank = rankOf(card);
      const int suit = static_cast<int>(suitOf(card));
      if (rank >= kRanks) return false;
      const int index = suit * kRanks + rank;
      if (seen[index]++ != 0) return false;
    }
  }
  for (const int count : seen) {
    if (count != 1) return false;
  }

  for (int i = 0; i < kPileCount; ++i) {
    piles_[i].count = in.counts[i];
    std::memcpy(piles_[i].cards, in.piles[i], kPileCapacity);
  }
  seed_ = in.seed;
  moveCount_ = in.moves;
  drawThree_ = in.drawThree;
  // Undo does not survive the save. Restoring a stack of moves against a board
  // that might have been edited underneath it is a way to corrupt a valid game.
  undoCount_ = 0;
  return true;
}

}  // namespace solitaire
