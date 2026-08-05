// The device's name, which every app shares, and which is also the drawing
// instruction for its face.
//
// The interesting property is not that it is random -- it is that it can never
// produce something the rest of the system cannot hold. A name that overflows
// kMaxNameLength would be truncated in a packet header, and because the face is
// derived from the name, a truncated name is somebody else's face on the other
// device. A reroll that lands back where it started would make the button look
// broken. All of it is checked exhaustively here, because the lists will be
// edited by hand later and that is exactly when a too-long word gets added.

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

size_t combinations() {
  size_t total = 1;
  for (int slot = 0; slot < player::kSlotCount; ++slot) total *= player::wordCount(slot);
  return total;
}

void testEveryTripleFits() {
  // Exhaustive over the whole cross product, not a sample: the failure mode is
  // one long word added to a list months from now, and a sample would miss it.
  size_t longest = 0;
  int overflowed = 0;
  int empty = 0;
  std::set<std::string> distinct;

  for (uint8_t hair = 0; hair < player::wordCount(player::SlotHair); ++hair) {
    for (uint8_t eyes = 0; eyes < player::wordCount(player::SlotEyes); ++eyes) {
      for (uint8_t mouth = 0; mouth < player::wordCount(player::SlotMouth); ++mouth) {
        player::Name name;
        name.word[player::SlotHair] = hair;
        name.word[player::SlotEyes] = eyes;
        name.word[player::SlotMouth] = mouth;
        char text[player::kMaxNameLength + 1] = {};
        player::compose(text, sizeof(text), name);
        const size_t length = strlen(text);
        if (length == 0) empty++;
        if (length > player::kMaxNameLength) overflowed++;
        if (length > longest) longest = length;
        distinct.insert(text);
      }
    }
  }

  CHECK(overflowed == 0);
  CHECK(empty == 0);
  CHECK(longest <= player::kMaxNameLength);
  // No two triples spell the same name, or two people would look different and
  // read identical.
  CHECK(distinct.size() == combinations());
  std::printf("  (%zu names, longest %zu of %zu)\n", distinct.size(), longest, player::kMaxNameLength);
}

void testEveryNameParsesBackToItself() {
  // The round trip is load-bearing twice over: it is how a saved name is read
  // at boot, and it is how the other device turns the name off the radio into a
  // face. A name that composes but does not parse is a stranger's portrait.
  int mismatched = 0;
  for (uint8_t hair = 0; hair < player::wordCount(player::SlotHair); ++hair) {
    for (uint8_t eyes = 0; eyes < player::wordCount(player::SlotEyes); ++eyes) {
      for (uint8_t mouth = 0; mouth < player::wordCount(player::SlotMouth); ++mouth) {
        player::Name name;
        name.word[player::SlotHair] = hair;
        name.word[player::SlotEyes] = eyes;
        name.word[player::SlotMouth] = mouth;
        char text[player::kMaxNameLength + 1] = {};
        player::compose(text, sizeof(text), name);
        const player::Name back = player::parse(text);
        if (!back.known() || memcmp(back.word, name.word, sizeof(name.word)) != 0) mismatched++;
      }
    }
  }
  CHECK(mismatched == 0);
}

void testAnUnreadableNameIsUnknownRatherThanWrong() {
  // Everything a different build could send. None of it may resolve to a real
  // word, because the face would then be confidently wrong rather than plain.
  const char* strangers[] = {
      "",
      " ",
      "SLY WOLF",              // the two-word name from before the face existed
      "BALD",                  // truncated after one word
      "BALD GRIM",             // truncated after two
      "MOHAWK PEERING BEARD",  // words from a build whose lists are not ours
      "bald grim grin",        // the lists are capitals; case is not folded
      "BALDGRIMGRIN",
      "GRIN GRIM BALD",  // right words, wrong slots
  };
  for (const char* stranger : strangers) {
    CHECK(!player::parse(stranger).known());
  }
  // ...and a slot that IS readable inside an unreadable name still resolves, so
  // a half-known name draws the half it understands.
  const player::Name partial = player::parse("BALD GRIM");
  CHECK(partial.word[player::SlotHair] == 0);
  CHECK(partial.word[player::SlotEyes] == 0);
  CHECK(partial.word[player::SlotMouth] == player::kUnknownWord);
}

void testTheSameSeedGivesTheSameName() {
  const player::Name first = player::roll(12345);
  const player::Name second = player::roll(12345);
  CHECK(memcmp(first.word, second.word, sizeof(first.word)) == 0);
  CHECK(first.known());
}

void testRollingReachesEveryName() {
  // Every combination is reachable, so no word is dead weight and the roll is
  // not walking a diagonal through the three lists.
  std::set<std::string> seen;
  for (uint32_t seed = 0; seed < 200000; ++seed) {
    const player::Name name = player::roll(seed);
    char text[player::kMaxNameLength + 1] = {};
    player::compose(text, sizeof(text), name);
    seen.insert(text);
  }
  CHECK(seen.size() == combinations());
}

void testSteppingASlotMovesOnlyThatSlot() {
  // Tapping the eyes must not change the hair. This is the whole reason there
  // are three buttons rather than one.
  int stuck = 0;
  int otherSlotsMoved = 0;
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    for (uint8_t start = 0; start < player::wordCount(slot); ++start) {
      player::Name name = player::roll(7);
      name.word[slot] = start;
      const player::Name next = player::nextWord(name, slot);
      if (next.word[slot] == start) stuck++;
      for (int other = 0; other < player::kSlotCount; ++other) {
        if (other != slot && next.word[other] != name.word[other]) otherSlotsMoved++;
      }
    }
  }
  CHECK(stuck == 0);
  CHECK(otherSlotsMoved == 0);
}

void testSteppingWalksTheWholeListAndComesBack() {
  // What the control promises at fourteen options: every word is reachable by
  // tapping, in the order the list is written, and you are never more than a
  // lap away from the one you just left. Random stepping could satisfy "always
  // moves" and still hide half the list behind a hundred taps.
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    const size_t count = player::wordCount(slot);
    for (uint8_t start = 0; start < count; ++start) {
      player::Name name = player::roll(3);
      name.word[slot] = start;

      std::set<int> visited;
      player::Name walking = name;
      for (size_t tap = 0; tap < count; ++tap) {
        walking = player::nextWord(walking, slot);
        visited.insert(walking.word[slot]);
        // In list order, not merely eventually.
        CHECK(walking.word[slot] == (start + tap + 1) % count);
      }
      // A full lap visits everything and lands exactly where it began.
      CHECK(visited.size() == count);
      CHECK(walking.word[slot] == start);
    }
  }
}

void testSteppingAnUnknownSlotLandsOnAKnownWord() {
  // Reachable from a name off the wire, and the one case where "the next one"
  // has no meaning, because there is no current one.
  player::Name stranger = player::parse("SLY WOLF");
  CHECK(!stranger.known());
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    const player::Name stepped = player::nextWord(stranger, slot);
    CHECK(stepped.word[slot] == 0);
  }
}

void testSteppingIsAFactRatherThanADistribution() {
  // No seed, no clock: the same word always steps to the same next word. This
  // is what makes the control learnable, and it is why the signature lost its
  // seed argument.
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    player::Name name = player::roll(11);
    const player::Name once = player::nextWord(name, slot);
    const player::Name twice = player::nextWord(name, slot);
    CHECK(memcmp(once.word, twice.word, sizeof(once.word)) == 0);
  }
}

void testNeighbouringSeedsDoNotGiveNeighbouringNames() {
  // The seed is a clock, so it arrives in small steps. Unmixed, a reroll a few
  // milliseconds later would land on an adjacent word and read as broken.
  int sameAsPrevious = 0;
  player::Name previous = player::roll(1000);
  for (uint32_t seed = 1001; seed < 1200; ++seed) {
    const player::Name current = player::roll(seed);
    if (memcmp(current.word, previous.word, sizeof(current.word)) == 0) sameAsPrevious++;
    previous = current;
  }
  // A handful of repeats across 200 draws from 512 names is chance; a run of
  // them is the mixing having failed.
  CHECK(sameAsPrevious <= 3);
}

void testAShortBufferTruncatesRatherThanOverruns() {
  for (size_t capacity = 1; capacity <= player::kMaxNameLength + 1; ++capacity) {
    char buffer[player::kMaxNameLength + 8];
    memset(buffer, '\xAA', sizeof(buffer));
    player::compose(buffer, capacity, player::roll(capacity));
    CHECK(strnlen(buffer, capacity) < capacity);
    // Every byte past what the caller allowed, not just the first one. compose
    // walks a cursor and hands snprintf the space it thinks is left, so the way
    // it goes wrong is the cursor running past the end and writing further
    // along -- which leaves the byte at `capacity` untouched and looks clean.
    // A mutation that did exactly that survived the one-byte version.
    int trampled = 0;
    for (size_t i = capacity; i < sizeof(buffer); ++i) {
      if (buffer[i] != '\xAA') trampled++;
    }
    CHECK(trampled == 0);
  }
}

void testTheShortNameIsAlwaysOneWordThatFits() {
  // Every name the lists can roll, plus everything a foreign build could send.
  // The point of this is a size guarantee: two games build sentences around it
  // ("BRAIDS'S MOVE", "BRAIDS SANK YOUR SUB") and both used to overflow with
  // the full three-word name, dropping the end of the sentence rather than the
  // name.
  int tooLong = 0;
  int hasSpace = 0;
  int empty = 0;
  for (uint8_t hair = 0; hair < player::wordCount(player::SlotHair); ++hair) {
    for (uint8_t eyes = 0; eyes < player::wordCount(player::SlotEyes); ++eyes) {
      for (uint8_t mouth = 0; mouth < player::wordCount(player::SlotMouth); ++mouth) {
        player::Name name;
        name.word[player::SlotHair] = hair;
        name.word[player::SlotEyes] = eyes;
        name.word[player::SlotMouth] = mouth;
        char full[player::kMaxNameLength + 1] = {};
        player::compose(full, sizeof(full), name);
        char shortened[player::kMaxShortNameLength + 1] = {};
        player::shortName(full, shortened, sizeof(shortened));
        if (strlen(shortened) > player::kMaxShortNameLength) tooLong++;
        if (strchr(shortened, ' ') != nullptr) hasSpace++;
        if (shortened[0] == '\0') empty++;
        // It is the hair word, whole and untruncated.
        if (strcmp(shortened, player::word(player::SlotHair, hair)) != 0) tooLong++;
      }
    }
  }
  CHECK(tooLong == 0);
  CHECK(hasSpace == 0);
  CHECK(empty == 0);

  // Names this build cannot parse still shorten, because it reads the text and
  // not the slots. This is the case that matters on the wire.
  char out[player::kMaxShortNameLength + 1] = {};
  player::shortName("MOHAWK PEERING BEARD", out, sizeof(out));
  CHECK(strcmp(out, "MOHAWK") == 0);
  player::shortName("  PUNK  SLY", out, sizeof(out));
  CHECK(strcmp(out, "PUNK") == 0);
  player::shortName("ONEWORD", out, sizeof(out));
  CHECK(strcmp(out, "ONEWOR") == 0);  // truncated, never overrun
  player::shortName("", out, sizeof(out));
  CHECK(out[0] == '\0');
  player::shortName(nullptr, out, sizeof(out));
  CHECK(out[0] == '\0');
  player::shortName("   ", out, sizeof(out));
  CHECK(out[0] == '\0');

  // Never writes past the caller's buffer, at any capacity.
  for (size_t capacity = 1; capacity <= player::kMaxShortNameLength + 1; ++capacity) {
    char buffer[player::kMaxShortNameLength + 8];
    memset(buffer, '\xAA', sizeof(buffer));
    player::shortName("BRAIDS SQUINT MUTTON", buffer, capacity);
    CHECK(strnlen(buffer, capacity) < capacity);
    int trampled = 0;
    for (size_t i = capacity; i < sizeof(buffer); ++i) {
      if (buffer[i] != '\xAA') trampled++;
    }
    CHECK(trampled == 0);
  }
}

void testTheWordListsHaveNoDuplicates() {
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    std::set<std::string> seen;
    for (uint8_t i = 0; i < player::wordCount(slot); ++i) seen.insert(player::word(slot, i));
    CHECK(seen.size() == player::wordCount(slot));
  }
}

void testOutOfRangeLookupsAreNullRatherThanGarbage() {
  CHECK(player::word(-1, 0) == nullptr);
  CHECK(player::word(player::kSlotCount, 0) == nullptr);
  CHECK(player::word(player::SlotHair, player::kUnknownWord) == nullptr);
  CHECK(player::word(player::SlotHair, static_cast<uint8_t>(player::wordCount(player::SlotHair))) == nullptr);
  CHECK(player::wordCount(-1) == 0);
  CHECK(player::wordCount(player::kSlotCount) == 0);
}

}  // namespace

int main() {
  testEveryTripleFits();
  testEveryNameParsesBackToItself();
  testAnUnreadableNameIsUnknownRatherThanWrong();
  testTheSameSeedGivesTheSameName();
  testRollingReachesEveryName();
  testSteppingASlotMovesOnlyThatSlot();
  testSteppingWalksTheWholeListAndComesBack();
  testSteppingAnUnknownSlotLandsOnAKnownWord();
  testSteppingIsAFactRatherThanADistribution();
  testNeighbouringSeedsDoNotGiveNeighbouringNames();
  testAShortBufferTruncatesRatherThanOverruns();
  testTheShortNameIsAlwaysOneWordThatFits();
  testTheWordListsHaveNoDuplicates();
  testOutOfRangeLookupsAreNullRatherThanGarbage();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
