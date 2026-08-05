// Host tests for the puzzle pack and the streaming JSON importer.
//
// The importer is a push parser fed by whatever sized chunks a socket produces,
// so the test that matters most is the one that feeds the same document split
// at every possible byte boundary and demands identical results. A state
// machine that only works on tidy input is a state machine that fails once, in
// the field, on a 1.3MB download.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/connections/ConnectionsImport.h"
#include "../../src/apps_local/connections/ConnectionsPack.h"
#include "../../src/apps_local/connections/ConnectionsResults.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_pack.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// --- byte sinks and sources standing in for the SD card --------------------

struct Bytes {
  std::vector<uint8_t> data;
};

bool writeBytes(void* ctx, const void* src, const uint32_t len) {
  auto* out = static_cast<Bytes*>(ctx);
  const auto* p = static_cast<const uint8_t*>(src);
  out->data.insert(out->data.end(), p, p + len);
  return true;
}

bool readBytes(void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
  const auto* in = static_cast<const Bytes*>(ctx);
  if (static_cast<size_t>(offset) + len > in->data.size()) return false;
  std::memcpy(dst, in->data.data() + offset, len);
  return true;
}

// A sink that fails after N bytes, standing in for a full card.
struct FailingBytes {
  Bytes bytes;
  size_t budget = 0;
};

bool writeFailing(void* ctx, const void* src, const uint32_t len) {
  auto* out = static_cast<FailingBytes*>(ctx);
  if (out->bytes.data.size() + len > out->budget) return false;
  return writeBytes(&out->bytes, src, len);
}

connections::Puzzle makePuzzle(const uint16_t id, const uint32_t date, const char* firstWord = "HAIL") {
  connections::Puzzle p;
  p.id = id;
  p.date = date;
  const char* names[4] = {"WET WEATHER", "NBA TEAMS", "KEYBOARD KEYS", "PALINDROMES"};
  const char* words[4][4] = {
      {"HAIL", "RAIN", "SLEET", "SNOW"},
      {"BUCKS", "HEAT", "JAZZ", "NETS"},
      {"OPTION", "RETURN", "SHIFT", "TAB"},
      {"KAYAK", "LEVEL", "MOM", "RACECAR"},
  };
  for (int g = 0; g < 4; ++g) {
    p.groups[g].level = static_cast<uint8_t>(g);
    std::snprintf(p.groups[g].name, sizeof(p.groups[g].name), "%s", names[g]);
    for (int m = 0; m < 4; ++m) {
      std::snprintf(p.groups[g].members[m], sizeof(p.groups[g].members[m]), "%s", words[g][m]);
    }
  }
  std::snprintf(p.groups[0].members[0], sizeof(p.groups[0].members[0]), "%s", firstWord);
  return p;
}

bool samePuzzle(const connections::Puzzle& a, const connections::Puzzle& b) {
  if (a.id != b.id || a.date != b.date) return false;
  for (int g = 0; g < 4; ++g) {
    if (a.groups[g].level != b.groups[g].level) return false;
    if (std::strcmp(a.groups[g].name, b.groups[g].name) != 0) return false;
    for (int m = 0; m < 4; ++m) {
      if (std::strcmp(a.groups[g].members[m], b.groups[g].members[m]) != 0) return false;
    }
  }
  return true;
}

// --- the pack ---------------------------------------------------------------

void testPackRoundTrip() {
  Bytes index;
  Bytes data;
  connections::PackWriter writer;
  CHECK(writer.begin(writeBytes, &index, &data));

  std::vector<connections::Puzzle> written;
  for (int i = 0; i < 40; ++i) {
    // Dates that cross a month boundary, so the ordering check is exercised on
    // real YYYYMMDD arithmetic rather than on consecutive integers.
    const uint32_t date = 20230612 + static_cast<uint32_t>(i < 19 ? i : i + 81);
    connections::Puzzle p = makePuzzle(static_cast<uint16_t>(i + 1), date);
    std::snprintf(p.groups[0].members[0], sizeof(p.groups[0].members[0]), "W%d", i);
    CHECK(writer.add(p));
    written.push_back(p);
  }
  CHECK(writer.written() == 40);

  connections::PackReader reader;
  CHECK(reader.open(readBytes, &index, &data, static_cast<uint32_t>(index.data.size())));
  CHECK(reader.count() == 40);

  for (size_t i = 0; i < written.size(); ++i) {
    connections::Puzzle back;
    const bool ok = reader.readPuzzle(static_cast<int>(i), back);
    check(ok, "every puzzle reads back", __LINE__);
    check(samePuzzle(back, written[i]), "round trip is lossless", __LINE__);
    uint32_t date = 0;
    check(reader.dateAt(static_cast<int>(i), date) && date == written[i].date, "date table agrees", __LINE__);
  }

  // Out of range is refused, not read past the end.
  connections::Puzzle scratch;
  CHECK(!reader.readPuzzle(-1, scratch));
  CHECK(!reader.readPuzzle(40, scratch));
}

void testPackSearch() {
  Bytes index;
  Bytes data;
  connections::PackWriter writer;
  writer.begin(writeBytes, &index, &data);
  const uint32_t dates[5] = {20230612, 20230613, 20230615, 20240101, 20260802};
  for (int i = 0; i < 5; ++i) writer.add(makePuzzle(static_cast<uint16_t>(i + 1), dates[i]));

  connections::PackReader reader;
  CHECK(reader.open(readBytes, &index, &data, static_cast<uint32_t>(index.data.size())));

  for (int i = 0; i < 5; ++i) CHECK(reader.indexOfDate(dates[i]) == i);
  CHECK(reader.indexOfDate(20230614) == -1);
  CHECK(reader.indexOfDate(20200101) == -1);
  CHECK(reader.indexOfDate(20991231) == -1);

  // "Today's puzzle" wants the newest one not after today, because the archive
  // can lag a day and yesterday's puzzle beats an empty screen.
  CHECK(reader.indexOnOrBefore(20230614) == 1);
  CHECK(reader.indexOnOrBefore(20230615) == 2);
  CHECK(reader.indexOnOrBefore(20991231) == 4);
  CHECK(reader.indexOnOrBefore(20230612) == 0);
  // Before the archive begins there is nothing to show.
  CHECK(reader.indexOnOrBefore(20230611) == -1);
}

void testPackRejectsBadInput() {
  Bytes index;
  Bytes data;
  connections::PackWriter writer;
  writer.begin(writeBytes, &index, &data);
  CHECK(writer.add(makePuzzle(1, 20230612)));
  // Out of order would make the reader's binary search silently wrong, which is
  // worse than refusing the write.
  CHECK(!writer.add(makePuzzle(2, 20230611)));
  CHECK(!writer.add(makePuzzle(3, 20230612)));
  CHECK(writer.written() == 1);

  // A puzzle with a repeated word is unplayable and never reaches the card.
  connections::Puzzle duped = makePuzzle(4, 20230613);
  std::snprintf(duped.groups[3].members[0], sizeof(duped.groups[3].members[0]), "RAIN");
  CHECK(!writer.add(duped));

  connections::PackReader reader;
  // Truncated index: a partial entry means the import was interrupted.
  Bytes shortIndex = index;
  shortIndex.data.resize(shortIndex.data.size() - 3);
  CHECK(!reader.open(readBytes, &shortIndex, &data, static_cast<uint32_t>(shortIndex.data.size())));
  // Header only, no puzzles.
  Bytes empty;
  empty.data.assign(index.data.begin(), index.data.begin() + connections::kIndexHeaderSize);
  CHECK(!reader.open(readBytes, &empty, &data, static_cast<uint32_t>(empty.data.size())));
  // Wrong magic, and wrong version: both mean re-import rather than misread.
  Bytes badMagic = index;
  badMagic.data[0] ^= 0xFF;
  CHECK(!reader.open(readBytes, &badMagic, &data, static_cast<uint32_t>(badMagic.data.size())));
  Bytes badVersion = index;
  badVersion.data[4] = 99;
  CHECK(!reader.open(readBytes, &badVersion, &data, static_cast<uint32_t>(badVersion.data.size())));

  // A data file truncated mid-record fails the read rather than dealing a
  // puzzle with half its words.
  Bytes cut = data;
  cut.data.resize(cut.data.size() / 2);
  connections::PackReader partial;
  CHECK(partial.open(readBytes, &index, &cut, static_cast<uint32_t>(index.data.size())));
  connections::Puzzle scratch;
  CHECK(!partial.readPuzzle(0, scratch));
}

void testWriterHandlesFullCard() {
  FailingBytes index;
  FailingBytes data;
  index.budget = 1000;
  data.budget = 60;  // enough for the header, not for a whole record
  connections::PackWriter writer;
  CHECK(writer.begin(writeFailing, &index, &data));
  CHECK(!writer.add(makePuzzle(1, 20230612)));
  CHECK(writer.written() == 0);
}

// --- the importer -----------------------------------------------------------

struct Collector {
  std::vector<connections::Puzzle> puzzles;
  int stopAfter = -1;
};

bool collect(void* ctx, const connections::Puzzle& p) {
  auto* c = static_cast<Collector*>(ctx);
  c->puzzles.push_back(p);
  return c->stopAfter < 0 || static_cast<int>(c->puzzles.size()) < c->stopAfter;
}

const char* kTwoPuzzles =
    "[{\"id\":1,\"date\":\"2023-06-12\",\"answers\":["
    "{\"level\":0,\"group\":\"WET WEATHER\",\"members\":[\"HAIL\",\"RAIN\",\"SLEET\",\"SNOW\"]},"
    "{\"level\":1,\"group\":\"NBA TEAMS\",\"members\":[\"BUCKS\",\"HEAT\",\"JAZZ\",\"NETS\"]},"
    "{\"level\":2,\"group\":\"KEYBOARD KEYS\",\"members\":[\"OPTION\",\"RETURN\",\"SHIFT\",\"TAB\"]},"
    "{\"level\":3,\"group\":\"PALINDROMES\",\"members\":[\"KAYAK\",\"LEVEL\",\"MOM\",\"RACECAR\"]}]},"
    "{\"id\":2,\"date\":\"2023-06-13\",\"answers\":["
    "{\"level\":-1,\"group\":\"SILENT \\u201cG\\u201d\",\"members\":[\"GNAT\",\"GNOME\",\"REIGN\",\"SIGN\"]},"
    "{\"level\":1,\"group\":\"CAF\\u00c9 ORDERS\",\"members\":[\"LATTE\",\"MOCHA\",\"CHAI\",\"DECAF\"]},"
    "{\"level\":2,\"group\":\"B\\u2019S\",\"members\":[\"BEE\",\"BE\",\"BEA\",\"BEAU\"]},"
    "{\"level\":3,\"group\":\"98.6\\u00b0\",\"members\":[\"WARM\",\"HOT\",\"MILD\",\"COOL\"]}]}]";

std::vector<connections::Puzzle> importAll(const std::string& json, size_t chunk, connections::ImportStats* out) {
  Collector c;
  connections::Importer importer;
  importer.begin(collect, &c);
  if (chunk == 0) chunk = json.size();
  for (size_t at = 0; at < json.size(); at += chunk) {
    const size_t n = json.size() - at < chunk ? json.size() - at : chunk;
    if (!importer.feed(reinterpret_cast<const uint8_t*>(json.data() + at), n)) break;
  }
  importer.finish();
  if (out != nullptr) *out = importer.stats();
  return c.puzzles;
}

void testImportBasics() {
  connections::ImportStats stats;
  const std::vector<connections::Puzzle> puzzles = importAll(kTwoPuzzles, 0, &stats);
  CHECK(puzzles.size() == 2);
  CHECK(stats.accepted == 2);
  CHECK(stats.skippedMalformed == 0);
  CHECK(!stats.sawSyntaxError);
  if (puzzles.size() != 2) return;

  CHECK(puzzles[0].id == 1);
  CHECK(puzzles[0].date == 20230612);
  CHECK(std::strcmp(puzzles[0].groups[0].name, "WET WEATHER") == 0);
  CHECK(std::strcmp(puzzles[0].groups[3].members[3], "RACECAR") == 0);
  CHECK(puzzles[0].groups[2].level == 2);

  // The folding table, on the exact escapes the real archive contains.
  CHECK(puzzles[1].date == 20230613);
  CHECK(std::strcmp(puzzles[1].groups[0].name, "SILENT \"G\"") == 0);
  CHECK(std::strcmp(puzzles[1].groups[1].name, "CAFE ORDERS") == 0);
  CHECK(std::strcmp(puzzles[1].groups[2].name, "B'S") == 0);
  CHECK(std::strcmp(puzzles[1].groups[3].name, "98.6 DEG") == 0);
  // level -1 is what the source publishes since 2025-09-20. Recorded as
  // unknown, not as yellow: 312 of 1143 puzzles would otherwise claim a
  // difficulty they never stated.
  CHECK(puzzles[1].groups[0].level == connections::kLevelUnknown);
  CHECK(puzzles[0].groups[3].level == 3);
}

void testEveryChunkBoundary() {
  // The whole point of a push parser. Splitting at 1 byte puts a boundary
  // inside every string, every number and every \uXXXX escape.
  const std::vector<connections::Puzzle> whole = importAll(kTwoPuzzles, 0, nullptr);
  const std::string json(kTwoPuzzles);

  bool allMatch = true;
  for (size_t chunk = 1; chunk <= json.size(); ++chunk) {
    const std::vector<connections::Puzzle> split = importAll(json, chunk, nullptr);
    if (split.size() != whole.size()) {
      allMatch = false;
      break;
    }
    for (size_t i = 0; i < split.size(); ++i) {
      if (!samePuzzle(split[i], whole[i])) {
        allMatch = false;
        break;
      }
    }
    if (!allMatch) break;
  }
  check(allMatch, "every chunk size from 1 byte upward yields identical puzzles", __LINE__);

  // And every possible single split point, which covers boundaries a uniform
  // chunk size would step over.
  bool allSplits = true;
  for (size_t at = 1; at < json.size(); ++at) {
    Collector c;
    connections::Importer importer;
    importer.begin(collect, &c);
    importer.feed(reinterpret_cast<const uint8_t*>(json.data()), at);
    importer.feed(reinterpret_cast<const uint8_t*>(json.data() + at), json.size() - at);
    importer.finish();
    if (c.puzzles.size() != whole.size() || !samePuzzle(c.puzzles[0], whole[0]) ||
        !samePuzzle(c.puzzles[1], whole[1])) {
      allSplits = false;
      std::printf("      split at byte %zu differs\n", at);
      break;
    }
  }
  check(allSplits, "every single split point yields identical puzzles", __LINE__);
}

void testImportRejects() {
  // An emoji word cannot be drawn, so the puzzle goes rather than the word.
  // 🤑 is the money-mouth face that appears in the real archive.
  const std::string emoji =
      "[{\"id\":9,\"date\":\"2024-04-01\",\"answers\":["
      "{\"level\":0,\"group\":\"FOOD\",\"members\":[\"\\ud83e\\udd53\",\"RAIN\",\"SLEET\",\"SNOW\"]},"
      "{\"level\":1,\"group\":\"B\",\"members\":[\"BUCKS\",\"HEAT\",\"JAZZ\",\"NETS\"]},"
      "{\"level\":2,\"group\":\"C\",\"members\":[\"OPTION\",\"RETURN\",\"SHIFT\",\"TAB\"]},"
      "{\"level\":3,\"group\":\"D\",\"members\":[\"KAYAK\",\"LEVEL\",\"MOM\",\"RACECAR\"]}]}]";
  connections::ImportStats stats;
  CHECK(importAll(emoji, 0, &stats).empty());
  CHECK(stats.skippedUnrenderable == 1);
  CHECK(stats.accepted == 0);

  // Three groups instead of four is malformed, not unrenderable.
  const std::string short3 =
      "[{\"id\":9,\"date\":\"2024-04-01\",\"answers\":["
      "{\"level\":0,\"group\":\"A\",\"members\":[\"HAIL\",\"RAIN\",\"SLEET\",\"SNOW\"]},"
      "{\"level\":1,\"group\":\"B\",\"members\":[\"BUCKS\",\"HEAT\",\"JAZZ\",\"NETS\"]},"
      "{\"level\":2,\"group\":\"C\",\"members\":[\"OPTION\",\"RETURN\",\"SHIFT\",\"TAB\"]}]}]";
  connections::ImportStats stats3;
  CHECK(importAll(short3, 0, &stats3).empty());
  CHECK(stats3.skippedMalformed == 1);

  // A missing date: nothing can index the puzzle, so it is dropped.
  const std::string undated =
      "[{\"id\":9,\"answers\":["
      "{\"level\":0,\"group\":\"A\",\"members\":[\"HAIL\",\"RAIN\",\"SLEET\",\"SNOW\"]},"
      "{\"level\":1,\"group\":\"B\",\"members\":[\"BUCKS\",\"HEAT\",\"JAZZ\",\"NETS\"]},"
      "{\"level\":2,\"group\":\"C\",\"members\":[\"OPTION\",\"RETURN\",\"SHIFT\",\"TAB\"]},"
      "{\"level\":3,\"group\":\"D\",\"members\":[\"KAYAK\",\"LEVEL\",\"MOM\",\"RACECAR\"]}]}]";
  connections::ImportStats statsU;
  CHECK(importAll(undated, 0, &statsU).empty());
  CHECK(statsU.skippedMalformed == 1);

  // Real defect in the published archive: the 2025-04-01 April Fools puzzle is
  // symbol-only, and the upstream scraper captured all sixteen members as empty
  // strings. Malformed, not unrenderable, because there is nothing to render.
  const std::string blanks =
      "[{\"id\":660,\"date\":\"2025-04-01\",\"answers\":["
      "{\"level\":0,\"group\":\"CURRENCY SYMBOLS\",\"members\":[\"\",\"\",\"\",\"\"]},"
      "{\"level\":1,\"group\":\"AND/TOGETHER WITH\",\"members\":[\"\",\"\",\"\",\"\"]},"
      "{\"level\":2,\"group\":\"EMOTICON MOUTHS\",\"members\":[\"\",\"\",\"\",\"\"]},"
      "{\"level\":3,\"group\":\"\\\"RIGHT\\\"\",\"members\":[\"\",\"\",\"\",\"\"]}]}]";
  connections::ImportStats statsBlank;
  CHECK(importAll(blanks, 0, &statsBlank).empty());
  CHECK(statsBlank.skippedMalformed == 1);
  CHECK(statsBlank.skippedUnrenderable == 0);

  // Garbage is reported, not silently treated as an empty archive.
  connections::ImportStats statsBad;
  importAll("[{\"id\":1,\"date\":@@@}]", 0, &statsBad);
  CHECK(statsBad.sawSyntaxError);

  // A truncated download must not look like a successful short import.
  Collector c;
  connections::Importer importer;
  importer.begin(collect, &c);
  const std::string json(kTwoPuzzles);
  importer.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size() - 30);
  CHECK(!importer.finish());
}

void testImportStops() {
  // The callback asking to stop (a full card) halts the import.
  Collector c;
  c.stopAfter = 1;
  connections::Importer importer;
  importer.begin(collect, &c);
  const std::string json(kTwoPuzzles);
  importer.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  CHECK(c.puzzles.size() == 1);
}

// Import straight into a pack, which is exactly what the device does.
void testImportToPack() {
  Bytes index;
  Bytes data;
  static connections::PackWriter writer;
  writer.begin(writeBytes, &index, &data);

  connections::Importer importer;
  importer.begin([](void*, const connections::Puzzle& p) { return writer.add(p); }, nullptr);
  const std::string json(kTwoPuzzles);
  // A deliberately awkward chunk size, prime and small.
  for (size_t at = 0; at < json.size(); at += 7) {
    const size_t n = json.size() - at < 7 ? json.size() - at : 7;
    importer.feed(reinterpret_cast<const uint8_t*>(json.data() + at), n);
  }
  CHECK(importer.finish());
  CHECK(writer.written() == 2);

  connections::PackReader reader;
  CHECK(reader.open(readBytes, &index, &data, static_cast<uint32_t>(index.data.size())));
  CHECK(reader.count() == 2);
  connections::Puzzle back;
  CHECK(reader.readPuzzle(1, back));
  CHECK(back.date == 20230613);
  CHECK(std::strcmp(back.groups[0].name, "SILENT \"G\"") == 0);
  CHECK(reader.indexOnOrBefore(20260802) == 1);
}

// --- results ----------------------------------------------------------------

void testResultIndex() {
  CHECK(connections::resultIndex(connections::kResultsEpoch) == 0);
  CHECK(connections::resultIndex(20230613) == 1);
  // Across a month, a year, and a leap day, since a wrong calendar would read
  // back somebody else's score for a date.
  CHECK(connections::resultIndex(20230701) == 19);
  CHECK(connections::resultIndex(20240612) == 366);  // 2024 is a leap year
  CHECK(connections::resultIndex(20250612) == 731);
  CHECK(connections::daysBetween(20240228, 20240301) == 2);  // leap
  CHECK(connections::daysBetween(20230228, 20230301) == 1);  // not
  CHECK(connections::daysBetween(20231231, 20240101) == 1);
  // Before the epoch, and nonsense dates, are refused rather than wrapping.
  CHECK(connections::resultIndex(20230611) == -1);
  CHECK(connections::resultIndex(20230230) == -1);
  CHECK(connections::resultIndex(20231301) == -1);
  CHECK(connections::resultIndex(0) == -1);
  // Every real puzzle date maps to a distinct, increasing slot.
  CHECK(connections::resultIndex(20260803) > connections::resultIndex(20260802));
}

void testCalendarMath() {
  // Known weekdays, checked against a real calendar rather than derived twice.
  CHECK(connections::dayOfWeek(20230612) == 1);  // Monday, the first puzzle
  CHECK(connections::dayOfWeek(20260803) == 1);  // Monday
  CHECK(connections::dayOfWeek(20260802) == 0);  // Sunday
  CHECK(connections::dayOfWeek(20240229) == 4);  // Thursday, a leap day
  CHECK(connections::dayOfWeek(20000101) == 6);  // Saturday
  CHECK(connections::dayOfWeek(20231301) == -1);

  CHECK(connections::daysInMonth(2026, 8) == 31);
  CHECK(connections::daysInMonth(2026, 2) == 28);
  CHECK(connections::daysInMonth(2024, 2) == 29);
  CHECK(connections::daysInMonth(2000, 2) == 29);  // divisible by 400
  CHECK(connections::daysInMonth(1900, 2) == 0);   // before the supported range
  CHECK(connections::daysInMonth(2026, 13) == 0);

  // Every day of a month lands on a distinct weekday cycle.
  for (int d = 1; d <= 31; ++d) {
    const uint32_t date = 20260800u + static_cast<uint32_t>(d);
    check(connections::dayOfWeek(date) == (d + 5) % 7, "weekday advances by one a day", __LINE__);
  }
}

void testRecordRoundTrip() {
  // A fresh file is zeroes, so zero must decode as "never played" rather than
  // as a flawless solve.
  CHECK(!connections::decodeRecord(0).played);

  for (int mistakes = 0; mistakes <= 4; ++mistakes) {
    for (int lost = 0; lost < 2; ++lost) {
      for (int hardest = 0; hardest < 2; ++hardest) {
        connections::Record in;
        in.played = true;
        in.mistakes = static_cast<uint8_t>(mistakes);
        in.lost = lost != 0;
        in.hardestFirst = hardest != 0;
        const connections::Record out = connections::decodeRecord(connections::encodeRecord(in));
        check(out.played, "played survives", __LINE__);
        check(out.mistakes == in.mistakes, "mistakes survive", __LINE__);
        check(out.lost == in.lost, "loss survives", __LINE__);
        check(out.hardestFirst == in.hardestFirst, "hardest-first survives", __LINE__);
      }
    }
  }
}

void testSummary() {
  // A record built by hand so every state the menu draws is present: a loss, a
  // clean solve, a solve with mistakes, and gaps.
  std::vector<uint8_t> results(40, 0);
  const auto put = [&](const int index, const int mistakes, const bool lost) {
    connections::Record record;
    record.played = true;
    record.mistakes = static_cast<uint8_t>(mistakes);
    record.lost = lost;
    results[static_cast<size_t>(index)] = connections::encodeRecord(record);
  };

  put(2, 0, false);
  put(9, 4, true);
  for (int i = 10; i <= 19; ++i) put(i, i % 3, false);

  // Newest is 20 and unplayed: the streak runs back through 19..10 and stops at
  // the loss on 9, and today not being played must not zero it.
  {
    const connections::Summary summary = connections::summarise(results.data(), 40, 20);
    CHECK(summary.played == 12);
    CHECK(summary.perfect == 4);  // index 2, then 12, 15 and 18
    CHECK(summary.streak == 10);
    CHECK(!summary.newestPlayed);
    // recent[] ends on the newest index, so cell 15 is day 20 and cell 4 is day 9.
    CHECK(summary.recent[15] == connections::kDayUnplayed);
    CHECK(summary.recent[4] == connections::kDayLost);
    CHECK(summary.recent[5] == connections::kDayWon);      // day 10, one mistake
    CHECK(summary.recent[7] == connections::kDayPerfect);  // day 12, clean
  }

  // Once today is played the streak includes it rather than starting over.
  put(20, 1, false);
  {
    const connections::Summary summary = connections::summarise(results.data(), 40, 20);
    CHECK(summary.streak == 11);
    CHECK(summary.newestPlayed);
    CHECK(summary.newestMistakes == 1);
    CHECK(summary.recent[15] == connections::kDayWon);
  }

  // A newest day that was lost breaks the streak outright, rather than being
  // skipped the way an unplayed day is.
  put(20, 4, true);
  CHECK(connections::summarise(results.data(), 40, 20).streak == 0);

  // Reading near the start of the file must not walk off it: cells before the
  // epoch are unplayed, not garbage.
  {
    const connections::Summary summary = connections::summarise(results.data(), 40, 2);
    CHECK(summary.recent[15] == connections::kDayPerfect);
    CHECK(summary.recent[0] == connections::kDayUnplayed);
    CHECK(summary.streak == 1);
  }

  // A short or missing file is safe.
  CHECK(connections::summarise(nullptr, 0, 20).played == 0);
  CHECK(connections::summarise(results.data(), 3, 20).streak == 0);
}

}  // namespace

int main() {
  testSummary();
  testResultIndex();
  testCalendarMath();
  testRecordRoundTrip();
  testPackRoundTrip();
  testPackSearch();
  testPackRejectsBadInput();
  testWriterHandlesFullCard();
  testImportBasics();
  testEveryChunkBoundary();
  testImportRejects();
  testImportStops();
  testImportToPack();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
