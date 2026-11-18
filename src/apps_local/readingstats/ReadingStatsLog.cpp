#include "ReadingStatsLog.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <cstdio>
#include <ctime>

#include "../../activities/reader/ReaderSession.h"
#include "ReadingStatsCore.h"

namespace readingstats {

namespace {

constexpr const char* kTag = "RSTATS";

// A HalFile as the core's ByteFile. Every write goes to the end explicitly:
// openFileForAppend positions there on the device and maps to O_APPEND in the
// simulator, and after a truncate the two can disagree about where "there" is.
class HalByteFile : public ByteFile {
 public:
  explicit HalByteFile(HalFile& file) : file_(file) {}
  size_t size() override { return file_.size(); }
  bool truncate(const size_t length) override { return file_.truncate(length); }
  bool readAt(const size_t offset, uint8_t* buffer, const size_t count) override {
    if (!file_.seek(offset)) return false;
    return file_.read(buffer, count) == static_cast<int>(count);
  }
  bool append(const uint8_t* buffer, const size_t count) override {
    if (!file_.seek(file_.size())) return false;
    const size_t written = file_.write(buffer, count);
    file_.flush();
    return written == count;
  }

 private:
  HalFile& file_;
};

// Seconds east of UTC in force at `epoch`, under the zone Settings applied to
// the clock (timezones::applyToClock). Asked of the C library rather than the
// settings so a daylight-saving rule is honoured.
int32_t utcOffsetAt(const int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm local{};
  if (localtime_r(&t, &local) == nullptr) return 0;
  const int64_t wall =
      static_cast<int64_t>(daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday)) * kSecondsPerDay +
      local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
  const int64_t offset = wall - epoch;
  // No zone is more than fourteen hours from UTC; anything else is a broken
  // TZ string, and UTC is the honest fallback.
  return offset >= -14 * 3600 && offset <= 14 * 3600 ? static_cast<int32_t>(offset) : 0;
}

// One finished book, dated today: the day the end was reached, decided now.
void recordFinished(const uint32_t book, const std::string& bookPath, const std::string& title) {
  int32_t today = 0;
  if (!localToday(today)) return;
  HalFile file;
  if (!Storage.openFileForAppend(kTag, kFinishedPath, file)) return;
  HalByteFile bytes(file);
  // Reaching the end twice in one year -- going back a page and forward again,
  // or turning onto a finished book's last page to look at it -- is one book.
  if (finishedSince(bytes, book, daysFromCivil(yearOf(today), 1, 1))) {
    LOG_DBG(kTag, "finished again this year, not recorded: book=%08lx", static_cast<unsigned long>(book));
    return;
  }
  // The record and its bytes are 280 bytes together, so they go on the heap
  // rather than onto the stack of an onExit() or a sleep that is already deep.
  struct Scratch {
    Finished record;
    uint8_t bytes[kFinishedSize];
  };
  const auto scratch = makeUniqueNoThrow<Scratch>();
  if (!scratch) {
    LOG_ERR(kTag, "OOM: finished record");
    return;
  }
  scratch->record.day = today;
  scratch->record.book = book;
  const std::string clean = cleanTitle(title.c_str(), bookPath.c_str());
  std::snprintf(scratch->record.title, sizeof(scratch->record.title), "%s", clean.c_str());
  encode(scratch->record, scratch->bytes);
  size_t torn = 0;
  const bool ok = appendRecord(bytes, scratch->bytes, kFinishedSize, &torn);
  if (torn > 0) LOG_ERR(kTag, "%s had a torn tail of %u bytes; cut before appending", kFinishedPath, (unsigned)torn);
  if (!ok) {
    LOG_ERR(kTag, "could not append to %s", kFinishedPath);
    return;
  }
  LOG_INF(kTag, "finished day=%ld book=%08lx title=\"%s\"", static_cast<long>(today), static_cast<unsigned long>(book),
          scratch->record.title);
}

}  // namespace

void recordSession(const std::string& bookPath, const std::string& title, const ReaderSession& session,
                   const int endPercent) {
  // The rule is readingstats::outcomeOf(), where the host suite can reach it.
  const Outcome outcome = outcomeOf(session.isEmitWorthy(), session.startTime(), session.endProgressBp(), endPercent);
  const bool reachedEnd = outcome.finished;
  const uint32_t book = bookId(bookPath.c_str());

  if (outcome.session) {
    const int64_t start = session.startTime();
    const int32_t offset = utcOffsetAt(start);
    const Session record =
        makeSession(start, session.durationSeconds(), offset, book, session.endProgressBp(), reachedEnd);
    uint8_t bytesOut[kSessionSize];
    encode(record, bytesOut);
    HalFile file;
    if (Storage.openFileForAppend(kTag, kSessionsPath, file)) {
      HalByteFile bytes(file);
      size_t torn = 0;
      const bool ok = appendRecord(bytes, bytesOut, kSessionSize, &torn);
      if (torn > 0) {
        LOG_ERR(kTag, "%s had a torn tail of %u bytes; cut before appending", kSessionsPath, (unsigned)torn);
      }
      if (ok) {
        LOG_INF(kTag, "session day=%ld offset=%ld start=%lld secs=%lu bp=%u..%u end=%d%% book=%08lx",
                static_cast<long>(record.day), static_cast<long>(offset), static_cast<long long>(start),
                static_cast<unsigned long>(record.seconds), static_cast<unsigned>(session.startProgressBp()),
                static_cast<unsigned>(record.endBp), endPercent, static_cast<unsigned long>(book));
      } else {
        LOG_ERR(kTag, "could not append to %s", kSessionsPath);
      }
    }
  } else {
    // Under ten seconds read, or no trusted clock to date it by.
    LOG_DBG(kTag, "session not recorded: start=%lld secs=%lu end=%d%%", static_cast<long long>(session.startTime()),
            static_cast<unsigned long>(session.durationSeconds()), endPercent);
  }

  if (reachedEnd) recordFinished(book, bookPath, title);
}

bool localToday(int32_t& day) {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return false;
  day = localDay(now, utcOffsetAt(now));
  return true;
}

void loadTally(Tally& tally) {
  HalFile sessions;
  HalFile finished;
  const bool haveSessions = Storage.exists(kSessionsPath) && Storage.openFileForRead(kTag, kSessionsPath, sessions);
  const bool haveFinished = Storage.exists(kFinishedPath) && Storage.openFileForRead(kTag, kFinishedPath, finished);
  HalByteFile sessionBytes(sessions);
  HalByteFile finishedBytes(finished);
  const int read = load(tally, haveSessions ? &sessionBytes : nullptr, haveFinished ? &finishedBytes : nullptr);
  LOG_INF(kTag, "loaded today=%ld sessions=%s finished=%s records=%d streak=%d week=%lus books=%u",
          static_cast<long>(tally.today()), haveSessions ? "yes" : "none", haveFinished ? "yes" : "none", read,
          tally.streak(), static_cast<unsigned long>(tally.weekSeconds()), static_cast<unsigned>(tally.books().size()));
}

}  // namespace readingstats
