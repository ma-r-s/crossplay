#pragma once

// Train Tracks' save file, /.crosspoint/traintracks.sav. Freestanding: the
// activity reads and writes the bytes, and this turns them into a game.
//
// One text line of small integers, then the squares, then the undo history:
//
//   version menuLevel hasGame onBoard level seed attempt hash solved hints
//   checks elapsedMs first count cursor
//   <size*size squares, row by row, three characters each: the state digit
//    (0 empty, 1 track, 2 cross), the chosen piece in hex (0: automatic) and
//    the shape last drawn in hex (Board::shape)>
//   <64 moves, six hex digits each: cell (2), from, to, fromPin, toPin>
//
// The puzzle itself is not saved. It is regenerated from (level, seed,
// attempt), and the stored hash has to match what that produces, so a save
// written by a generator that has since changed is discarded rather than laid
// over a different puzzle.

#include <cstdio>
#include <cstdlib>

#include "TrainTracksGame.h"

namespace traintracks {

// Bumped when the format changes, and whenever the generator or the rater
// changes what a (seed, attempt) builds (see testPinnedPuzzles).
constexpr int kSaveVersion = 4;
constexpr int kSaveBytes = 1024;

struct SaveState {
  // The difficulty last picked on the new-puzzle screen, which opens on it.
  Level menuLevel = Level::Easy;
  // An unfinished or just-finished game is in `game`.
  bool hasGame = false;
  // The board was on screen when the app last closed: sleep, or the Home
  // gesture. Reopening goes straight back to it.
  bool onBoard = false;
  Game game{};
};

inline char hexDigit(const int v) { return "0123456789ABCDEF"[v & 0xF]; }

// A piece a square may hold: none (0), or one of the six two-way pieces, and
// only on track. Anything else in a file is corruption.
inline bool validPin(const int pin, const int state) {
  if (pin == 0) return true;
  if (state != kTrack) return false;
  for (const uint8_t piece : kPieces) {
    if (pin == piece) return true;
  }
  return false;
}

inline int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Returns the bytes written, or 0 if `size` is too small.
inline int writeSave(const SaveState& s, char* out, const int size) {
  const Game& g = s.game;
  const History& h = g.board.history;
  int used =
      std::snprintf(out, static_cast<size_t>(size), "%d %d %d %d %d %u %u %u %d %u %u %u %u %u %u\n", kSaveVersion,
                    static_cast<int>(s.menuLevel), s.hasGame ? 1 : 0, s.onBoard ? 1 : 0, static_cast<int>(g.level),
                    static_cast<unsigned>(g.seed), static_cast<unsigned>(g.attempt),
                    static_cast<unsigned>(puzzleHash(g.puzzle)), g.solved ? 1 : 0, static_cast<unsigned>(g.hints),
                    static_cast<unsigned>(g.checks), static_cast<unsigned>(g.elapsedMs), static_cast<unsigned>(h.first),
                    static_cast<unsigned>(h.count), static_cast<unsigned>(h.cursor));
  const int n = g.puzzle.size;
  if (used <= 0 || used + 3 * n * n + 1 + 6 * kHistoryDepth + 2 >= size) return 0;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      const uint8_t state = g.board.cell[i];
      out[used++] = static_cast<char>('0' + (state <= kCross ? state : 0));
      out[used++] = hexDigit(g.board.pin[i]);
      out[used++] = hexDigit(g.board.cell[i] == kTrack ? g.board.shape[i] : 0);
    }
  }
  out[used++] = '\n';
  for (int k = 0; k < kHistoryDepth; ++k) {
    const Move& m = h.moves[k];
    out[used++] = hexDigit(m.cell >> 4);
    out[used++] = hexDigit(m.cell);
    out[used++] = hexDigit(m.from);
    out[used++] = hexDigit(m.to);
    out[used++] = hexDigit(m.fromPin);
    out[used++] = hexDigit(m.toPin);
  }
  out[used++] = '\n';
  out[used] = '\0';
  return used;
}

// Parses a save into `s`, regenerating its puzzle with `work`. False on
// anything that does not hold together: a wrong version, a truncated file, a
// field out of range, or a puzzle that no longer matches. A bad header leaves
// `s` untouched; a bad game leaves `s` empty but for menuLevel, since a
// remembered difficulty is worth keeping even when the game is not.
inline bool readSave(const char* text, SaveState& s, GenWork& work) {
  constexpr int kHeader = 15;
  unsigned long v[kHeader] = {};
  const char* cursor = text;
  for (int i = 0; i < kHeader; ++i) {
    // Digits only: strtoul would take "-5" as a huge number and carry on.
    while (*cursor == ' ') ++cursor;
    if (*cursor < '0' || *cursor > '9') return false;
    char* next = nullptr;
    const unsigned long value = std::strtoul(cursor, &next, 10);
    if (next == cursor) return false;
    v[i] = value;
    cursor = next;
  }
  if (v[0] != static_cast<unsigned long>(kSaveVersion)) return false;
  if (v[1] >= static_cast<unsigned long>(kLevelCount)) return false;

  SaveState parsed;
  parsed.menuLevel = static_cast<Level>(v[1]);
  parsed.hasGame = v[2] != 0;
  parsed.onBoard = v[3] != 0;
  if (!parsed.hasGame) {
    parsed.onBoard = false;
    s = parsed;
    return true;
  }

  // From here a bad game still keeps the difficulty.
  s = SaveState{};
  s.menuLevel = parsed.menuLevel;

  // Every field in range for what it is read into: a 32-bit seed, hash or
  // clock that overflowed would otherwise come back as some other number.
  if (v[4] >= static_cast<unsigned long>(kLevelCount) || v[5] > 0xFFFFFFFFul || v[6] > 0xFFFFul ||
      v[7] > 0xFFFFFFFFul || v[9] > 0xFFFFul || v[10] > 0xFFFFul || v[11] > 0xFFFFFFFFul) {
    return false;
  }
  if (v[12] >= static_cast<unsigned long>(kHistoryDepth) || v[13] > static_cast<unsigned long>(kHistoryDepth) ||
      v[14] > v[13]) {
    return false;
  }
  Game& g = parsed.game;
  g.level = static_cast<Level>(v[4]);
  g.seed = static_cast<uint32_t>(v[5]);
  g.attempt = static_cast<uint16_t>(v[6]);
  g.solved = v[8] != 0;
  g.hints = static_cast<uint16_t>(v[9]);
  g.checks = static_cast<uint16_t>(v[10]);
  g.elapsedMs = static_cast<uint32_t>(v[11]);

  Generated made;
  if (!generateAttempt(g.level, g.seed, g.attempt, made, work)) return false;
  if (puzzleHash(made.puzzle) != static_cast<uint32_t>(v[7])) return false;
  g.puzzle = made.puzzle;
  resetBoard(g.board, g.puzzle);

  while (*cursor == ' ' || *cursor == '\r' || *cursor == '\n') ++cursor;
  const int n = g.puzzle.size;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const char ch = *cursor++;
      if (ch < '0' || ch > '2') return false;
      const int pin = hexValue(*cursor++);
      if (pin < 0 || !validPin(pin, ch - '0')) return false;
      const int shape = hexValue(*cursor++);
      if (shape < 0) return false;
      const int i = cellIndex(r, c);
      // A given is track whatever the file says, and only track has a piece.
      if (!g.puzzle.given[i]) {
        g.board.cell[i] = static_cast<uint8_t>(ch - '0');
        g.board.pin[i] = g.board.cell[i] == kTrack ? static_cast<uint8_t>(pin) : 0;
      }
      g.board.shape[i] = g.board.cell[i] == kTrack ? static_cast<uint8_t>(shape) : 0;
    }
  }

  while (*cursor == ' ' || *cursor == '\r' || *cursor == '\n') ++cursor;
  History& h = g.board.history;
  for (int k = 0; k < kHistoryDepth; ++k) {
    int digits[6];
    for (int d = 0; d < 6; ++d) {
      digits[d] = hexValue(*cursor++);
      if (digits[d] < 0) return false;
    }
    const int cell = digits[0] * 16 + digits[1];
    if (cell >= kMaxCells || digits[2] > kCross || digits[3] > kCross) return false;
    if (!validPin(digits[4], digits[2]) || !validPin(digits[5], digits[3])) return false;
    h.moves[k] = Move{static_cast<uint8_t>(cell), static_cast<uint8_t>(digits[2]), static_cast<uint8_t>(digits[3]),
                      static_cast<uint8_t>(digits[4]), static_cast<uint8_t>(digits[5])};
  }
  h.first = static_cast<uint8_t>(v[12]);
  h.count = static_cast<uint8_t>(v[13]);
  h.cursor = static_cast<uint8_t>(v[14]);
  // A history move that names a given or a square off this grid would let
  // undo write over a locked square, so the whole history goes instead.
  for (int k = 0; k < h.count; ++k) {
    const Move& m = h.moves[(h.first + k) % kHistoryDepth];
    if (!inside(g.puzzle, rowOf(m.cell), colOf(m.cell)) || g.puzzle.given[m.cell]) {
      h = History{};
      break;
    }
  }

  // A game marked solved has to be solved: otherwise a corrupt file would put
  // SOLVED over a board that is not.
  if (g.solved && !isSolved(g.puzzle, g.board, work.solver)) return false;

  s = parsed;
  return true;
}

}  // namespace traintracks
