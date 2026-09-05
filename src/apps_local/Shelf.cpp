#include "Shelf.h"

#include <HalStorage.h>
#include <Logging.h>
#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../activities/ActivityManager.h"
#include "ShelfFolderActivity.h"
#include "ShelfState.h"
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
#include "wallpapers/WallpapersActivity.h"
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
    {"WALLPAPERS", &icon_wallpapers_32, &WallpapersActivity::create},
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

// The remembered position, mirroring /.crosspoint/shelf.cfg:
//
// - `lastFolder`: which shelf row Home should land on when you come back out.
//   CrossPoint restores Home's selection by matching the departing activity's
//   name against its own HomeMenuItem list, which cannot know about ours, so
//   without this you leave GAMES and the cursor is sitting on Browse Files.
// - `resumeRow`: per folder, the row it reopens on. The row of the item last
//   opened from it, or -- when you paged and then walked out without opening
//   anything -- the first row of the page you were looking at. It is the page
//   you were ON, not the page holding the game you last played; those are the
//   same thing until you browse and leave, and browsing and leaving is the case
//   that was wrong.
// - `openTitle`: the item that was open when the device went to sleep, which is
//   what wake reopens instead of dropping you on Home.
//
// All of it survives the device going to sleep, which none of it did before.
// `main.cpp` deep-sleeps on the idle timeout and says of it that wake is
// effectively a chip reset, so every time Mario put the device down and came
// back the shelf had forgotten which game he was playing.
//
// Written next to the reader's own state rather than into CrossPointState,
// which is upstream's file: a fork-local fact belongs in a fork-local file, and
// player.cfg already established the pattern.
shelf::State state;
bool stateLoaded = false;

// The Activity that the open item launched, by name. `openFolderIndex` alone
// cannot answer "is that item still what is on screen": the Home gesture leaves
// an app without going through leave(), so a sleep from Home would otherwise
// record the game you left ten minutes ago as still open. A pushed sub-screen
// (the frontlight panel) also fails this check, and falls back to Home the way
// every wake used to.
std::string openActivityName;

// A title that does not fit the state file cannot be resumed, and would fail
// silently at the write. Caught at compile time instead, next to the icon and
// mark checks, so a long-titled new game does not build.
constexpr bool everyTitleFitsTheStateFile() {
  for (const auto& folder : kFolders) {
    for (int i = 0; i < folder.count; ++i) {
      if (shelf::constexprLength(folder.items[i].title) > shelf::MAX_ITEM_TITLE) return false;
    }
  }
  return true;
}
static_assert(everyTitleFitsTheStateFile(), "shelf item titles must fit shelf::MAX_ITEM_TITLE; see ShelfState.h");
static_assert(kFolderCount <= shelf::MAX_FOLDERS, "raise shelf::MAX_FOLDERS in ShelfState.h");

// Row limits as the registry stands now, for parseState's clamp.
const int* itemLimits() {
  static int limits[shelf::MAX_FOLDERS] = {};
  for (int i = 0; i < kFolderCount; ++i) limits[i] = kFolders[i].count - 1;
  return limits;
}

// The folder and row of the item with this title, case-insensitively. The one
// place a title is turned back into a row, shared by wake and by the
// autostart environment variable.
bool findItemByTitle(const char* title, int& folder, int& item) {
  for (int f = 0; f < kFolderCount; ++f) {
    for (int i = 0; i < kFolders[f].count; ++i) {
      if (strcasecmp(kFolders[f].items[i].title, title) == 0) {
        folder = f;
        item = i;
        return true;
      }
    }
  }
  return false;
}

// The parse and the format live in ShelfState.cpp, where a host test can reach
// them without a card: the file has to survive a truncated write, a file
// written before wake could resume, and a game renamed since it was written.
void loadState() {
  stateLoaded = true;
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kStatePath)) return;
  char buffer[96] = {};
  if (Storage.readFileToBuffer(kStatePath, buffer, sizeof(buffer)) == 0) return;
  // Fails quietly: the worst a corrupt file can cost is starting at the top,
  // and there is nothing for anyone to do about it.
  shelf::parseState(buffer, kFolderCount, itemLimits(), state);
#endif
}

void saveState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[96];
  const size_t used = shelf::formatState(state, kFolderCount, line, sizeof(line));
  if (used == 0) {
    LOG_ERR("SHELF", "State line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  Storage.writeFile(kStatePath, String(line));
#endif
}

// Every path that reads or writes the remembered position goes through this
// first. Lazily rather than at boot because the shelf has no init hook, and
// unconditionally rather than only on the read paths because openFolder passes
// the current resumeRow back in: without the load, the first navigation of a
// session would write the defaults over the saved file and the persistence
// would silently do nothing.
void ensureLoaded() {
  if (!stateLoaded) loadState();
}

// Only when something actually changed. Opening a folder happens on every Back,
// and SPIFFS sectors have a finite erase count, so an unconditional write here
// would be a write per navigation for no gain.
void saveIfChanged(const int folder, const int row) {
  ensureLoaded();
  if (state.lastFolder == folder && (folder < 0 || state.resumeRow[folder] == row)) return;
  state.lastFolder = folder;
  if (folder >= 0) state.resumeRow[folder] = row;
  saveState();
}

// The item wake should reopen, or none. Kept separate from saveIfChanged
// because the two facts change on different events: the position changes as you
// navigate, this changes when an item opens, closes, or is left behind.
void setOpenTitle(const char* title) {
  ensureLoaded();
  const char* wanted = title == nullptr ? "" : title;
  if (strcmp(state.openTitle, wanted) == 0) return;
  snprintf(state.openTitle, sizeof(state.openTitle), "%s", wanted);
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
  // Read back from the activity itself rather than written down beside the
  // registry: a table mapping titles to activity names would be a second copy
  // of a fact the activity already carries, and would rot the first time one is
  // renamed. Asked after the request, because Activity::name is not ours to
  // read directly.
  openActivityName = activityManager.currentActivityName();
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
  saveIfChanged(index, state.resumeRow[index]);
  setOpenTitle(nullptr);
  replaceWith(ShelfFolderActivity::create(renderer, mappedInput, index), kFolders[index].title);
}

bool openItem(const int folder, const int item, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (folder < 0 || folder >= kFolderCount) {
    LOG_ERR("SHELF", "Bad folder index: %d", folder);
    return false;
  }
  const Folder& parent = kFolders[folder];
  if (item < 0 || item >= parent.count) {
    LOG_ERR("SHELF", "Bad item index %d in %s", item, parent.title);
    return false;
  }

  // Recorded before the launch, not after: replaceActivity destroys the caller,
  // so there is no "after" to run in.
  openFolderIndex = folder;
  saveIfChanged(folder, item);
  setOpenTitle(parent.items[item].title);
  if (!replaceWith(parent.items[item].create(renderer, mappedInput), parent.items[item].title)) {
    openFolderIndex = -1;
    setOpenTitle(nullptr);
    return false;
  }
  return true;
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
  int folder = -1;
  int item = -1;
  if (!findItemByTitle(wanted, folder, item)) {
    LOG_ERR("SHELF", "Autostart: no item titled '%s'", wanted);
    return;
  }
  LOG_INF("SHELF", "Autostart into %s", kFolders[folder].items[item].title);
  openItem(folder, item, renderer, mappedInput);
}

void rememberForWake(const char* currentActivityName) {
  ensureLoaded();
  if (state.openTitle[0] == '\0') return;
  // An item is only still open if the activity it launched is the one on
  // screen. Leaving a game by the Home gesture never passes through leave(),
  // so without this a sleep taken on Home would resume into the game you left.
  const char* onScreen = currentActivityName == nullptr ? "" : currentActivityName;
  if (openActivityName.empty() || openActivityName != onScreen) {
    LOG_DBG("SHELF", "Sleeping on %s, not %s: nothing to resume", onScreen, state.openTitle);
    setOpenTitle(nullptr);
  }
}

bool resumeFromWake(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  ensureLoaded();
  if (state.openTitle[0] == '\0') return false;

  int folder = -1;
  int item = -1;
  if (!findItemByTitle(state.openTitle, folder, item)) {
    // The card outlives firmware updates, so the item may have been renamed or
    // removed since it was written. Home, and forget it.
    LOG_INF("SHELF", "Wake: nothing titled '%s' any more", state.openTitle);
    setOpenTitle(nullptr);
    return false;
  }

  LOG_INF("SHELF", "Wake: resuming %s", kFolders[folder].items[item].title);
  return openItem(folder, item, renderer, mappedInput);
}

void openPlayer(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  // Only the folder footer offers it, and only a folder that shows the name, so
  // lastFolder is the folder we are standing in. Recorded before the launch for
  // the same reason openItem does it: replaceActivity destroys the caller.
  ensureLoaded();
  openFolderIndex = state.lastFolder;
  setOpenTitle(nullptr);
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
  return state.lastFolder;
}

int resumeRowIn(const int index) {
  ensureLoaded();
  return index >= 0 && index < kFolderCount ? state.resumeRow[index] : 0;
}

void rememberRowIn(const int index, const int row) {
  if (index < 0 || index >= kFolderCount) {
    LOG_ERR("SHELF", "Bad folder index: %d", index);
    return;
  }
  if (row < 0 || row >= kFolders[index].count) {
    LOG_ERR("SHELF", "Bad row %d in %s", row, kFolders[index].title);
    return;
  }
  saveIfChanged(index, row);
}

const freeink::Icon* folderMark(const int index) {
  return index >= 0 && index < kFolderCount ? kFolders[index].mark : nullptr;
}

}  // namespace shelf
