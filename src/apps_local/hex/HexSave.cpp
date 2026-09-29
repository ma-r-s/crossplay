#include "HexSave.h"

#include <cstdio>
#include <cstdlib>

namespace hexsave {
namespace {

int appendInt(char* out, const int capacity, const int used, const int value) {
  if (used < 0) return -1;
  const int written = std::snprintf(out + used, static_cast<size_t>(capacity - used), " %d", value);
  if (written <= 0 || used + written >= capacity) return -1;
  return used + written;
}

}  // namespace

Record recordAfterLink(const Record& counted, const Record& onCard) {
  // Whichever has seen more games. The memory holds the tally the match started
  // from plus the match itself, so it is never behind the card -- and stating
  // it as "more games" rather than "the memory, always" makes the function
  // total: a teardown with nothing counted, which is a match the opponent left
  // before a stone went down, keeps what the card has instead of writing a
  // blank record over it.
  return counted.wins + counted.losses >= onCard.wins + onCard.losses ? counted : onCard;
}

int pack(const Save& save, char* out, const int capacity) {
  if (out == nullptr || capacity <= 0) return 0;
  const hex::Game& game = save.game;

  int used = std::snprintf(out, static_cast<size_t>(capacity), "%d %d %d %d %d %d", kVersion, save.wins, save.losses,
                           static_cast<int>(save.opponent), static_cast<int>(save.level), save.playAs);
  if (used <= 0 || used >= capacity) return 0;

  used = appendInt(out, capacity, used, save.hasHistory ? 1 : 0);
  used = appendInt(out, capacity, used, save.lastWon ? 1 : 0);
  for (int i = 0; i < hex::kCellBytes; ++i) used = appendInt(out, capacity, used, save.lastCells[i]);

  used = appendInt(out, capacity, used, save.inProgress ? 1 : 0);
  used = appendInt(out, capacity, used, save.seat);
  for (int i = 0; i < hex::kCellBytes; ++i) used = appendInt(out, capacity, used, game.cell[i]);
  // The union-find travels with the position rather than being rebuilt from it.
  // Rebuilding would be a second implementation of the one fact the whole file
  // is about, and the two would only have to disagree once.
  for (int i = 0; i < hex::kNodes; ++i) used = appendInt(out, capacity, used, game.parent[i]);
  used = appendInt(out, capacity, used, game.toMove);
  used = appendInt(out, capacity, used, game.winner);
  used = appendInt(out, capacity, used, game.lastMove);
  used = appendInt(out, capacity, used, game.moveNumber);
  if (used < 0) return 0;

  const int written = std::snprintf(out + used, static_cast<size_t>(capacity - used), "\n");
  if (written <= 0 || used + written >= capacity) return 0;
  return used + written;
}

bool unpack(const char* text, Save& save) {
  if (text == nullptr) return false;

  // Parsed into a local and committed only at the end, so a line that runs out
  // halfway leaves the caller's record alone rather than half-replacing it.
  Save parsed;
  const char* cursor = text;
  bool ok = true;
  const auto take = [&cursor, &ok]() -> long {
    if (!ok) return 0;
    char* next = nullptr;
    const long value = std::strtol(cursor, &next, 10);
    if (next == cursor) {
      ok = false;
      return 0;
    }
    cursor = next;
    return value;
  };

  const long version = take();
  if (!ok || version != kVersion) return false;

  // Checkpoint one: the record and the settings. A file that stops before the
  // end of these is a file this build cannot use at all.
  parsed.wins = static_cast<int>(take());
  parsed.losses = static_cast<int>(take());
  const long opponent = take();
  const long level = take();
  const long playAs = take();
  if (!ok) return false;
  parsed.opponent =
      opponent == static_cast<long>(hex::Opponent::Human) ? hex::Opponent::Human : hex::Opponent::Computer;
  parsed.level =
      level >= 0 && level < static_cast<long>(hex::Level::Count_) ? static_cast<hex::Level>(level) : hex::Level::Normal;
  parsed.playAs = playAs == hex::kWhite ? hex::kWhite : hex::kBlack;

  // Checkpoint two: the ornament. Missing means a device that has finished no
  // game this build can draw, which is exactly what a fresh one looks like.
  const bool hasHistory = take() != 0;
  const bool lastWon = take() != 0;
  uint8_t lastCells[hex::kCellBytes];
  for (int i = 0; i < hex::kCellBytes; ++i) lastCells[i] = static_cast<uint8_t>(take());
  if (ok) {
    parsed.hasHistory = hasHistory;
    parsed.lastWon = lastWon;
    for (int i = 0; i < hex::kCellBytes; ++i) parsed.lastCells[i] = lastCells[i];
  } else {
    ok = true;
    save = parsed;
    return true;
  }

  // Checkpoint three: the game. A resumed screen is only meaningful with the
  // state behind it, so anything this build cannot play costs the resume and
  // leaves the record and the settings standing.
  hex::Game game{};
  hex::reset(game);
  const bool inProgress = take() != 0;
  const long seat = take();
  for (int i = 0; i < hex::kCellBytes; ++i) game.cell[i] = static_cast<uint8_t>(take());
  for (int i = 0; i < hex::kNodes; ++i) game.parent[i] = static_cast<uint8_t>(take());
  game.toMove = static_cast<uint8_t>(take());
  game.winner = static_cast<uint8_t>(take());
  game.lastMove = static_cast<uint8_t>(take());
  game.moveNumber = static_cast<uint16_t>(take());
  if (!ok) {
    save = parsed;
    return true;
  }

  bool usable = inProgress;
  if (usable) {
    if (game.toMove != hex::kBlack && game.toMove != hex::kWhite) usable = false;
    if (game.winner != hex::kEmpty && !hex::isStone(game.winner)) usable = false;
    if (game.lastMove != hex::kNoCell && !hex::validCell(game.lastMove)) usable = false;
    for (int i = 0; i < hex::kNodes && usable; ++i) {
      if (game.parent[i] >= hex::kNodes) usable = false;
    }
  }
  if (usable) {
    parsed.inProgress = true;
    parsed.game = game;
    parsed.seat = seat == hex::kWhite ? hex::kWhite : hex::kBlack;
  }

  save = parsed;
  return true;
}

}  // namespace hexsave
