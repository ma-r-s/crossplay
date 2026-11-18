// Writes a Reading Stats history onto a simulator card, through the same
// encoder and append the device uses, so a seeded card is byte for byte one the
// reader could have written.
//
//   seed_stats <card>/.crosspoint <YYYY-MM-DD> <profile>
//
// Profiles:
//   real    nine weeks of sessions ending today, a nine-day streak, two missed
//           days inside the chart, and eleven books finished this year -- long
//           titles, accented ones, Cyrillic, one in Japanese, one far longer
//           than any ordinary title -- plus one finished last December, which
//           must not appear
//   few     today's reading only and two books, so the main screen's list ends
//           without a door
//   empty   no files at all: the first run
//
// Built and run by scripts_local/shoot-readingstats.sh.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ReadingStatsCore.h"

using namespace readingstats;

namespace {

class StdioFile : public ByteFile {
 public:
  explicit StdioFile(const std::string& path) {
    // Reset per run: a seed describes a whole history, not an addition to one.
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f != nullptr) std::fclose(f);
    file_ = std::fopen(path.c_str(), "r+b");
  }
  ~StdioFile() override {
    if (file_ != nullptr) std::fclose(file_);
  }
  bool ok() const { return file_ != nullptr; }
  size_t size() override {
    std::fseek(file_, 0, SEEK_END);
    return static_cast<size_t>(std::ftell(file_));
  }
  bool truncate(size_t) override { return false; }
  bool readAt(const size_t offset, uint8_t* buffer, const size_t count) override {
    std::fseek(file_, static_cast<long>(offset), SEEK_SET);
    return std::fread(buffer, 1, count, file_) == count;
  }
  bool append(const uint8_t* buffer, const size_t count) override {
    std::fseek(file_, 0, SEEK_END);
    return std::fwrite(buffer, 1, count, file_) == count;
  }

 private:
  std::FILE* file_ = nullptr;
};

void session(StdioFile& file, const int32_t day, const int hour, const uint32_t minutes, const char* path) {
  const int64_t start = static_cast<int64_t>(day) * kSecondsPerDay + hour * 3600;
  const Session s = makeSession(start, minutes * 60, 0, bookId(path), 4000, false);
  uint8_t record[kSessionSize];
  encode(s, record);
  appendRecord(file, record, kSessionSize);
}

void finished(StdioFile& file, const int32_t day, const char* path, const char* title) {
  Finished f;
  f.day = day;
  f.book = bookId(path);
  std::snprintf(f.title, sizeof(f.title), "%s", cleanTitle(title, path).c_str());
  uint8_t record[kFinishedSize];
  encode(f, record);
  appendRecord(file, record, kFinishedSize);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: seed_stats <card>/.crosspoint <YYYY-MM-DD> real|few|empty\n");
    return 2;
  }
  const std::string dir = argv[1];
  int y = 0, m = 0, d = 0;
  if (std::sscanf(argv[2], "%d-%d-%d", &y, &m, &d) != 3) return 2;
  const int32_t today = daysFromCivil(y, m, d);
  const std::string profile = argv[3];
  const std::string sessionsPath = dir + "/reading-sessions.dat";
  const std::string finishedPath = dir + "/reading-finished.dat";
  std::remove(sessionsPath.c_str());
  std::remove(finishedPath.c_str());
  if (profile == "empty") return 0;

  StdioFile sessions(sessionsPath);
  StdioFile done(finishedPath);
  if (!sessions.ok() || !done.ok()) return 1;

  if (profile == "few") {
    session(sessions, today, 8, 34, "/books/a.epub");
    finished(done, today - 20, "/books/hobbit.epub", "The Hobbit, or There and Back Again");
    finished(done, today - 3, "/books/solaris.epub", "Solaris");
    return 0;
  }

  // Nine weeks back to today. Weekdays are a commute's worth, weekends longer;
  // days 9 and 12 back were missed, so the streak is the nine days since.
  for (int ago = 63; ago >= 0; --ago) {
    const int32_t day = today - ago;
    if (ago == 9 || ago == 12 || ago == 30 || ago == 31 || ago == 47) continue;
    const int wd = weekday(day);
    const bool weekend = wd == 0 || wd == 6;
    const uint32_t base = weekend ? 70 : 24;
    const uint32_t minutes = base + static_cast<uint32_t>((ago * 37) % (weekend ? 60 : 30));
    session(sessions, day, 7, minutes / 2 + 3, "/books/current.epub");
    if (ago != 0) session(sessions, day, 22, minutes - minutes / 2, "/books/current.epub");
  }

  finished(done, daysFromCivil(y - 1, 12, 28), "/books/old.epub", "Last Year's Book");
  finished(done, daysFromCivil(y, 1, 9), "/books/frank.epub", "Frankenstein; or, The Modern Prometheus");
  finished(done, daysFromCivil(y, 2, 3), "/books/norwegian.epub", "ノルウェイの森");
  finished(done, daysFromCivil(y, 3, 21), "/books/cien.epub", "Cien años de soledad");
  finished(done, daysFromCivil(y, 4, 30), "/books/sapiens.epub", "Sapiens: A Brief History of Humankind");
  finished(done, daysFromCivil(y, 6, 14), "/books/zen.epub",
           "Zen and the Art of Motorcycle Maintenance: An Inquiry into Values, 25th Anniversary Edition with a "
           "New Introduction by the Author");
  finished(done, daysFromCivil(y, 7, 2), "/books/geb.epub", "Gödel, Escher, Bach: An Eternal Golden Braid");
  finished(done, daysFromCivil(y, 8, 19), "/books/master.epub", "Мастер и Маргарита");
  finished(done, daysFromCivil(y, 9, 8), "/books/jekyll.epub",
           "The Strange Case of Dr Jekyll and Mr Hyde and Other Tales of Terror");
  finished(done, daysFromCivil(y, 10, 1), "/books/monte.epub", "Le Comte de Monte-Cristo");
  finished(done, today - 16, "/books/hhgttg.epub", "The Hitchhiker\xe2\x80\x99s Guide to the Galaxy");
  finished(done, today - 2, "/books/lefthand.epub", "The Left Hand of Darkness");
  return 0;
}
