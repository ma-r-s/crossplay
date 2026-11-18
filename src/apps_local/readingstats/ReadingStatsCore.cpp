#include "ReadingStatsCore.h"

#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace readingstats {

// --- Days --------------------------------------------------------------------

int32_t daysFromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned mp = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
  const unsigned doy = (153 * mp + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t days, int& year, int& month, int& day) {
  days += 719468;
  const int32_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
}

int weekday(const int32_t days) {
  // 1970-01-01 was a Thursday, four days after a Sunday.
  const int32_t w = (days + 4) % 7;
  return static_cast<int>(w < 0 ? w + 7 : w);
}

int32_t localDay(const int64_t utcEpoch, const int32_t utcOffsetSeconds) {
  const int64_t local = utcEpoch + utcOffsetSeconds;
  int64_t day = local / kSecondsPerDay;
  if (local % kSecondsPerDay < 0) --day;
  return static_cast<int32_t>(day);
}

int yearOf(const int32_t days) {
  int y = 0, m = 0, d = 0;
  civilFromDays(days, y, m, d);
  return y;
}

// --- Books -------------------------------------------------------------------

uint32_t bookId(const char* path) {
  uint32_t hash = 2166136261u;
  for (const char* p = path; p != nullptr && *p != '\0'; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

namespace {

// Strict UTF-8: the codepoint at `in[i]`, advancing `i`, or -1 for a byte that
// does not start a well-formed sequence (which then advances by one).
int32_t nextCodepoint(const std::string& in, size_t& i) {
  const auto byte = [&in](const size_t at) { return static_cast<uint8_t>(in[at]); };
  const uint8_t lead = byte(i);
  if (lead < 0x80) {
    ++i;
    return lead;
  }
  int extra = 0;
  uint32_t cp = 0;
  uint32_t minimum = 0;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    cp = lead & 0x1F;
    minimum = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    cp = lead & 0x0F;
    minimum = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    cp = lead & 0x07;
    minimum = 0x10000;
  } else {
    ++i;
    return -1;
  }
  if (i + static_cast<size_t>(extra) >= in.size()) {
    ++i;
    return -1;
  }
  for (int k = 1; k <= extra; ++k) {
    const uint8_t c = byte(i + static_cast<size_t>(k));
    if ((c & 0xC0) != 0x80) {
      ++i;
      return -1;
    }
    cp = (cp << 6) | (c & 0x3F);
  }
  if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    ++i;
    return -1;
  }
  i += static_cast<size_t>(extra) + 1;
  return static_cast<int32_t>(cp);
}

// A character that has no business in a title: the C0 and C1 controls, DEL,
// and the line and paragraph separators. They become a space, so the words
// either side of a newline stay two words.
bool isControl(const uint32_t cp) {
  return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029;
}

// Invisible formatting that would only make two equal titles compare unequal or
// put an empty box on the panel: the soft hyphen, byte-order mark, the
// directional controls. Dropped outright.
bool isInvisible(const uint32_t cp) {
  return cp == 0x00AD || cp == 0xFEFF || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
         (cp >= 0x2060 && cp <= 0x2064) || (cp >= 0x2066 && cp <= 0x2069);
}

std::string cleaned(const std::string& raw) {
  std::string valid;
  valid.reserve(raw.size());
  size_t i = 0;
  while (i < raw.size()) {
    const size_t at = i;
    const int32_t cp = nextCodepoint(raw, i);
    if (cp < 0 || isInvisible(static_cast<uint32_t>(cp))) continue;
    if (isControl(static_cast<uint32_t>(cp))) {
      valid.push_back(' ');
      continue;
    }
    valid.append(raw, at, i - at);
  }
  std::string text = utf8CollapseWhitespace(utf8FoldTypography(valid));
  // utf8CollapseWhitespace leaves one space at either end where there were any.
  const size_t first = text.find_first_not_of(' ');
  if (first == std::string::npos) return std::string();
  const size_t last = text.find_last_not_of(' ');
  return text.substr(first, last - first + 1);
}

// The file's own name, for a book whose metadata carries no title.
std::string stem(const char* path) {
  if (path == nullptr) return std::string();
  std::string name(path);
  const size_t slash = name.find_last_of('/');
  if (slash != std::string::npos) name.erase(0, slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.erase(dot);
  for (char& c : name) {
    if (c == '_') c = ' ';
  }
  return name;
}

// The longest prefix of `text` no longer than `limit` bytes that ends on a
// codepoint boundary.
size_t boundaryAtMost(const std::string& text, size_t limit) {
  if (limit >= text.size()) return text.size();
  while (limit > 0 && (static_cast<uint8_t>(text[limit]) & 0xC0) == 0x80) --limit;
  return limit;
}

}  // namespace

std::string cleanTitle(const char* raw, const char* path) {
  std::string title = cleaned(raw == nullptr ? std::string() : std::string(raw));
  if (title.empty()) title = cleaned(stem(path));
  if (title.size() <= kTitleCapacity) return title;

  static constexpr const char* kMore = "...";
  size_t cut = boundaryAtMost(title, kTitleCapacity - 3);
  // Back to a word boundary when one is reasonably near: a title is easier to
  // read short a word than short half of one. CJK has no spaces to find, and
  // then the codepoint boundary stands.
  const size_t space = title.rfind(' ', cut);
  if (space != std::string::npos && space >= cut / 2) cut = space;
  title.resize(cut);
  while (!title.empty() && title.back() == ' ') title.pop_back();
  return title + kMore;
}

// --- Records -----------------------------------------------------------------

namespace {

// The title's length is one byte, and the title, its 12 bytes of header and
// the CRC must fit the record.
static_assert(kTitleCapacity <= 255, "a finished record stores the title's length in one byte");
static_assert(12 + kTitleCapacity + 2 <= kFinishedSize, "the title overruns its record");

constexpr uint8_t kSessionMagic = 'R';
constexpr uint8_t kFinishedMagic = 'F';
constexpr uint8_t kVersion = 1;

// CRC-16/CCITT-FALSE.
uint16_t crc16(const uint8_t* data, const size_t count) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < count; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void put16(uint8_t* out, const uint16_t v) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* out, const uint32_t v) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t get16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }
uint32_t get32(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) | (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

bool sealed(const uint8_t* record, const size_t size) { return get16(record + size - 2) == crc16(record, size - 2); }

}  // namespace

Outcome outcomeOf(const bool counted, const int64_t startTime, const uint16_t endBp, const int endPercent) {
  Outcome outcome;
  outcome.session = counted;
  outcome.finished = (endPercent >= 100 || endBp >= 10000) && startTime > 0;
  return outcome;
}

Session makeSession(const int64_t startUtc, const uint32_t seconds, const int32_t utcOffsetSeconds, const uint32_t book,
                    const uint16_t endBp, const bool finished) {
  Session session;
  session.day = localDay(startUtc, utcOffsetSeconds);
  session.start = startUtc > 0 ? static_cast<uint32_t>(startUtc) : 0;
  session.seconds = seconds;
  session.book = book;
  session.endBp = endBp > 10000 ? 10000 : endBp;
  session.finished = finished;
  return session;
}

void encode(const Session& session, uint8_t out[kSessionSize]) {
  std::memset(out, 0, kSessionSize);
  out[0] = kSessionMagic;
  out[1] = kVersion;
  out[2] = session.finished ? 1 : 0;
  put32(out + 4, static_cast<uint32_t>(session.day));
  put32(out + 8, session.start);
  put32(out + 12, session.seconds);
  put32(out + 16, session.book);
  put16(out + 20, session.endBp);
  put16(out + 22, crc16(out, kSessionSize - 2));
}

bool decode(const uint8_t* in, Session& session) {
  if (in[0] != kSessionMagic || in[1] != kVersion || (in[2] & ~1u) != 0 || in[3] != 0) return false;
  if (!sealed(in, kSessionSize)) return false;
  session.day = static_cast<int32_t>(get32(in + 4));
  session.start = get32(in + 8);
  session.seconds = get32(in + 12);
  session.book = get32(in + 16);
  session.endBp = get16(in + 20);
  session.finished = (in[2] & 1u) != 0;
  return session.endBp <= 10000;
}

void encode(const Finished& finished, uint8_t out[kFinishedSize]) {
  std::memset(out, 0, kFinishedSize);
  const size_t length = strnlen(finished.title, kTitleCapacity);
  out[0] = kFinishedMagic;
  out[1] = kVersion;
  out[2] = static_cast<uint8_t>(length);
  put32(out + 4, static_cast<uint32_t>(finished.day));
  put32(out + 8, finished.book);
  std::memcpy(out + 12, finished.title, length);
  put16(out + kFinishedSize - 2, crc16(out, kFinishedSize - 2));
}

bool decode(const uint8_t* in, Finished& finished) {
  if (in[0] != kFinishedMagic || in[1] != kVersion || in[2] > kTitleCapacity || in[3] != 0) return false;
  if (!sealed(in, kFinishedSize)) return false;
  finished.day = static_cast<int32_t>(get32(in + 4));
  finished.book = get32(in + 8);
  std::memset(finished.title, 0, sizeof(finished.title));
  std::memcpy(finished.title, in + 12, in[2]);
  // No NUL inside the stored length. The title is NOT re-cleaned here: a fold
  // rule added in a later build must not make an old record unreadable.
  return std::strlen(finished.title) == in[2];
}

// --- Files -------------------------------------------------------------------

bool appendRecord(ByteFile& file, const uint8_t* record, const size_t recordSize, size_t* tornBytes) {
  const size_t size = file.size();
  const size_t whole = wholeLength(size, recordSize);
  if (tornBytes != nullptr) *tornBytes = size - whole;
  if (whole != size && !file.truncate(whole)) {
    // A card that will not cut the tail: pad it out to the next boundary
    // instead. The torn bytes and the padding read as one record that fails
    // its check, which the scan steps over, and this record starts aligned.
    static constexpr uint8_t kZeros[16] = {};
    size_t pad = recordSize - (size - whole);
    while (pad > 0) {
      const size_t n = std::min(pad, sizeof(kZeros));
      if (!file.append(kZeros, n)) return false;
      pad -= n;
    }
  }
  return file.append(record, recordSize);
}

namespace {

// Newest first, in chunks. `visit` returns false to stop. A record that does
// not decode is stepped over, and so is a chunk that cannot be read at all: an
// unreadable sector costs the records in it, never everything older.
template <size_t Size, typename Visit>
int scanBackward(ByteFile& file, Visit visit) {
  // At most 192 bytes of stack whichever file it is: eight sessions or one
  // finished book per read. SdFat's sector cache makes the small reads cheap.
  constexpr size_t kChunkRecords = 192 / Size > 0 ? 192 / Size : 1;
  uint8_t chunk[Size * kChunkRecords];
  size_t end = wholeLength(file.size(), Size);
  int valid = 0;
  while (end > 0) {
    const size_t count = std::min(end / Size, kChunkRecords);
    const size_t begin = end - count * Size;
    if (file.readAt(begin, chunk, count * Size)) {
      for (size_t i = count; i-- > 0;) {
        bool decoded = false;
        const bool more = visit(chunk + i * Size, decoded);
        if (decoded) ++valid;
        if (!more) return valid;
      }
    }
    end = begin;
  }
  return valid;
}

}  // namespace

int scanSessions(ByteFile& file, const SessionFn fn, void* context) {
  return scanBackward<kSessionSize>(file, [fn, context](const uint8_t* record, bool& decoded) {
    Session session;
    decoded = decode(record, session);
    return !decoded || fn(session, context);
  });
}

int scanFinished(ByteFile& file, const FinishedFn fn, void* context) {
  return scanBackward<kFinishedSize>(file, [fn, context](const uint8_t* record, bool& decoded) {
    Finished finished;
    decoded = decode(record, finished);
    if (decoded) fn(finished, context);
    return true;
  });
}

bool finishedSince(ByteFile& file, const uint32_t book, const int32_t fromDay) {
  struct Probe {
    uint32_t book;
    int32_t fromDay;
    bool found;
  } probe{book, fromDay, false};
  scanFinished(
      file,
      [](const Finished& finished, void* context) {
        auto* p = static_cast<Probe*>(context);
        if (finished.book == p->book && finished.day >= p->fromDay) p->found = true;
      },
      &probe);
  return probe.found;
}

// --- What it adds up to ------------------------------------------------------

Tally::Tally(const int32_t today) : today_(today) {}

void Tally::addSession(const Session& session) {
  const int32_t ago = today_ - session.day;
  if (ago < 0 || ago >= kStreakWindow) return;
  ++sessions_;
  const uint32_t seconds = std::min<uint32_t>(session.seconds, kSecondsPerDay);
  seconds_[ago] = std::min<uint32_t>(seconds_[ago] + seconds, kSecondsPerDay);
}

void Tally::addFinished(const Finished& finished) {
  if (finished.day > today_ || yearOf(finished.day) != year()) return;
  for (Book& book : books_) {
    if (book.book != finished.book) continue;
    // Read newest first, so an older record of the same book is the first time
    // it was finished this year, which is the date the list shows.
    if (finished.day < book.day) {
      book.day = finished.day;
      book.title = finished.title;
    }
    return;
  }
  if (static_cast<int>(books_.size()) >= kMaxBooks) return;
  if (books_.empty()) books_.reserve(16);
  Book book;
  book.day = finished.day;
  book.book = finished.book;
  book.title = finished.title;
  books_.push_back(std::move(book));
}

void Tally::settle() {
  std::stable_sort(books_.begin(), books_.end(), [](const Book& a, const Book& b) { return a.day > b.day; });
}

uint32_t Tally::secondsOn(const int32_t day) const {
  const int32_t ago = today_ - day;
  if (ago < 0 || ago >= kStreakWindow) return 0;
  return seconds_[ago];
}

uint32_t Tally::weekSeconds() const {
  uint32_t total = 0;
  for (int32_t day = today_ - weekday(today_); day <= today_; ++day) total += secondsOn(day);
  return total;
}

int Tally::streak() const {
  int32_t day = counted(today_) ? today_ : today_ - 1;
  int run = 0;
  while (run < kStreakWindow - 1 && counted(day)) {
    ++run;
    --day;
  }
  return run;
}

bool Tally::settledBefore(const int32_t day) const {
  // Everything after `complete` has been read, give or take the order slack.
  const int32_t complete = day + kOrderSlackDays;
  if (complete >= today_ - (kChartDays - 1)) return false;
  // The streak is settled once it has ended on a day that is complete, or run
  // to the edge of the window.
  for (int32_t d = counted(today_) ? today_ : today_ - 1; d > today_ - kStreakWindow; --d) {
    if (d <= complete) return false;
    if (!counted(d)) return true;
  }
  return true;
}

bool Tally::anyInChart() const {
  for (int i = 0; i < kChartDays; ++i) {
    if (seconds_[i] > 0) return true;
  }
  return false;
}

int load(Tally& tally, ByteFile* sessions, ByteFile* finished) {
  int read = 0;
  if (sessions != nullptr) {
    read += scanSessions(
        *sessions,
        [](const Session& session, void* context) {
          auto* t = static_cast<Tally*>(context);
          t->addSession(session);
          return !t->settledBefore(session.day);
        },
        &tally);
  }
  // The finished file is read whole: a book a year is a few KB, and reading all
  // of it means no clock that ever stepped backwards can hide one.
  if (finished != nullptr) {
    read += scanFinished(
        *finished, [](const Finished& record, void* context) { static_cast<Tally*>(context)->addFinished(record); },
        &tally);
  }
  tally.settle();
  return read;
}

// --- Words -------------------------------------------------------------------

void formatDay(const int32_t day, char* out, const size_t size) {
  static constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  int y = 0, m = 0, d = 0;
  civilFromDays(day, y, m, d);
  std::snprintf(out, size, "%d %s", d, kMonths[(m - 1) % 12]);
}

char weekdayLetter(const int32_t day) {
  static constexpr char kLetters[] = {'S', 'M', 'T', 'W', 'T', 'F', 'S'};
  return kLetters[weekday(day)];
}

}  // namespace readingstats
