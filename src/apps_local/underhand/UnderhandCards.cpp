#include "UnderhandCards.h"

#include <cstdlib>
#include <cstring>

namespace underhand {

namespace {

// The original's marker for "no random shuffle" in a shuffle's bounds.
constexpr int kNoShuffle = 999;

constexpr const char* kResourceNames[kResources] = {"relic", "money", "cultist", "food", "prisoner", "suspicion"};

int resourceIndex(const char* name) {
  for (int i = 0; i < kResources; ++i) {
    if (std::strcmp(name, kResourceNames[i]) == 0) return i;
  }
  return -1;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool blank(const char* s, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (!isSpace(s[i])) return false;
  }
  return true;
}

int toInt(const char* s) { return static_cast<int>(std::strtol(s, nullptr, 10)); }

bool isCardId(int v) { return v >= 1 && v <= 255; }

}  // namespace

int Cards::godIndex(const char* name, size_t len) const {
  for (int i = 0; i < godCount_; ++i) {
    const char* g = text(gods_[i].name);
    if (std::strlen(g) == len && std::memcmp(g, name, len) == 0) return i;
  }
  return -1;
}

CardsReader::CardsReader(Cards& into, File file)
    : cards_(into),
      file_(file),
      parser_(JsonCallbacks{this, &CardsReader::onKey, &CardsReader::onString, &CardsReader::onNumber,
                            &CardsReader::onBool, nullptr, &CardsReader::onObjectStart, &CardsReader::onObjectEnd,
                            &CardsReader::onArrayStart, &CardsReader::onArrayEnd}) {}

bool CardsReader::read(Cards& into, File file, const char* data, size_t len, const char** error) {
  CardsReader reader(into, file);
  reader.feed(data, len);
  const bool ok = reader.finish();
  if (error) *error = reader.error();
  return ok;
}

void CardsReader::fail(const char* why) {
  if (!error_) error_ = why;
}

bool CardsReader::key(int depth, const char* name) const {
  return depth < kDepth && std::strcmp(keys_[depth], name) == 0;
}

// Copies a string into the text store with its whitespace folded: the game's
// strings carry stray tabs and doubled spaces from hand entry.
uint16_t CardsReader::store(const char* s, size_t len) {
  size_t begin = 0;
  while (begin < len && isSpace(s[begin])) ++begin;
  if (begin == len) return 0;
  const size_t at = cards_.textUsed_;
  size_t out = at;
  bool gap = false;
  for (size_t i = begin; i < len; ++i) {
    if (isSpace(s[i])) {
      gap = true;
      continue;
    }
    if (out + 2 >= kTextBytes) {
      fail("card text is larger than kTextBytes");
      return 0;
    }
    if (gap) cards_.text_[out++] = ' ';
    gap = false;
    cards_.text_[out++] = s[i];
  }
  cards_.text_[out++] = '\0';
  cards_.textUsed_ = out;
  return static_cast<uint16_t>(at);
}

void CardsReader::onKey(void* ctx, const char* s, size_t len) {
  auto* self = static_cast<CardsReader*>(ctx);
  if (self->depth_ >= kDepth) return;
  if (len >= kKey) len = kKey - 1;
  std::memcpy(self->keys_[self->depth_], s, len);
  self->keys_[self->depth_][len] = '\0';
}

void CardsReader::onString(void* ctx, const char* s, size_t len) {
  static_cast<CardsReader*>(ctx)->value(s, len, true);
}

void CardsReader::onNumber(void* ctx, const char* s, size_t len) {
  static_cast<CardsReader*>(ctx)->value(s, len, false);
}

void CardsReader::onBool(void* ctx, bool v) { static_cast<CardsReader*>(ctx)->value(v ? "1" : "0", 1, false); }

void CardsReader::onObjectStart(void* ctx) {
  auto* self = static_cast<CardsReader*>(ctx);
  ++self->depth_;
  if (self->depth_ < kDepth) self->keys_[self->depth_][0] = '\0';
  if (self->file_ != File::Cards) return;
  if (self->depth_ == 2) {
    self->card_ = Card{};
    const int id = toInt(self->keys_[1]);
    if (id < 1 || id > 255) return self->fail("card id outside 1..255");
    self->card_.id = static_cast<uint8_t>(id);
  } else if (self->depth_ == 3 && std::strncmp(self->keys_[2], "option", 6) == 0) {
    self->option_ = Option{};
    self->optionOpen_ = true;
    self->optionBlank_ = true;
    self->lowerBound_ = kNoShuffle;
    self->upperBound_ = kNoShuffle;
    self->hasForesight_ = false;
    self->canDiscard_ = false;
    self->idCount_ = 0;
    self->copyCount_ = 0;
    self->copiesScalar_ = 0;
    self->copiesIsList_ = false;
  }
}

void CardsReader::onObjectEnd(void* ctx) {
  auto* self = static_cast<CardsReader*>(ctx);
  if (self->file_ == File::Cards) {
    if (self->depth_ == 3 && self->optionOpen_) self->endOption();
    if (self->depth_ == 2) self->endCard();
  }
  if (self->depth_ > 0) --self->depth_;
}

void CardsReader::onArrayStart(void* ctx) {
  auto* self = static_cast<CardsReader*>(ctx);
  self->inArray_ = true;
  if (self->file_ == File::Cards && self->depth_ == 4 && self->key(4, "numcards")) {
    self->copiesIsList_ = true;
  }
  if (self->file_ == File::Gods && self->depth_ == 2 && self->key(1, "unlocked_cards")) {
    self->unlockGod_ = self->cards_.godIndex(self->keys_[2], std::strlen(self->keys_[2]));
    self->unlockCount_ = 0;
    if (self->unlockGod_ < 0) self->fail("unlocked_cards names a god missing from names");
  }
}

void CardsReader::onArrayEnd(void* ctx) {
  auto* self = static_cast<CardsReader*>(ctx);
  self->inArray_ = false;
  if (self->file_ == File::Gods && self->unlockGod_ >= 0) {
    if (self->unlockCount_ != 2) self->fail("a god does not unlock exactly two cards");
    self->unlockGod_ = -1;
  }
}

void CardsReader::value(const char* s, size_t len, bool isString) {
  if (file_ == File::Gods) {
    godValue(s, len, isString);
  } else {
    cardValue(s, len, isString);
  }
}

void CardsReader::godValue(const char* s, size_t len, bool isString) {
  if (depth_ != 2) return;
  if (key(1, "names") && isString) {
    const int i = toInt(keys_[2]);
    if (i < 0 || i >= kMaxGods) return fail("god index outside 0..kMaxGods");
    cards_.gods_[i].name = store(s, len);
    if (i >= cards_.godCount_) cards_.godCount_ = i + 1;
  } else if (key(1, "unlocked_cards") && inArray_ && !isString && unlockGod_ >= 0) {
    if (unlockCount_ >= 2) return fail("a god unlocks more than two cards");
    const int id = toInt(s);
    if (!isCardId(id)) return fail("an unlock is not a card id");
    cards_.gods_[unlockGod_].unlock[unlockCount_++] = static_cast<uint8_t>(id);
  }
}

void CardsReader::cardValue(const char* s, size_t len, bool isString) {
  if (depth_ == 2) {
    if (key(2, "title")) {
      card_.title = store(s, len);
    } else if (key(2, "flavortext")) {
      card_.flavor = store(s, len);
    } else if (key(2, "isinitial")) {
      card_.initial = toInt(s) != 0;
    } else if (key(2, "isrecurring")) {
      card_.recurring = toInt(s) != 0;
    } else if (key(2, "weight")) {
      const int w = toInt(s);
      if (w < 1 || w > 255) return fail("card weight outside 1..255");
      card_.weight = static_cast<uint8_t>(w);
    }
    return;
  }
  if (!optionOpen_) return;

  if (depth_ == 3) {
    if (key(3, "optiontext")) {
      optionBlank_ = blank(s, len);
      option_.text = store(s, len);
    } else if (key(3, "outputtext")) {
      option_.outcome = store(s, len);
    } else if (key(3, "cultistequalsprisoner")) {
      option_.swap = toInt(s) != 0;
    } else if (key(3, "randomrequirements")) {
      const int n = toInt(s);
      if (n < 0 || n > 255) return fail("randomrequirements outside 0..255");
      option_.randomCost = static_cast<uint8_t>(n);
    } else if (key(3, "islose")) {
      option_.lose = toInt(s) != 0;
    } else if (key(3, "iswin") && isString && !blank(s, len)) {
      const int god = cards_.godIndex(s, len);
      if (god < 0) return fail("an option summons a god missing from the gods file");
      option_.win = static_cast<int8_t>(god);
    }
    return;
  }

  if (depth_ != 4 || isString) return;
  const int v = toInt(s);
  if (key(3, "requirements") || key(3, "rewards")) {
    const int r = resourceIndex(keys_[4]);
    if (r < 0) return fail("unknown resource name");
    if (v < 0 || v > kOnlyIfNone) return fail("resource count outside 0..840");
    (key(3, "requirements") ? option_.cost : option_.gain)[r] = static_cast<int16_t>(v);
  } else if (key(3, "foresight")) {
    if (key(4, "hasforesight")) hasForesight_ = v != 0;
    if (key(4, "candiscard")) canDiscard_ = v != 0;
  } else if (key(3, "shuffle")) {
    if (key(4, "lowerbound")) {
      lowerBound_ = v;
    } else if (key(4, "upperbound")) {
      upperBound_ = v;
    } else if (key(4, "allowsdupes")) {
      option_.rollRepeats = v != 0;
    } else if (key(4, "specificids") && inArray_) {
      if (idCount_ >= kMaxAdds) return fail("an option adds more card kinds than kMaxAdds");
      if (!isCardId(v)) return fail("a shuffled-in id is not a card id");
      ids_[idCount_++] = static_cast<uint8_t>(v);
    } else if (key(4, "numcards")) {
      if (inArray_) {
        if (copyCount_ >= kMaxAdds) return fail("an option adds more card kinds than kMaxAdds");
        if (v < 1 || v > 255) return fail("a shuffle count outside 1..255");
        copies_[copyCount_++] = static_cast<uint8_t>(v);
      } else {
        copiesScalar_ = v;
      }
    }
  }
}

void CardsReader::endOption() {
  optionOpen_ = false;
  if (optionBlank_) return;  // an unused slot
  if (card_.optionCount >= kMaxOptions) return fail("a card has more than three options");

  option_.foresight = hasForesight_;
  option_.foresightDiscard = hasForesight_ && canDiscard_;
  for (int16_t g : option_.gain) {
    if (g == kOnlyIfNone) return fail("840 means nothing in a reward");
  }

  for (uint8_t i = 0; i < idCount_; ++i) {
    const int copies = copiesIsList_ ? (i < copyCount_ ? copies_[i] : -1) : copiesScalar_;
    if (copies < 1 || copies > 255) return fail("a shuffled-in card has no copy count");
    option_.add[i] = Option::Add{ids_[i], static_cast<uint8_t>(copies)};
  }
  option_.addCount = idCount_;
  if (copiesIsList_ && copyCount_ != idCount_) return fail("shuffle ids and counts differ in length");

  if (lowerBound_ != kNoShuffle) {
    if (copiesIsList_) return fail("a random shuffle has a list of counts");
    if (lowerBound_ < 1 || upperBound_ > 255 || lowerBound_ > upperBound_) return fail("bad random shuffle bounds");
    if (copiesScalar_ < 1 || copiesScalar_ > kMaxRoll) return fail("bad random shuffle count");
    if (!option_.rollRepeats && upperBound_ - lowerBound_ + 1 < copiesScalar_) {
      return fail("a random shuffle without repeats asks for more cards than its range holds");
    }
    option_.rollCount = static_cast<uint8_t>(copiesScalar_);
    option_.rollLow = static_cast<uint8_t>(lowerBound_);
    option_.rollHigh = static_cast<uint8_t>(upperBound_);
  }
  card_.option[card_.optionCount++] = option_;
}

void CardsReader::endCard() {
  if (error_) return;
  if (card_.id == 0) return fail("a card without an id");
  if (card_.weight == 0) return fail("a card without a weight");
  if (card_.title == 0) return fail("a card without a title");
  if (card_.optionCount == 0) return fail("a card without options");
  if (cards_.index_[card_.id]) return fail("two cards share an id");
  if (cards_.count_ >= kMaxCards) return fail("more cards than kMaxCards");
  cards_.cards_[cards_.count_++] = card_;
  cards_.index_[card_.id] = static_cast<uint8_t>(cards_.count_);
}

bool CardsReader::finish() {
  if (parser_.hasError()) fail("malformed JSON");
  if (depth_ != 0) fail("the file ends inside an object");
  if (file_ == File::Gods) {
    if (cards_.godCount_ == 0) fail("no gods");
    for (int i = 0; i < cards_.godCount_; ++i) {
      if (cards_.gods_[i].name == 0) fail("a gap in the god list");
    }
    return error_ == nullptr;
  }

  if (cards_.count_ == 0) fail("no cards");
  for (int i = 0; i < cards_.count_ && !error_; ++i) {
    const Card& c = cards_.cards_[i];
    for (int k = 0; k < c.optionCount; ++k) {
      const Option& o = c.option[k];
      for (int a = 0; a < o.addCount; ++a) {
        if (!cards_.card(o.add[a].card)) fail("an option shuffles in a card that does not exist");
      }
      for (int id = o.rollLow; o.rollCount && id <= o.rollHigh; ++id) {
        if (!cards_.card(id)) fail("a random shuffle's range includes a card that does not exist");
      }
    }
  }
  for (int g = 0; g < cards_.godCount_ && !error_; ++g) {
    for (uint8_t id : cards_.gods_[g].unlock) {
      if (!cards_.card(id)) fail("a god unlocks a card that does not exist");
    }
  }
  return error_ == nullptr;
}

}  // namespace underhand
