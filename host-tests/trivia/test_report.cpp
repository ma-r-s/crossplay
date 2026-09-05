// The report queue, pack.meta and the manifest reader, without a panel.
//
// Everything here protects one outcome: a report that names the wrong pack.
// Downstream that resolves an index through another build's map and deletes a
// question nobody reported, silently, leaving a pack one row smaller. So most
// of these cases are refusals, and each was written by undoing the check and
// watching it go red.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "TriviaReport.h"

using namespace trivia;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

// A card in memory. Writes past the end grow it, like a real file.
class MemSource final : public WritableByteSource {
 public:
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (static_cast<size_t>(offset) + length > bytes.size()) return false;
    std::memcpy(dst, bytes.data() + offset, length);
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes.size()); }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (static_cast<size_t>(offset) + length > bytes.size()) bytes.resize(offset + length, 0);
    std::memcpy(bytes.data() + offset, src, length);
    return true;
  }
  bool flush() override { return true; }

  std::vector<uint8_t> bytes;
};

const char* kPack = "abc123def456";

void testQueueBasics() {
  MemSource card;
  ReportQueue q;
  CHECK(q.open(card, kPack, 100) == QueueOpen::Started);
  CHECK(card.size() == kReportHeaderBytes);
  CHECK(q.count() == 0 && q.pending() == 0);

  CHECK(q.add(4, Reason::Wrong));
  CHECK(q.add(9, Reason::None));
  CHECK(q.count() == 2 && q.pending() == 2);

  uint32_t index = 0;
  Reason reason = Reason::Count;
  CHECK(q.entry(0, index, reason) && index == 4 && reason == Reason::Wrong);
  CHECK(q.entry(1, index, reason) && index == 9 && reason == Reason::None);
  CHECK(!q.entry(2, index, reason));

  // An index outside the pack is refused: it names no question.
  CHECK(!q.add(100, Reason::Wrong));
  CHECK(!q.add(4000, Reason::Wrong));
  CHECK(q.count() == 2);

  // Reporting the same question twice is ONE report, amended rather than
  // duplicated -- otherwise a player tapping HIDE twice spends the queue.
  CHECK(q.add(4, Reason::Broken));
  CHECK(q.count() == 2);
  CHECK(q.entry(0, index, reason) && reason == Reason::Broken);

  // The WHY? screen: file first, attach a reason second.
  CHECK(q.setReason(9, Reason::Giveaway));
  CHECK(q.entry(1, index, reason) && reason == Reason::Giveaway);
  CHECK(!q.setReason(77, Reason::Wrong));  // never queued
}

// The queue must survive being reopened, because that is what every app launch
// does. A queue that forgot its entries would look exactly like a queue that
// synced successfully.
void testQueueReopens() {
  MemSource card;
  {
    ReportQueue q;
    CHECK(q.open(card, kPack, 50) == QueueOpen::Started);
    CHECK(q.add(1, Reason::Wrong));
    CHECK(q.add(2, Reason::Easy));
  }
  ReportQueue again;
  CHECK(again.open(card, kPack, 50) == QueueOpen::Ready);
  CHECK(again.count() == 2);
  uint32_t index = 0;
  Reason reason = Reason::Count;
  CHECK(again.entry(1, index, reason) && index == 2 && reason == Reason::Easy);
  CHECK(std::strcmp(again.packId(), kPack) == 0);
}

// THE ONE THAT MATTERS. A queue filed against another pack is never
// re-labelled: those indices name questions that are no longer there.
void testForeignQueueIsNotRelabelled() {
  MemSource card;
  {
    ReportQueue q;
    CHECK(q.open(card, "oldpack00001", 50) == QueueOpen::Started);
    CHECK(q.add(7, Reason::Wrong));
  }

  ReportQueue after;
  CHECK(after.open(card, "newpack00002", 60) == QueueOpen::Foreign);
  // It still describes the pack it was filed against, so a sync can send it
  // correctly rather than mislabelling it.
  CHECK(std::strcmp(after.packId(), "oldpack00001") == 0);
  CHECK(after.packCount() == 50);
  CHECK(after.count() == 1);

  // Nothing may be added under the new pack while those are undelivered.
  CHECK(after.add(3, Reason::Wrong));  // 3 < 50, so it lands in the OLD queue
  uint32_t index = 0;
  Reason reason = Reason::Count;
  CHECK(after.entry(1, index, reason) && index == 3);
  CHECK(std::strcmp(after.packId(), "oldpack00001") == 0);

  // Once delivered, the queue is safe to reuse for the new pack.
  CHECK(after.markSent(after.count()));
  ReportQueue fresh;
  CHECK(fresh.open(card, "newpack00002", 60) == QueueOpen::Started);
  CHECK(fresh.count() == 0);
  CHECK(std::strcmp(fresh.packId(), "newpack00002") == 0);
}

// A same-count replacement is the case the device cannot otherwise see. The id
// is what catches it; the count alone would not.
void testSameCountDifferentPack() {
  MemSource card;
  {
    ReportQueue q;
    CHECK(q.open(card, "buildaaaaaaa", 50) == QueueOpen::Started);
    CHECK(q.add(7, Reason::Wrong));
  }
  ReportQueue after;
  CHECK(after.open(card, "buildbbbbbbb", 50) == QueueOpen::Foreign);
  CHECK(std::strcmp(after.packId(), "buildaaaaaaa") == 0);
}

void testUnknownBuildQueuesNothing() {
  MemSource card;
  ReportQueue q;
  // No id: the card holds a pack and does not know which build. A report that
  // cannot name its pack can never be resolved, so none is taken.
  CHECK(q.open(card, nullptr, 50) == QueueOpen::Unusable);
  CHECK(!q.add(1, Reason::Wrong));
  CHECK(q.open(card, "", 50) == QueueOpen::Unusable);
  CHECK(card.size() == 0);
}

// markSent advances a cursor instead of truncating, so a report filed while the
// request was in flight is not destroyed by its success.
void testMarkSentKeepsLateReports() {
  MemSource card;
  ReportQueue q;
  CHECK(q.open(card, kPack, 50) == QueueOpen::Started);
  CHECK(q.add(1, Reason::Wrong));
  CHECK(q.add(2, Reason::Broken));
  const uint32_t sending = q.count();
  // ... the request is in flight, and the player hides another question ...
  CHECK(q.add(3, Reason::Easy));
  CHECK(q.markSent(sending));
  CHECK(q.sent() == 2);
  CHECK(q.count() == 3);
  CHECK(q.pending() == 1);

  uint32_t index = 0;
  Reason reason = Reason::Count;
  CHECK(q.entry(2, index, reason) && index == 3 && reason == Reason::Easy);

  // A delivered entry is a fact on the far end and is not rewritten here.
  CHECK(!q.setReason(1, Reason::Ambiguous));
  CHECK(q.entry(0, index, reason) && reason == Reason::Wrong);

  // The cursor never moves backwards or past what exists.
  CHECK(!q.markSent(1));
  CHECK(!q.markSent(4));
  CHECK(q.sent() == 2);
}

// UNDO on the HIDDEN notice. The queue is fixed-width and cannot shrink, so a
// withdrawn entry is tombstoned rather than removed -- and the slot is NOT
// reused, because `sent` is a position and an out-of-order queue would make
// markSent claim to have delivered something it had not.
void testWithdraw() {
  MemSource card;
  ReportQueue q;
  CHECK(q.open(card, kPack, 50) == QueueOpen::Started);
  CHECK(q.add(1, Reason::Wrong));
  CHECK(q.add(2, Reason::Broken));

  CHECK(q.withdraw(1));
  uint32_t index = 0;
  Reason reason = Reason::Count;
  CHECK(q.entry(0, index, reason) && index == kWithdrawnIndex);
  // The other report is untouched.
  CHECK(q.entry(1, index, reason) && index == 2 && reason == Reason::Broken);
  // Withdrawing something that is not queued is not a silent success.
  CHECK(!q.withdraw(1));
  CHECK(!q.withdraw(44));

  // A new report APPENDS rather than reusing the tombstone.
  CHECK(q.add(3, Reason::Easy));
  CHECK(q.count() == 3);
  CHECK(q.entry(2, index, reason) && index == 3);

  // A delivered report cannot be taken back: it is a fact on the far end, and
  // pretending otherwise leaves the two disagreeing with nothing to notice it.
  CHECK(q.markSent(q.count()));
  CHECK(!q.withdraw(2));
  CHECK(q.entry(1, index, reason) && index == 2);

  // The tombstone survives a reopen and is still skipped by the reader.
  ReportQueue again;
  CHECK(again.open(card, kPack, 50) == QueueOpen::Ready);
  CHECK(again.count() == 3);
  CHECK(again.entry(0, index, reason) && index == kWithdrawnIndex);
}

void testQueueCapDropsTheNewest() {
  MemSource card;
  ReportQueue q;
  CHECK(q.open(card, kPack, 1000) == QueueOpen::Started);
  for (uint32_t i = 0; i < kMaxQueuedReports; ++i) CHECK(q.add(i, Reason::Wrong));
  CHECK(q.full());
  // The newest is dropped, and it still reports success: the FLAGGED bit landed,
  // so the question IS hidden, which is what the player asked for.
  CHECK(q.add(999, Reason::Wrong));
  CHECK(q.count() == kMaxQueuedReports);
  uint32_t index = 0;
  Reason reason = Reason::Count;
  CHECK(q.entry(0, index, reason) && index == 0);
}

void testTornAndForeignFiles() {
  {
    MemSource card;
    card.bytes.assign(kReportHeaderBytes + 3, 0);
    std::memcpy(card.bytes.data(), kReportMagic, 8);
    card.bytes[8] = 1;
    ReportQueue q;
    CHECK(q.open(card, kPack, 50) == QueueOpen::Unusable);
  }
  {
    MemSource card;
    card.bytes.assign(kReportHeaderBytes, 0);
    std::memcpy(card.bytes.data(), "NOTAQUEU", 8);
    ReportQueue q;
    CHECK(q.open(card, kPack, 50) == QueueOpen::Unusable);
  }
  {
    MemSource card;  // a version this reader does not know
    card.bytes.assign(kReportHeaderBytes, 0);
    std::memcpy(card.bytes.data(), kReportMagic, 8);
    card.bytes[8] = 99;
    ReportQueue q;
    CHECK(q.open(card, kPack, 50) == QueueOpen::Unusable);
  }
  {
    MemSource card;  // claims more sent than it holds
    card.bytes.assign(kReportHeaderBytes + kReportEntryBytes, 0);
    std::memcpy(card.bytes.data(), kReportMagic, 8);
    card.bytes[8] = 1;
    card.bytes[16] = 5;
    std::memcpy(card.bytes.data() + 20, kPack, std::strlen(kPack));
    ReportQueue q;
    CHECK(q.open(card, kPack, 50) == QueueOpen::Unusable);
  }
}

// --- pack.meta ---------------------------------------------------------------

void testMeta() {
  char buf[128] = {};
  const size_t n = formatMeta(buf, sizeof(buf), kPack, 49958, 6624675);
  CHECK(n > 0);

  PackMeta good = parseMeta(buf, n, 49958, 6624675);
  CHECK(good.valid);
  CHECK(std::strcmp(good.id, kPack) == 0);
  CHECK(good.count == 49958 && good.bytes == 6624675);

  // The hand-copy: same question count, different pack. Only `bytes` catches it.
  CHECK(!parseMeta(buf, n, 49958, 6624999).valid);
  // And the ordinary case: a different count.
  CHECK(!parseMeta(buf, n, 25866, 6624675).valid);

  // A meta missing any field is unknown, not half-read.
  const char* noBytes = "id\tabc\ncount\t10\n";
  CHECK(!parseMeta(noBytes, std::strlen(noBytes), 10, 99).valid);
  const char* noId = "count\t10\nbytes\t99\n";
  CHECK(!parseMeta(noId, std::strlen(noId), 10, 99).valid);
  CHECK(!parseMeta("", 0, 10, 99).valid);
  CHECK(!parseMeta(nullptr, 0, 10, 99).valid);

  // Junk in a value is a refusal, never a partial number.
  const char* junk = "id\tabc\ncount\t1x0\nbytes\t99\n";
  CHECK(!parseMeta(junk, std::strlen(junk), 10, 99).valid);

  // An id with a path separator in it must never come back: it reaches a URL
  // and a filename on the way out.
  const char* nasty = "id\t../../etc\ncount\t10\nbytes\t99\n";
  CHECK(!parseMeta(nasty, std::strlen(nasty), 10, 99).valid);

  // A count that would wrap is refused rather than silently reduced.
  const char* huge = "id\tabc\ncount\t99999999999999\nbytes\t99\n";
  CHECK(!parseMeta(huge, std::strlen(huge), 10, 99).valid);

  // CRLF, because a card can be edited on a PC.
  const char* crlf = "id\tabc\r\ncount\t10\r\nbytes\t99\r\n";
  const PackMeta win = parseMeta(crlf, std::strlen(crlf), 10, 99);
  CHECK(win.valid && std::strcmp(win.id, "abc") == 0);

  char tiny[8] = {};
  CHECK(formatMeta(tiny, sizeof(tiny), kPack, 49958, 6624675) == 0);
}

// --- the manifest ------------------------------------------------------------

void testManifest() {
  const char* json =
      "{\"built\":\"2026-09-05T04:00:02+00:00\",\"bytes\":6624675,\"count\":49958,"
      "\"id\":\"abc123def456\",\"manifest\":1}";
  const PackManifest man = parseManifest(json, std::strlen(json));
  CHECK(man.valid);
  CHECK(std::strcmp(man.id, "abc123def456") == 0);
  CHECK(man.count == 49958);
  CHECK(man.bytes == 6624675);

  // Whitespace and a different field order are both fine: this is JSON we
  // publish, but a reader that only works on one serialisation is a trap.
  const char* spaced = "{ \"id\" : \"zz9\" , \"count\" : 7 , \"bytes\" : 12 }";
  const PackManifest sp = parseManifest(spaced, std::strlen(spaced));
  CHECK(sp.valid && std::strcmp(sp.id, "zz9") == 0 && sp.count == 7 && sp.bytes == 12);

  // A field the device does not know must be ignored, not refused, or the
  // server can never add one without stranding every old reader.
  const char* extra = "{\"id\":\"zz9\",\"count\":7,\"bytes\":12,\"newthing\":\"whatever\"}";
  CHECK(parseManifest(extra, std::strlen(extra)).valid);

  // Half a manifest is not a manifest: acting on one is how a device decides it
  // is current when it is not.
  CHECK(!parseManifest("{\"id\":\"zz9\",\"count\":7}", 22).valid);
  CHECK(!parseManifest("{\"count\":7,\"bytes\":12}", 22).valid);
  CHECK(!parseManifest("", 0).valid);
  CHECK(!parseManifest("not json at all", 15).valid);
  // A zero count would make every index out of range.
  CHECK(!parseManifest("{\"id\":\"z\",\"count\":0,\"bytes\":12}", 31).valid);

  // A key that merely CONTAINS the name must not match: "packid" is not "id".
  const char* decoy = "{\"packid\":\"nope\",\"id\":\"yes9\",\"count\":7,\"bytes\":12}";
  const PackManifest d = parseManifest(decoy, std::strlen(decoy));
  CHECK(d.valid && std::strcmp(d.id, "yes9") == 0);
}

void testFreshness() {
  char buf[128] = {};
  const size_t n = formatMeta(buf, sizeof(buf), "aaa111", 10, 20);
  const PackMeta held = parseMeta(buf, n, 10, 20);
  CHECK(held.valid);

  PackManifest same;
  std::strcpy(same.id, "aaa111");
  same.count = 10;
  same.bytes = 20;
  same.valid = true;
  CHECK(compare(held, same) == Freshness::Current);

  PackManifest other = same;
  std::strcpy(other.id, "bbb222");
  CHECK(compare(held, other) == Freshness::Newer);

  // A pack whose build is unknown is Unknown, never "current". Reporting
  // "you are up to date" without knowing is the lie that keeps a stale pack.
  const PackMeta unknown;
  CHECK(compare(unknown, same) == Freshness::Unknown);

  const PackManifest broken;
  CHECK(compare(held, broken) == Freshness::NoManifest);
  CHECK(compare(unknown, broken) == Freshness::NoManifest);
}

// The wire format is shared with tools_local/trivia/reports.py. A reordering
// here would make a report mean something else on arrival, so the numbers are
// pinned rather than merely used.
void testReasonCodesArePinned() {
  CHECK(static_cast<uint8_t>(Reason::None) == 0);
  CHECK(static_cast<uint8_t>(Reason::Wrong) == 1);
  CHECK(static_cast<uint8_t>(Reason::Nonsense) == 2);
  CHECK(static_cast<uint8_t>(Reason::Giveaway) == 3);
  CHECK(static_cast<uint8_t>(Reason::Ambiguous) == 4);
  CHECK(static_cast<uint8_t>(Reason::Outdated) == 5);
  CHECK(static_cast<uint8_t>(Reason::Broken) == 6);
  CHECK(static_cast<uint8_t>(Reason::Regional) == 7);
  CHECK(static_cast<uint8_t>(Reason::Us) == 8);
  CHECK(static_cast<uint8_t>(Reason::Hard) == 9);
  CHECK(static_cast<uint8_t>(Reason::Easy) == 10);
  CHECK(static_cast<uint8_t>(Reason::Count) == 11);
  CHECK(kReportHeaderBytes == 52);
  CHECK(kReportEntryBytes == 8);

  MemSource card;
  ReportQueue q;
  CHECK(q.open(card, kPack, 50) == QueueOpen::Started);
  // An out-of-range reason must never be stored: it would read back as a
  // different report, or as none at all.
  CHECK(!q.add(1, static_cast<Reason>(200)));
  CHECK(q.count() == 0);

  // Every reason has a wire name, they are all distinct, and none is empty --
  // an empty name would be sent as a field the endpoint refuses, turning one
  // reason into a rejected batch.
  const char* names[static_cast<int>(Reason::Count)] = {};
  for (int i = 0; i < static_cast<int>(Reason::Count); ++i) {
    names[i] = reasonName(static_cast<Reason>(i));
    CHECK(names[i] != nullptr && names[i][0] != '\0');
  }
  for (int i = 0; i < static_cast<int>(Reason::Count); ++i) {
    for (int j = i + 1; j < static_cast<int>(Reason::Count); ++j) {
      CHECK(std::strcmp(names[i], names[j]) != 0);
    }
  }
  // The sentinel must NOT produce a name the endpoint would accept.
  CHECK(std::strcmp(reasonName(Reason::Count), "") == 0);
}

}  // namespace

int main() {
  testQueueBasics();
  testQueueReopens();
  testForeignQueueIsNotRelabelled();
  testSameCountDifferentPack();
  testUnknownBuildQueuesNothing();
  testMarkSentKeepsLateReports();
  testWithdraw();
  testQueueCapDropsTheNewest();
  testTornAndForeignFiles();
  testMeta();
  testManifest();
  testFreshness();
  testReasonCodesArePinned();

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
