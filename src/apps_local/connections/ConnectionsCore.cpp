#include "ConnectionsCore.h"

#include <cstring>

namespace connections {

namespace {

// A small LCG rather than <random>: freestanding, deterministic across
// toolchains, and the whole point is that a given seed reproduces a given board
// after the app is closed and reopened. Numerical Recipes constants.
uint32_t nextRandom(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

}  // namespace

void Game::start(const Puzzle& puzzle, const uint32_t shuffleSeed) {
  puzzle_ = puzzle;
  tiles = kTiles;
  selected = 0;
  solved = 0;
  mistakes_ = 0;
  result_ = Result::Playing;
  std::memset(chosen, 0, sizeof(chosen));
  std::memset(solvedOrder, 0, sizeof(solvedOrder));
  std::memset(guessed, 0, sizeof(guessed));
  guessCount = 0;
  for (int i = 0; i < kTiles; ++i) order[i] = static_cast<uint8_t>(i);
  shuffle(shuffleSeed);
}

const char* Game::tileWord(const int index) const {
  if (index < 0 || index >= tiles) return "";
  const int packed = order[index];
  return puzzle_.groups[packed / kMembers].members[packed % kMembers];
}

int Game::tileGroup(const int index) const {
  if (index < 0 || index >= tiles) return -1;
  return groupOf(index);
}

bool Game::isSelected(const int index) const { return index >= 0 && index < tiles && chosen[index]; }

void Game::toggleTile(const int index) {
  if (index < 0 || index >= tiles || result_ != Result::Playing) return;
  if (chosen[index]) {
    chosen[index] = false;
    --selected;
    return;
  }
  // Four is the whole guess, so a fifth tap is a no-op rather than silently
  // dropping someone's first pick.
  if (selected >= kMembers) return;
  chosen[index] = true;
  ++selected;
}

void Game::deselectAll() {
  std::memset(chosen, 0, sizeof(chosen));
  selected = 0;
}

void Game::removeSolvedGroup(const int group) {
  // Compact the solved group's tiles off the end of the live range, preserving
  // the order of everything else so the board does not reshuffle itself under
  // the player's eyes.
  int write = 0;
  uint8_t kept[kTiles];
  for (int i = 0; i < tiles; ++i) {
    if (groupOf(i) != group) kept[write++] = order[i];
  }
  for (int i = 0; i < write; ++i) order[i] = kept[i];
  tiles = write;
  std::memset(chosen, 0, sizeof(chosen));
  selected = 0;
}

uint16_t Game::selectionMask() const {
  uint16_t mask = 0;
  for (int i = 0; i < tiles; ++i) {
    // Keyed on the packed (group, member) position, not the board slot, so the
    // mask survives a shuffle.
    if (chosen[i]) mask |= static_cast<uint16_t>(1u << order[i]);
  }
  return mask;
}

bool Game::selectionAlreadyGuessed() const {
  if (selected != kMembers) return false;
  const uint16_t mask = selectionMask();
  for (int i = 0; i < guessCount; ++i) {
    // cppcheck-suppress useStlAlgorithm
    if (guessed[i] == mask) return true;
  }
  return false;
}

Guess Game::submit() {
  if (!canSubmit()) return Guess::Invalid;
  // Checked before anything is counted: repeating a guess is a memory lapse,
  // not a wrong answer, and charging a mistake for it would punish the player
  // for the board not remembering on their behalf.
  if (selectionAlreadyGuessed()) return Guess::AlreadyGuessed;
  if (guessCount < kMaxHistory) guessed[guessCount++] = selectionMask();

  int perGroup[kGroups] = {};
  for (int i = 0; i < tiles; ++i) {
    if (chosen[i]) ++perGroup[groupOf(i)];
  }

  int best = 0;
  int bestGroup = 0;
  for (int g = 0; g < kGroups; ++g) {
    if (perGroup[g] > best) {
      best = perGroup[g];
      bestGroup = g;
    }
  }

  if (best == kMembers) {
    solvedOrder[solved++] = static_cast<uint8_t>(bestGroup);
    removeSolvedGroup(bestGroup);
    if (solved == kGroups) result_ = Result::Won;
    return Guess::Solved;
  }

  ++mistakes_;
  if (mistakes_ >= kMaxMistakes) result_ = Result::Lost;
  // Three of four is the one hint this game gives. Reported even on the final
  // mistake: knowing how close you were is the consolation.
  return best == kMembers - 1 ? Guess::OneAway : Guess::Wrong;
}

void Game::shuffle(const uint32_t seed) {
  seed_ = seed;
  // Fisher-Yates over the unsolved range only, so shuffling mid-game cannot
  // drag a solved group back onto the board.
  // No special case for a zero seed: the generator's additive constant means
  // zero is not a fixed point, and mapping it to 1 would only make seed 0 and
  // seed 1 deal the same board.
  uint32_t state = seed;
  for (int i = tiles - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(state) % static_cast<uint32_t>(i + 1));
    const uint8_t tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  deselectAll();
}

const Group& Game::solvedGroup(const int index) const {
  if (result_ == Result::Lost) {
    // Everything is revealed on a loss: the groups already found first, in the
    // order they were found, then the rest.
    if (index < solved) return puzzle_.groups[solvedOrder[index]];
    int seen = solved;
    for (int g = 0; g < kGroups; ++g) {
      bool found = false;
      for (int s = 0; s < solved; ++s) {
        if (solvedOrder[s] == g) found = true;
      }
      if (found) continue;
      if (seen++ == index) return puzzle_.groups[g];
    }
    return puzzle_.groups[0];
  }
  const int clamped = index < 0 ? 0 : (index >= solved ? (solved > 0 ? solved - 1 : 0) : index);
  return puzzle_.groups[solvedOrder[clamped]];
}

Game::Save Game::save() const {
  Save state;
  state.seed = seed_;
  state.solvedCount = static_cast<uint8_t>(solved);
  state.mistakes = static_cast<uint8_t>(mistakes_);
  for (int i = 0; i < kGroups; ++i) state.solvedOrder[i] = solvedOrder[i];
  state.guessCount = guessCount;
  for (int i = 0; i < kMaxHistory; ++i) state.guessed[i] = guessed[i];
  return state;
}

bool Game::restore(const Save& state) {
  if (state.solvedCount > kGroups || state.mistakes > kMaxMistakes) return false;
  // A repeated group would solve the same four tiles twice and leave the board
  // one row short with no way to finish it.
  for (int i = 0; i < state.solvedCount; ++i) {
    if (state.solvedOrder[i] >= kGroups) return false;
    for (int j = i + 1; j < state.solvedCount; ++j) {
      if (state.solvedOrder[i] == state.solvedOrder[j]) return false;
    }
  }

  start(puzzle_, state.seed);
  for (int i = 0; i < state.solvedCount; ++i) {
    const int group = state.solvedOrder[i];
    solvedOrder[solved++] = static_cast<uint8_t>(group);
    removeSolvedGroup(group);
  }
  mistakes_ = state.mistakes;
  guessCount = state.guessCount > kMaxHistory ? kMaxHistory : state.guessCount;
  for (int i = 0; i < guessCount; ++i) guessed[i] = state.guessed[i];
  if (solved == kGroups) {
    result_ = Result::Won;
  } else if (mistakes_ >= kMaxMistakes) {
    result_ = Result::Lost;
  }
  return true;
}

bool isPlayable(const Puzzle& puzzle) {
  for (int g = 0; g < kGroups; ++g) {
    if (puzzle.groups[g].name[0] == '\0') return false;
    for (int m = 0; m < kMembers; ++m) {
      if (puzzle.groups[g].members[m][0] == '\0') return false;
    }
  }
  // A duplicated word makes two tiles indistinguishable, so a correct guess can
  // select the wrong copy and read as wrong.
  for (int a = 0; a < kTiles; ++a) {
    const char* wordA = puzzle.groups[a / kMembers].members[a % kMembers];
    for (int b = a + 1; b < kTiles; ++b) {
      if (std::strcmp(wordA, puzzle.groups[b / kMembers].members[b % kMembers]) == 0) return false;
    }
  }
  return true;
}

}  // namespace connections
