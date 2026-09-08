#pragma once

// Reader for the on-SD deck files. See docs/apps/study-deck-format.md for the
// layout and the reasoning behind it.
//
// Freestanding C++17 -- no Arduino, no HalStorage, no heap. All I/O goes
// through the ByteSource interface, which is what lets host-tests/study parse a
// real converted deck on a laptop. The device supplies a HalStorage-backed
// source; the tests supply one over a plain file.
//
// Nothing here allocates. A Note is a fixed buffer the caller owns, because the
// review path touches one note per card and a per-card heap round trip on a
// device with no PSRAM headroom is exactly the kind of churn that fragments.

#include <cstddef>
#include <cstdint>

#include "StudyFsrs.h"

namespace study {

// Random-access bytes. Returning false must mean "did not read `length`
// bytes", never a short read, so callers can treat failure as fatal.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual bool read(uint32_t offset, void* dst, uint32_t length) = 0;
  virtual uint32_t size() const = 0;
};

// Writable source, for cards.dat. Split from ByteSource so deck.dat can be
// opened through a handle that physically cannot write to it.
class WritableByteSource : public ByteSource {
 public:
  virtual bool write(uint32_t offset, const void* src, uint32_t length) = 0;
  virtual bool flush() = 0;
};

// Eight since deck.dat v3 added clozeQuestion. A v2 deck has seven and is
// still read: the eighth simply comes back empty, which is exactly what a
// vocabulary note writes anyway.
inline constexpr int kFieldCount = 8;

// The largest note in Mario's 5001-card deck is 421 bytes; the mean is 125.
// 512 was room enough for that plus its terminators, and a note that would
// overflow is rejected rather than truncated -- a half-copied UTF-8 sequence
// renders as garbage and would be blamed on the font.
//
// Doubled for cloze, which stores its text twice: the question face and the
// answer face of one card differ only at the hole, and keeping both means the
// device does no text-building at review time. A cloze paragraph is also
// simply longer than a vocabulary headword. The cost is 512 bytes of DRAM in
// the one Note the activity holds -- not stack: loadNote unpacks inside this
// buffer rather than into a local of its own, so the render task's deepest
// path got *shorter* in the same change.
inline constexpr uint32_t kMaxNoteBytes = 1024;

enum class Field : uint8_t {
  Headword = 0,
  Reading,
  Meaning,
  PartOfSpeech,
  Sentence,
  SentenceReading,
  SentenceMeaning,
  // The question face of a cloze card: the note's text with this card's hole
  // shown as [...] (or as its hint) and every other card's hole filled in,
  // which is what Anki's {{cloze:}} does. The answer face is `Sentence`, with
  // the hole filled and the emphasis span over what was hidden -- so a cloze
  // card reuses the sentence face, its font, its wrapping and its per-card
  // font fallback rather than growing a second text path.
  ClozeQuestion,
};

// Anki allows an arbitrary number of learning steps; six is well past what
// anyone uses and keeps DeckMeta a fixed-size struct.
inline constexpr int kMaxLearningSteps = 6;

struct DeckMeta {
  float params[kNumParams] = {};
  float desiredRetention = 0.9f;
  int32_t maximumInterval = 36500;
  int32_t newPerDay = 20;
  int32_t reviewsPerDay = 200;
  int64_t collectionCreated = 0;  // epoch seconds of day zero
  uint8_t rolloverHour = 4;
  // Learning and relearning steps, in minutes, read out of the deck's Anki
  // preset. Mario's is 1m/10m and 10m. They travel with the deck for the same
  // reason the FSRS weights do: they are per-preset and he changes them.
  float learnSteps[kMaxLearningSteps] = {};
  float relearnSteps[kMaxLearningSteps] = {};
  uint8_t learnStepCount = 0;
  uint8_t relearnStepCount = 0;
  char name[64] = {};

  // Whether this deck's example sentence belongs on the question face as well
  // as the answer. False for a vocabulary deck, where the sentence is part of
  // what you are trying to recall; true for an HSK-style deck, where reading
  // the word in a sentence IS the exercise. Written by the converter from the
  // note type, because only the deck knows which kind it is: for its first
  // year this was hardcoded on, so a Barron's SAT card showed its own example
  // sentence while asking you to define the word.
  bool sentenceOnQuestion = false;

  // True when the deck shipped real optimized weights rather than zeros.
  bool hasParams() const;
};

// One card's scheduling state. Mirrors the 32-byte record in cards.dat exactly;
// see docs/apps/study-deck-format.md.
struct CardState {
  int64_t ankiCardId = 0;
  float stability = 0.0f;
  float difficulty = 0.0f;
  int32_t dueDay = 0;
  int32_t lastReviewDay = -1;
  uint16_t reps = 0;
  uint16_t lapses = 0;
  uint8_t state = 0;      // 0 new, 1 learning, 2 review, 3 relearning
  uint8_t stepIndex = 0;  // position in the learning or relearning step list
  // Minutes since dueDay began. Only meaningful while the card is inside a step
  // list, where "come back in ten minutes" cannot be said in day numbering.
  uint16_t dueMinute = 0;

  bool isNew() const { return state == 0; }
  Memory memory() const;
  void setMemory(const Memory& m);
};

inline constexpr uint32_t kCardRecordSize = 32;

// One note's text. Fields are NUL-terminated in place so they can be handed
// straight to drawText and snprintf without a copy: string_view would need a
// conversion at every one of those call sites, and the C API boundary is where
// this codebase has been bitten before.
class Note {
 public:
  const char* field(Field f) const;
  uint16_t length(Field f) const;
  bool empty(Field f) const { return length(f) == 0; }

  // Where the example sentence emphasised its target word, in codepoints, or
  // length 0 when it did not. Anki bolds it; there is no bold CJK face here,
  // so the renderer underlines instead.
  uint8_t emphasisOffset() const { return emphasisOffset_; }
  uint8_t emphasisLength() const { return emphasisLength_; }

  // A cloze card, told apart by the one thing only a cloze card has. No kind
  // byte: the field is the marker, so a deck file stays a flat list of
  // length-prefixed fields and every tool that walks it by the header's field
  // count keeps working.
  bool isCloze() const { return lengths_[static_cast<int>(Field::ClozeQuestion)] > 0; }

 private:
  friend class StudyDeck;
  char bytes_[kMaxNoteBytes] = {};
  uint16_t offsets_[kFieldCount] = {};
  uint16_t lengths_[kFieldCount] = {};
  uint8_t emphasisOffset_ = 0;
  uint8_t emphasisLength_ = 0;
};

class StudyDeck {
 public:
  // Parse meta.dat. Must succeed before the FSRS parameters are meaningful.
  bool openMeta(ByteSource& meta);
  // Parse deck.dat's header. The index stays on disk and is read per note:
  // 5001 entries is 20KB, which is not worth holding to save one 4-byte read.
  bool openDeck(ByteSource& deck);

  int noteCount() const { return noteCount_; }
  const DeckMeta& meta() const { return meta_; }

  // Read one note's text. `index` is 0..noteCount-1, and is also the card's
  // index in cards.dat -- the converter writes them in the same order so the
  // review path never needs a lookup.
  bool loadNote(ByteSource& deck, int index, Note& out) const;

  // Read and write one card's scheduling state.
  bool loadCard(ByteSource& cards, int index, CardState& out) const;
  bool storeCard(WritableByteSource& cards, int index, const CardState& in) const;

 private:
  DeckMeta meta_;
  int noteCount_ = 0;
  uint8_t fieldCount_ = 0;
  uint32_t indexBase_ = 0;  // file offset of the note index
};

// Day numbering, shared with Anki: whole days since the collection was created,
// counted from the deck's rollover hour. Keeping Anki's own numbering is what
// lets a due date computed here and one computed on the phone agree without any
// timezone conversation.
int dayNumber(const DeckMeta& meta, int64_t nowEpochSeconds);

}  // namespace study
