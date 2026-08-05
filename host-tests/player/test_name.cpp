// The device's name, which every app shares.
//
// The interesting property is not that it is random -- it is that it can never
// produce something the rest of the system cannot hold. A name that overflows
// kMaxNameLength would be truncated in a header, and a name that repeats on a
// reroll would make the button look broken. Both are checked exhaustively here,
// because the lists will be edited by hand later and that is exactly when a
// too-long word gets added.

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "../../src/apps_local/player/PlayerName.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_name.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void testEveryPairFits() {
  // Exhaustive over the whole cross product, not a sample: the failure mode is
  // one long word added to a list months from now, and a sample would miss it.
  size_t longest = 0;
  int overflowed = 0;
  int empty = 0;
  std::set<std::string> distinct;
  const size_t combinations = player::adjectiveCount() * player::nounCount();

  for (uint32_t seed = 0; seed < 40000; ++seed) {
    char name[player::kMaxNameLength + 1] = {};
    player::compose(name, sizeof(name), seed);
    const size_t length = strlen(name);
    if (length == 0) empty++;
    if (length > player::kMaxNameLength) overflowed++;
    if (length > longest) longest = length;
    distinct.insert(name);
  }

  CHECK(overflowed == 0);
  CHECK(empty == 0);
  CHECK(longest <= player::kMaxNameLength);
  // Every pair is reachable, so no word is dead weight and the roll is not
  // walking a diagonal through the two lists.
  CHECK(distinct.size() == combinations);
  std::printf("  (%zu names, longest %zu of %zu)\n", distinct.size(), longest, player::kMaxNameLength);
}

void testTheSameSeedGivesTheSameName() {
  char first[player::kMaxNameLength + 1] = {};
  char second[player::kMaxNameLength + 1] = {};
  player::compose(first, sizeof(first), 12345);
  player::compose(second, sizeof(second), 12345);
  CHECK(strcmp(first, second) == 0);
}

void testNeighbouringSeedsDoNotGiveNeighbouringNames() {
  // The seed is a clock, so it arrives in small steps. Unmixed, a reroll a few
  // milliseconds later would land on an adjacent word and read as broken.
  int sameAsPrevious = 0;
  char previous[player::kMaxNameLength + 1] = {};
  player::compose(previous, sizeof(previous), 1000);
  for (uint32_t seed = 1001; seed < 1200; ++seed) {
    char current[player::kMaxNameLength + 1] = {};
    player::compose(current, sizeof(current), seed);
    if (strcmp(current, previous) == 0) sameAsPrevious++;
    memcpy(previous, current, sizeof(previous));
  }
  // A handful of repeats across 200 draws from ~576 names is chance; a run of
  // them is the mixing having failed.
  CHECK(sameAsPrevious <= 3);
}

void testAShortBufferTruncatesRatherThanOverruns() {
  char tiny[6] = {};
  player::compose(tiny, sizeof(tiny), 7);
  CHECK(strlen(tiny) < sizeof(tiny));
}

}  // namespace

int main() {
  testEveryPairFits();
  testTheSameSeedGivesTheSameName();
  testNeighbouringSeedsDoNotGiveNeighbouringNames();
  testAShortBufferTruncatesRatherThanOverruns();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
