#pragma once

// Underhand's cards, read straight from the original game's own data files:
// assets/json/savedatafiletemplate.json (the gods and their unlocks) and
// assets/json/cardwip.json (the cards). Freestanding, so the engine can be
// tested on a host.

#include <StreamingJsonParser.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace underhand {

enum Resource : uint8_t { Relic, Money, Cultist, Food, Prisoner, Suspicion };
constexpr int kResources = 6;
using Counts = std::array<int16_t, kResources>;

// The original's sentinels in a cost or gain, resolved when the card is drawn.
constexpr int16_t kHalf = 420;        // half of what you hold, rounded up
constexpr int16_t kOnlyIfNone = 840;  // payable only while you hold none

constexpr int kMaxCards = 128;  // the game has 118
constexpr int kMaxOptions = 3;
constexpr int kMaxAdds = 2;  // the game adds at most one kind per option
constexpr int kMaxRoll = 4;  // the game rolls at most one card per option
constexpr int kMaxGods = 8;  // the game has 7
constexpr size_t kTextBytes = 20 * 1024;

struct Option {
  uint16_t text = 0;     // offsets into Cards::text()
  uint16_t outcome = 0;  // the original's one-line effect note, often empty
  Counts cost{};
  Counts gain{};
  uint8_t randomCost = 0;  // resources lost at random on top of the cost
  bool swap = false;       // cultists and prisoners pay for each other
  bool foresight = false;
  bool foresightDiscard = false;
  bool lose = false;
  int8_t win = -1;  // god index
  uint8_t addCount = 0;
  struct Add {
    uint8_t card;
    uint8_t copies;
  } add[kMaxAdds]{};
  uint8_t rollCount = 0;  // cards drawn at random from rollLow..rollHigh
  uint8_t rollLow = 0;
  uint8_t rollHigh = 0;
  bool rollRepeats = false;
};

struct Card {
  uint8_t id = 0;
  uint8_t weight = 0;
  bool initial = false;
  bool recurring = false;
  uint16_t title = 0;
  uint16_t flavor = 0;
  uint8_t optionCount = 0;
  Option option[kMaxOptions];
};

struct God {
  uint16_t name = 0;
  uint8_t unlock[2] = {};
};

class Cards {
 public:
  const Card* card(int id) const { return id > 0 && id < 256 && index_[id] ? &cards_[index_[id] - 1] : nullptr; }
  const Card& at(int i) const { return cards_[i]; }
  int count() const { return count_; }
  const God& god(int i) const { return gods_[i]; }
  int godCount() const { return godCount_; }
  int godIndex(const char* name, size_t len) const;
  const char* text(uint16_t offset) const { return text_ + offset; }
  size_t textUsed() const { return textUsed_; }

 private:
  friend class CardsReader;

  Card cards_[kMaxCards];
  uint8_t index_[256] = {};  // id -> position + 1
  int count_ = 0;
  God gods_[kMaxGods];
  int godCount_ = 0;
  char text_[kTextBytes] = {};  // offset 0 is the empty string
  size_t textUsed_ = 1;
};

// Reads one of the two files into a Cards, whole or a chunk at a time. Read the
// gods first: a card names the god its option summons.
class CardsReader {
 public:
  enum class File : uint8_t { Gods, Cards };

  CardsReader(Cards& into, File file);
  CardsReader(const CardsReader&) = delete;
  CardsReader& operator=(const CardsReader&) = delete;

  void feed(const char* data, size_t len) { parser_.feed(data, len); }
  // True when the whole file was read and made sense. error() says why not.
  bool finish();
  const char* error() const { return error_; }

  static bool read(Cards& into, File file, const char* data, size_t len, const char** error = nullptr);

 private:
  static constexpr int kDepth = 6;
  static constexpr int kKey = 24;

  static void onKey(void* ctx, const char* s, size_t len);
  static void onString(void* ctx, const char* s, size_t len);
  static void onNumber(void* ctx, const char* s, size_t len);
  static void onBool(void* ctx, bool value);
  static void onObjectStart(void* ctx);
  static void onObjectEnd(void* ctx);
  static void onArrayStart(void* ctx);
  static void onArrayEnd(void* ctx);

  bool key(int depth, const char* name) const;
  void value(const char* s, size_t len, bool isString);
  void godValue(const char* s, size_t len, bool isString);
  void cardValue(const char* s, size_t len, bool isString);
  void endCard();
  void endOption();
  void fail(const char* why);
  uint16_t store(const char* s, size_t len);

  Cards& cards_;
  File file_;
  StreamingJsonParser parser_;
  int depth_ = 0;
  char keys_[kDepth][kKey] = {};
  bool inArray_ = false;
  const char* error_ = nullptr;

  // The card and option being read.
  Card card_;
  Option option_;
  bool optionOpen_ = false;
  bool optionBlank_ = true;
  int lowerBound_ = 0;
  int upperBound_ = 0;
  bool hasForesight_ = false;
  bool canDiscard_ = false;
  uint8_t ids_[kMaxAdds] = {};
  uint8_t idCount_ = 0;
  uint8_t copies_[kMaxAdds] = {};
  uint8_t copyCount_ = 0;
  int copiesScalar_ = 0;
  bool copiesIsList_ = false;
  int unlockGod_ = -1;
  int unlockCount_ = 0;
};

}  // namespace underhand
