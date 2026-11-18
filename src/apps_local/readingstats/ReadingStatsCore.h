#pragma once

// Reading Stats: what the reader records, how it is kept, and what it adds up
// to. Freestanding C++17 -- no Arduino, no renderer, no SD card -- so every rule
// here runs in host-tests/readingstats on a laptop.
//
// ---------------------------------------------------------------------------
// Where the numbers come from
//
// The reader already measures a session (src/activities/reader/ReaderSession.h):
// seconds of dwell between forward page turns, a start time from the trusted
// clock, and how far through the book it ended. ReaderActivity hands each one
// to readingstats::recordSession() through one fork-local call in
// flushReaderSession(); ReadingStatsLog.cpp turns it into the records below.
// Nothing in the reader's own screens changes.
//
// Two files on the card, both under /.crosspoint/ and neither inside a book's
// cache, so Settings > Clear cache leaves them alone:
//
//   reading-sessions.dat   one 24-byte record per session
//   reading-finished.dat   one 144-byte record per book finished, with its title
//
// Both are append-only runs of fixed-size records, each carrying a magic byte,
// a version and a CRC. Four rules make a damaged card cost the damage and no
// more:
//
//   * a file whose length is not a whole number of records has a torn tail --
//     power cut mid-append -- and the partial record is simply not read;
//   * the next append cuts that tail off first (or pads it out, where the card
//     will not truncate), so the new record starts on a record boundary and
//     everything after the tear stays readable;
//   * a whole record that fails its CRC is skipped, never treated as the end;
//   * a chunk the card cannot read is stepped over, never treated as the end.
//
// THE DAY IS DECIDED WHEN THE RECORD IS WRITTEN, with the time zone in force at
// that moment, and stored as a day number. A session belongs to the local day
// it STARTED on; a finished book to the day its record is written, which is
// the day the end was reached. Nothing re-derives either later, so moving to
// another time zone, or reading across midnight, never moves a past record to a
// different day.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace readingstats {

// --- Days --------------------------------------------------------------------
// A day is a count of days since 1970-01-01 in LOCAL time. Proleptic Gregorian,
// Howard Hinnant's civil-from-days, so it needs no table and no time.h.

constexpr int32_t kSecondsPerDay = 86400;

int32_t daysFromCivil(int year, int month, int day);
void civilFromDays(int32_t days, int& year, int& month, int& day);
// 0 = Sunday ... 6 = Saturday. The fork's calendars run Sunday first
// (Connections, Wordle), so a week here does too.
int weekday(int32_t days);
// The local day containing `utcEpoch` under a zone `utcOffsetSeconds` east of
// UTC. Floors, so 23:59 local stays on its own day whatever the sign.
int32_t localDay(int64_t utcEpoch, int32_t utcOffsetSeconds);
int yearOf(int32_t days);

// --- Books -------------------------------------------------------------------

// Which book a record is about: FNV-1a over the path on the card. The path is
// what the reader opens a book by; a renamed file reads as a different book,
// which is also what the reader's own progress cache does.
uint32_t bookId(const char* path);

// The longest title a finished record holds, in bytes of UTF-8. Sized by what
// the screen can show rather than the other way round: 128 bytes of ordinary
// words -- English, accented Spanish, German compounds -- is the most that
// always fits three lines of the smallest cut in a list row, and
// host-tests/readingstats measures exactly that in the real face. A record
// that held more would hand the screen a title it could only cut.
constexpr size_t kTitleCapacity = 128;

// A book's title as it may be stored and drawn. Titles come from EPUB metadata,
// which is somebody else's text: this drops invalid UTF-8 and control
// characters, folds typographic punctuation to the ASCII spelling
// (utf8FoldTypography), collapses runs of whitespace and trims. An empty result
// falls back to the file's name without its extension. A title past
// kTitleCapacity is cut at a word and ends in "..." -- the record has a size,
// and saying a title was shortened is better than cutting it mid-letter.
std::string cleanTitle(const char* raw, const char* path);

// --- Records -----------------------------------------------------------------

constexpr size_t kSessionSize = 24;
constexpr size_t kFinishedSize = 144;  // 12 of header, the title, 2 spare, the CRC

struct Session {
  int32_t day = 0;       // local day it started on, fixed when written
  uint32_t start = 0;    // UTC epoch seconds it started at
  uint32_t seconds = 0;  // reading seconds, as ReaderSession counts them
  uint32_t book = 0;     // bookId()
  uint16_t endBp = 0;    // progress at the end, 0..10000
  bool finished = false;
};

struct Finished {
  int32_t day = 0;  // local day of the session that reached the end
  uint32_t book = 0;
  char title[kTitleCapacity + 1] = {};
};

// The record for one reading session. `utcOffsetSeconds` is the zone in force
// when the record is written; the day is the local day of `startUtc` under it,
// and is never computed again.
Session makeSession(int64_t startUtc, uint32_t seconds, int32_t utcOffsetSeconds, uint32_t book, uint16_t endBp,
                    bool finished);

// What one session the reader hands over becomes.
//
// `counted` is ReaderSession::isEmitWorthy() -- ten seconds of reading against a
// trusted clock -- and decides the session record. `startTime` is set by the
// first page turn the session counted. `endBp` is the furthest the session's
// counted pages reached, and `endPercent` the reader's own end-of-page progress
// (ScreenshotInfo::endProgressPercent), which is 100 on the book's last page
// and on its end-of-book screen.
//
// The book is finished when the reader says it is at the end AND a page turned
// in this session was read: opening a finished book and closing it again turns
// no page, so it finishes nothing; a last session too short to count as reading
// time still finishes the book.
struct Outcome {
  bool session = false;
  bool finished = false;
};
Outcome outcomeOf(bool counted, int64_t startTime, uint16_t endBp, int endPercent);

// Little-endian, magic + version + CRC-16. decode() refuses anything that is
// not exactly what encode() would have written.
void encode(const Session& session, uint8_t out[kSessionSize]);
bool decode(const uint8_t* in, Session& session);
void encode(const Finished& finished, uint8_t out[kFinishedSize]);
bool decode(const uint8_t* in, Finished& finished);

// --- Files -------------------------------------------------------------------
// The two operations the card does, written once against this interface so the
// host suite runs the exact code the device runs. ReadingStatsLog.cpp adapts a
// HalFile; the tests adapt a byte vector they can tear and corrupt.

class ByteFile {
 public:
  virtual ~ByteFile() = default;
  virtual size_t size() = 0;
  virtual bool truncate(size_t length) = 0;
  // Exactly `count` bytes at `offset`, or false.
  virtual bool readAt(size_t offset, uint8_t* buffer, size_t count) = 0;
  // `count` bytes at the end of the file; false unless all of them landed.
  virtual bool append(const uint8_t* buffer, size_t count) = 0;
};

// The length of the file's whole records. Anything past it is a torn append.
constexpr size_t wholeLength(const size_t fileSize, const size_t recordSize) {
  return fileSize - fileSize % recordSize;
}

// Append one record. A torn tail is cut off first, so the record starts on a
// boundary; `tornBytes` (optional) reports how much was dropped.
bool appendRecord(ByteFile& file, const uint8_t* record, size_t recordSize, size_t* tornBytes = nullptr);

// Sessions are appended in time order and the trusted clock never runs
// backwards, so a session's day can only be behind an earlier record's by a
// time-zone change: under two days. Reading newest first, everything more than
// this after the record in hand has already been seen.
constexpr int32_t kOrderSlackDays = 3;

// Every valid record, newest first, until `fn` returns false. Returns how many
// valid records were read.
using SessionFn = bool (*)(const Session& session, void* context);
int scanSessions(ByteFile& file, SessionFn fn, void* context);
// Every valid finished record, newest first: the file is small, so always all.
using FinishedFn = void (*)(const Finished& finished, void* context);
int scanFinished(ByteFile& file, FinishedFn fn, void* context);

// Has `book` a finished record on or after `fromDay`? So reaching the end of a
// book twice in one year writes it down once.
bool finishedSince(ByteFile& file, uint32_t book, int32_t fromDay);

// --- What it adds up to ------------------------------------------------------

constexpr int kChartDays = 14;
// A day counts toward the streak once it holds a whole minute: the same minute
// the chart shows, so a day the chart draws as nothing never props up a streak.
constexpr uint32_t kCountedSeconds = 60;
// How far back the streak can see. Past it the screen says "999+".
constexpr int kStreakWindow = 1000;
// The most finished books one year's list keeps.
constexpr int kMaxBooks = 400;

struct Book {
  int32_t day = 0;
  uint32_t book = 0;
  std::string title;
};

// Everything the screen shows, built from the records by one pass over each
// file. 4KB, so the activity holds it on the heap.
class Tally {
 public:
  explicit Tally(int32_t today);

  void addSession(const Session& session);
  void addFinished(const Finished& finished);
  // Orders the books newest first. load() calls it; a test adding records by
  // hand calls it before reading books().
  void settle();

  int32_t today() const { return today_; }
  int year() const { return yearOf(today_); }
  // Reading newest first and now at a record dated `day`: can anything older
  // still change what this tally shows? No once the chart's fortnight is
  // complete and the streak has ended on a complete day (or reached the edge
  // of its window). This is what keeps opening the app quick however long the
  // history is: a reader with a ten-day streak reads about three weeks back.
  bool settledBefore(int32_t day) const;

  // Seconds read on `day`; 0 outside the window.
  uint32_t secondsOn(int32_t day) const;
  // Sunday through today.
  uint32_t weekSeconds() const;
  // Consecutive counted days ending today -- or yesterday, while today has not
  // been read yet: a streak is not broken until the day is over.
  int streak() const;
  bool streakAtCap() const { return streak() >= kStreakWindow - 1; }
  // Anything at all in the chart's fortnight.
  bool anyInChart() const;
  // Any session ever, within the window.
  bool anySession() const { return sessions_ > 0; }

  // This year's finished books, newest first, each book once (its first finish
  // this year).
  const std::vector<Book>& books() const { return books_; }

 private:
  bool counted(int32_t day) const { return secondsOn(day) >= kCountedSeconds; }

  int32_t today_;
  int sessions_ = 0;
  uint32_t seconds_[kStreakWindow] = {};  // [0] is today, [i] is i days ago
  std::vector<Book> books_;
};

// Read both files into `tally`; returns how many valid records were read.
int load(Tally& tally, ByteFile* sessions, ByteFile* finished);

// --- Words -------------------------------------------------------------------

// "12 MAR"
void formatDay(int32_t day, char* out, size_t size);
// "S" for Sunday ... the chart's column letters.
char weekdayLetter(int32_t day);

}  // namespace readingstats
