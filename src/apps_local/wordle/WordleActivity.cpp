#include "WordleActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;
namespace ui = wordleui;

constexpr char kAnswersPath[] = "/.crosspoint/wordle.ans";
constexpr char kWordsPath[] = "/.crosspoint/wordle.words";
constexpr char kResultsPath[] = "/.crosspoint/wordle.res";
constexpr char kGamesPath[] = "/.crosspoint/wordle.games";

// The archive: every answer since the first puzzle, in two files, and NYT's
// list of accepted guesses. Its daily update lands a day behind, so the days
// between its newest and today come from NYT's own per-day endpoint.
constexpr char kEra1Url[] = "https://raw.githubusercontent.com/mfilej/wrdl/main/solutions-era1.txt";
constexpr char kRecentUrl[] = "https://raw.githubusercontent.com/mfilej/wrdl/main/solutions.txt";
constexpr char kWordsUrl[] = "https://raw.githubusercontent.com/mfilej/wrdl/main/valid.txt";
constexpr char kDailyUrl[] = "https://www.nytimes.com/svc/wordle/v2/%04d-%02d-%02d.json";
// A gap longer than this is fetched from the archive rather than day by day.
constexpr int kMaxDailyFetches = 14;

const char* const kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

bool readAll(const char* path, std::string& out) {
  out.clear();
  HalFile file;
  if (!Storage.openFileForRead("WRDL", path, file)) return false;
  const size_t size = file.fileSize();
  out.resize(size);
  return size == 0 || file.read(&out[0], size) == static_cast<int>(size);
}

// Written beside and renamed over, so a power cut leaves the old file whole.
bool writeAll(const char* path, const char* data, const size_t size) {
  char tmp[48];
  std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  Storage.remove(tmp);
  {
    HalFile file;
    if (!Storage.openFileForWrite("WRDL", tmp, file)) return false;
    if (size > 0 && file.write(reinterpret_cast<const uint8_t*>(data), size) != size) {
      file.close();
      Storage.remove(tmp);
      return false;
    }
    file.close();
  }
  Storage.remove(path);
  return Storage.rename(tmp, path);
}

void formatDay(const int day, const bool withYear, char* out, const size_t cap) {
  int y = 0;
  int m = 0;
  int d = 0;
  wordle::dateOfDay(day, y, m, d);
  if (withYear) {
    std::snprintf(out, cap, "%d %s %d", d, kMonths[m - 1], y);
  } else {
    std::snprintf(out, cap, "%s %d", kMonths[m - 1], d);
  }
}

// Lines split across network chunks are carried to the next chunk.
struct LineFeed {
  std::string partial;
  void (*onLine)(void* ctx, const char* line, size_t length) = nullptr;
  void* ctx = nullptr;

  void feed(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; ++i) {
      const char c = static_cast<char>(data[i]);
      if (c == '\n') {
        onLine(ctx, partial.data(), partial.size());
        partial.clear();
      } else if (partial.size() < 64) {
        partial += c;
      }
    }
  }
  void finish() {
    if (!partial.empty()) onLine(ctx, partial.data(), partial.size());
    partial.clear();
  }
};

bool isListed(void* ctx, const char* word) {
  const auto* self = static_cast<std::pair<const char*, size_t>*>(ctx);
  return wordle::isWord(self->first, self->second, word);
}

}  // namespace

std::unique_ptr<Activity> WordleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WordleActivity>(renderer, mappedInput);
}

void WordleActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  readAll(kAnswersPath, answers_);
  readAll(kResultsPath, results_);
  view_ = View::Menu;
  requestUpdate();
}

void WordleActivity::onExit() {
  if (view_ == View::Game) saveGame();
  words_.reset();
  Activity::onExit();
  if (wifiActivated_ && !devmode::holdsRadio()) {
    // The teardown every Wi-Fi user in this firmware performs.
    WiFi.disconnect(false);
    esp_wifi_stop();
    delay(30);
    silentRestart();
  }
}

bool WordleActivity::loadWords() {
  if (words_) return true;
  HalFile file;
  if (!Storage.openFileForRead("WRDL", kWordsPath, file)) return false;
  const size_t size = file.fileSize();
  if (size == 0 || size % wordle::kLetters != 0) return false;
  words_ = makeUniqueNoThrow<char[]>(size);
  if (!words_) {
    LOG_ERR("WRDL", "OOM: word list %u bytes", static_cast<unsigned>(size));
    return false;
  }
  if (file.read(words_.get(), size) != static_cast<int>(size)) {
    words_.reset();
    return false;
  }
  wordCount_ = size / wordle::kLetters;
  return true;
}

int WordleActivity::today() const {
  const time_t now = time(nullptr);
  struct tm parts;
  if (now > 0) localtime_r(&now, &parts);
  // No clock: the newest puzzle on the card stands in for today.
  if (now <= 0 || parts.tm_year + 1900 < 2021) return wordle::lastKnownDay(answers_);
  return wordle::dayIndex(parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
}

// The puzzle the menu offers: today's, or the newest held when today's is not
// on the card yet.
int WordleActivity::newestDay() const {
  const int last = wordle::lastKnownDay(answers_);
  const int now = today();
  return last < now ? last : now;
}

void WordleActivity::openDay(const int day) {
  char answer[wordle::kLetters];
  if (!wordle::answerFor(answers_, day, answer)) return;
  if (!loadWords()) {
    message_ = "Get the puzzles again.";
    return;
  }
  // Every game played is kept, one line per day, so a day reopened from the
  // archive shows what was put down.
  game_.start(day, answer);
  std::string games;
  readAll(kGamesPath, games);
  char key[16];
  const int keyLen = std::snprintf(key, sizeof(key), "%d ", day);
  size_t at = 0;
  while (at < games.size()) {
    const size_t end = games.find('\n', at);
    const size_t len = (end == std::string::npos ? games.size() : end) - at;
    if (len > static_cast<size_t>(keyLen) && games.compare(at, keyLen, key) == 0) {
      if (!game_.load(day, games.substr(at + keyLen, len - keyLen))) game_.start(day, answer);
      break;
    }
    if (end == std::string::npos) break;
    at = end + 1;
  }
  if (wordle::resultOf(results_, day) == 0) {
    wordle::putResult(results_, day, static_cast<uint8_t>(wordle::kStarted | (day == today() ? wordle::kOnTheDay : 0)));
    writeAll(kResultsPath, results_.data(), results_.size());
  }
  message_ = nullptr;
  view_ = View::Game;
  LOG_INF("WRDL", "day %d opened, %d guess(es) in", day, game_.guesses());
}

void WordleActivity::saveGame() {
  if (game_.day() < 0) return;
  std::string games;
  readAll(kGamesPath, games);
  char key[16];
  const int keyLen = std::snprintf(key, sizeof(key), "%d ", game_.day());
  std::string out;
  out.reserve(games.size() + 48);
  size_t at = 0;
  while (at < games.size()) {
    const size_t end = games.find('\n', at);
    const size_t len = (end == std::string::npos ? games.size() : end) - at;
    if (!(len > static_cast<size_t>(keyLen) && games.compare(at, keyLen, key) == 0) && len > 0) {
      out.append(games, at, len);
      out += '\n';
    }
    if (end == std::string::npos) break;
    at = end + 1;
  }
  if (game_.guesses() > 0) {
    out += key;
    out += game_.save();
    out += '\n';
  }
  if (!writeAll(kGamesPath, out.data(), out.size())) LOG_ERR("WRDL", "could not save day %d", game_.day());
}

void WordleActivity::submit() {
  std::pair<const char*, size_t> list{words_.get(), wordCount_};
  const wordle::Game::Submit result = game_.submit(isListed, &list);
  message_ = nullptr;
  switch (result) {
    case wordle::Game::Submit::Short:
    case wordle::Game::Submit::Over:
      return;
    case wordle::Game::Submit::NotAWord:
      message_ = "Not in the word list.";
      return;
    case wordle::Game::Submit::Scored:
      saveGame();
      return;
    case wordle::Game::Submit::Won:
    case wordle::Game::Submit::Lost: {
      saveGame();
      // The first finish is the one kept: replaying a day never rewrites it,
      // and a finish on the puzzle's own date is what the record counts.
      const uint8_t before = wordle::resultOf(results_, game_.day());
      if ((before & 0x7F) == 0 || (before & 0x7F) == wordle::kStarted) {
        const uint8_t value =
            result == wordle::Game::Submit::Won ? static_cast<uint8_t>(game_.guesses()) : wordle::kLost;
        const bool onTheDay = game_.day() == today();
        wordle::putResult(results_, game_.day(), static_cast<uint8_t>(value | (onTheDay ? wordle::kOnTheDay : 0)));
        writeAll(kResultsPath, results_.data(), results_.size());
      }
      LOG_INF("WRDL", "day %d %s in %d", game_.day(), result == wordle::Game::Submit::Won ? "won" : "lost",
              game_.guesses());
      return;
    }
  }
}

// --- Archive ----------------------------------------------------------------

void WordleActivity::showMonthOf(const int day) {
  int d = 0;
  wordle::dateOfDay(day, calYear_, calMonth_, d);
  buildCalendar();
}

void WordleActivity::buildCalendar() {
  for (auto& cell : calCells_) cell = connectionsui::CalendarDay{};
  calPlayed_ = 0;
  const int first = wordle::dayIndex(calYear_, calMonth_, 1);
  const int next =
      calMonth_ == 12 ? wordle::dayIndex(calYear_ + 1, 1, 1) : wordle::dayIndex(calYear_, calMonth_ + 1, 1);
  // Day 0, 2021-06-19, was a Saturday; cells run Sunday first.
  const int lead = ((first % 7) + 7 + 6) % 7;
  const int newest = newestDay();
  for (int day = first; day < next; ++day) {
    const int i = lead + day - first;
    if (i >= 42) break;
    connectionsui::CalendarDay& cell = calCells_[i];
    cell.day = static_cast<uint8_t>(day - first + 1);
    char w[wordle::kLetters];
    cell.inArchive = day <= newest && wordle::answerFor(answers_, day, w);
    if (!cell.inArchive) continue;
    const uint8_t r = wordle::resultOf(results_, day) & 0x7F;
    cell.played = r != 0;
    cell.finished = (r >= 1 && r <= wordle::kRows) || r == wordle::kLost;
    cell.lost = r == wordle::kLost;
    cell.mistakes = 0;  // a solved day shows the sparkle, however many guesses
    if (cell.finished) ++calPlayed_;
  }
}

bool WordleActivity::canStep(const int months) const {
  int month = calMonth_ + months;
  int year = calYear_;
  while (month < 1) {
    month += 12;
    --year;
  }
  while (month > 12) {
    month -= 12;
    ++year;
  }
  const int first = wordle::dayIndex(year, month, 1);
  const int next = month == 12 ? wordle::dayIndex(year + 1, 1, 1) : wordle::dayIndex(year, month + 1, 1);
  return next > 0 && first <= newestDay();
}

void WordleActivity::step(const int months) {
  if (!canStep(months)) return;
  calMonth_ += months;
  while (calMonth_ < 1) {
    calMonth_ += 12;
    --calYear_;
  }
  while (calMonth_ > 12) {
    calMonth_ -= 12;
    ++calYear_;
  }
  buildCalendar();
}

// --- Download ----------------------------------------------------------------

void WordleActivity::beginImport() {
  view_ = View::Importing;
  importStep_ = ImportStep::Connecting;
  imported_ = 0;
  importDetail_ = "JOINING WI-FI";
  requestUpdate();
  wifiActivated_ = true;
  if (WiFi.status() == WL_CONNECTED) {
    importStep_ = ImportStep::Ready;
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             importStep_ = ImportStep::Failed;
                             importDetail_ = "NO WI-FI";
                           } else {
                             importStep_ = ImportStep::Ready;
                             importDetail_ = "DOWNLOADING";
                           }
                           requestUpdate();
                         });
}

void WordleActivity::runImport() {
  std::string answers = answers_;
  const bool haveWords = Storage.exists(kWordsPath);
  const int now = today();
  const int held = wordle::lastKnownDay(answers);

  // The archive, when the card has none or is too far behind to top up.
  if (!haveWords || held < 0 || now - held > kMaxDailyFetches) {
    LineFeed lines;
    lines.ctx = &answers;
    lines.onLine = [](void* ctx, const char* line, const size_t length) {
      int day = -1;
      char w[wordle::kLetters];
      if (wordle::parseAnswerLine(line, length, day, w)) wordle::putAnswer(*static_cast<std::string*>(ctx), day, w);
    };
    bool fetched = true;
    for (const char* url : {kEra1Url, kRecentUrl}) {
      fetched = fetched && HttpDownloader::fetchUrl(url, [&lines](const uint8_t* data, const size_t len) {
                  lines.feed(data, len);
                  return true;
                });
      lines.finish();
    }

    std::string words;
    words.reserve(80000);
    bool sorted = true;
    LineFeed wordLines;
    struct WordSink {
      std::string* out;
      bool* sorted;
    } sink{&words, &sorted};
    wordLines.ctx = &sink;
    wordLines.onLine = [](void* ctx, const char* line, size_t length) {
      auto* s = static_cast<WordSink*>(ctx);
      while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == ' ')) --length;
      if (length != wordle::kLetters) return;
      char w[wordle::kLetters];
      for (int i = 0; i < wordle::kLetters; ++i) {
        char c = line[i];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c < 'A' || c > 'Z') return;
        w[i] = c;
      }
      if (!s->out->empty() &&
          std::memcmp(s->out->data() + s->out->size() - wordle::kLetters, w, wordle::kLetters) >= 0) {
        *s->sorted = false;
      }
      s->out->append(w, wordle::kLetters);
    };
    const bool gotWords = HttpDownloader::fetchUrl(kWordsUrl, [&wordLines](const uint8_t* data, const size_t len) {
      wordLines.feed(data, len);
      return true;
    });
    wordLines.finish();

    if (!fetched || !gotWords || wordle::lastKnownDay(answers) < 0 || words.size() < 1000 * wordle::kLetters) {
      importStep_ = ImportStep::Failed;
      importDetail_ = "DOWNLOAD FAILED";
      LOG_ERR("WRDL", "archive fetch failed: answers %d, words %u, status %d", fetched ? 1 : 0,
              static_cast<unsigned>(words.size()), HttpDownloader::lastStatus());
      requestUpdate();
      return;
    }
    if (!sorted) {
      importStep_ = ImportStep::Failed;
      importDetail_ = "BAD DATA";
      LOG_ERR("WRDL", "guess list arrived out of order");
      requestUpdate();
      return;
    }
    if (!writeAll(kWordsPath, words.data(), words.size())) {
      importStep_ = ImportStep::Failed;
      importDetail_ = "CANNOT WRITE TO CARD";
      requestUpdate();
      return;
    }
    words_.reset();
  }

  // The days the archive has not caught up with, never past today, so nothing
  // on the card is a word nobody has played yet.
  for (int day = wordle::lastKnownDay(answers) + 1, fetches = 0; day <= now && fetches < kMaxDailyFetches;
       ++day, ++fetches) {
    int y = 0;
    int m = 0;
    int d = 0;
    wordle::dateOfDay(day, y, m, d);
    char url[80];
    std::snprintf(url, sizeof(url), kDailyUrl, y, m, d);
    std::string body;
    int got = -1;
    char w[wordle::kLetters];
    if (!HttpDownloader::fetchUrl(url, body) || !wordle::parseDailyJson(body, got, w) || got != day) {
      LOG_ERR("WRDL", "day %d from NYT: status %d", day, HttpDownloader::lastStatus());
      break;
    }
    wordle::putAnswer(answers, day, w);
  }

  if (!writeAll(kAnswersPath, answers.data(), answers.size())) {
    importStep_ = ImportStep::Failed;
    importDetail_ = "CANNOT WRITE TO CARD";
    requestUpdate();
    return;
  }
  answers_ = answers;
  imported_ = wordle::lastKnownDay(answers_) + 1;
  importStep_ = ImportStep::Done;
  importDetail_ = "TAP TO PLAY";
  LOG_INF("WRDL", "puzzles through day %d", wordle::lastKnownDay(answers_));
  requestUpdate();
}

// --- Input -------------------------------------------------------------------

void WordleActivity::loop() {
  if (view_ == View::Importing && importStep_ == ImportStep::Ready) {
    importStep_ = ImportStep::Downloading;
    importDetail_ = "DOWNLOADING";
    // The DOWNLOADING frame must be on the panel before the fetch blocks.
    requestUpdateAndWait();
    return;
  }
  if (view_ == View::Importing && importStep_ == ImportStep::Downloading) {
    runImport();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Menu) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    if (view_ == View::Importing && importStep_ != ImportStep::Done && importStep_ != ImportStep::Failed) return;
    if (view_ == View::Game) saveGame();
    view_ = View::Menu;
    requestUpdate();
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (view_ == View::Importing) {
    if (importStep_ == ImportStep::Done || importStep_ == ImportStep::Failed) {
      view_ = View::Menu;
      requestUpdate();
    }
    return;
  }
  if (view_ == View::HowTo) {
    view_ = View::Menu;
    requestUpdate();
    return;
  }
  if (!interactionsReady_) return;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  routeAction(interactions_.route(input), tapX, tapY);
}

void WordleActivity::routeAction(const fui::ActionEvent& event, const int tapX, const int tapY) {
  switch (event.action) {
    case ui::ActionKeyboard: {
      const char key = ui::keyAt(keys_, tapX, tapY);
      if (key == 0) return;
      message_ = nullptr;
      if (key == ui::kEnter) {
        submit();
      } else if (key == ui::kErase) {
        game_.erase();
      } else {
        game_.type(key);
      }
      requestUpdate();
      return;
    }
    case ui::ActionAnywhere:
      view_ = View::Menu;
      requestUpdate();
      return;
    case ui::ActionMenu:
      switch (event.value) {
        case 0:
          if (newestDay() >= 0) openDay(newestDay());
          break;
        case 1:
          view_ = View::HowTo;
          break;
        case 2:
          if (newestDay() < 0) return;
          showMonthOf(newestDay());
          view_ = View::Archive;
          break;
        case 3:
          beginImport();
          return;
        default:
          return;
      }
      requestUpdate();
      return;
    case connectionsui::ActionCalendarDay: {
      int cell = -1;
      if (!connectionsui::dayCellAt(calLayout_, tapX, tapY, cell)) return;
      const connectionsui::CalendarDay& day = calCells_[cell];
      if (day.day == 0 || !day.inArchive) return;
      openDay(wordle::dayIndex(calYear_, calMonth_, day.day));
      requestUpdate();
      return;
    }
    case connectionsui::ActionCalendarToday:
      showMonthOf(newestDay());
      requestUpdate();
      return;
    case connectionsui::ActionCalendarYear:
      step(event.value < 0 ? -12 : 12);
      requestUpdate();
      return;
    case connectionsui::ActionCalendarMonth:
      step(event.value < 0 ? -1 : 1);
      requestUpdate();
      return;
    default:
      return;
  }
}

// --- Render ------------------------------------------------------------------

void WordleActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // The archive and the download screen are Connections', so they are drawn in
  // its faces; the game and the menu in Toybox's own.
  const bool borrowed = view_ == View::Archive || view_ == View::Importing;
  fui::GfxRendererTarget target =
      borrowed ? toybox::makeTarget(renderer, toybox::serifMenuFaces()) : toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  calLayout_ = connectionsui::CalendarLayout{};

  switch (view_) {
    case View::Game: {
      char date[16];
      formatDay(game_.day(), false, date, sizeof(date));
      ui::GameModel model;
      model.game = &game_;
      model.date = date;
      model.message = message_;
      keys_ = ui::buildGame(screen, model);
      break;
    }
    case View::Archive: {
      connectionsui::CalendarModel cal;
      cal.year = calYear_;
      cal.month = calMonth_;
      cal.cells = calCells_;
      cal.playedThisMonth = calPlayed_;
      cal.canPrevYear = canStep(-12);
      cal.canNextYear = canStep(12);
      cal.canPrevMonth = canStep(-1);
      cal.canNextMonth = canStep(1);
      const int now = today();
      int y = 0;
      int m = 0;
      int d = 0;
      wordle::dateOfDay(now, y, m, d);
      if (y == calYear_ && m == calMonth_) {
        const int first = wordle::dayIndex(calYear_, calMonth_, 1);
        cal.todayCell = ((first % 7) + 7 + 6) % 7 + d - 1;
      }
      calLayout_ = connectionsui::buildCalendar(screen, cal);
      break;
    }
    case View::HowTo:
      ui::buildHowTo(screen);
      break;
    case View::Importing: {
      connectionsui::ImportModel model;
      model.detail = importDetail_;
      model.puzzles = imported_;
      model.done = importStep_ == ImportStep::Done;
      model.failed = importStep_ == ImportStep::Failed;
      connectionsui::buildImport(screen, model);
      break;
    }
    case View::Menu: {
      ui::MenuModel model;
      const int day = newestDay();
      char date[20];
      char state[24];
      if (day >= 0) {
        formatDay(day, true, date, sizeof(date));
        model.date = date;
        const uint8_t r = wordle::resultOf(results_, day) & 0x7F;
        if (r >= 1 && r <= wordle::kRows) {
          std::snprintf(state, sizeof(state), "SOLVED IN %d", r);
          model.state = state;
        } else if (r == wordle::kLost) {
          model.state = "NOT SOLVED";
        } else if (r == wordle::kStarted) {
          model.state = "IN PROGRESS";
        }
      }
      model.stats = wordle::statsFor(results_, today());
      model.puzzles = day + 1;
      model.upToDate = day >= 0 && wordle::lastKnownDay(answers_) >= today();
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Wordle");
  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
