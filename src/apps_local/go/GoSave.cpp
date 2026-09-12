#include "GoSave.h"

#include <cstdio>
#include <cstdlib>

namespace gosave {
namespace {

constexpr int kMaskBytes = (go::kPoints + 7) / 8;

int appendInt(char* out, const int capacity, int used, const int value) {
  if (used < 0) return -1;
  const int written = std::snprintf(out + used, static_cast<size_t>(capacity - used), " %d", value);
  if (written <= 0 || used + written >= capacity) return -1;
  return used + written;
}

}  // namespace

int pack(const Save& save, char* out, const int capacity) {
  if (out == nullptr || capacity <= 0) return 0;
  const go::Game& game = save.game;

  int used = std::snprintf(out, static_cast<size_t>(capacity), "%d %d %d %d %d %d %d %d %d %d %d %d", kVersion,
                           save.wins, save.losses, save.hasHistory ? 1 : 0, save.lastWon ? 1 : 0, save.lastMarginHalves,
                           static_cast<int>(save.opponent), static_cast<int>(save.level), save.playAs,
                           save.inProgress ? 1 : 0, save.seat, static_cast<int>(go::kPoints));
  if (used <= 0 || used >= capacity) return 0;

  for (int i = 0; i < go::kPoints; ++i) used = appendInt(out, capacity, used, save.lastPoints[i]);
  for (int i = 0; i < go::kPoints; ++i) used = appendInt(out, capacity, used, game.point[i]);
  for (int i = 0; i < kMaskBytes; ++i) used = appendInt(out, capacity, used, game.dead[i]);
  used = appendInt(out, capacity, used, game.toMove);
  used = appendInt(out, capacity, used, game.ko);
  used = appendInt(out, capacity, used, game.passes);
  used = appendInt(out, capacity, used, game.lastMove);
  used = appendInt(out, capacity, used, game.stage);
  // Komi and handicap are the LEVEL, written into the game when it started.
  // Leaving them out cost every resumed game its komi: a 44/37 split, which is
  // an ordinary result, scored W+0.5 as played and B+7.0 after a resume, and
  // komi 0 made the draw this ruleset exists to avoid reachable again.
  used = appendInt(out, capacity, used, game.komiHalves);
  used = appendInt(out, capacity, used, game.handicap);
  used = appendInt(out, capacity, used, game.accepted);
  used = appendInt(out, capacity, used, game.moveNumber);
  used = appendInt(out, capacity, used, game.capturedBy[go::kBlack]);
  used = appendInt(out, capacity, used, game.capturedBy[go::kWhite]);
  // The superko ring travels with the game. Dropping it would make a resumed
  // game accept a repetition the same game refused a minute earlier, which is
  // a rules bug that only ever appears after a sleep.
  used = appendInt(out, capacity, used, game.recentCount);
  for (int i = 0; i < go::kHistory; ++i) {
    // Written as two halves: these are uint32 and the parser reads signed longs.
    used = appendInt(out, capacity, used, static_cast<int>(game.recent[i] >> 16));
    used = appendInt(out, capacity, used, static_cast<int>(game.recent[i] & 0xFFFFu));
  }
  if (used < 0) return 0;

  const int written = std::snprintf(out + used, static_cast<size_t>(capacity - used), "\n");
  if (written <= 0 || used + written >= capacity) return 0;
  return used + written;
}

bool unpack(const char* text, Save& save) {
  if (text == nullptr) return false;

  // Parsed into a local and committed only when the whole line is good, so a
  // truncated file leaves the record alone rather than half-replacing it.
  Save parsed;
  const char* cursor = text;
  const auto take = [&cursor](bool& ok) -> long {
    char* next = nullptr;
    const long value = std::strtol(cursor, &next, 10);
    if (next == cursor) ok = false;
    cursor = next;
    return value;
  };

  bool ok = true;
  const long version = take(ok);
  // An OLDER version is refused outright rather than read as far as it goes.
  // Every field after the header is positional, so a v1 line parsed as v2 does
  // not fail at the missing number, it reads the NEXT one in its place and
  // shifts a whole board along by one. That is a game that loads and is wrong,
  // which is worse than a game that does not load.
  if (!ok || version != kVersion) return false;
  parsed.wins = static_cast<int>(take(ok));
  parsed.losses = static_cast<int>(take(ok));
  parsed.hasHistory = take(ok) != 0;
  parsed.lastWon = take(ok) != 0;
  parsed.lastMarginHalves = static_cast<int>(take(ok));
  parsed.opponent = static_cast<go::Opponent>(take(ok));
  parsed.level = static_cast<go::Level>(take(ok));
  parsed.playAs = static_cast<uint8_t>(take(ok));
  parsed.inProgress = take(ok) != 0;
  parsed.seat = static_cast<uint8_t>(take(ok));
  const long points = take(ok);
  if (!ok) return false;
  // The board size is written down so a file from a different board cannot be
  // read as a short one. There will never be a nineteen by nineteen here, and
  // that is exactly why this costs one integer rather than an argument.
  if (points != go::kPoints) return false;

  for (int i = 0; i < go::kPoints; ++i) parsed.lastPoints[i] = static_cast<uint8_t>(take(ok));
  for (int i = 0; i < go::kPoints; ++i) parsed.game.point[i] = static_cast<uint8_t>(take(ok));
  for (int i = 0; i < kMaskBytes; ++i) parsed.game.dead[i] = static_cast<uint8_t>(take(ok));
  parsed.game.toMove = static_cast<uint8_t>(take(ok));
  parsed.game.ko = static_cast<uint8_t>(take(ok));
  parsed.game.passes = static_cast<uint8_t>(take(ok));
  parsed.game.lastMove = static_cast<uint8_t>(take(ok));
  parsed.game.stage = static_cast<uint8_t>(take(ok));
  parsed.game.komiHalves = static_cast<int16_t>(take(ok));
  parsed.game.handicap = static_cast<uint8_t>(take(ok));
  parsed.game.accepted = static_cast<uint8_t>(take(ok));
  parsed.game.moveNumber = static_cast<uint16_t>(take(ok));
  parsed.game.capturedBy[0] = 0;
  parsed.game.capturedBy[go::kBlack] = static_cast<uint16_t>(take(ok));
  parsed.game.capturedBy[go::kWhite] = static_cast<uint16_t>(take(ok));
  parsed.game.recentCount = static_cast<uint8_t>(take(ok));
  for (int i = 0; i < go::kHistory; ++i) {
    const uint32_t high = static_cast<uint32_t>(take(ok));
    const uint32_t low = static_cast<uint32_t>(take(ok));
    parsed.game.recent[i] = (high << 16) | (low & 0xFFFFu);
  }
  if (!ok) return false;

  // A resumed screen is only meaningful with the state behind it. A game whose
  // side to move is not a colour, or whose stage is not one of the three, is a
  // file this build cannot play, so the record survives and the game does not.
  if (parsed.game.toMove != go::kBlack && parsed.game.toMove != go::kWhite) return false;
  if (parsed.game.stage > static_cast<uint8_t>(go::Stage::Over)) return false;
  if (parsed.game.recentCount > go::kHistory) return false;
  // A komi that cannot settle a game is a komi this app never set, so the file
  // is either damaged or from a build that did not write one.
  if (!go::settlesEveryGame(parsed.game.komiHalves)) return false;
  if (parsed.game.handicap > go::kMaxHandicap) return false;
  if (parsed.level > go::Level::Hard) parsed.level = go::Level::Medium;
  if (parsed.opponent > go::Opponent::Human) parsed.opponent = go::Opponent::Computer;
  if (parsed.playAs != go::kBlack && parsed.playAs != go::kWhite) parsed.playAs = go::kBlack;
  if (parsed.seat != go::kBlack && parsed.seat != go::kWhite) parsed.seat = go::kBlack;

  save = parsed;
  return true;
}

}  // namespace gosave
