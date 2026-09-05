#include "ConnectionsActivity.h"

#include <HalStorage.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/ActivityManager.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "ConnectionsImport.h"
#include "ConnectionsResults.h"

namespace {

namespace fui = freeink::ui;
namespace ui = connectionsui;

constexpr char kIndexPath[] = "/.crosspoint/connections.idx";
constexpr char kDataPath[] = "/.crosspoint/connections.dat";
constexpr char kSavePath[] = "/.crosspoint/connections.sav";
// One byte per puzzle, indexed by days since the archive began. Survives a
// re-import, because it is keyed on the date rather than on a pack position.
constexpr char kResultsPath[] = "/.crosspoint/connections.res";
constexpr char kResultsTmp[] = "/.crosspoint/connections.res.tmp";
// Eleven years of daily puzzles. The file only grows to the newest date played,
// so this is a ceiling, not an allocation.
constexpr int kMaxResults = 4096;
// Written to `.tmp` and renamed on success, so a download interrupted halfway
// leaves the pack you were playing untouched rather than half-replaced.
constexpr char kIndexTmp[] = "/.crosspoint/connections.idx.tmp";
constexpr char kDataTmp[] = "/.crosspoint/connections.dat.tmp";

// The published archive: every puzzle since 2023 in one file, updated daily.
// One fetch is a top-up, not a sync -- there is nothing to come back for until
// you want more, which is why this is a button and not a background task.
constexpr char kArchiveUrl[] =
    "https://raw.githubusercontent.com/Eyefyre/NYT-Connections-Answers/main/connections.json";

// Streaming state for the import. File-scope because the download callback is a
// plain function pointer with no context of its own.
HalFile gOutIndex;
HalFile gOutData;
connections::PackWriter gWriter;
bool gWriteFailed = false;
// Read by the progress repaint while the download blocks. Plain globals because
// the write callback is a function pointer with no context of its own.
int gImportedSoFar = 0;
uint32_t gReachedDate = 0;

bool writeTo(void* ctx, const void* src, const uint32_t len) {
  auto* file = static_cast<HalFile*>(ctx);
  if (file->write(src, len) != len) {
    gWriteFailed = true;
    return false;
  }
  return true;
}

// The two pack files stay open for the life of the activity: a binary search is
// a dozen seeks, and reopening per read would dominate it.
HalFile gIndexFile;
HalFile gDataFile;

bool readAt(void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
  auto* file = static_cast<HalFile*>(ctx);
  if (!file->seek(offset)) return false;
  return file->read(dst, len) == static_cast<int>(len);
}

// Today, as YYYYMMDD. The device keeps an RTC; without one this falls back to
// the newest puzzle in the pack, which is what indexOnOrBefore already does for
// any date past the end.
uint32_t today() {
  const time_t now = time(nullptr);
  if (now <= 0) return 99999999u;
  struct tm parts;
  localtime_r(&now, &parts);
  return static_cast<uint32_t>((parts.tm_year + 1900) * 10000 + (parts.tm_mon + 1) * 100 + parts.tm_mday);
}

}  // namespace

std::unique_ptr<Activity> ConnectionsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ConnectionsActivity>(renderer, mappedInput);
}

bool ConnectionsActivity::openPack() {
  closePack();
  if (!Storage.exists(kIndexPath) || !Storage.exists(kDataPath)) return false;
  if (!Storage.openFileForRead("CONN", kIndexPath, gIndexFile)) return false;
  if (!Storage.openFileForRead("CONN", kDataPath, gDataFile)) return false;
  packOpen = pack.open(readAt, &gIndexFile, &gDataFile, static_cast<uint32_t>(gIndexFile.fileSize()));
  if (packOpen && pack.count() > 0) {
    pack.dateAt(0, firstDate);
    pack.dateAt(pack.count() - 1, lastDate);
  }
  return packOpen;
}

void ConnectionsActivity::closePack() {
  packOpen = false;
  gIndexFile = HalFile{};
  gDataFile = HalFile{};
}

void ConnectionsActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  openPack();
  // Always the menu. A saved game is still resumed -- loadProgress() has run and
  // the board is exactly where it was left -- but arriving straight on it robs
  // you of the one screen that says what there is to do, and there is no way
  // back to it that is not "leave the puzzle you were in".
  loadProgress();
  view = View::Menu;
  requestUpdate();
}

void ConnectionsActivity::onExit() {
  if (view == View::Board && puzzleIndex >= 0) saveProgress();
  closePack();
  Activity::onExit();
  if (wifiActivated && !devmode::holdsRadio()) {
    // Same teardown every wifi user in this firmware performs: drop the radio,
    // then restart to clear the heap fragmentation a TLS session leaves behind.
    // Skipped when Developer Mode holds the radio -- wifiActivated says this app
    // wanted Wi-Fi, not that it was the one who switched it on.
    WiFi.disconnect(false);
    esp_wifi_stop();
    delay(30);
    silentRestart();
  }
}

bool ConnectionsActivity::startPuzzle(const int index) {
  connections::Puzzle puzzle;
  if (!packOpen || !pack.readPuzzle(index, puzzle)) return false;
  puzzleIndex = index;
  // Seeded from the puzzle's own id, so the same puzzle always deals the same
  // board: reopening it after a week shows what you put down, not a new shuffle.
  game.start(puzzle, 0x9E3779B9u ^ (static_cast<uint32_t>(puzzle.id) * 2654435761u));
  toast = nullptr;
  view = View::Board;
  return true;
}

void ConnectionsActivity::saveProgress() const {
  // Format 2 adds the guess history. Version 1 files are not migrated: the only
  // thing lost is one puzzle's progress, and silently resuming without the
  // history would let a mistake be spent on a combination already ruled out.
  const connections::Game::Save state = game.save();
  char line[192];
  int at = std::snprintf(line, sizeof(line), "2 %d %u %u %u %u %u %u %u", puzzleIndex, state.seed,
                         static_cast<unsigned>(state.solvedCount), static_cast<unsigned>(state.mistakes),
                         static_cast<unsigned>(state.solvedOrder[0]), static_cast<unsigned>(state.solvedOrder[1]),
                         static_cast<unsigned>(state.solvedOrder[2]), static_cast<unsigned>(state.guessCount));
  for (int i = 0; i < state.guessCount && at > 0 && at < static_cast<int>(sizeof(line)) - 8; ++i) {
    at += std::snprintf(line + at, sizeof(line) - static_cast<size_t>(at), " %u",
                        static_cast<unsigned>(state.guessed[i]));
  }
  std::snprintf(line + at, sizeof(line) - static_cast<size_t>(at), "\n");
  Storage.writeFile(kSavePath, String(line));
}

bool ConnectionsActivity::loadProgress() {
  if (!Storage.exists(kSavePath)) return false;
  char buffer[96] = {};
  if (Storage.readFileToBuffer(kSavePath, buffer, sizeof(buffer)) == 0) return false;
  int version = 0;
  int index = 0;
  unsigned seed = 0;
  unsigned solved = 0;
  unsigned mistakes = 0;
  unsigned order[3] = {};
  unsigned guessCount = 0;
  int consumed = 0;
  if (std::sscanf(buffer, "%d %d %u %u %u %u %u %u %u%n", &version, &index, &seed, &solved, &mistakes, &order[0],
                  &order[1], &order[2], &guessCount, &consumed) != 9) {
    return false;
  }
  if (version != 2 || !startPuzzle(index)) return false;

  connections::Game::Save state;
  state.seed = seed;
  state.solvedCount = static_cast<uint8_t>(solved);
  state.mistakes = static_cast<uint8_t>(mistakes);
  for (int i = 0; i < 3; ++i) state.solvedOrder[i] = static_cast<uint8_t>(order[i]);
  state.guessCount = static_cast<uint8_t>(guessCount > 8 ? 8 : guessCount);
  const char* at = buffer + consumed;
  for (int i = 0; i < state.guessCount; ++i) {
    unsigned mask = 0;
    int used = 0;
    if (std::sscanf(at, " %u%n", &mask, &used) != 1) return false;
    state.guessed[i] = static_cast<uint16_t>(mask);
    at += used;
  }
  // The fourth group is implied: solving three leaves one, and the save stays
  // one line. restore() rejects the state if that is inconsistent.
  if (solved == connections::kGroups) {
    for (int g = 0; g < connections::kGroups; ++g) {
      bool used = false;
      for (int i = 0; i < 3; ++i) {
        if (state.solvedOrder[i] == g) used = true;
      }
      if (!used) state.solvedOrder[3] = static_cast<uint8_t>(g);
    }
  }
  return game.restore(state);
}

void ConnectionsActivity::showMonthOf(const uint32_t date) {
  if (date == 0) return;
  calYear = static_cast<int>(date / 10000);
  calMonth = static_cast<int>((date / 100) % 100);
  buildCalendar();
}

uint32_t ConnectionsActivity::dateForCell(const int cell) const {
  if (cell < 0 || cell >= 42) return 0;
  const connectionsui::CalendarDay& day = calCells[cell];
  if (day.day == 0 || !day.inArchive) return 0;
  return static_cast<uint32_t>(calYear) * 10000u + static_cast<uint32_t>(calMonth) * 100u +
         static_cast<uint32_t>(day.day);
}

void ConnectionsActivity::buildCalendar() {
  for (auto& cell : calCells) cell = connectionsui::CalendarDay{};
  calPlayed = 0;
  const uint32_t firstOfMonth = static_cast<uint32_t>(calYear) * 10000u + static_cast<uint32_t>(calMonth) * 100u + 1u;
  const int lead = connections::dayOfWeek(firstOfMonth);
  const int count = connections::daysInMonth(calYear, calMonth);
  if (lead < 0 || count == 0) return;

  for (int d = 1; d <= count; ++d) {
    const int i = lead + d - 1;
    if (i >= 42) break;
    const uint32_t date = firstOfMonth + static_cast<uint32_t>(d - 1);
    calCells[i].day = static_cast<uint8_t>(d);
    // A date is in the archive when the pack actually holds it, not merely when
    // it falls in range: two dates are missing on purpose (emoji puzzles) and a
    // range check would offer them and then fail to open them.
    calCells[i].inArchive = packOpen && pack.indexOfDate(date) >= 0;
    if (!calCells[i].inArchive) continue;
    const connections::Record record = readResult(date);
    calCells[i].played = record.played;
    calCells[i].finished = record.played;
    calCells[i].lost = record.lost;
    calCells[i].mistakes = record.mistakes;
    if (record.played) ++calPlayed;
  }
}

bool ConnectionsActivity::canStepMonth(const int delta) const {
  int month = calMonth + delta;
  int year = calYear;
  if (month < 1) {
    month = 12;
    --year;
  } else if (month > 12) {
    month = 1;
    ++year;
  }
  // A month is reachable when any of its days is in the archive, so stepping
  // stops at the edges instead of opening a month of dithered numbers.
  const uint32_t firstOfMonth = static_cast<uint32_t>(year) * 10000u + static_cast<uint32_t>(month) * 100u + 1u;
  const uint32_t lastOfMonth = firstOfMonth + static_cast<uint32_t>(connections::daysInMonth(year, month)) - 1u;
  if (firstDate != 0 && lastOfMonth < firstDate) return false;
  if (lastDate != 0 && firstOfMonth > lastDate) return false;
  return true;
}

bool ConnectionsActivity::canStepYear(const int delta) const {
  const int year = calYear + delta;
  if (firstDate != 0 && year < static_cast<int>(firstDate / 10000)) return false;
  if (lastDate != 0 && year > static_cast<int>(lastDate / 10000)) return false;
  return true;
}

void ConnectionsActivity::stepMonth(const int delta) {
  if (!canStepMonth(delta)) return;
  int month = calMonth + delta;
  int year = calYear;
  if (month < 1) {
    month = 12;
    --year;
  } else if (month > 12) {
    month = 1;
    ++year;
  }
  calYear = year;
  calMonth = month;
  buildCalendar();
}

void ConnectionsActivity::stepYear(const int delta) {
  if (!canStepYear(delta)) return;
  calYear += delta;
  buildCalendar();
}

void ConnectionsActivity::handleSubmit() {
  const connections::Guess guess = game.submit();
  switch (guess) {
    case connections::Guess::OneAway:
      toast = "ONE AWAY";
      break;
    case connections::Guess::Wrong:
      toast = "NOT A GROUP";
      break;
    case connections::Guess::AlreadyGuessed:
      toast = "ALREADY GUESSED";
      requestUpdate();
      return;
    case connections::Guess::Solved:
      toast = nullptr;
      break;
    default:
      return;
  }
  if (game.result() != connections::Result::Playing) {
    saveProgress();
    connections::Record record;
    record.played = true;
    record.lost = game.result() == connections::Result::Lost;
    record.mistakes = static_cast<uint8_t>(game.mistakes());
    // Only claimable where the source published colour data; solvedGroup(0) is
    // the group found first.
    record.hardestFirst = !record.lost && game.solvedCount() > 0 && game.solvedGroup(0).level == 3;
    writeResult(game.puzzle().date, record);
  }
  requestUpdate();
}

connections::Record ConnectionsActivity::readResult(const uint32_t date) const {
  connections::Record record;
  const int index = connections::resultIndex(date);
  if (index < 0) return record;
  HalFile file;
  if (!Storage.openFileForRead("CONN", kResultsPath, file)) return record;
  if (!file.seek(static_cast<size_t>(index))) return record;
  uint8_t byte = 0;
  if (file.read(&byte, 1) != 1) return record;
  return connections::decodeRecord(byte);
}

void ConnectionsActivity::fillStats(connectionsui::MenuModel& model) const {
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kMaxResults);
  if (!buffer) return;
  std::memset(buffer.get(), 0, kMaxResults);
  HalFile file;
  size_t read = 0;
  if (Storage.openFileForRead("CONN", kResultsPath, file)) {
    read = file.read(buffer.get(), kMaxResults);
  }

  const connections::Summary summary =
      connections::summarise(buffer.get(), static_cast<int>(read), connections::resultIndex(model.newestDate));
  model.played = summary.played;
  model.perfect = summary.perfect;
  model.streak = summary.streak;
  model.todayDone = summary.newestPlayed;
  model.todayMistakes = summary.newestMistakes;
  for (int i = 0; i < 16; ++i) {
    model.recent[i] = static_cast<connectionsui::DayResult>(summary.recent[i]);
  }

  // A puzzle loaded but not yet recorded is one you walked away from. Date 0
  // means nothing is loaded, and no real puzzle carries date 0.
  model.inProgress = game.puzzle().date == model.newestDate && !summary.newestPlayed;
}

void ConnectionsActivity::writeResult(const uint32_t date, const connections::Record& record) const {
  const int index = connections::resultIndex(date);
  if (index < 0 || index >= kMaxResults) return;

  // Read, patch, rewrite. The whole file is ~1.2KB today and grows a byte a
  // day, so rewriting it is cheaper than the seek-and-poke this API cannot do,
  // and it goes through a temp file for the same reason the pack does: this is
  // every score you have, and a half-finished write should not cost the lot.
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kMaxResults);
  if (!buffer) {
    LOG_ERR("CONN", "OOM writing results");
    return;
  }
  std::memset(buffer.get(), 0, kMaxResults);

  size_t existing = 0;
  HalFile in;
  if (Storage.openFileForRead("CONN", kResultsPath, in)) {
    existing = in.fileSize();
    if (existing > kMaxResults) existing = kMaxResults;
    if (in.read(buffer.get(), existing) != static_cast<int>(existing)) existing = 0;
    in = HalFile{};
  }
  // A puzzle played out of order leaves zeroes behind it, which decode as
  // "never played" -- exactly right, and why the file needs no other bookkeeping.
  const size_t length = existing > static_cast<size_t>(index) + 1 ? existing : static_cast<size_t>(index) + 1;
  buffer[index] = connections::encodeRecord(record);

  HalFile out;
  if (!Storage.openFileForWrite("CONN", kResultsTmp, out)) return;
  const bool ok = out.write(buffer.get(), length) == length;
  out.flush();
  out = HalFile{};
  if (!ok) {
    Storage.remove(kResultsTmp);
    return;
  }
  Storage.remove(kResultsPath);
  Storage.rename(kResultsTmp, kResultsPath);
}

uint32_t ConnectionsActivity::newestPackDate() const {
  uint32_t date = 0;
  if (packOpen && pack.count() > 0) pack.dateAt(pack.count() - 1, date);
  return date;
}

void ConnectionsActivity::beginImport() {
  view = View::Importing;
  importStep = ImportStep::Connecting;
  importedCount = 0;
  importDetail = "JOINING WI-FI";
  requestUpdate();

  // Past this point the radio is on, and onExit owes a teardown.
  wifiActivated = true;
  if (WiFi.status() == WL_CONNECTED) {
    importStep = ImportStep::Ready;
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             importStep = ImportStep::Failed;
                             importDetail = "NO WI-FI";
                           } else {
                             importStep = ImportStep::Ready;
                             importDetail = "DOWNLOADING";
                           }
                           requestUpdate();
                         });
}

void ConnectionsActivity::runImport() {
  // The pack being replaced must not be open underneath us.
  closePack();
  Storage.remove(kIndexTmp);
  Storage.remove(kDataTmp);
  gWriteFailed = false;

  if (!Storage.openFileForWrite("CONN", kIndexTmp, gOutIndex) ||
      !Storage.openFileForWrite("CONN", kDataTmp, gOutData) || !gWriter.begin(writeTo, &gOutIndex, &gOutData)) {
    importStep = ImportStep::Failed;
    importDetail = "CANNOT WRITE TO CARD";
    requestUpdate();
    return;
  }

  connections::Importer importer;
  importer.begin(
      [](void*, const connections::Puzzle& puzzle) {
        // A puzzle the writer rejects (out of order, unplayable) is skipped, not
        // fatal. Only a failed write stops the import, and gWriteFailed is what
        // distinguishes the two.
        if (gWriter.add(puzzle)) {
          gImportedSoFar = gWriter.written();
          gReachedDate = puzzle.date;
        }
        return !gWriteFailed;
      },
      nullptr);

  // Streamed, never buffered: the archive is ~1.3MB and this device has less RAM
  // than that. Chunks arrive at whatever size the socket produces and go straight
  // into the parser, so peak memory is one puzzle.
  // The fetch blocks for the whole download, so the only place a progress
  // repaint can happen is inside the chunk callback. Throttled to one every two
  // seconds: a partial refresh costs ~0.3s, and repainting per chunk would
  // spend more time drawing than downloading.
  gImportedSoFar = 0;
  gReachedDate = 0;
  uint32_t lastPaint = millis();
  const bool fetched =
      HttpDownloader::fetchUrl(kArchiveUrl, [this, &importer, &lastPaint](const uint8_t* data, const size_t len) {
        if (!importer.feed(data, len)) return false;
        // NOT SAFE HERE: painting from inside the fetch hangs the device. The
        // renderer expects to own the frame for the duration of a paint, and
        // this callback runs deep inside a blocking socket read, so
        // displayBuffer() never returns. Verified by hanging the simulator at
        // exactly the millisecond the fetch begins. Left as a marker: the count
        // below is live and correct, it just cannot be shown until the fetch
        // returns. Doing this properly means moving the download onto its own
        // task and letting the activity loop keep painting, which is a bigger
        // change than this slice.
        (void)lastPaint;
        return true;
      });
  const bool parsed = importer.finish();
  const int accepted = importer.stats().accepted;

  gOutIndex.flush();
  gOutData.flush();
  gOutIndex = HalFile{};
  gOutData = HalFile{};

  if (!fetched || !parsed || gWriteFailed || accepted == 0) {
    Storage.remove(kIndexTmp);
    Storage.remove(kDataTmp);
    importStep = ImportStep::Failed;
    importDetail = !fetched ? "DOWNLOAD FAILED" : (gWriteFailed ? "CARD FULL" : "BAD DATA");
    requestUpdate();
    return;
  }

  // Swap in only now that a complete, parsed pack exists on the card.
  Storage.remove(kIndexPath);
  Storage.remove(kDataPath);
  const bool swapped = Storage.rename(kIndexTmp, kIndexPath) && Storage.rename(kDataTmp, kDataPath);
  importedCount = accepted;
  importStep = swapped ? ImportStep::Done : ImportStep::Failed;
  importDetail = swapped ? "TAP TO PLAY" : "COULD NOT REPLACE PACK";
  openPack();
  requestUpdate();
}

void ConnectionsActivity::routeAction(const fui::ActionEvent& event) {
  switch (event.action) {
    case ui::ActionTile:
      toast = nullptr;
      game.toggleTile(event.value);
      requestUpdate();
      break;
    case ui::ActionSubmit:
      handleSubmit();
      break;
    case ui::ActionShuffle:
      toast = nullptr;
      game.shuffle(game.seed() * 1103515245u + 12345u);
      requestUpdate();
      break;
    case ui::ActionDeselect:
      toast = nullptr;
      game.deselectAll();
      requestUpdate();
      break;
    case ui::ActionOpenMenu:
      saveProgress();
      view = View::Menu;
      requestUpdate();
      break;
    case ui::ActionCalendarDay: {
      const uint32_t date = dateForCell(event.value);
      const int index = date != 0 ? pack.indexOfDate(date) : -1;
      if (index >= 0) {
        startPuzzle(index);
      }
      requestUpdate();
      break;
    }
    case ui::ActionCalendarToday:
      showMonthOf(lastDate != 0 && today() > lastDate ? lastDate : today());
      requestUpdate();
      break;
    case ui::ActionCalendarYear:
      stepYear(event.value);
      requestUpdate();
      break;
    case ui::ActionCalendarMonth:
      stepMonth(event.value);
      requestUpdate();
      break;
    case ui::ActionNewest:
      if (event.value == 0) {
        if (startPuzzle(pack.indexOnOrBefore(today()))) requestUpdate();
      } else if (event.value == 1) {
        view = View::Archive;
        showMonthOf(lastDate != 0 && today() > lastDate ? lastDate : today());
        requestUpdate();
      } else if (event.value == 3) {
        view = View::HowTo;
        requestUpdate();
      } else if (!(packOpen && pack.count() > 0 && newestPackDate() >= today())) {
        beginImport();
      }
      break;
    case ui::ActionHowTo:
      // Tap anywhere on the page to leave it.
      view = View::Menu;
      requestUpdate();
      break;
    default:
      break;
  }
}

void ConnectionsActivity::loop() {
  // Deferred by a pass so "DOWNLOADING" is on the panel before the socket
  // opens: the fetch blocks for seconds with no repaint and no input, which is
  // indistinguishable from a crash if the screen still says "JOINING WI-FI".
  if (view == View::Importing && importStep == ImportStep::Ready) {
    importStep = ImportStep::Downloading;
    importDetail = "DOWNLOADING";
    requestUpdate();
    return;
  }
  if (view == View::Importing && importStep == ImportStep::Downloading) {
    runImport();
    return;
  }
  if (view == View::Importing) {
    // Any input on a finished or failed import goes back to the menu.
    int x = 0;
    int y = 0;
    // No Confirm: it is PIN_UNASSIGNED on the X4 Pro and can never fire, so
    // OR-ing it in implied a control that is not there. Tap or Back.
    if (mappedInput.wasScreenTapped(x, y) || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (importStep == ImportStep::Done || importStep == ImportStep::Failed) {
        view = View::Menu;
        requestUpdate();
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      // See src/apps_local/Shelf.h: no app names its own destination.
      shelf::leave(renderer, mappedInput);
    } else {
      if (view == View::Board) saveProgress();
      view = View::Menu;
      requestUpdate();
    }
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // No button navigation. This fork targets the X4 Pro, which has a touch
  // panel; upstream's "every screen must work with buttons too" rule exists
  // because the X3 and X4 have none, and it does not apply here. Back leaves,
  // everything else is a tap -- which is one cursor, one focus model and one
  // whole class of bug that simply is not present.
  if (!input.touchReleased || !interactionsReady) return;
  fui::ActionEvent hit = interactions.route(input);
  // The month is one hit region, so the event says "somewhere in the calendar"
  // and the cell comes from the layout that drew it. A tap on a seam between
  // cells resolves to nothing and is dropped rather than rounded to a neighbour.
  if (hit.action == ui::ActionCalendarDay) {
    int cell = -1;
    if (!ui::dayCellAt(calLayout, tapX, tapY, cell)) return;
    hit.value = static_cast<int16_t>(cell);
  }
  routeAction(hit);
}

void ConnectionsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // The board needs both tile cuts bound at once so a long word can shrink;
  // the menu needs its rows in the serif instead. Same three slots, spent
  // differently.
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, view == View::Board ? toybox::serifBoardFaces() : toybox::serifMenuFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  // Cleared every paint, so a layout can only ever describe the screen that is
  // actually up. Only the archive view fills it in.
  calLayout = connectionsui::CalendarLayout{};

  switch (view) {
    case View::Board: {
      ui::BoardModel model;
      model.game = &game;
      model.date = game.puzzle().date;
      model.toast = toast;
      // Two passes with one slot rebound between them. Rebinding a font slot is
      // a single assignment on the target, so "three slots" was never a real
      // ceiling: the chrome speaks Jersey, the tiles speak the game's serif.
      {
        const ui::BoardLayout layout = ui::buildBoardChrome(screen, model);
        target.setFont(fui::GfxRendererTarget::FONT_BODY, toybox::kSerifSmallFontId);
        ui::buildBoardTiles(screen, model, layout);
      }
      break;
    }
    case View::Archive: {
      ui::CalendarModel cal;
      cal.year = calYear;
      cal.month = calMonth;
      cal.cells = calCells;
      cal.playedThisMonth = calPlayed;
      cal.canPrevYear = canStepYear(-1);
      cal.canNextYear = canStepYear(1);
      cal.canPrevMonth = canStepMonth(-1);
      cal.canNextMonth = canStepMonth(1);
      const uint32_t now = today();
      if (static_cast<int>(now / 10000) == calYear && static_cast<int>((now / 100) % 100) == calMonth) {
        const int lead = connections::dayOfWeek(static_cast<uint32_t>(calYear) * 10000u +
                                                static_cast<uint32_t>(calMonth) * 100u + 1u);
        if (lead >= 0) cal.todayCell = lead + static_cast<int>(now % 100) - 1;
      }
      calLayout = ui::buildCalendar(screen, cal);
      break;
    }
    case View::HowTo:
      ui::buildHowTo(screen);
      break;
    case View::Menu:
    default: {
      ui::MenuModel model;
      model.hasPuzzles = packOpen && pack.count() > 0;
      model.puzzleCount = packOpen ? pack.count() : 0;
      if (model.hasPuzzles) pack.dateAt(pack.count() - 1, model.newestDate);
      // The archive publishes daily, so holding today's puzzle means holding
      // all of them. Nothing to fetch, and the row stops offering.
      model.upToDate = model.hasPuzzles && model.newestDate >= today();
      model.selected = menuIndex;
      if (model.hasPuzzles) fillStats(model);
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Connections");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
