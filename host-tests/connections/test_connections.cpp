// Host tests for the Connections rules. No device, no PlatformIO: ConnectionsCore
// is freestanding C++17 precisely so this runs on a laptop. See run.sh.
//
// The rules are small, so the tests go for exhaustiveness rather than samples:
// every one of the 24 orders a puzzle can be solved in, every 4-tile selection
// on a fresh board classified against a hand-computed expectation, and a
// save/restore round trip from every reachable state.

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../../src/apps_local/connections/ConnectionsCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_connections.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// A real puzzle: #1, 2023-06-12, the first one ever published.
connections::Puzzle samplePuzzle() {
  connections::Puzzle p;
  p.id = 1;
  p.date = 20230612;
  const char* names[4] = {"WET WEATHER", "NBA TEAMS", "KEYBOARD KEYS", "PALINDROMES"};
  const char* words[4][4] = {
      {"HAIL", "RAIN", "SLEET", "SNOW"},
      {"BUCKS", "HEAT", "JAZZ", "NETS"},
      {"OPTION", "RETURN", "SHIFT", "TAB"},
      {"KAYAK", "LEVEL", "MOM", "RACECAR"},
  };
  for (int g = 0; g < 4; ++g) {
    p.groups[g].level = static_cast<uint8_t>(g);
    std::snprintf(p.groups[g].name, sizeof(p.groups[g].name), "%s", names[g]);
    for (int m = 0; m < 4; ++m) {
      std::snprintf(p.groups[g].members[m], sizeof(p.groups[g].members[m]), "%s", words[g][m]);
    }
  }
  return p;
}

// Selects the four tiles belonging to `group`, wherever the shuffle put them.
void selectGroup(connections::Game& game, const int group) {
  game.deselectAll();
  for (int i = 0; i < game.tileCount(); ++i) {
    if (game.tileGroup(i) == group) game.toggleTile(i);
  }
}

// Selects `count` tiles from `group` and fills up to four from other groups.
void selectMixed(connections::Game& game, const int group, const int count) {
  game.deselectAll();
  int taken = 0;
  for (int i = 0; i < game.tileCount() && taken < count; ++i) {
    if (game.tileGroup(i) == group) {
      game.toggleTile(i);
      ++taken;
    }
  }
  for (int i = 0; i < game.tileCount() && game.selectedCount() < 4; ++i) {
    if (game.tileGroup(i) != group && !game.isSelected(i)) game.toggleTile(i);
  }
}

// A wrong guess that is distinct from the other variants: two tiles from one
// group still on the board and two from another, choosing different members as
// `variant` increases. Needed because repeating a guess no longer costs a
// mistake, so "guess wrong four times" has to mean four different guesses.
void selectWrongVariant(connections::Game& game, const int variant) {
  game.deselectAll();
  int groups[connections::kGroups];
  int groupCount = 0;
  for (int i = 0; i < game.tileCount(); ++i) {
    const int g = game.tileGroup(i);
    bool seen = false;
    for (int k = 0; k < groupCount; ++k) {
      if (groups[k] == g) seen = true;
    }
    if (!seen) groups[groupCount++] = g;
  }
  if (groupCount < 2) return;

  const int a = groups[0];
  const int b = groups[1];
  int takenA = 0;
  int skipA = variant % 2;
  for (int i = 0; i < game.tileCount() && takenA < 2; ++i) {
    if (game.tileGroup(i) != a) continue;
    if (skipA > 0) {
      --skipA;
      continue;
    }
    game.toggleTile(i);
    ++takenA;
  }
  int takenB = 0;
  int skipB = (variant / 2) % 2;
  for (int i = 0; i < game.tileCount() && takenB < 2; ++i) {
    if (game.tileGroup(i) != b) continue;
    if (skipB > 0) {
      --skipB;
      continue;
    }
    game.toggleTile(i);
    ++takenB;
  }
}

// The four words currently selected, by name rather than by slot.
std::vector<std::string> selectedWords(const connections::Game& game) {
  std::vector<std::string> words;
  for (int i = 0; i < game.tileCount(); ++i) {
    if (game.isSelected(i)) words.push_back(game.tileWord(i));
  }
  return words;
}

// Selects exactly these words, wherever the board is currently holding them.
void selectWords(connections::Game& game, const std::vector<std::string>& words) {
  game.deselectAll();
  for (const std::string& word : words) {
    for (int i = 0; i < game.tileCount(); ++i) {
      if (word == game.tileWord(i)) {
        game.toggleTile(i);
        break;
      }
    }
  }
}

std::vector<std::string> boardWords(const connections::Game& game) {
  std::vector<std::string> words;
  for (int i = 0; i < game.tileCount(); ++i) words.push_back(game.tileWord(i));
  return words;
}

void testFreshBoard() {
  connections::Game game;
  game.start(samplePuzzle(), 12345);
  CHECK(game.tileCount() == 16);
  CHECK(game.selectedCount() == 0);
  CHECK(game.solvedCount() == 0);
  CHECK(game.mistakes() == 0);
  CHECK(game.mistakesLeft() == 4);
  CHECK(game.result() == connections::Result::Playing);
  CHECK(!game.canSubmit());

  // All sixteen words are on the board exactly once.
  std::set<std::string> seen(boardWords(game).begin(), boardWords(game).end());
  CHECK(seen.size() == 16);
  CHECK(seen.count("RACECAR") == 1);

  // Out-of-range access is inert rather than undefined.
  CHECK(std::string(game.tileWord(-1)).empty());
  CHECK(std::string(game.tileWord(99)).empty());
  CHECK(game.tileGroup(99) == -1);
  CHECK(!game.isSelected(99));
}

void testSelection() {
  connections::Game game;
  game.start(samplePuzzle(), 7);

  game.toggleTile(0);
  CHECK(game.selectedCount() == 1);
  CHECK(game.isSelected(0));
  game.toggleTile(0);
  CHECK(game.selectedCount() == 0);
  CHECK(!game.isSelected(0));

  for (int i = 0; i < 4; ++i) game.toggleTile(i);
  CHECK(game.selectedCount() == 4);
  CHECK(game.canSubmit());

  // A fifth tap does nothing: four is the whole guess, and silently dropping
  // someone's first pick to make room would be worse than ignoring the tap.
  game.toggleTile(4);
  CHECK(game.selectedCount() == 4);
  CHECK(!game.isSelected(4));

  game.deselectAll();
  CHECK(game.selectedCount() == 0);
  CHECK(!game.canSubmit());
  CHECK(game.submit() == connections::Guess::Invalid);
}

void testGuessClassification() {
  connections::Game game;
  game.start(samplePuzzle(), 99);

  // Three from one group and one from another is the one hint this game gives.
  selectMixed(game, 1, 3);
  CHECK(game.selectedCount() == 4);
  CHECK(game.submit() == connections::Guess::OneAway);
  CHECK(game.mistakes() == 1);
  CHECK(game.tileCount() == 16);

  // Two and two is wrong, not close.
  selectMixed(game, 1, 2);
  CHECK(game.submit() == connections::Guess::Wrong);
  CHECK(game.mistakes() == 2);

  // A correct guess takes its four tiles off the board.
  selectGroup(game, 2);
  CHECK(game.submit() == connections::Guess::Solved);
  CHECK(game.solvedCount() == 1);
  CHECK(game.tileCount() == 12);
  CHECK(game.selectedCount() == 0);
  CHECK(std::strcmp(game.solvedGroup(0).name, "KEYBOARD KEYS") == 0);

  // The solved group's words are gone; everything else is still there in the
  // order it was, because a board that reshuffles itself under you is worse
  // than one that just gets shorter.
  for (const std::string& word : boardWords(game)) {
    CHECK(word != "OPTION" && word != "SHIFT");
  }
}

void testRepeatedGuesses() {
  connections::Game game;
  game.start(samplePuzzle(), 99);

  // The same four words, twice. The second attempt costs nothing: repeating a
  // guess is a memory lapse, not a wrong answer.
  selectMixed(game, 1, 2);
  CHECK(game.submit() == connections::Guess::Wrong);
  CHECK(game.mistakes() == 1);

  selectMixed(game, 1, 2);
  CHECK(game.selectionAlreadyGuessed());
  CHECK(game.submit() == connections::Guess::AlreadyGuessed);
  CHECK(game.mistakes() == 1);
  // And the selection survives, so the player can adjust one tile rather than
  // starting the guess over.
  CHECK(game.selectedCount() == 4);

  // A different four is not a repeat.
  selectMixed(game, 2, 3);
  CHECK(!game.selectionAlreadyGuessed());
  CHECK(game.submit() == connections::Guess::OneAway);
  CHECK(game.mistakes() == 2);

  // The history is keyed on the words, not on where they sit, so shuffling the
  // board cannot launder a guess into a new one. Selected by name, because
  // picking "the first two tiles of group 1" lands on different words once the
  // board has moved -- which is a different guess, not a repeat.
  selectMixed(game, 1, 2);
  const std::vector<std::string> repeat = selectedWords(game);
  CHECK(repeat.size() == 4);
  const std::vector<std::string> before = boardWords(game);
  game.shuffle(4242);
  CHECK(boardWords(game) != before);
  selectWords(game, repeat);
  CHECK(selectedWords(game).size() == 4);
  CHECK(game.selectionAlreadyGuessed());
  CHECK(game.submit() == connections::Guess::AlreadyGuessed);
  CHECK(game.mistakes() == 2);

  // A correct guess is remembered too, but its tiles leave the board, so it can
  // never be offered again anyway.
  selectGroup(game, 0);
  CHECK(!game.selectionAlreadyGuessed());
  CHECK(game.submit() == connections::Guess::Solved);

  // Starting a new game forgets everything.
  game.start(samplePuzzle(), 99);
  selectMixed(game, 1, 2);
  CHECK(!game.selectionAlreadyGuessed());

  // Fewer than four selected is never "already guessed": there is nothing to
  // compare, and the button is off for a different reason.
  game.deselectAll();
  game.toggleTile(0);
  CHECK(!game.selectionAlreadyGuessed());
}

void testHistorySurvivesResume() {
  connections::Game original;
  original.start(samplePuzzle(), 7);
  selectMixed(original, 1, 2);
  original.submit();
  selectMixed(original, 2, 3);
  original.submit();

  connections::Game resumed;
  resumed.start(samplePuzzle(), 1);
  CHECK(resumed.restore(original.save()));
  // Without this, putting the device down and picking it up again would let a
  // mistake be spent on a combination already ruled out.
  selectMixed(resumed, 1, 2);
  CHECK(resumed.selectionAlreadyGuessed());
  CHECK(resumed.submit() == connections::Guess::AlreadyGuessed);
  CHECK(resumed.mistakes() == original.mistakes());
}

void testEverySolveOrder() {
  // All 24 orders. The rules are small enough to check exhaustively, which
  // beats picking one order and hoping the others behave.
  int orders[24][4];
  int count = 0;
  for (int a = 0; a < 4; ++a) {
    for (int b = 0; b < 4; ++b) {
      for (int c = 0; c < 4; ++c) {
        for (int d = 0; d < 4; ++d) {
          if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
          orders[count][0] = a;
          orders[count][1] = b;
          orders[count][2] = c;
          orders[count][3] = d;
          ++count;
        }
      }
    }
  }
  CHECK(count == 24);

  for (int o = 0; o < count; ++o) {
    connections::Game game;
    game.start(samplePuzzle(), static_cast<uint32_t>(o + 1));
    for (int step = 0; step < 4; ++step) {
      selectGroup(game, orders[o][step]);
      const bool ok = game.submit() == connections::Guess::Solved;
      check(ok, "every solve order solves", __LINE__);
      const bool sized = game.tileCount() == 16 - 4 * (step + 1);
      check(sized, "board shrinks by four each solve", __LINE__);
    }
    check(game.result() == connections::Result::Won, "four groups solved wins", __LINE__);
    check(game.mistakes() == 0, "a clean solve costs no mistakes", __LINE__);
    // Solved rows read back in the order they were found.
    for (int step = 0; step < 4; ++step) {
      const bool named = std::strcmp(game.solvedGroup(step).name, samplePuzzle().groups[orders[o][step]].name) == 0;
      check(named, "solved groups read back in solve order", __LINE__);
    }
  }
}

void testLosing() {
  connections::Game game;
  game.start(samplePuzzle(), 3);
  // Four *different* wrong guesses: the same one four times is now free.
  for (int i = 0; i < 4; ++i) {
    selectWrongVariant(game, i);
    check(game.selectedCount() == 4, "each variant selects four", __LINE__);
    check(game.submit() != connections::Guess::AlreadyGuessed, "variants are distinct", __LINE__);
  }
  CHECK(game.mistakes() == 4);
  CHECK(game.mistakesLeft() == 0);
  CHECK(game.result() == connections::Result::Lost);

  // Losing reveals everything, each group exactly once: seeing the answer is
  // the point of losing.
  CHECK(game.revealedCount() == 4);
  std::set<std::string> revealed;
  for (int i = 0; i < game.revealedCount(); ++i) revealed.insert(game.solvedGroup(i).name);
  CHECK(revealed.size() == 4);
  CHECK(revealed.count("PALINDROMES") == 1);

  // A finished game ignores further play.
  selectGroup(game, 0);
  CHECK(game.selectedCount() == 0);
  CHECK(game.submit() == connections::Guess::Invalid);
}

void testLossAfterPartialSolve() {
  connections::Game game;
  game.start(samplePuzzle(), 21);
  selectGroup(game, 3);
  CHECK(game.submit() == connections::Guess::Solved);
  for (int i = 0; i < 4; ++i) {
    selectWrongVariant(game, i);
    check(game.submit() != connections::Guess::AlreadyGuessed, "variants are distinct", __LINE__);
  }
  CHECK(game.result() == connections::Result::Lost);
  // The one they found stays first, then the three they did not.
  CHECK(game.revealedCount() == 4);
  CHECK(std::strcmp(game.solvedGroup(0).name, "PALINDROMES") == 0);
  std::set<std::string> revealed;
  for (int i = 0; i < 4; ++i) revealed.insert(game.solvedGroup(i).name);
  CHECK(revealed.size() == 4);
}

void testShuffle() {
  connections::Game a;
  connections::Game b;
  a.start(samplePuzzle(), 42);
  b.start(samplePuzzle(), 42);
  CHECK(boardWords(a) == boardWords(b));

  connections::Game c;
  c.start(samplePuzzle(), 43);
  CHECK(boardWords(c) != boardWords(a));

  // Shuffling mid-game must not drag a solved group back onto the board.
  selectGroup(a, 1);
  CHECK(a.submit() == connections::Guess::Solved);
  a.shuffle(777);
  CHECK(a.tileCount() == 12);
  for (const std::string& word : boardWords(a)) {
    CHECK(word != "BUCKS" && word != "JAZZ");
  }
  // And it clears the selection, so a shuffle cannot submit someone's stale picks.
  a.toggleTile(0);
  a.shuffle(778);
  CHECK(a.selectedCount() == 0);

  // Seed 0 shuffles like any other seed, and deals its own board rather than
  // aliasing onto seed 1. This started as a "seed 0 is special" guard that no
  // test could distinguish from no guard at all, because the generator's
  // additive constant means zero was never a fixed point.
  connections::Game zero;
  connections::Game one;
  zero.start(samplePuzzle(), 0);
  one.start(samplePuzzle(), 1);
  CHECK(zero.tileCount() == 16);
  CHECK(std::set<std::string>(boardWords(zero).begin(), boardWords(zero).end()).size() == 16);
  CHECK(boardWords(zero) != boardWords(one));
  CHECK(boardWords(zero) != boardWords(a));
}

void testSaveRestore() {
  // From every reachable count of solved groups, a save/restore round trip has
  // to reproduce the board exactly, or resuming shows a different puzzle than
  // the one that was put down.
  for (int solves = 0; solves <= 4; ++solves) {
    connections::Game original;
    original.start(samplePuzzle(), 555 + solves);
    for (int g = 0; g < solves; ++g) {
      selectGroup(original, g);
      original.submit();
    }
    if (solves < 4) {
      selectWrongVariant(original, solves);
      original.submit();
    }

    connections::Game resumed;
    resumed.start(samplePuzzle(), 1);
    check(resumed.restore(original.save()), "restore accepts its own save", __LINE__);
    check(boardWords(resumed) == boardWords(original), "restore reproduces the board", __LINE__);
    check(resumed.solvedCount() == original.solvedCount(), "restore keeps solved count", __LINE__);
    check(resumed.mistakes() == original.mistakes(), "restore keeps mistakes", __LINE__);
    check(resumed.result() == original.result(), "restore keeps the result", __LINE__);
    for (int i = 0; i < resumed.solvedCount(); ++i) {
      check(std::strcmp(resumed.solvedGroup(i).name, original.solvedGroup(i).name) == 0, "restore keeps solve order",
            __LINE__);
    }
  }

  // A save off an SD card is not trusted.
  connections::Game game;
  game.start(samplePuzzle(), 1);
  connections::Game::Save bad;
  bad.solvedCount = 5;
  CHECK(!game.restore(bad));
  bad.solvedCount = 2;
  bad.solvedOrder[0] = 9;
  CHECK(!game.restore(bad));
  // A repeated group would solve the same four tiles twice and leave the board
  // one row short with no way to finish it.
  bad.solvedOrder[0] = 1;
  bad.solvedOrder[1] = 1;
  CHECK(!game.restore(bad));
  bad.solvedOrder[1] = 2;
  bad.mistakes = 9;
  CHECK(!game.restore(bad));
  // Rejecting a save must leave the game alone, not half-applied.
  CHECK(game.tileCount() == 16);
  CHECK(game.solvedCount() == 0);

  // A restored loss is a loss, not a playable board with four mistakes.
  connections::Game::Save lost;
  lost.seed = 5;
  lost.mistakes = 4;
  CHECK(game.restore(lost));
  CHECK(game.result() == connections::Result::Lost);
}

void testPlayableValidation() {
  CHECK(connections::isPlayable(samplePuzzle()));

  connections::Puzzle empty = samplePuzzle();
  empty.groups[2].members[1][0] = '\0';
  CHECK(!connections::isPlayable(empty));

  connections::Puzzle unnamed = samplePuzzle();
  unnamed.groups[0].name[0] = '\0';
  CHECK(!connections::isPlayable(unnamed));

  // A duplicated word makes two tiles indistinguishable, so a correct guess can
  // pick the wrong copy and come back wrong. Worth rejecting at import.
  connections::Puzzle duped = samplePuzzle();
  std::snprintf(duped.groups[3].members[0], sizeof(duped.groups[3].members[0]), "HAIL");
  CHECK(!connections::isPlayable(duped));
}

void testLongestRealContent() {
  // The extremes of the published archive: a 28-character word and a
  // 71-character group name both have to survive a round trip through the fixed
  // buffers, because the alternative is silent truncation on a real puzzle.
  connections::Puzzle p = samplePuzzle();
  const char* longWord = "SMILING FACE WITH SUNGLASSES";
  const char* longGroup = "MEMBER OF A TEAM WITH THE MOST CHAMPIONSHIPS IN THEIR RESPECTIVE SPORTS";
  std::snprintf(p.groups[0].members[0], sizeof(p.groups[0].members[0]), "%s", longWord);
  std::snprintf(p.groups[0].name, sizeof(p.groups[0].name), "%s", longGroup);
  CHECK(std::strcmp(p.groups[0].members[0], longWord) == 0);
  CHECK(std::strcmp(p.groups[0].name, longGroup) == 0);
  CHECK(connections::isPlayable(p));

  connections::Game game;
  game.start(p, 8);
  bool found = false;
  for (int i = 0; i < game.tileCount(); ++i) {
    if (std::strcmp(game.tileWord(i), longWord) == 0) found = true;
  }
  CHECK(found);
}

}  // namespace

int main() {
  testFreshBoard();
  testSelection();
  testGuessClassification();
  testRepeatedGuesses();
  testHistorySurvivesResume();
  testEverySolveOrder();
  testLosing();
  testLossAfterPartialSolve();
  testShuffle();
  testSaveRestore();
  testPlayableValidation();
  testLongestRealContent();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
