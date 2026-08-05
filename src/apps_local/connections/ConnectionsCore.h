#pragma once

// Connections rules, freestanding.
//
// No Arduino, no renderer, no storage, no heap: the same split as ChessCore,
// for the same reason. The rules are what a test can pin down exactly, so they
// live where a laptop can run them (host-tests/connections/).
//
// The game: sixteen words, four secret groups of four. Pick four, submit. Four
// wrong guesses and it is over. The only wrinkle worth knowing is "one away",
// which tells you when three of your four belong together, because without it a
// near miss is indistinguishable from a wild one.

#include <cstdint>

namespace connections {

constexpr int kGroups = 4;
constexpr int kMembers = 4;
constexpr int kTiles = kGroups * kMembers;
constexpr int kMaxMistakes = 4;

// Sized from the whole published archive (1143 puzzles, 18288 words): the
// longest word is 28 characters and the longest group name is 71. Fixed buffers
// rather than std::string keeps a Puzzle a plain value with no allocation,
// which is what lets it live on the stack of a 380KB device.
constexpr int kMaxWordLen = 32;
constexpr int kMaxGroupLen = 80;

// The source stopped publishing colour data on 2025-09-20: 831 puzzles have
// real levels, the 312 since do not, and it is a clean cutoff rather than
// scattered gaps. So anything keyed on difficulty works for the back catalogue
// and not for today's puzzle, which is the one most people play.
constexpr uint8_t kLevelUnknown = 0xFF;

struct Group {
  // 0 yellow (easiest) to 3 purple (hardest), the NYT's own ordering, or
  // kLevelUnknown. Recorded as unknown rather than defaulted to 0: claiming
  // every group in a recent puzzle is yellow would be a confident lie, and
  // anything built on it (a "purple first" badge) would be wrong 27% of the
  // time without ever looking wrong.
  uint8_t level = kLevelUnknown;
  char name[kMaxGroupLen + 1] = {};
  char members[kMembers][kMaxWordLen + 1] = {};
};

struct Puzzle {
  uint16_t id = 0;
  // YYYYMMDD. Sorts and compares as an integer, and prints without a calendar.
  uint32_t date = 0;
  Group groups[kGroups] = {};
};

enum class Result : uint8_t { Playing, Won, Lost };

enum class Guess : uint8_t {
  Invalid,  // not exactly four tiles selected
  Wrong,
  OneAway,  // three of the four share a group: the NYT's one real hint
  Solved,
  // These four have been submitted before. Costs no mistake, because charging
  // for a guess the player already paid for is just punishing them for losing
  // track of what they tried.
  AlreadyGuessed,
};

// One puzzle in progress. Copyable and self-contained, so saving it is writing
// the few fields that are not derived.
class Game {
 public:
  // `shuffleSeed` fixes the tile order. Stored rather than re-rolled so that
  // leaving the app and coming back shows the same board, which matters on a
  // device people put down mid-puzzle.
  void start(const Puzzle& puzzle, uint32_t shuffleSeed);

  // --- board ---------------------------------------------------------------
  // Unsolved tiles only, so a solved group leaves the board rather than
  // lingering greyed out. Solved groups are read back through solvedGroup().
  int tileCount() const { return tiles; }
  const char* tileWord(int index) const;
  // Which group a tile belongs to. Only meaningful to the renderer after the
  // game ends, when the unsolved rows are revealed.
  int tileGroup(int index) const;
  bool isSelected(int index) const;

  void toggleTile(int index);
  void deselectAll();
  int selectedCount() const { return selected; }
  bool canSubmit() const { return selected == kMembers && result_ == Result::Playing; }

  // Judges the current selection and, when correct, moves that group off the
  // board. Returns Invalid without changing anything unless four are selected,
  // and AlreadyGuessed without changing anything if this exact four has been
  // submitted before.
  Guess submit();

  // True when this exact combination has already been submitted. Exposed so the
  // board can grey SUBMIT out rather than let it be pressed for nothing.
  bool selectionAlreadyGuessed() const;

  // Reorders the unsolved tiles. Solved groups are unaffected.
  void shuffle(uint32_t seed);

  // --- progress ------------------------------------------------------------
  int mistakes() const { return mistakes_; }
  int mistakesLeft() const { return kMaxMistakes - mistakes_; }
  int solvedCount() const { return solved; }
  Result result() const { return result_; }
  // Solved groups in the order they were found. After a loss every group is
  // listed, because seeing the answer is the point of losing.
  const Group& solvedGroup(int index) const;
  int revealedCount() const { return result_ == Result::Lost ? kGroups : solved; }

  const Puzzle& puzzle() const { return puzzle_; }
  uint32_t seed() const { return seed_; }

  // --- resume --------------------------------------------------------------
  // Everything not derivable from the puzzle: which groups are solved, in what
  // order, how many mistakes, and the tile order. Replaying that onto a fresh
  // start() restores the board exactly.
  struct Save {
    uint32_t seed = 0;
    uint8_t solvedOrder[kGroups] = {};
    uint8_t solvedCount = 0;
    uint8_t mistakes = 0;
    // Guess history, so resuming does not forget what has been tried and let
    // the player spend a mistake on a combination they already ruled out.
    uint16_t guessed[8] = {};
    uint8_t guessCount = 0;
  };
  Save save() const;
  // Returns false and leaves the game at its starting position if the save is
  // inconsistent with the puzzle, rather than trusting a file off an SD card.
  bool restore(const Save& state);

 private:
  // Index into puzzle_.groups[g].members[m], packed as g*kMembers+m. The board
  // is a permutation of these, with solved groups swapped to the tail.
  uint8_t order[kTiles] = {};
  bool chosen[kTiles] = {};

  Puzzle puzzle_;
  uint32_t seed_ = 0;
  int tiles = 0;
  int selected = 0;
  int solved = 0;
  int mistakes_ = 0;
  Result result_ = Result::Playing;
  uint8_t solvedOrder[kGroups] = {};

  // Every combination submitted so far, as a 16-bit mask over the puzzle's
  // packed (group, member) positions rather than over board slots -- a mask of
  // slots would stop meaning the same thing the moment the board is shuffled.
  // Eight is the ceiling that matters: four wrong guesses ends the game and
  // four right ones wins it, and a repeat is rejected without being recorded.
  static constexpr int kMaxHistory = 8;
  uint16_t guessed[kMaxHistory] = {};
  uint8_t guessCount = 0;

  uint16_t selectionMask() const;
  int groupOf(int slot) const { return order[slot] / kMembers; }
  void removeSolvedGroup(int group);
};

// True when every group has four members, no word is empty, and no word is
// repeated. Import validates with this so a malformed puzzle never reaches the
// board, where the failure would be a stuck game rather than a skipped one.
bool isPlayable(const Puzzle& puzzle);

}  // namespace connections
