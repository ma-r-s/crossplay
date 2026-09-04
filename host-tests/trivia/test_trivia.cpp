// The trivia pack reader and round logic, checked without a panel. TriviaCore
// is freestanding C++17, so the format, the chooser and the option shuffle are
// all testable on a laptop.
//
// The pack under test is BUILT HERE, byte by byte, rather than loaded from a
// file the suite would have to ship. That way the test pins the format itself:
// if the writer in tools_local/trivia/pack_format.py and this reader ever
// disagree, one of them has to be edited to make the other pass.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "TriviaCore.h"

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

// --- a ByteSource over a vector, which is what the device's HalStorage-backed
// source has to behave like ---
class MemorySource final : public WritableByteSource {
 public:
  explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  bool read(uint32_t offset, void* dst, uint32_t length) override {
    if (static_cast<uint64_t>(offset) + length > bytes_.size()) return false;  // never a short read
    std::memcpy(dst, bytes_.data() + offset, length);
    return true;
  }
  bool write(uint32_t offset, const void* src, uint32_t length) override {
    if (static_cast<uint64_t>(offset) + length > bytes_.size()) return false;
    std::memcpy(bytes_.data() + offset, src, length);
    ++writes;
    return true;
  }
  bool flush() override { return true; }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }
  std::vector<uint8_t>& raw() { return bytes_; }
  int writes = 0;

 private:
  std::vector<uint8_t> bytes_;
};

struct Item {
  std::string clue, answer;
  uint8_t difficulty;
  uint16_t year;
  std::vector<std::string> alts, wrong;
};

void put16(std::vector<uint8_t>& v, uint16_t n) {
  v.push_back(n & 0xFF);
  v.push_back((n >> 8) & 0xFF);
}
void put32(std::vector<uint8_t>& v, uint32_t n) {
  for (int i = 0; i < 4; ++i) v.push_back((n >> (8 * i)) & 0xFF);
}

std::vector<uint8_t> record(const Item& it) {
  std::vector<uint8_t> r;
  r.push_back(it.difficulty);
  put16(r, it.year);
  r.push_back(static_cast<uint8_t>(it.alts.size()));
  r.push_back(static_cast<uint8_t>(it.wrong.size()));
  std::vector<std::string> fields{it.clue, it.answer};
  for (const auto& a : it.alts) fields.push_back(a);
  for (const auto& w : it.wrong) fields.push_back(w);
  for (const auto& f : fields) {
    put16(r, static_cast<uint16_t>(f.size()));
    r.insert(r.end(), f.begin(), f.end());
  }
  return r;
}

// Exactly the layout tools_local/trivia/pack_format.py writes.
std::vector<uint8_t> buildPack(const std::vector<Item>& items) {
  std::vector<std::vector<uint8_t>> recs;
  for (const auto& it : items) recs.push_back(record(it));
  std::vector<uint8_t> out;
  const char magic[8] = {'X', 'T', 'R', 'I', 'V', 'I', 'A', 0};
  out.insert(out.end(), magic, magic + 8);
  put16(out, 1);
  out.push_back(0);
  out.push_back(0);
  put32(out, static_cast<uint32_t>(recs.size()));
  uint32_t off = 0;
  for (const auto& r : recs) {
    put32(out, off);
    off += static_cast<uint32_t>(r.size());
  }
  put32(out, off);  // sentinel
  for (const auto& r : recs) out.insert(out.end(), r.begin(), r.end());
  return out;
}

std::vector<Item> sampleItems(const int n) {
  std::vector<Item> items;
  for (int i = 0; i < n; ++i) {
    Item it;
    it.clue = "Clue number " + std::to_string(i) + " about this thing";
    it.answer = "Answer" + std::to_string(i);
    it.difficulty = static_cast<uint8_t>(1 + (i % 5));
    it.year = static_cast<uint16_t>(1990 + (i % 30));
    if (i % 3 == 0) it.alts = {"The Answer" + std::to_string(i)};
    if (i % 2 == 0) {
      for (int w = 0; w < 6; ++w) it.wrong.push_back("Wrong" + std::to_string(i) + "_" + std::to_string(w));
    }
    items.push_back(it);
  }
  return items;
}

// --- format ---------------------------------------------------------------
void testFormat() {
  const auto items = sampleItems(40);
  MemorySource src(buildPack(items));
  Pack pack;
  CHECK(pack.open(src));
  CHECK(pack.count() == 40);

  for (uint32_t i = 0; i < pack.count(); ++i) {
    Question q;
    CHECK(pack.read(i, q));
    CHECK(std::strcmp(q.clue(), items[i].clue.c_str()) == 0);
    CHECK(std::strcmp(q.answer(), items[i].answer.c_str()) == 0);
    CHECK(q.difficulty() == items[i].difficulty);
    CHECK(q.year() == items[i].year);
    CHECK(q.alternateCount() == static_cast<int>(items[i].alts.size()));
    CHECK(q.distractorCount() == static_cast<int>(items[i].wrong.size()));
    for (int a = 0; a < q.alternateCount(); ++a) {
      CHECK(std::strcmp(q.alternate(a), items[i].alts[a].c_str()) == 0);
    }
  }

  Question q;
  CHECK(!pack.read(40, q));  // past the end
  CHECK(!pack.read(9999, q));
}

void testRejectsBadFiles() {
  const auto items = sampleItems(4);

  auto bad = buildPack(items);
  bad[0] = 'Y';  // wrong magic
  MemorySource m1(bad);
  Pack p1;
  CHECK(!p1.open(m1));

  bad = buildPack(items);
  bad[8] = 99;  // unsupported version
  MemorySource m2(bad);
  Pack p2;
  CHECK(!p2.open(m2));

  bad = buildPack(items);
  bad.resize(20);  // truncated before the index ends
  MemorySource m3(bad);
  Pack p3;
  CHECK(!p3.open(m3));

  // A pack torn mid-write: header and index intact, blob short. Without the
  // sentinel check this opens happily and the tail records read as whatever
  // was on the card before -- corrupt questions rather than a missing pack.
  const auto whole = buildPack(sampleItems(30));
  for (size_t cut : {size_t(4), size_t(64), size_t(400)}) {
    auto torn = whole;
    torn.resize(whole.size() - cut);
    MemorySource m(torn);
    Pack p;
    CHECK(!p.open(m));
  }
  MemorySource intact(whole);
  Pack good;
  CHECK(good.open(intact));  // and the untorn one still opens

  // A record claiming to be larger than the buffer must be REJECTED, never
  // truncated: half a UTF-8 sequence renders as garbage and gets blamed on the
  // font rather than on the file.
  std::vector<Item> huge = sampleItems(2);
  huge[0].clue = std::string(kMaxRecordBytes + 50, 'x');
  MemorySource m4(buildPack(huge));
  Pack p4;
  CHECK(p4.open(m4));
  Question q;
  CHECK(!p4.read(0, q));
  CHECK(p4.read(1, q));  // the neighbour still reads
}

// --- state ----------------------------------------------------------------
void testState() {
  MemorySource src(std::vector<uint8_t>(50, 0));
  PackState state;
  CHECK(state.open(src, 50));
  CHECK(state.flags(3) == 0);
  CHECK(!state.seen(3));

  CHECK(state.setFlag(3, kSeen));
  CHECK(state.seen(3));
  CHECK(!state.flagged(3));
  CHECK(state.setFlag(3, kFlagged));
  CHECK(state.seen(3) && state.flagged(3));
  CHECK(state.flags(4) == 0);  // neighbours untouched

  const int before = src.writes;
  CHECK(state.setFlag(3, kSeen));  // already set
  CHECK(src.writes == before);     // and therefore writes nothing

  // A state file shorter than the pack means the pack was replaced under it.
  MemorySource small(std::vector<uint8_t>(10, 0));
  PackState mismatched;
  CHECK(!mismatched.open(small, 50));

  // And so does a LONGER one, which is the half that had no coverage. The pack
  // is addressed by index and the state file is one byte per index, so the two
  // lengths are the same fact written twice; any disagreement means the pack
  // under the state file changed. A SHORTER file was rejected and a longer one
  // was accepted, on the reasoning that every index still had a byte -- but the
  // bytes describe questions that are no longer at those indices. A FLAGGED bit
  // landing on an arbitrary question hides it from every draw with nothing on
  // screen and no way for a player to clear it.
  //
  // Reachable as soon as a pack SHRINKS, which is what a rated pack does to the
  // 50,000 it replaces.
  MemorySource oversized(std::vector<uint8_t>(90, 0));
  PackState stale;
  CHECK(!stale.open(oversized, 50));
}

// --- chooser --------------------------------------------------------------
// Consecutive draws must not be ADJACENT RECORDS. The chooser used to walk
// forward from the previous pick, and the pack is built from a Jeopardy archive
// ordered by game and then category -- so a real round served twelve "this
// country" clues answering Italy every time, with the same four distractors. A
// player who knew the first answer got the other eleven free.
//
// Asserted as a property of the sequence rather than of one draw: with a fresh
// random start each call, adjacency happens at chance (about 1 in count) and
// cannot be the rule. Walking forward makes EVERY gap exactly 1.
// The counts must SURVIVE A REOPEN. They were maintained only in setFlag, so
// every boot started at zero and the front door reported the current session
// rather than the pack -- which renders perfectly plausibly and is why it took a
// second pair of eyes on the file rather than on the screen.
void testStateCountsSurviveReopen() {
  constexpr int kCount = 64;
  const auto items = sampleItems(kCount);
  MemorySource packSrc(buildPack(items));
  MemorySource stateSrc(std::vector<uint8_t>(kCount, 0));
  Pack pack;
  CHECK(pack.open(packSrc));

  {
    PackState state;
    CHECK(state.open(stateSrc, kCount));
    CHECK(state.seenCount() == 0);
    CHECK(state.flaggedCount() == 0);
    state.setFlag(3, kSeen);
    state.setFlag(4, kSeen);
    state.setFlag(4, kSeen);  // again: must not double-count
    state.setFlag(9, kFlagged);
    CHECK(state.seenCount() == 2);
    CHECK(state.flaggedCount() == 1);
  }

  // A fresh PackState over the SAME bytes, as a reboot would build.
  PackState reopened;
  CHECK(reopened.open(stateSrc, kCount));
  CHECK(reopened.seenCount() == 2);
  CHECK(reopened.flaggedCount() == 1);
  CHECK(reopened.count() == kCount);
}

void testChooserDoesNotServeNeighbours() {
  constexpr int kCount = 200;
  const auto items = sampleItems(kCount);
  MemorySource packSrc(buildPack(items));
  MemorySource stateSrc(std::vector<uint8_t>(kCount, 0));
  Pack pack;
  PackState state;
  CHECK(pack.open(packSrc));
  CHECK(state.open(stateSrc, kCount));

  Rng rng(11);
  Chooser chooser;
  chooser.begin(pack, state, rng);

  constexpr int kDraws = 40;
  uint32_t previous = 0;
  int adjacent = 0;
  for (int i = 0; i < kDraws; ++i) {
    uint32_t idx = 0;
    CHECK(chooser.next(idx, false, 0));
    if (i > 0 && idx == (previous + 1) % kCount) ++adjacent;
    previous = idx;
    state.setFlag(idx, kSeen);
  }

  // Forward-walking scores 39 here. Chance is well under one. Three leaves room
  // for the scan stepping over a seen record onto its neighbour without letting
  // the old behaviour through.
  CHECK(adjacent <= 3);
}

void testChooser() {
  const auto items = sampleItems(60);
  MemorySource packSrc(buildPack(items));
  MemorySource stateSrc(std::vector<uint8_t>(60, 0));
  Pack pack;
  PackState state;
  CHECK(pack.open(packSrc));
  CHECK(state.open(stateSrc, 60));

  Rng rng(7);
  Chooser chooser;
  chooser.begin(pack, state, rng);

  // Unseen preferred: 60 draws must be 60 distinct questions.
  bool drawn[60] = {};
  int distinct = 0;
  for (int i = 0; i < 60; ++i) {
    uint32_t idx = 0;
    CHECK(chooser.next(idx, false, 0));
    if (!drawn[idx]) {
      drawn[idx] = true;
      ++distinct;
    }
    state.setFlag(idx, kSeen);
  }
  CHECK(distinct == 60);

  // Exhausted, it must keep dealing rather than dead-end.
  uint32_t idx = 0;
  CHECK(chooser.next(idx, false, 0));

  // Flagged questions are never served again.
  MemorySource s2(std::vector<uint8_t>(60, 0));
  PackState st2;
  CHECK(st2.open(s2, 60));
  for (uint32_t i = 0; i < 59; ++i) st2.setFlag(i, kFlagged);
  Chooser c2;
  Rng r2(3);
  c2.begin(pack, st2, r2);
  for (int i = 0; i < 20; ++i) {
    uint32_t got = 0;
    CHECK(c2.next(got, false, 0));
    CHECK(got == 59);
  }

  // Everything flagged: no question, and no infinite loop.
  MemorySource s3(std::vector<uint8_t>(60, 0));
  PackState st3;
  CHECK(st3.open(s3, 60));
  for (uint32_t i = 0; i < 60; ++i) st3.setFlag(i, kFlagged);
  Chooser c3;
  Rng r3(3);
  c3.begin(pack, st3, r3);
  uint32_t none = 0;
  CHECK(!c3.next(none, false, 0));

  // Solo mode may only be handed questions that carry distractors.
  MemorySource s4(std::vector<uint8_t>(60, 0));
  PackState st4;
  CHECK(st4.open(s4, 60));
  Chooser c4;
  Rng r4(11);
  c4.begin(pack, st4, r4);
  for (int i = 0; i < 40; ++i) {
    uint32_t got = 0;
    CHECK(c4.next(got, true, 0));
    Question q;
    CHECK(pack.read(got, q));
    CHECK(q.playableAsChoice());
    st4.setFlag(got, kSeen);
  }

  // And a requested difficulty is honoured.
  MemorySource s5(std::vector<uint8_t>(60, 0));
  PackState st5;
  CHECK(st5.open(s5, 60));
  Chooser c5;
  Rng r5(5);
  c5.begin(pack, st5, r5);
  for (int i = 0; i < 10; ++i) {
    uint32_t got = 0;
    CHECK(c5.next(got, false, 4));
    Question q;
    CHECK(pack.read(got, q));
    CHECK(q.difficulty() == 4);
    st5.setFlag(got, kSeen);
  }
}

// --- options --------------------------------------------------------------
void testChoices() {
  const auto items = sampleItems(20);
  MemorySource src(buildPack(items));
  Pack pack;
  CHECK(pack.open(src));

  Question q;
  CHECK(pack.read(0, q));  // even index, so it carries distractors
  CHECK(q.playableAsChoice());

  Rng rng(42);
  int position[kOptions] = {};
  for (int trial = 0; trial < 4000; ++trial) {
    Choices c;
    CHECK(buildChoices(q, rng, c));
    CHECK(std::strcmp(c.option[c.correct], q.answer()) == 0);
    for (int i = 0; i < kOptions; ++i) {
      CHECK(c.option[i] != nullptr && c.option[i][0] != '\0');
      for (int j = i + 1; j < kOptions; ++j) CHECK(std::strcmp(c.option[i], c.option[j]) != 0);
    }
    ++position[c.correct];
  }
  // The answer must not settle into a slot. The corpora this pack is built
  // from leak exactly this: 28% on A/B against a 25% baseline.
  for (int i = 0; i < kOptions; ++i) {
    CHECK(position[i] > 4000 / kOptions - 200);
    CHECK(position[i] < 4000 / kOptions + 200);
  }

  Question quizmasterOnly;
  CHECK(pack.read(1, quizmasterOnly));  // odd index, no distractors
  CHECK(!quizmasterOnly.playableAsChoice());
  Choices unused;
  CHECK(!buildChoices(quizmasterOnly, rng, unused));
}

// --- options at the pack builder's boundary shape -------------------------
// assemble_pack.py (tools_local/trivia/assemble_pack.py, STORED = 3) writes
// exactly THREE distractors per multiple-choice question, and omits the `w`
// key entirely when it cannot find three sound ones. Both shapes are new: the
// shipped pack has always carried six, so every existing case above exercises
// n == 6 or n == 0 and nothing in between.
//
// n == 3 is the exact boundary of buildChoices' partial Fisher-Yates. The draw
// loop runs kOptions - 1 == 3 times over a pool of n, so its last step asks for
// rng.below(n - i) == below(0) at n == 2 and below(1) at n == 3. At n == 3 the
// partial shuffle is therefore a COMPLETE one and every stored distractor must
// be used exactly once; one fewer and the loop would read pool[2], which was
// never initialised. playableAsChoice() is the only thing standing between the
// pack and that read, so it is asserted here as load-bearing, not cosmetic.
void testChoicesAtThreeOptions() {
  std::vector<Item> items(3);
  // 0: the assemble_pack.py shape -- exactly three distractors.
  items[0].clue = "Clue about the thing with exactly three wrong answers";
  items[0].answer = "Correct";
  items[0].difficulty = 3;
  items[0].year = 2001;
  items[0].wrong = {"WrongOne", "WrongTwo", "WrongThree"};
  // 1: the read-aloud shape -- no `w` key at all.
  items[1].clue = "Clue the builder could not find three sound options for";
  items[1].answer = "Unopposed";
  items[1].difficulty = 3;
  items[1].year = 2002;
  // 2: the legacy six, so this test would notice if it regressed.
  items[2].clue = "Clue carrying the six distractors the shipped pack has";
  items[2].answer = "Sixfold";
  items[2].difficulty = 3;
  items[2].year = 2003;
  for (int w = 0; w < 6; ++w) items[2].wrong.push_back("Six" + std::to_string(w));

  MemorySource src(buildPack(items));
  Pack pack;
  CHECK(pack.open(src));

  Question three;
  CHECK(pack.read(0, three));
  CHECK(three.distractorCount() == 3);
  CHECK(three.playableAsChoice());  // 3 >= kOptions - 1, the boundary itself

  // nullptr-safe compare: a missing slot must print FAIL, not segfault the
  // suite before it can say which assertion went red.
  const auto eq = [](const char* a, const char* b) { return a != nullptr && b != nullptr && std::strcmp(a, b) == 0; };

  Rng rng(7);
  int position[kOptions] = {};
  for (int trial = 0; trial < 4000; ++trial) {
    Choices c;
    CHECK(buildChoices(three, rng, c));

    // Every slot is filled. An empty slot is the failure a player sees.
    for (int i = 0; i < kOptions; ++i) CHECK(c.option[i] != nullptr && c.option[i][0] != '\0');

    // No option repeats.
    for (int i = 0; i < kOptions; ++i) {
      for (int j = i + 1; j < kOptions; ++j) CHECK(!eq(c.option[i], c.option[j]));
    }

    // The answer is offered exactly once, and c.correct points at it.
    int answers = 0;
    for (int i = 0; i < kOptions; ++i) {
      if (eq(c.option[i], three.answer())) ++answers;
    }
    CHECK(answers == 1);
    CHECK(c.correct >= 0 && c.correct < kOptions);
    CHECK(eq(c.option[c.correct], three.answer()));

    // At exactly three stored, the draw has no freedom: all three must appear.
    // A loop bound that stopped one short would silently leave a distractor
    // out and repeat or blank a slot, and only this assertion sees it.
    for (int d = 0; d < three.distractorCount(); ++d) {
      int seen = 0;
      for (int i = 0; i < kOptions; ++i) {
        if (eq(c.option[i], three.distractor(d))) ++seen;
      }
      CHECK(seen == 1);
    }
    if (c.correct >= 0 && c.correct < kOptions) ++position[c.correct];
  }
  // The final shuffle still moves the answer off slot zero. With only one legal
  // option set, position is the ONLY thing left that can be wrong at n == 3.
  for (int i = 0; i < kOptions; ++i) {
    CHECK(position[i] > 4000 / kOptions - 200);
    CHECK(position[i] < 4000 / kOptions + 200);
  }

  // The `w`-absent shape is excluded cleanly: refused outright, not drawn as a
  // partial set, and the Choices the caller passed in is left untouched rather
  // than half-filled.
  Question absent;
  CHECK(pack.read(1, absent));
  CHECK(absent.distractorCount() == 0);
  CHECK(!absent.playableAsChoice());
  Choices untouched;
  CHECK(!buildChoices(absent, rng, untouched));
  for (int i = 0; i < kOptions; ++i) CHECK(untouched.option[i] == nullptr);

  // And solo mode never reaches it: the chooser filters it out, so the
  // discarded return value of buildChoices in TriviaActivity::deal cannot bite.
  MemorySource st(std::vector<uint8_t>(3, 0));
  PackState state;
  CHECK(state.open(st, 3));
  Chooser chooser;
  Rng cr(19);
  chooser.begin(pack, state, cr);
  for (int i = 0; i < 200; ++i) {
    uint32_t got = 0;
    if (!chooser.next(got, true, 0)) break;
    CHECK(got != 1);  // the read-aloud record is never served to solo mode
    Question q;
    CHECK(pack.read(got, q));
    CHECK(q.playableAsChoice());
  }

  // The legacy six still works, so this file covers 6, 3 and 0 together.
  Question six;
  CHECK(pack.read(2, six));
  CHECK(six.distractorCount() == 6);
  Choices sc;
  CHECK(buildChoices(six, rng, sc));
  for (int i = 0; i < kOptions; ++i) CHECK(sc.option[i] != nullptr && sc.option[i][0] != '\0');
}

void testAnswerMatching() {
  std::vector<Item> items(1);
  items[0].clue = "This is a clue about this thing";
  items[0].answer = "Netherlands";
  items[0].difficulty = 1;
  items[0].year = 2000;
  items[0].alts = {"the Netherlands", "Holland"};
  MemorySource src(buildPack(items));
  Pack pack;
  CHECK(pack.open(src));
  Question q;
  CHECK(pack.read(0, q));

  CHECK(answerMatches(q, "Netherlands"));
  CHECK(answerMatches(q, "netherlands"));  // case is not the player's problem
  CHECK(answerMatches(q, "the Netherlands"));
  CHECK(answerMatches(q, "HOLLAND"));
  CHECK(!answerMatches(q, "Belgium"));
  CHECK(!answerMatches(q, "Nether"));  // a prefix is not an answer
  CHECK(!answerMatches(q, ""));
}

void testRngIsDeterministic() {
  Rng a(12345), b(12345);
  for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
  Rng c(1);
  bool moved = false;
  const uint32_t first = c.next();
  for (int i = 0; i < 50; ++i) {
    if (c.next() != first) moved = true;
  }
  CHECK(moved);
  Rng d(9);
  for (int i = 0; i < 500; ++i) CHECK(d.below(7) < 7);
  CHECK(Rng(0).next() != 0);  // a zero seed must not lock the generator
}

// The card-room decision. It lives in TriviaCore precisely so it can be tested:
// TriviaActivity includes WiFi.h and cannot be built on the host at all, so the
// same logic inside the activity would be untestable by construction.
void testRoomFor() {
  using trivia::Room;
  const uint64_t floor = trivia::kPackFreeFloorBytes;

  // A failed query is UNKNOWN, whatever byte count came back with it. This is
  // the whole point: sdUsedBytes() reports a failed walk as 0 used, so a caller
  // deriving free space would see an EMPTY card and write. Zero free plus a
  // failed query must never read as Ok, and must never read as TooSmall either
  // -- "we could not tell" is not "it is full".
  CHECK(trivia::roomFor(false, 0, floor) == Room::Unknown);
  CHECK(trivia::roomFor(false, floor * 4, floor) == Room::Unknown);
  CHECK(trivia::roomFor(false, floor - 1, floor) == Room::Unknown);

  // A successful query decides on the number alone.
  CHECK(trivia::roomFor(true, floor, floor) == Room::Ok);  // exactly the floor fits
  CHECK(trivia::roomFor(true, floor + 1, floor) == Room::Ok);
  CHECK(trivia::roomFor(true, floor - 1, floor) == Room::TooSmall);
  CHECK(trivia::roomFor(true, 0, floor) == Room::TooSmall);  // a genuinely empty-of-room card

  // The floor must leave real headroom above today's pack, or it is not a floor
  // at all -- it is the pack size wearing a different name, and the next pack
  // that grows past it fills the card silently.
  constexpr uint64_t kPackTodayBytes = 6510000;  // ~6.21MB, measured 2026-08-31
  CHECK(floor > kPackTodayBytes);
  CHECK(floor - kPackTodayBytes > 1024u * 1024u);

  // 64-bit throughout: a 64GB card's free space overflows uint32 and must not
  // wrap into TooSmall.
  CHECK(trivia::roomFor(true, 64ull * 1024 * 1024 * 1024, floor) == Room::Ok);
}

}  // namespace

int main() {
  testFormat();
  testRejectsBadFiles();
  testState();
  testChooser();
  testChooserDoesNotServeNeighbours();
  testStateCountsSurviveReopen();
  testChoices();
  testChoicesAtThreeOptions();
  testAnswerMatching();
  testRngIsDeterministic();
  testRoomFor();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
