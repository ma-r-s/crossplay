#pragma once

// Turns the published archive's JSON into puzzles, one chunk at a time.
//
// The source is a 1.3MB array of objects, and it arrives from HttpDownloader in
// whatever sized pieces the socket produces, so this is a push parser: feed it
// bytes, it calls back with whole puzzles. Nothing is buffered beyond the
// puzzle being assembled, which is why a device with 400KB of RAM can import a
// file three times that size.
//
// The parser is deliberately narrow. It is not a general JSON reader: it knows
// this one schema, tracks the handful of keys it needs, and ignores everything
// else. That is a smaller surface to get wrong than a general parser, and the
// schema has been stable across all 1143 published puzzles.
//
//   [{"id":1,"date":"2023-06-12","answers":[
//       {"level":0,"group":"WET WEATHER","members":["HAIL","RAIN","SLEET","SNOW"]}, ...4 ]}, ...]
//
// Text arrives as pure ASCII with everything else as \uXXXX escapes (checked
// across the whole archive: the only escapes present are \" and \u). See
// ConnectionsText.h for what happens to the escapes.

#include <cstddef>
#include <cstdint>

#include "ConnectionsCore.h"

namespace connections {

struct ImportStats {
  int accepted = 0;
  // Puzzles skipped because a *word* held a character the display cannot
  // render, which across the published archive is two dates' worth of emoji
  // puzzles. Tracked rather than silently dropped so the count can be shown.
  int skippedUnrenderable = 0;
  // Skipped because the shape was wrong: not four groups of four, a repeated
  // word, an empty name. Zero across the whole published archive so far, so a
  // non-zero value here means the source changed.
  int skippedMalformed = 0;
  bool sawSyntaxError = false;
};

class Importer {
 public:
  // Called for each accepted puzzle, in source order. Return false to stop the
  // import (a full card, a cancelled download).
  using PuzzleFn = bool (*)(void* ctx, const Puzzle& puzzle);

  void begin(PuzzleFn onPuzzle, void* ctx);
  // Feed the next chunk. Returns false once the import should stop, either
  // because the callback asked to or because the JSON is malformed.
  bool feed(const uint8_t* data, size_t len);
  // Call after the last chunk. Returns false if the document ended mid-value.
  bool finish();

  const ImportStats& stats() const { return stats_; }

 private:
  // The scanner is a byte-at-a-time state machine so a chunk boundary can fall
  // anywhere, including inside a \uXXXX escape.
  enum class Scan : uint8_t { Value, String, Escape, Unicode, Number, Literal };
  // Where in the fixed schema we are. Depth alone is ambiguous (a member string
  // and a group name sit at different depths but the same nesting shape), so
  // the parser tracks the key it last saw at each level.
  enum class Field : uint8_t { None, Id, Date, Level, GroupName, Members };

  void beginString();
  void appendChar(uint32_t codepoint);
  void endString();
  void endNumber();
  void openContainer(char c);
  void closeContainer(char c);
  void emitIfComplete();
  void resetPuzzle();

  Scan scan_ = Scan::Value;
  int depth_ = 0;
  bool inKey_ = false;
  Field field_ = Field::None;
  // Set while a puzzle's shape is wrong; the puzzle is still parsed to its end
  // so the scanner stays in sync, then dropped.
  bool puzzleUnrenderable_ = false;

  char text_[kMaxGroupLen * 2 + 8] = {};
  int textLen_ = 0;
  bool textOverflow_ = false;
  uint32_t pendingCodepoint_ = 0;
  uint32_t highSurrogate_ = 0;
  int unicodeDigits_ = 0;
  char number_[16] = {};
  int numberLen_ = 0;

  Puzzle puzzle_;
  int groupIndex_ = -1;
  int memberIndex_ = 0;
  int groupsSeen_ = 0;

  PuzzleFn onPuzzle_ = nullptr;
  void* ctx_ = nullptr;
  bool stopped_ = false;
  ImportStats stats_;
};

}  // namespace connections
