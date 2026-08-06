#include "Shelf.h"

#include <Logging.h>

#include "../activities/ActivityManager.h"
#include "ShelfFolderActivity.h"
#include "battleship/BattleshipActivity.h"
#include "chess/ChessActivity.h"
#include "connections/ConnectionsActivity.h"
#include "dungeon/DungeonActivity.h"
#include "hackernews/HackerNewsActivity.h"
#include "insider/InsiderActivity.h"
#include "murdle/MurdleActivity.h"
#include "player/PlayerActivity.h"
#include "solitaire/SolitaireActivity.h"
#include "study/StudyActivity.h"
#include "ui/ToyboxIcons.h"

namespace {

// Icons come from tools_local/icons.txt via Lucide. Picked for silhouette
// rather than literalness: a crown, a hull, a grid and a card suit share no
// shape, so a row is scannable before the label is read.
constexpr shelf::Item kGames[] = {
    {"CHESS", &icon_chess_32, &ChessActivity::create},
    {"BATTLESHIP", &icon_battleship_32, &BattleshipActivity::create},
    {"CONNECTIONS", &icon_connections_32, &ConnectionsActivity::create},
    {"SOLITAIRE", &icon_solitaire_32, &SolitaireActivity::create},
    {"DUNGEONS", &icon_dungeon_32, &DungeonActivity::create},
    {"INSIDER", &icon_insider_32, &InsiderActivity::create},
    {"MURDLE", &icon_murdle_32, &MurdleActivity::create},
};
constexpr shelf::Item kApps[] = {
    {"STUDY", &icon_study_32, &StudyActivity::create},
    {"HACKER NEWS", &icon_hackernews_32, &HackerNewsActivity::create},
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
static_assert(everyItemHasAnIcon(), "every shelf item needs an icon; see tools_local/icons.txt");

constexpr bool everyFolderHasAMark() {
  for (const auto& folder : kFolders) {
    if (folder.mark == nullptr) return false;
  }
  return true;
}
static_assert(everyFolderHasAMark(), "every shelf folder needs a mark; see tools_local/icons.txt");

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
  lastFolder = index;
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
  lastItem[folder] = item;
  if (!replaceWith(parent.items[item].create(renderer, mappedInput), parent.items[item].title)) {
    openFolderIndex = -1;
  }
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

int lastFolderOnHome() { return lastFolder; }

int lastItemIn(const int index) { return index >= 0 && index < kFolderCount ? lastItem[index] : 0; }

const freeink::Icon* folderMark(const int index) {
  return index >= 0 && index < kFolderCount ? kFolders[index].mark : nullptr;
}

}  // namespace shelf
