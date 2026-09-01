#pragma once

// Reader and round logic for the on-SD trivia pack. The format, and why it is
// shaped this way, is docs/apps/trivia-pack-format.md.
//
// Freestanding C++17 -- no Arduino, no HalStorage, no heap, following
// StudyDeck. All I/O goes through ByteSource, which is what lets
// host-tests/trivia read a real pack on a laptop. The device supplies a
// HalStorage-backed source; the tests supply one over a plain file.
//
// Nothing here allocates. A Question is a fixed buffer the caller owns: the
// play path touches one question at a time and a per-question heap round trip
// on a device with no PSRAM headroom is exactly the churn that fragments.

#include <cstddef>
#include <cstdint>

namespace trivia {

// Random-access bytes. Returning false must mean "did not read `length`
// bytes", never a short read, so callers can treat failure as fatal.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual bool read(uint32_t offset, void* dst, uint32_t length) = 0;
  virtual uint32_t size() const = 0;
};

// Split out so pack.dat can be opened through a handle that physically cannot
// write to it. Only pack.state is ever written.
class WritableByteSource : public ByteSource {
 public:
  virtual bool write(uint32_t offset, const void* src, uint32_t length) = 0;
  virtual bool flush() = 0;
};

// Measured against the shipped pack: largest record 358 bytes, longest clue
// 275, longest answer 25, at most 2 alternates and 6 distractors. 448 leaves
// room for a new season without a format bump, and a record that would
// overflow is REJECTED rather than truncated -- a half-copied UTF-8 sequence
// renders as garbage and gets blamed on the font.
inline constexpr uint32_t kMaxRecordBytes = 448;
inline constexpr int kMaxAlternates = 4;
inline constexpr int kMaxDistractors = 6;
inline constexpr int kOptions = 4;          // one right, three wrong
inline constexpr int kDifficulties = 5;

inline constexpr uint8_t kMagic[8] = {'X', 'T', 'R', 'I', 'V', 'I', 'A', 0};
inline constexpr uint16_t kFormatVersion = 1;

// pack.state bits, one byte per question at the question's own index.
inline constexpr uint8_t kSeen = 0x01;
inline constexpr uint8_t kFlagged = 0x02;

// One question, parsed. Owns its bytes; the accessors point into them.
class Question {
 public:
  uint8_t difficulty() const { return difficulty_; }
  uint16_t year() const { return year_; }
  int alternateCount() const { return altCount_; }
  int distractorCount() const { return wrongCount_; }

  const char* clue() const { return field(0); }
  const char* answer() const { return field(1); }
  const char* alternate(const int i) const {
    return (i >= 0 && i < altCount_) ? field(2 + i) : nullptr;
  }
  const char* distractor(const int i) const {
    return (i >= 0 && i < wrongCount_) ? field(2 + altCount_ + i) : nullptr;
  }
  // Quizmaster questions have no distractors, so only some of the pack can be
  // played as multiple choice. That is why the app has two modes, not one.
  bool playableAsChoice() const { return wrongCount_ >= kOptions - 1; }

 private:
  friend class Pack;
  const char* field(const int i) const {
    return (i >= 0 && i < fieldCount_) ? bytes_ + offset_[i] : "";
  }

  uint8_t difficulty_ = 0;
  uint16_t year_ = 0;
  uint8_t altCount_ = 0;
  uint8_t wrongCount_ = 0;
  int fieldCount_ = 0;
  uint16_t offset_[2 + kMaxAlternates + kMaxDistractors] = {};
  char bytes_[kMaxRecordBytes] = {};
};

// The pack file. Holds NO index in RAM: 195KB of offsets for a 50k pack is
// memory the device does not have, so an index entry is itself a read.
class Pack {
 public:
  bool open(ByteSource& source);
  bool isOpen() const { return count_ > 0; }
  uint32_t count() const { return count_; }

  // Two reads: the index pair, then the record. False on a torn or oversized
  // record, which the caller must treat as fatal for that question rather than
  // drawing whatever was in the buffer.
  bool read(uint32_t index, Question& out) const;

 private:
  ByteSource* source_ = nullptr;
  uint32_t count_ = 0;
  uint32_t indexAt_ = 0;
  uint32_t base_ = 0;
};

// pack.state. One byte per question, so marking is one write at a computed
// offset and never a rewrite -- a power loss mid-write can lose one question's
// state and can never touch the question text.
class PackState {
 public:
  bool open(WritableByteSource& source, uint32_t count);
  uint8_t flags(uint32_t index) const;
  bool setFlag(uint32_t index, uint8_t bit);
  bool seen(const uint32_t i) const { return (flags(i) & kSeen) != 0; }
  bool flagged(const uint32_t i) const { return (flags(i) & kFlagged) != 0; }

  // How many questions have been served, out of count(). Counted once when the
  // state file is opened and kept up to date by setFlag, because the answer is
  // wanted on the front door every time it draws and a rescan there would read
  // the whole file per paint.
  uint32_t seenCount() const { return seenCount_; }
  // How many a player has rejected. Counted in the same pass as seenCount --
  // one scan of the file answers both, and the flag screen wants to say how
  // many so the action has a visible effect.
  uint32_t flaggedCount() const { return flaggedCount_; }
  uint32_t count() const { return count_; }

 private:
  void scanCounts();

  WritableByteSource* source_ = nullptr;
  uint32_t count_ = 0;
  uint32_t seenCount_ = 0;
  uint32_t flaggedCount_ = 0;
};

// Deterministic, seedable, and ours -- so a host test can assert an exact
// sequence and the device does not depend on rand() being seeded.
class Rng {
 public:
  explicit Rng(const uint32_t seed = 1) : state_(seed ? seed : 1) {}
  uint32_t next();
  uint32_t below(uint32_t bound);
  void seed(const uint32_t s) { state_ = s ? s : 1; }

 private:
  uint32_t state_;
};

// Picks the next question, preferring ones this device has not served. Walks
// forward from a random start rather than retrying at random: a retry loop
// gets slower exactly as the pack is exhausted, which is when someone has
// played enough to care.
class Chooser {
 public:
  void begin(const Pack& pack, PackState& state, Rng& rng);
  // requireChoice restricts to questions carrying distractors (solo mode).
  // difficulty 0 means any.
  bool next(uint32_t& indexOut, bool requireChoice, int difficulty);

 private:
  const Pack* pack_ = nullptr;
  PackState* state_ = nullptr;
  Rng* rng_ = nullptr;
};

// Lays out the four options for solo mode: the answer plus three distractors,
// shuffled. Six distractors are stored and three are drawn, so replaying a
// question does not give the same four options.
struct Choices {
  const char* option[kOptions] = {};
  int correct = 0;
};
bool buildChoices(const Question& question, Rng& rng, Choices& out);

// Answer matching for solo mode is exact-by-construction: the player taps an
// option. The alternates exist for the quizmaster reading aloud, who needs to
// know that "the Netherlands" and "Netherlands" are both right.
bool answerMatches(const Question& question, const char* given);

// A round's tally. Deliberately not persisted: a bar game's score belongs to
// the evening, and a saved high score turns a party into a leaderboard.
struct Score {
  int asked = 0;
  int right = 0;
  void record(const bool correct) {
    ++asked;
    if (correct) ++right;
  }
  void reset() { asked = right = 0; }
};

// Whether the card has room for a pack download, kept HERE rather than in the
// activity so it can be tested: TriviaActivity includes WiFi.h and cannot be
// built on the host at all, so any decision left inside it is untestable by
// construction.
enum class Room : uint8_t { Ok, Unknown, TooSmall };

// The floor the card must clear, NOT the pack's size. The pack is ~6.2MB today
// and grows whenever questions are added, and its true size arrives only with
// the server's Content-Length -- after the point where refusing is still free.
// So this is a deliberate over-estimate with room for a larger pack and for the
// FAT to record it. Raise it if the pack approaches it; never pin it to the
// current byte count.
constexpr uint64_t kPackFreeFloorBytes = 12ull * 1024 * 1024;

// queryOk mirrors HalStorage::freeBytes()'s return exactly: false means the card
// COULD NOT ANSWER, never that it is full. The two are different facts and the
// caller must not collapse them -- deriving free space from total minus used
// reports a failed query as an empty card, which is the trap this replaces.
constexpr Room roomFor(const bool queryOk, const uint64_t freeBytes, const uint64_t floorBytes) {
  if (!queryOk) return Room::Unknown;
  return freeBytes < floorBytes ? Room::TooSmall : Room::Ok;
}

}  // namespace trivia
