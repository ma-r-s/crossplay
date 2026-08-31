#include "Shelf.h"

#include <HalStorage.h>
#include <Logging.h>
#include <strings.h>

#include <cstdio>
#include <cstdlib>

#include "../activities/ActivityManager.h"
#include "ShelfFolderActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "battleship/BattleshipActivity.h"
#include "checkers/CheckersActivity.h"
#include "chess/ChessActivity.h"
#include "connectfour/ConnectFourActivity.h"
#include "connections/ConnectionsActivity.h"
#include "dungeon/DungeonActivity.h"
#include "forehead/ForeheadActivity.h"
#include "hackernews/HackerNewsActivity.h"
#include "insider/InsiderActivity.h"
#include "instapaper/InstapaperActivity.h"
#include "jaipur/JaipurActivity.h"
#include "knucklebones/KnucklebonesActivity.h"
#include "minesweeper/MinesweeperActivity.h"
#include "murdle/MurdleActivity.h"
#include "player/PlayerActivity.h"
#include "seasalt/SeaSaltActivity.h"
#include "solitaire/SolitaireActivity.h"
#include "study/StudyActivity.h"
#include "sudoku/SudokuActivity.h"
#include "toybattle/ToyBattleActivity.h"
#include "trivia/TriviaActivity.h"
#include "ui/ToyboxIcons.h"
#include "wavelength/WavelengthActivity.h"
#include "xkcd/XkcdActivity.h"
#include "yahtzee/YahtzeeActivity.h"

namespace {

// Icons come from tools_local/toybox/icons.txt via Lucide. Picked for silhouette
// rather than literalness: a crown, a hull, a grid and a card suit share no
// shape, so a row is scannable before the label is read.
constexpr shelf::Item kGames[] = {
    {"CHESS", &icon_chess_32, &ChessActivity::create},
    {"BATTLESHIP", &icon_battleship_32, &BattleshipActivity::create},
    {"CONNECTIONS", &icon_connections_32, &ConnectionsActivity::create},
    {"SOLITAIRE", &icon_solitaire_32, &SolitaireActivity::create},
    {"D&DIAGRAMS", &icon_dungeon_32, &DungeonActivity::create},
    {"INSIDER", &icon_insider_32, &InsiderActivity::create},
    {"JAIPUR", &icon_jaipur_32, &JaipurActivity::create},
    {"SEA SALT", &icon_seasalt_32, &SeaSaltActivity::create},
    {"MURDLE", &icon_murdle_32, &MurdleActivity::create},
    {"CHECKERS", &icon_checkers_32, &CheckersActivity::create},
    {"CONNECT FOUR", &icon_connectfour_32, &ConnectFourActivity::create},
    {"YAHTZEE", &icon_yahtzee_32, &YahtzeeActivity::create},
    {"KNUCKLEBONES", &icon_knucklebones_32, &KnucklebonesActivity::create},
    {"MINESWEEPER", &icon_minesweeper_32, &MinesweeperActivity::create},
    {"SUDOKU", &icon_sudoku_32, &SudokuActivity::create},
    {"TOY BATTLE", &icon_toybattle_32, &ToyBattleActivity::create},
    {"FOREHEAD", &icon_forehead_32, &ForeheadActivity::create},
    {"TRIVIA", &icon_trivia_32, &TriviaActivity::create},
    {"WAVELENGTH", &icon_wavelength_32, &WavelengthActivity::create},
};
constexpr shelf::Item kApps[] = {
    {"STUDY", &icon_study_32, &StudyActivity::create},
    {"HACKER NEWS", &icon_hackernews_32, &HackerNewsActivity::create},
    {"XKCD", &icon_xkcd_32, &XkcdActivity::create},
    {"GET BOOKS", &icon_getbooks_32, &OpdsBookBrowserActivity::create},
    {"INSTAPAPER", &icon_instapaper_32, &InstapaperActivity::create},
};

// The two rows Home grows, in reading order. Titles are Title Case because
// these sit in upstream's Home list and have to look like it; the folder screen
// shouts its own header, which is our side of the line. A third folder is one row here and
// nothing else: the Home hook counts this table rather than knowing its length.
constexpr shelf::Folder kFolders[] = {
    {"Games", UIIcon::Games, &icon_games_32, kGames, static_cast<int>(sizeof(kGames) / sizeof(shelf::Item)), true},
    {"Apps", UIIcon::Apps, &icon_apps_32, kApps, static_cast<int>(sizeof(kApps) / sizeof(shelf::Item)), false},
};

constexpr int kFolderCount = static_cast<int>(sizeof(kFolders) / sizeof(kFolders[0]));

// An item with no icon draws a blank gutter and nothing says so. Caught at
// compile time rather than in a test, because a test can be forgotten and this
// cannot: a new row without an icon does not build.
constexpr bool everyItemHasAnIcon() {
  for (const auto& folder : kFolders) {
    for (int i = 0; i < folder.count; ++i) {
      if (folder.items[i].icon == nullptr) return false;
    }
  }
  return true;
}
static_assert(everyItemHasAnIcon(), "every shelf item needs an icon; see tools_local/toybox/icons.txt");

constexpr bool everyFolderHasAMark() {
  for (const auto& folder : kFolders) {
    if (folder.mark == nullptr) return false;
  }
  return true;
}
static_assert(everyFolderHasAMark(), "every shelf folder needs a mark; see tools_local/toybox/icons.txt");

// Beside the reader's own state and the player's name, so clearing
// `.crosspoint/` clears this too and there is one place to look. Inside the
// guard because the host build has no storage and an unused constant is a
// -Werror failure there.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kStatePath[] = "/.crosspoint/shelf.cfg";
#endif

// Where leave() sends an app. Set when an item is opened, read when it leaves.
// -1 means "nothing is open below Home", which is what a folder itself sees.
int openFolderIndex = -1;

// Which shelf row Home should land on when you come back out. CrossPoint
// restores Home's selection by matching the departing activity's name against
// its own HomeMenuItem list, which cannot know about ours, so without this you
// leave GAMES and the cursor is sitting on Browse Files.
int lastFolder = -1;

// Per folder, the row that was opened last. Sized by the table so a third
// folder needs no thought here.
int lastItem[kFolderCount] = {};

// Both of the above survive the device going to sleep, which they did not
// before. `main.cpp` deep-sleeps on the idle timeout and says of it that wake is
// effectively a chip reset, so every time Mario put the device down and came
// back the shelf had forgotten which game he was playing. That was invisible
// while a folder held eight games and all eight were on screen. Now that the
// folder pages, forgetting also means landing on page one, so the game he plays
// most is behind a tap he should not have to make.
//
// Written next to the reader's own state rather than into CrossPointState,
// which is upstream's file: a fork-local fact belongs in a fork-local file, and
// player.cfg already established the pattern.
bool stateLoaded = false;

void loadState() {
  stateLoaded = true;
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kStatePath)) return;
  char buffer[64] = {};
  if (Storage.readFileToBuffer(kStatePath, buffer, sizeof(buffer)) == 0) return;

  // Parsed into locals and committed only if the whole line is good, so a
  // truncated or corrupt file leaves the defaults rather than half of them. The
  // worst this can cost is starting at the top, which is why it fails quietly
  // instead of logging: there is nothing for anyone to do about it.
  char* cursor = buffer;
  char* next = nullptr;
  const long folderValue = strtol(cursor, &next, 10);
  if (next == cursor) return;
  cursor = next;

  int items[kFolderCount] = {};
  for (int i = 0; i < kFolderCount; ++i) {
    const long value = strtol(cursor, &next, 10);
    if (next == cursor) return;
    cursor = next;
    // Clamped against the registry as it is now, not as it was when written. A
    // game removed since the last boot would otherwise select a row that no
    // longer exists.
    const int limit = kFolders[i].count - 1;
    items[i] = value < 0 ? 0 : (value > limit ? (limit > 0 ? limit : 0) : static_cast<int>(value));
  }

  lastFolder = folderValue < 0 || folderValue >= kFolderCount ? -1 : static_cast<int>(folderValue);
  for (int i = 0; i < kFolderCount; ++i) lastItem[i] = items[i];
#endif
}

void saveState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[64];
  int used = snprintf(line, sizeof(line), "%d", lastFolder);
  for (int i = 0; i < kFolderCount && used > 0 && used < static_cast<int>(sizeof(line)); ++i) {
    used += snprintf(line + used, sizeof(line) - used, " %d", lastItem[i]);
  }
  if (used <= 0 || used >= static_cast<int>(sizeof(line))) {
    LOG_ERR("SHELF", "State line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  snprintf(line + used, sizeof(line) - used, "\n");
  Storage.writeFile(kStatePath, String(line));
#endif
}

// Every path that reads or writes the remembered position goes through this
// first. Lazily rather than at boot because the shelf has no init hook, and
// unconditionally rather than only on the read paths because openFolder passes
// the current lastItem back in: without the load, the first navigation of a
// session would write the defaults over the saved file and the persistence
// would silently do nothing.
void ensureLoaded() {
  if (!stateLoaded) loadState();
}

// Only when something actually changed. Opening a folder happens on every Back,
// and SPIFFS sectors have a finite erase count, so an unconditional write here
// would be a write per navigation for no gain.
void saveIfChanged(const int folder, const int item) {
  ensureLoaded();
  if (lastFolder == folder && (folder < 0 || lastItem[folder] == item)) return;
  lastFolder = folder;
  if (folder >= 0) lastItem[folder] = item;
  saveState();
}

// Replaces the running activity, or logs and stays put. Every launch in this
// file funnels through here so an OOM cannot leave the shelf thinking it opened
// something it did not.
bool replaceWith(std::unique_ptr<Activity> activity, const char* what) {
  if (!activity) {
    LOG_ERR("SHELF", "OOM opening %s", what);
    return false;
  }
  activityManager.replaceActivity(std::move(activity));
  return true;
}

}  // namespace

namespace shelf {

const Folder* folders() { return kFolders; }

int folderCount() { return kFolderCount; }

void openFolder(const int index, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (index < 0 || index >= kFolderCount) {
    LOG_ERR("SHELF", "Bad folder index: %d", index);
    return;
  }
  // Opening a folder means nothing below it is open any more. Clearing here
  // rather than in leave() keeps the fact true even when a folder is reached by
  // some route that did not go through leave().
  openFolderIndex = -1;
  ensureLoaded();
  saveIfChanged(index, lastItem[index]);
  replaceWith(ShelfFolderActivity::create(renderer, mappedInput, index), kFolders[index].title);
}

void openItem(const int folder, const int item, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (folder < 0 || folder >= kFolderCount) {
    LOG_ERR("SHELF", "Bad folder index: %d", folder);
    return;
  }
  const Folder& parent = kFolders[folder];
  if (item < 0 || item >= parent.count) {
    LOG_ERR("SHELF", "Bad item index %d in %s", item, parent.title);
    return;
  }

  // Recorded before the launch, not after: replaceActivity destroys the caller,
  // so there is no "after" to run in.
  openFolderIndex = folder;
  saveIfChanged(folder, item);
  if (!replaceWith(parent.items[item].create(renderer, mappedInput), parent.items[item].title)) {
    openFolderIndex = -1;
  }
}

void autostartFromEnv(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  // Once per process: leaving the app afterwards must land on the shelf like
  // any other exit, not bounce straight back in.
  static bool consumed = false;
  if (consumed) {
    return;
  }
  const char* wanted = std::getenv("CROSSPLAY_AUTOSTART");
  if (wanted == nullptr || *wanted == '\0') {
    return;
  }
  consumed = true;
  for (int folder = 0; folder < kFolderCount; ++folder) {
    for (int item = 0; item < kFolders[folder].count; ++item) {
      if (strcasecmp(kFolders[folder].items[item].title, wanted) == 0) {
        LOG_INF("SHELF", "Autostart into %s", kFolders[folder].items[item].title);
        openItem(folder, item, renderer, mappedInput);
        return;
      }
    }
  }
  LOG_ERR("SHELF", "Autostart: no item titled '%s'", wanted);
}

void openPlayer(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  // Only the folder footer offers it, and only a folder that shows the name, so
  // lastFolder is the folder we are standing in. Recorded before the launch for
  // the same reason openItem does it: replaceActivity destroys the caller.
  openFolderIndex = lastFolder;
  if (!replaceWith(PlayerActivity::create(renderer, mappedInput), "Player")) {
    openFolderIndex = -1;
  }
}

void leave(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (openFolderIndex >= 0) {
    openFolder(openFolderIndex, renderer, mappedInput);
    return;
  }
  activityManager.goHome();
}

int lastFolderOnHome() {
  ensureLoaded();
  return lastFolder;
}

int lastItemIn(const int index) {
  ensureLoaded();
  return index >= 0 && index < kFolderCount ? lastItem[index] : 0;
}

const freeink::Icon* folderMark(const int index) {
  return index >= 0 && index < kFolderCount ? kFolders[index].mark : nullptr;
}

}  // namespace shelf
