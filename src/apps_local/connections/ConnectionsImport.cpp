#include "ConnectionsImport.h"

#include <cstdlib>
#include <cstring>

#include "ConnectionsText.h"

namespace connections {

namespace {

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "2023-06-12" -> 20230612. Returns 0 on anything else, which the caller treats
// as a malformed puzzle rather than guessing a date.
uint32_t parseDate(const char* s) {
  if (std::strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
  uint32_t value = 0;
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) continue;
    if (s[i] < '0' || s[i] > '9') return 0;
    value = value * 10 + static_cast<uint32_t>(s[i] - '0');
  }
  return value;
}

void copyBounded(char* dst, const size_t cap, const char* src) {
  const size_t len = std::strlen(src);
  const size_t n = len < cap - 1 ? len : cap - 1;
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

void Importer::begin(const PuzzleFn onPuzzle, void* ctx) {
  *this = Importer{};
  onPuzzle_ = onPuzzle;
  ctx_ = ctx;
}

void Importer::resetPuzzle() {
  puzzle_ = Puzzle{};
  groupIndex_ = -1;
  memberIndex_ = 0;
  groupsSeen_ = 0;
  puzzleUnrenderable_ = false;
}

void Importer::beginString() {
  textLen_ = 0;
  textOverflow_ = false;
  text_[0] = '\0';
}

void Importer::appendChar(const uint32_t codepoint) {
  char folded[4];
  const int n = foldToAscii(codepoint, folded);
  if (n < 0) return;  // zero-width: dropping it is correct, not lossy
  if (n == 0) {
    // No ASCII form. Emoji and the like: the puzzle is unplayable on a display
    // that cannot draw it, so mark it and keep parsing to stay in sync.
    puzzleUnrenderable_ = true;
    return;
  }
  if (textLen_ + n >= static_cast<int>(sizeof(text_))) {
    textOverflow_ = true;
    return;
  }
  for (int i = 0; i < n; ++i) text_[textLen_++] = folded[i];
  text_[textLen_] = '\0';
}

void Importer::endString() {
  if (inKey_) {
    // Keys are matched at the point they are read; the schema is flat enough
    // that no key name is reused at two meanings.
    if (std::strcmp(text_, "id") == 0) {
      field_ = Field::Id;
    } else if (std::strcmp(text_, "date") == 0) {
      field_ = Field::Date;
    } else if (std::strcmp(text_, "level") == 0) {
      field_ = Field::Level;
    } else if (std::strcmp(text_, "group") == 0) {
      field_ = Field::GroupName;
    } else if (std::strcmp(text_, "members") == 0) {
      field_ = Field::Members;
    } else {
      field_ = Field::None;
    }
    return;
  }

  switch (field_) {
    case Field::Date:
      puzzle_.date = parseDate(text_);
      field_ = Field::None;
      break;
    case Field::GroupName:
      if (groupIndex_ >= 0 && groupIndex_ < kGroups) {
        // A group name that overflows is truncated rather than rejected: the
        // name is a label, and losing its tail costs less than losing the
        // puzzle. A word that overflows is a different matter, below.
        copyBounded(puzzle_.groups[groupIndex_].name, sizeof(puzzle_.groups[groupIndex_].name), text_);
      }
      field_ = Field::None;
      break;
    case Field::Members:
      if (groupIndex_ >= 0 && groupIndex_ < kGroups && memberIndex_ < kMembers) {
        if (textOverflow_ || textLen_ > kMaxWordLen) {
          // A truncated tile would be a different word, and the player would
          // have no way to know. Drop the puzzle instead.
          puzzleUnrenderable_ = true;
        } else {
          copyBounded(puzzle_.groups[groupIndex_].members[memberIndex_],
                      sizeof(puzzle_.groups[groupIndex_].members[memberIndex_]), text_);
        }
        ++memberIndex_;
      }
      break;
    default:
      break;
  }
}

void Importer::endNumber() {
  number_[numberLen_] = '\0';
  const long value = std::strtol(number_, nullptr, 10);
  if (field_ == Field::Id) {
    puzzle_.id = static_cast<uint16_t>(value < 0 ? 0 : (value > 0xFFFF ? 0xFFFF : value));
  } else if (field_ == Field::Level && groupIndex_ >= 0 && groupIndex_ < kGroups) {
    // The source uses -1 where the v2 API stopped publishing colour data. Kept
    // as unknown rather than clamped to 0, which would assert that all four
    // groups of every puzzle since 2025-09-20 are yellow.
    puzzle_.groups[groupIndex_].level = (value < 0 || value > 3) ? kLevelUnknown : static_cast<uint8_t>(value);
  }
  field_ = Field::None;
  numberLen_ = 0;
}

void Importer::openContainer(const char c) {
  ++depth_;
  if (c == '{') {
    // depth 1 is the outer array, so a puzzle object opens at depth 2 and a
    // group object at depth 4 (inside "answers").
    if (depth_ == 2) {
      resetPuzzle();
    } else if (depth_ == 4) {
      if (groupsSeen_ < kGroups) groupIndex_ = groupsSeen_;
      memberIndex_ = 0;
    }
  } else if (c == '[' && field_ == Field::Members) {
    memberIndex_ = 0;
  }
}

void Importer::closeContainer(const char c) {
  if (c == '}') {
    if (depth_ == 4) {
      if (groupIndex_ >= 0) ++groupsSeen_;
      groupIndex_ = -1;
    } else if (depth_ == 2) {
      emitIfComplete();
    }
  }
  --depth_;
  field_ = Field::None;
}

void Importer::emitIfComplete() {
  if (puzzleUnrenderable_) {
    ++stats_.skippedUnrenderable;
    return;
  }
  if (groupsSeen_ != kGroups || puzzle_.date == 0 || !isPlayable(puzzle_)) {
    ++stats_.skippedMalformed;
    return;
  }
  ++stats_.accepted;
  if (onPuzzle_ != nullptr && !onPuzzle_(ctx_, puzzle_)) stopped_ = true;
}

bool Importer::feed(const uint8_t* data, const size_t len) {
  if (stopped_ || stats_.sawSyntaxError) return false;

  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(data[i]);

    switch (scan_) {
      case Scan::String:
        if (c == '\\') {
          scan_ = Scan::Escape;
        } else if (c == '"') {
          scan_ = Scan::Value;
          endString();
        } else {
          // The archive is pure ASCII with everything else escaped, but a raw
          // UTF-8 byte would still arrive here; treat it as unrenderable rather
          // than emitting a broken byte into a word.
          appendChar(static_cast<uint8_t>(c));
        }
        break;

      case Scan::Escape:
        switch (c) {
          case 'u':
            scan_ = Scan::Unicode;
            pendingCodepoint_ = 0;
            unicodeDigits_ = 0;
            break;
          case 'n':
            appendChar('\n');
            scan_ = Scan::String;
            break;
          case 't':
            appendChar('\t');
            scan_ = Scan::String;
            break;
          case 'b':
          case 'f':
          case 'r':
            appendChar(' ');
            scan_ = Scan::String;
            break;
          default:  // \" \\ \/ and anything else: the character itself
            appendChar(static_cast<uint8_t>(c));
            scan_ = Scan::String;
            break;
        }
        break;

      case Scan::Unicode: {
        const int digit = hexValue(c);
        if (digit < 0) {
          stats_.sawSyntaxError = true;
          return false;
        }
        pendingCodepoint_ = (pendingCodepoint_ << 4) | static_cast<uint32_t>(digit);
        if (++unicodeDigits_ == 4) {
          if (pendingCodepoint_ >= 0xD800 && pendingCodepoint_ <= 0xDBFF) {
            // Lead of a surrogate pair: an emoji. Hold it so the trail does not
            // get folded on its own.
            highSurrogate_ = pendingCodepoint_;
          } else if (pendingCodepoint_ >= 0xDC00 && pendingCodepoint_ <= 0xDFFF && highSurrogate_ != 0) {
            appendChar(0x10000 + ((highSurrogate_ - 0xD800) << 10) + (pendingCodepoint_ - 0xDC00));
            highSurrogate_ = 0;
          } else {
            highSurrogate_ = 0;
            appendChar(pendingCodepoint_);
          }
          scan_ = Scan::String;
        }
        break;
      }

      case Scan::Number:
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
          if (numberLen_ < static_cast<int>(sizeof(number_)) - 1) number_[numberLen_++] = c;
          break;
        }
        endNumber();
        scan_ = Scan::Value;
        --i;  // re-handle the terminator as structure
        break;

      case Scan::Literal:
        // true / false / null: nothing in this schema needs their value.
        if ((c >= 'a' && c <= 'z')) break;
        scan_ = Scan::Value;
        --i;
        break;

      case Scan::Value:
        if (isSpace(c)) break;
        if (c == '"') {
          // A string in key position is followed by ':'. Tracked with a flag
          // set on ',' and '{' rather than by lookahead, which a chunked feed
          // cannot do.
          scan_ = Scan::String;
          beginString();
        } else if (c == '{' || c == '[') {
          openContainer(c);
          inKey_ = (c == '{');
        } else if (c == '}' || c == ']') {
          closeContainer(c);
          inKey_ = false;
        } else if (c == ':') {
          inKey_ = false;
        } else if (c == ',') {
          // Inside an object the next string is a key; inside an array it is a
          // value. depth_ alone cannot tell, so use what container we are in:
          // members arrays keep field_ set, objects do not.
          inKey_ = (field_ != Field::Members);
        } else if ((c >= '0' && c <= '9') || c == '-') {
          scan_ = Scan::Number;
          numberLen_ = 0;
          number_[numberLen_++] = c;
        } else if (c == 't' || c == 'f' || c == 'n') {
          scan_ = Scan::Literal;
        } else {
          stats_.sawSyntaxError = true;
          return false;
        }
        break;
    }

    if (stopped_) return false;
  }
  return true;
}

bool Importer::finish() {
  if (scan_ == Scan::Number) {
    endNumber();
    scan_ = Scan::Value;
  }
  // Anything else unfinished means the document was cut short: a half-read
  // string or an unclosed object. Better to report it than to keep whatever
  // puzzles arrived before the truncation and call the import complete.
  return !stats_.sawSyntaxError && scan_ == Scan::Value && depth_ == 0;
}

}  // namespace connections
