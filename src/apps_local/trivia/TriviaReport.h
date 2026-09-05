#pragma once

// The outbound report queue, and which build this card holds.
//
// Freestanding C++17 like TriviaCore, over the same ByteSource, so both are
// testable on a laptop. Nothing here allocates.
//
// TWO FILES, TWO JOBS. pack.state's FLAGGED bit is local and permanent -- never
// serve me this again. reports.dat is the outbound copy -- tell someone about
// this. Deriving one from the other loses information both ways: a player who
// hides a question without giving a reason still hid it, and a queue drained by
// a successful sync must not un-hide anything.
//
// THE HEADER IS THE BINDING. An index means nothing without the pack it
// indexes, so the queue records the pack id and question count it was filed
// against. That is not decoration: docs/apps/trivia-pack-format.md's residual
// is that a replacement pack with the SAME count keeps pack.state, and nothing
// on the device can see that its indices changed meaning. A queue that names
// its own pack survives that, because the sync sends what the HEADER says
// rather than what the card currently holds.
//
// The format is tools_local/trivia/reports.py, byte for byte, and
// host-tests/trivia asserts the two agree.

#include <cstddef>
#include <cstdint>

#include "TriviaCore.h"

namespace trivia {

inline constexpr uint8_t kReportMagic[8] = {'X', 'T', 'R', 'I', 'V', 'R', 'P', 'T'};
inline constexpr uint16_t kReportVersion = 1;

inline constexpr uint32_t kPackIdBytes = 32;
inline constexpr uint32_t kReportHeaderBytes = 52;  // magic 8, ver 2, resv 2, count 4, sent 4, id 32
inline constexpr uint32_t kReportEntryBytes = 8;    // index 4, reason 1, resv 3

// A cap, because the file is on a card that other apps share. Reached only
// after a very long time offline, and when it is reached the NEWEST report is
// dropped rather than the oldest: the first report of a question is the one the
// player meant, and a queue this full means sync has not run in months.
inline constexpr uint32_t kMaxQueuedReports = 64;

// Mirrors REASONS in tools_local/trivia/reports.py. The VALUES are the wire
// format, so a reordering here is a silent corpus edit -- a report meaning
// "wrong answer" would arrive meaning something else. Pinned by a host test.
//
// None is not "unset". It is a complete report with no reason attached, which
// is the whole of Mario's rule on card #257: a report with no reason is still a
// report, and demanding a category is how you get no reports.
enum class Reason : uint8_t {
  None = 0,
  Wrong = 1,       // the answer is factually wrong
  Nonsense = 2,    // the clue does not parse as a question
  Giveaway = 3,    // solo only: the options tell you the answer
  Ambiguous = 4,   // more than one option is defensibly right
  Outdated = 5,    // true when written, false now
  Broken = 6,      // mangled text or encoding
  Regional = 7,    // only someone local could know it
  Us = 8,          // a US question the pack failed to mark
  Hard = 9,        // levelled too easy for what it is
  Easy = 10,       // levelled too hard for what it is
  Count = 11,
};

// What opening a queue produced. Ready and Started both mean "you may add";
// Foreign means there are undelivered reports about a DIFFERENT pack, which
// must be synced before this card's reports can be queued. Re-labelling them
// would file them against a pack they were never about.
enum class QueueOpen : uint8_t { Ready, Started, Foreign, Unusable };

class ReportQueue {
 public:
  // `packId` is the build this card holds, or nullptr when it is unknown --
  // which is a real state, not an error: see PackMeta below. With no id nothing
  // can be queued, because a report that cannot name its pack cannot be
  // resolved and would only be noise on the far end.
  QueueOpen open(WritableByteSource& source, const char* packId, uint32_t packCount);

  bool isOpen() const { return source_ != nullptr; }
  uint32_t count() const { return count_; }
  uint32_t sent() const { return sent_; }
  uint32_t pending() const { return count_ - sent_; }
  uint32_t packCount() const { return packCount_; }
  const char* packId() const { return packId_; }
  bool full() const { return count_ >= kMaxQueuedReports; }

  // Files one report. A question already queued is amended rather than
  // duplicated, so tapping HIDE twice is one report -- the same rule the
  // service applies with its report key, kept here so the device does not
  // spend the queue on repeats.
  bool add(uint32_t index, Reason reason);

  // Attaches a reason to an already-queued question. This is the WHY? screen:
  // the report is filed on the first tap and the reason is an optional second,
  // so a player who walks away has still reported it.
  bool setReason(uint32_t index, Reason reason);

  bool entry(uint32_t i, uint32_t& indexOut, Reason& reasonOut) const;

  // After a sync delivered the first `n` entries. Advances a cursor rather than
  // truncating: a report filed while the request was in flight sits after `n`
  // and survives. Truncating to zero on a 2xx destroys exactly those.
  bool markSent(uint32_t n);

 private:
  bool writeHeader();
  bool find(uint32_t index, uint32_t& slotOut) const;

  WritableByteSource* source_ = nullptr;
  uint32_t count_ = 0;
  uint32_t sent_ = 0;
  uint32_t packCount_ = 0;
  char packId_[kPackIdBytes + 1] = {};
};

// pack.meta: which BUILD this card holds.
//
// The id is sha256(pack.dat)[:12], computed by the builder and carried in the
// manifest -- derived from the pack rather than declared beside it, so the two
// cannot disagree.
//
// It stores `count` and `bytes` as well, because a record of what you hold is
// only worth having if it can be caught lying. Copying a pack to a card by hand
// is a documented case, and a hand-copy that replaces pack.dat and leaves
// pack.meta describes a build the card no longer has. Both checks are free: a
// size the caller already knows, and the count already in the pack header.
//
// A meta failing either check is DISCARDED, never repaired. "I hold a pack and
// do not know which build" suppresses reporting for one sync; a confidently
// wrong id resolves reports through another build's index map and deletes
// questions nobody reported.
struct PackMeta {
  char id[kPackIdBytes + 1] = {};
  uint32_t count = 0;
  uint32_t bytes = 0;
  bool valid = false;
};

// `text` is the file's contents; `actualCount` and `actualBytes` are what the
// pack beside it really has. Returns a meta whose `valid` is false when they
// disagree, which callers must treat as "unknown build".
PackMeta parseMeta(const char* text, size_t length, uint32_t actualCount, uint32_t actualBytes);

// Renders pack.meta. Returns the number of bytes written, or 0 if it would not
// fit. Kept beside the parser so the two cannot drift.
size_t formatMeta(char* out, size_t capacity, const char* id, uint32_t count, uint32_t bytes);

// The manifest a device fetches from the release. Only the four fields the
// device acts on; anything else in the JSON is ignored rather than refused, so
// the server can add a field without stranding an old reader.
struct PackManifest {
  char id[kPackIdBytes + 1] = {};
  uint32_t count = 0;
  uint32_t bytes = 0;
  bool valid = false;
};

// A deliberately small reader: the manifest is ~200 bytes of flat JSON that we
// publish ourselves, so a scanner beats a parser here. It requires `id`,
// `count` and `bytes`; a missing one makes the manifest invalid rather than
// half-read, because acting on half a manifest is how a device decides it is
// current when it is not.
PackManifest parseManifest(const char* json, size_t length);

// What the sync should tell the player, decided from the two ids rather than
// from whichever screen is on. Separated out so it is testable without a panel.
enum class Freshness : uint8_t {
  Current,      // same build
  Newer,        // a different build is published
  Unknown,      // we hold a pack but not which build; a fetch would settle it
  NoManifest,   // could not read what is published
};
Freshness compare(const PackMeta& held, const PackManifest& published);

}  // namespace trivia
