// Reading Stats, checked without a card or a clock.
//
// Everything the screen shows is a function of two files and a "today", so the
// rules are asserted here through the same code the device runs: the record
// format, the day a session belongs to, the streak, the week, this year's
// finished list, and what a torn or corrupted file still gives back. The files
// are byte vectors behind the same ByteFile interface the HalFile adapter
// implements, so tearing one is a resize and corrupting one is a byte flip.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ReadingStatsCore.h"

using namespace readingstats;

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

class VectorFile : public ByteFile {
 public:
  std::vector<uint8_t> bytes;
  // A card that refuses to cut a file, and a region it cannot read back.
  bool refuseTruncate = false;
  size_t badFrom = 0;
  size_t badTo = 0;
  size_t reads = 0;
  size_t size() override { return bytes.size(); }
  bool truncate(const size_t length) override {
    if (refuseTruncate || length > bytes.size()) return false;
    bytes.resize(length);
    return true;
  }
  bool readAt(const size_t offset, uint8_t* buffer, const size_t count) override {
    ++reads;
    if (offset + count > bytes.size()) return false;
    if (offset < badTo && badFrom < offset + count) return false;
    std::memcpy(buffer, bytes.data() + offset, count);
    return true;
  }
  bool append(const uint8_t* buffer, const size_t count) override {
    bytes.insert(bytes.end(), buffer, buffer + count);
    return true;
  }
};

constexpr int32_t kUtcPlus2 = 2 * 3600;
constexpr int32_t kUtcMinus5 = -5 * 3600;

// UTC epoch of a local wall-clock time in a zone `offset` seconds east of UTC.
int64_t epochAt(const int y, const int m, const int d, const int hh, const int mm, const int32_t offset) {
  return static_cast<int64_t>(daysFromCivil(y, m, d)) * kSecondsPerDay + hh * 3600 + mm * 60 - offset;
}

// The record ReadingStatsLog.cpp writes for a session, through the same call.
Session sessionAt(const int64_t start, const int32_t offset, const uint32_t seconds, const uint32_t book = 1) {
  return makeSession(start, seconds, offset, book, 5000, false);
}

// False on the first append that did not land; the callers CHECK it once.
bool appendSession(VectorFile& file, const Session& s) {
  uint8_t record[kSessionSize];
  encode(s, record);
  return appendRecord(file, record, kSessionSize);
}

bool appendFinished(VectorFile& file, const int32_t day, const uint32_t book, const char* title) {
  Finished f;
  f.day = day;
  f.book = book;
  std::snprintf(f.title, sizeof(f.title), "%s", title);
  uint8_t record[kFinishedSize];
  encode(f, record);
  return appendRecord(file, record, kFinishedSize);
}

int countSessions(VectorFile& file) {
  return scanSessions(file, [](const Session&, void*) { return true; }, nullptr);
}

// --- Days ----------------------------------------------------------------

void testCalendar() {
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  CHECK(daysFromCivil(2000, 3, 1) == 11017);
  CHECK(weekday(0) == 4);                            // a Thursday
  CHECK(weekday(daysFromCivil(2026, 11, 18)) == 3);  // a Wednesday
  CHECK(weekday(daysFromCivil(2026, 11, 15)) == 0);  // a Sunday
  CHECK(yearOf(daysFromCivil(2026, 12, 31)) == 2026);
  CHECK(yearOf(daysFromCivil(2027, 1, 1)) == 2027);
  // Every day of four centuries, both directions, leap years included.
  bool roundTrips = true;
  for (int32_t day = daysFromCivil(1900, 1, 1); day <= daysFromCivil(2300, 12, 31); ++day) {
    int y = 0, m = 0, d = 0;
    civilFromDays(day, y, m, d);
    if (daysFromCivil(y, m, d) != day) roundTrips = false;
  }
  CHECK(roundTrips);
  // Floor, not truncation: one second before local midnight west of UTC is
  // still the earlier day.
  CHECK(localDay(epochAt(2026, 3, 9, 23, 59, kUtcMinus5) + 59, kUtcMinus5) == daysFromCivil(2026, 3, 9));
  CHECK(localDay(epochAt(2026, 3, 10, 0, 0, kUtcMinus5), kUtcMinus5) == daysFromCivil(2026, 3, 10));
  CHECK(localDay(-1, 0) == -1);
}

// A session that starts before midnight and ends after it belongs to the day it
// started, and stays there.
void testMidnight() {
  const int32_t monday = daysFromCivil(2026, 3, 9);
  const Session late = sessionAt(epochAt(2026, 3, 9, 23, 50, kUtcMinus5), kUtcMinus5, 40 * 60);
  CHECK(late.day == monday);

  VectorFile file;
  CHECK(appendSession(file, late));
  Tally tally(monday + 1);
  load(tally, &file, nullptr);
  CHECK(tally.secondsOn(monday) == 40 * 60);
  CHECK(tally.secondsOn(monday + 1) == 0);
}

// The zone in force when a record was written decides its day, once. Moving to
// another zone afterwards changes what TODAY is and nothing about the past.
void testTimeZoneChange() {
  // 00:30 on the 10th in UTC+2 is 22:30 on the 9th in UTC and 17:30 on the 9th
  // in UTC-5: the same instant, three different calendar days depending on who
  // asks. The reader was in UTC+2, so it was read on the 10th.
  const int64_t start = epochAt(2026, 3, 10, 0, 30, kUtcPlus2);
  const Session s = sessionAt(start, kUtcPlus2, 20 * 60);
  CHECK(s.day == daysFromCivil(2026, 3, 10));
  CHECK(localDay(start, kUtcMinus5) == daysFromCivil(2026, 3, 9));

  VectorFile file;
  CHECK(appendSession(file, s));
  // Later, in UTC-5: today is the 11th there.
  const int32_t today = localDay(epochAt(2026, 3, 11, 9, 0, kUtcMinus5), kUtcMinus5);
  CHECK(today == daysFromCivil(2026, 3, 11));
  Tally tally(today);
  load(tally, &file, nullptr);
  CHECK(tally.secondsOn(daysFromCivil(2026, 3, 10)) == 20 * 60);
  CHECK(tally.secondsOn(daysFromCivil(2026, 3, 9)) == 0);

  // A record whose day is AHEAD of today (written east of a zone the reader
  // later moved west of, within the same instant) is not counted on any day,
  // rather than being folded onto today.
  Tally behind(daysFromCivil(2026, 3, 9));
  load(behind, &file, nullptr);
  CHECK(behind.secondsOn(daysFromCivil(2026, 3, 9)) == 0);
  CHECK(!behind.anySession());
}

// --- What a session becomes ---------------------------------------------

void testWhatASessionBecomes() {
  const int64_t now = 1790000000;
  // An ordinary session, somewhere in the middle of the book.
  Outcome o = outcomeOf(true, now, 4200, 43);
  CHECK(o.session && !o.finished);
  // Read to the last page and closed there: the reader's end percentage is
  // 100 on the last page, even though the page's own start is short of it.
  o = outcomeOf(true, now, 9972, 100);
  CHECK(o.session && o.finished);
  // Turned onto the end-of-book screen.
  o = outcomeOf(true, now, 10000, 100);
  CHECK(o.session && o.finished);
  // The last few pages in a session too short to count as reading time: no
  // session, but the book is still finished.
  o = outcomeOf(false, now, 10000, 100);
  CHECK(!o.session && o.finished);
  // A finished book opened on its last page and closed again: no page turned,
  // nothing counted, nothing finished.
  o = outcomeOf(false, 0, 0, 100);
  CHECK(!o.session && !o.finished);
  // A book opened and left without reading.
  o = outcomeOf(false, 0, 0, 12);
  CHECK(!o.session && !o.finished);
}

// --- Streak and week -----------------------------------------------------

void testStreak() {
  const int32_t today = daysFromCivil(2026, 11, 18);
  auto streakOf = [today](const std::vector<std::pair<int, uint32_t>>& agoSeconds) {
    Tally tally(today);
    for (const auto& [ago, seconds] : agoSeconds) {
      Session s;
      s.day = today - ago;
      s.seconds = seconds;
      tally.addSession(s);
    }
    return tally.streak();
  };

  CHECK(streakOf({}) == 0);
  CHECK(streakOf({{0, 600}}) == 1);
  CHECK(streakOf({{0, 600}, {1, 600}, {2, 600}}) == 3);
  // Today not read yet: the streak ending yesterday is still alive.
  CHECK(streakOf({{1, 600}, {2, 600}}) == 2);
  // ...but one ending the day before yesterday is not.
  CHECK(streakOf({{2, 600}, {3, 600}}) == 0);
  // A gap ends it.
  CHECK(streakOf({{0, 600}, {1, 600}, {3, 600}, {4, 600}}) == 2);
  // A day counts once it holds a whole minute, the minute the chart shows.
  CHECK(streakOf({{0, 59}}) == 0);
  CHECK(streakOf({{0, 60}}) == 1);
  // Several short sessions on one day add up.
  CHECK(streakOf({{0, 30}, {0, 30}}) == 1);
  CHECK(streakOf({{0, 600}, {1, 59}, {2, 600}}) == 1);

  // The window's edge: a streak longer than the window reads as at the cap.
  Tally longRun(today);
  for (int ago = 0; ago < kStreakWindow + 50; ++ago) {
    Session s;
    s.day = today - ago;
    s.seconds = 120;
    longRun.addSession(s);
  }
  CHECK(longRun.streak() == kStreakWindow - 1);
  CHECK(longRun.streakAtCap());
}

void testWeek() {
  // Wednesday the 18th: the week is Sunday the 15th to today.
  const int32_t today = daysFromCivil(2026, 11, 18);
  Tally tally(today);
  auto add = [&tally](const int32_t day, const uint32_t seconds) {
    Session s;
    s.day = day;
    s.seconds = seconds;
    tally.addSession(s);
  };
  add(daysFromCivil(2026, 11, 14), 3600);  // Saturday: last week
  add(daysFromCivil(2026, 11, 15), 600);   // Sunday
  add(daysFromCivil(2026, 11, 17), 900);
  add(today, 300);
  add(today, 300);
  CHECK(tally.weekSeconds() == 2100);
  CHECK(tally.secondsOn(today) == 600);
  CHECK(tally.anyInChart());

  // On a Sunday the week is that one day.
  Tally sunday(daysFromCivil(2026, 11, 15));
  Session s;
  s.day = daysFromCivil(2026, 11, 14);
  s.seconds = 900;
  sunday.addSession(s);
  CHECK(sunday.weekSeconds() == 0);
  CHECK(sunday.anyInChart());
}

// --- The files -----------------------------------------------------------

void testRecordFormat() {
  Session s;
  s.day = daysFromCivil(2026, 2, 3);
  s.start = 1770000000u;
  s.seconds = 1234;
  s.book = 0xDEADBEEF;
  s.endBp = 10000;
  s.finished = true;
  uint8_t record[kSessionSize];
  encode(s, record);
  Session back;
  CHECK(decode(record, back));
  CHECK(back.day == s.day && back.start == s.start && back.seconds == s.seconds && back.book == s.book &&
        back.endBp == s.endBp && back.finished);

  // Every single-byte change is refused: magic, version, payload and seal.
  bool allRefused = true;
  for (size_t i = 0; i < kSessionSize; ++i) {
    uint8_t bad[kSessionSize];
    std::memcpy(bad, record, sizeof(bad));
    bad[i] ^= 0x10;
    Session ignored;
    if (decode(bad, ignored)) allRefused = false;
  }
  CHECK(allRefused);

  Finished f;
  f.day = s.day;
  f.book = 7;
  std::snprintf(f.title, sizeof(f.title), "%s", "Война и мир");
  uint8_t big[kFinishedSize];
  encode(f, big);
  Finished fb;
  CHECK(decode(big, fb));
  CHECK(std::string(fb.title) == "Война и мир" && fb.book == 7 && fb.day == f.day);
  bool bigRefused = true;
  for (size_t i = 0; i < kFinishedSize; ++i) {
    uint8_t bad[kFinishedSize];
    std::memcpy(bad, big, sizeof(bad));
    bad[i] ^= 0x01;
    Finished ignored;
    if (decode(bad, ignored)) bigRefused = false;
  }
  CHECK(bigRefused);
}

// The rule the card depends on: damage costs what was damaged and nothing
// before it, and the next session is still written where it can be read.
void testTornAndCorruptFiles() {
  const int32_t today = daysFromCivil(2026, 11, 18);
  VectorFile file;
  bool appended = true;
  for (int ago = 4; ago >= 0; --ago) {
    Session s;
    s.day = today - ago;
    s.seconds = 600;
    appended = appendSession(file, s) && appended;
  }
  CHECK(appended);
  CHECK(file.size() == 5 * kSessionSize);
  CHECK(countSessions(file) == 5);

  // Power cut mid-append: eleven bytes of a sixth record reached the card.
  uint8_t sixth[kSessionSize];
  Session s6;
  s6.day = today;
  s6.seconds = 300;
  encode(s6, sixth);
  file.bytes.insert(file.bytes.end(), sixth, sixth + 11);
  CHECK(countSessions(file) == 5);
  {
    Tally tally(today);
    load(tally, &file, nullptr);
    CHECK(tally.secondsOn(today) == 600);
    CHECK(tally.streak() == 5);
  }

  // The next session is written on a record boundary, so it -- and every one
  // after it -- is readable.
  size_t torn = 0;
  CHECK(appendRecord(file, sixth, kSessionSize, &torn));
  CHECK(torn == 11);
  CHECK(file.size() == 6 * kSessionSize);
  CHECK(countSessions(file) == 6);
  {
    Tally tally(today);
    load(tally, &file, nullptr);
    CHECK(tally.secondsOn(today) == 900);
  }

  // A whole record gone bad in the middle is skipped, not taken for the end.
  file.bytes[2 * kSessionSize + 9] ^= 0xFF;
  CHECK(countSessions(file) == 5);
  {
    Tally tally(today);
    load(tally, &file, nullptr);
    CHECK(tally.secondsOn(today - 2) == 0);
    CHECK(tally.secondsOn(today - 3) == 600);
    CHECK(tally.secondsOn(today - 4) == 600);
  }

  // A torn FINISHED record is the same rule at its own size.
  VectorFile done;
  CHECK(appendFinished(done, today - 10, 1, "First"));
  CHECK(appendFinished(done, today - 5, 2, "Second"));
  done.bytes.resize(done.bytes.size() + 100, 0xAB);
  Tally tally(today);
  load(tally, nullptr, &done);
  CHECK(tally.books().size() == 2);
  CHECK(appendFinished(done, today - 1, 3, "Third"));
  CHECK(done.size() == 3 * kFinishedSize);
  Tally after(today);
  load(after, nullptr, &done);
  CHECK(after.books().size() == 3);

  // A chunk the card cannot read back costs the records in it, never the ones
  // before it.
  VectorFile sore;
  bool soreAppended = true;
  for (int ago = 39; ago >= 0; --ago) {
    Session s;
    s.day = today - ago;
    s.seconds = 600;
    soreAppended = appendSession(sore, s) && soreAppended;
  }
  CHECK(soreAppended);
  sore.badFrom = sore.size() - 2 * kSessionSize;
  sore.badTo = sore.size();
  const int readable = countSessions(sore);
  CHECK(readable >= 40 - 8 && readable < 40);
  {
    Tally tally(today);
    load(tally, &sore, nullptr);
    CHECK(tally.secondsOn(today - 13) == 600);
    CHECK(tally.secondsOn(today - 20) == 600 || tally.secondsOn(today - 20) == 0);
  }

  // A card that will not cut the torn tail: the next record is padded onto a
  // boundary instead, and everything stays readable.
  VectorFile stubborn;
  Session s1;
  s1.day = today - 1;
  s1.seconds = 600;
  CHECK(appendSession(stubborn, s1));
  stubborn.bytes.insert(stubborn.bytes.end(), sixth, sixth + 7);
  stubborn.refuseTruncate = true;
  CHECK(appendRecord(stubborn, sixth, kSessionSize));
  CHECK(stubborn.size() == 3 * kSessionSize);
  CHECK(countSessions(stubborn) == 2);

  // Junk from end to end reads as nothing, not as a crash or a wrong number.
  VectorFile junk;
  junk.bytes.assign(1000, 0x52);
  CHECK(countSessions(junk) == 0);
  VectorFile empty;
  CHECK(countSessions(empty) == 0);
}

// Opening the app reads from the end and stops once nothing older can change
// what it shows: the fortnight is complete and the streak has ended.
void testLoadStopsOnceSettled() {
  const int32_t today = daysFromCivil(2026, 11, 18);
  auto history = [today](VectorFile& file, const int days, const int gapAgo) {
    bool appended = true;
    for (int ago = days; ago >= 0; --ago) {
      if (ago == gapAgo) continue;
      for (int k = 0; k < 3; ++k) {
        Session s;
        s.day = today - ago;
        s.seconds = 120;
        appended = appendSession(file, s) && appended;
      }
    }
    return appended;
  };

  // Eight years of three sessions a day, the streak broken ten days ago.
  VectorFile broken;
  CHECK(history(broken, 3000, 10));
  Tally tally(today);
  const int read = load(tally, &broken, nullptr);
  CHECK(tally.streak() == 10);
  CHECK(tally.secondsOn(today - 13) == 360);
  CHECK(read >= 3 * 14 && read <= 3 * (14 + kOrderSlackDays + 2));

  // Broken yesterday: still the whole fortnight, for the chart.
  VectorFile yesterday;
  CHECK(history(yesterday, 3000, 1));
  Tally t2(today);
  const int read2 = load(t2, &yesterday, nullptr);
  CHECK(t2.streak() == 1);
  CHECK(t2.secondsOn(today - 13) == 360);
  CHECK(read2 <= 3 * (14 + kOrderSlackDays + 2));

  // Never broken: read to the edge of the window, and the screen says 999+.
  VectorFile unbroken;
  CHECK(history(unbroken, 3000, -1));
  Tally t3(today);
  const int read3 = load(t3, &unbroken, nullptr);
  CHECK(t3.streak() == kStreakWindow - 1);
  CHECK(t3.streakAtCap());
  CHECK(read3 <= 3 * (kStreakWindow + kOrderSlackDays + 2));

  // Past the fortnight too: a twenty-day streak whose records for 16 and 17
  // days ago were written in the wrong order (a move west between them). Read
  // newest first, 17 arrives before 16, and stopping there would end the
  // streak at the day not yet seen.
  VectorFile west;
  bool westAppended = true;
  const int order[] = {25, 24, 23, 22, 21, 19, 18, 16, 17, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  for (const int ago : order) {
    Session s;
    s.day = today - ago;
    s.seconds = 600;
    westAppended = appendSession(west, s) && westAppended;
  }
  CHECK(westAppended);
  Tally t5(today);
  load(t5, &west, nullptr);
  CHECK(t5.streak() == 20);

  // A session out of order by a time-zone change does not cut the read short:
  // yesterday's record written after today's still counts.
  VectorFile shuffled;
  Session a;
  a.day = today - 1;
  a.seconds = 600;
  Session b;
  b.day = today;
  b.seconds = 600;
  CHECK(appendSession(shuffled, b));
  CHECK(appendSession(shuffled, a));
  Tally t4(today);
  load(t4, &shuffled, nullptr);
  CHECK(t4.streak() == 2);
}

// --- This year's finished books -----------------------------------------

void testFinishedThisYear() {
  const int32_t today = daysFromCivil(2026, 11, 18);
  VectorFile done;
  CHECK(appendFinished(done, daysFromCivil(2025, 12, 30), 1, "Last Year's Book"));
  CHECK(appendFinished(done, daysFromCivil(2026, 1, 2), 2, "January"));
  CHECK(appendFinished(done, daysFromCivil(2026, 6, 1), 3, "Re-read Twice"));
  CHECK(appendFinished(done, daysFromCivil(2026, 9, 9), 3, "Re-read Twice"));
  CHECK(appendFinished(done, daysFromCivil(2026, 11, 17), 4, "Yesterday"));

  Tally tally(today);
  load(tally, nullptr, &done);
  const auto& books = tally.books();
  CHECK(books.size() == 3);
  // The year's list is this year only, each book once, newest first, and a
  // book finished twice shows the first time.
  if (books.size() == 3) {
    CHECK(books[0].title == "Yesterday");
    CHECK(books[1].title == "Re-read Twice" && books[1].day == daysFromCivil(2026, 6, 1));
    CHECK(books[2].title == "January");
  }

  CHECK(finishedSince(done, 3, daysFromCivil(2026, 1, 1)));
  CHECK(!finishedSince(done, 1, daysFromCivil(2026, 1, 1)));
  CHECK(finishedSince(done, 1, daysFromCivil(2025, 1, 1)));

  // A record dated before January among this year's (a clock that once ran
  // back) does not hide the ones written before it: the file is read whole.
  VectorFile odd;
  CHECK(appendFinished(odd, daysFromCivil(2026, 1, 5), 10, "A"));
  CHECK(appendFinished(odd, daysFromCivil(2026, 1, 8), 11, "B"));
  CHECK(appendFinished(odd, daysFromCivil(2025, 12, 20), 12, "C"));
  CHECK(appendFinished(odd, daysFromCivil(2026, 1, 12), 13, "D"));
  Tally january(daysFromCivil(2026, 1, 15));
  load(january, nullptr, &odd);
  CHECK(january.books().size() == 3);
  CHECK(finishedSince(odd, 10, daysFromCivil(2026, 1, 1)));

  // On New Year's Day the list starts again.
  Tally newYear(daysFromCivil(2027, 1, 1));
  load(newYear, nullptr, &done);
  CHECK(newYear.books().empty());
}

// --- Titles --------------------------------------------------------------

void testTitles() {
  CHECK(cleanTitle("Moby-Dick; or, The Whale", "/books/x.epub") == "Moby-Dick; or, The Whale");
  // Somebody else's text: controls become spaces, runs collapse, ends trim.
  CHECK(cleanTitle("  The\tSecret\n\nHistory \r", nullptr) == "The Secret History");
  // ...and the controls collapsing whitespace would not catch: C0, DEL, C1.
  CHECK(cleanTitle("A\x01"
                   "B\x7f"
                   "C\xc2\x85"
                   "D",
                   nullptr) == "A B C D");
  // Typographic punctuation folds to what every cut carries.
  CHECK(cleanTitle("Don\xe2\x80\x99t Panic \xe2\x80\x94 A Guide", nullptr) == "Don't Panic -- A Guide");
  // Invalid UTF-8 is dropped, valid non-Latin text kept byte for byte.
  CHECK(cleanTitle("Bad\xff\xfe Bytes", nullptr) == "Bad Bytes");
  CHECK(cleanTitle("\xc3", nullptr).empty());
  CHECK(cleanTitle("Война и мир", nullptr) == "Война и мир");
  CHECK(cleanTitle("ノルウェイの森", nullptr) == "ノルウェイの森");
  // Overlong encodings and surrogates are not UTF-8.
  CHECK(cleanTitle("A\xc0\xaf"
                   "B",
                   nullptr) == "AB");
  CHECK(cleanTitle("A\xed\xa0\x80"
                   "B",
                   nullptr) == "AB");
  // Invisible formatting is dropped.
  CHECK(cleanTitle("Soft\xc2\xadhyphen\xef\xbb\xbf", nullptr) == "Softhyphen");
  // No title at all: the file's own name.
  CHECK(cleanTitle("", "/books/The_Hobbit.epub") == "The Hobbit");
  CHECK(cleanTitle("   ", "/books/dune.epub") == "dune");
  CHECK(cleanTitle(nullptr, "/books/noext") == "noext");

  // Too long for the record: cut at a word, said with "...", never past the cap.
  std::string longTitle;
  while (longTitle.size() < 400) longTitle += "Remembrance of Things Past ";
  const std::string capped = cleanTitle(longTitle.c_str(), nullptr);
  CHECK(capped.size() <= kTitleCapacity);
  CHECK(capped.size() > kTitleCapacity / 2);
  CHECK(capped.compare(capped.size() - 3, 3, "...") == 0);
  // ...at a word: what precedes the "..." is the start of the title, ending
  // where a word of it ended.
  const size_t kept = capped.size() - 3;
  CHECK(longTitle.compare(0, kept, capped, 0, kept) == 0 && longTitle[kept] == ' ');
  // No space to cut at: a codepoint boundary, never half a character.
  std::string cjk;
  while (cjk.size() < 400) cjk += "源氏物語";
  const std::string cappedCjk = cleanTitle(cjk.c_str(), nullptr);
  CHECK(cappedCjk.size() <= kTitleCapacity);
  CHECK(cleanTitle(cappedCjk.c_str(), nullptr) == cappedCjk);
  // A title exactly at the capacity is kept whole.
  const std::string exact(kTitleCapacity, 'x');
  CHECK(cleanTitle(exact.c_str(), nullptr) == exact);
}

// --- Words ---------------------------------------------------------------

void testWords() {
  char buf[32];
  formatDay(daysFromCivil(2026, 3, 12), buf, sizeof(buf));
  CHECK(std::string(buf) == "12 MAR");
  formatDay(daysFromCivil(2026, 12, 1), buf, sizeof(buf));
  CHECK(std::string(buf) == "1 DEC");
  CHECK(weekdayLetter(daysFromCivil(2026, 11, 15)) == 'S');
  CHECK(weekdayLetter(daysFromCivil(2026, 11, 16)) == 'M');
}

}  // namespace

int main() {
  testCalendar();
  testMidnight();
  testWhatASessionBecomes();
  testTimeZoneChange();
  testStreak();
  testWeek();
  testRecordFormat();
  testTornAndCorruptFiles();
  testLoadStopsOnceSettled();
  testFinishedThisYear();
  testTitles();
  testWords();

  std::printf("%s  readingstats: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures == 0 ? 0 : 1;
}
