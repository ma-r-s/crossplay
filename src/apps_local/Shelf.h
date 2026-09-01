#pragma once

// The shelf: everything this fork adds to the device, and the only way in or
// out of it.
//
// Home gets two rows, GAMES and APPS, as siblings. Each opens a folder holding
// the things of that kind. That is the whole hierarchy.
//
//   Home
//     Browse Files / Recent Books / File Transfer / Settings   (upstream's)
//     GAMES >   chess, battleship, connections, solitaire
//     APPS  >   study, hacker news, ...
//
// ---------------------------------------------------------------------------
// Three rules, and the reason each exists.
//
// 1. A folder holds apps, never another folder. Depth is capped at two by the
//    type, not by discipline: there is nowhere in `Folder` to put a folder.
//    Two taps reaches anything, forever, and on a panel that repaints in half a
//    second a third tap is a real cost.
//
// 2. Games and apps are the same kind of row. The only difference between
//    chess and a spaced-repetition deck is which folder it sits in, so there is
//    one `Item` type and one launcher. The previous fork had two near-identical
//    entry structs for no reason anyone could name.
//
// 3. An app never knows where it came from. It calls leave() and lands in its
//    folder; a folder calls leave() and lands on Home. This is the one exit and
//    every app uses it.
//
// Rule 3 is why this file was written before any app moved. The fork this
// replaces carried a `setReturnHere()` / `takeReturnHere()` breadcrumb that had
// to be *redeemed by a different activity on entry*, so a missed redemption
// stranded you in the wrong menu -- and it existed only because upstream's own
// games each hardcoded `goToApps()` and could not be edited. Every app here is
// ours. None of them should ever name a destination.
// ---------------------------------------------------------------------------

#include <Icon.h>

#include <memory>

#include "../components/themes/BaseTheme.h"  // UIIcon

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace shelf {

// One openable thing. Raw, untranslated title: routing these through tr() would
// mean editing lib/I18n/translations/*.yaml per app, which is the per-app
// upstream churn this whole directory exists to avoid.
struct Item {
  const char* title;
  // A real icon, not a UIIcon. Upstream's palette has thirteen entries and none
  // of them is a crown or a ship; adding variants would mean editing
  // BaseTheme.h and LyraTheme.cpp per app, which is the churn this directory
  // exists to avoid. These are generated from Lucide SVGs into
  // ui/ToyboxIcons.h -- an asset the shelf resolves, so the palette is the
  // 1735 icons Lucide ships rather than a growing enum.
  const freeink::Icon* icon;
  std::unique_ptr<Activity> (*create)(GfxRenderer&, MappedInputManager&);
};

// A titled list of items. Note what is absent: a Folder cannot contain a
// Folder. That is rule 1, expressed as a type rather than a convention.
struct Folder {
  const char* title;
  // Home draws this one, and upstream's menu takes a UIIcon and nothing else.
  // Folder is honest: these are folders, in their list, in their language.
  UIIcon icon;
  // Ours, drawn in the folder's own header where we own the whole screen.
  // Home cannot use it: drawButtonMenu only indents the label when its own
  // palette resolves a bitmap, so placing ours there means either overlapping
  // the label or erasing upstream's icon first -- and erasing means copying the
  // row's inset, its font's line height, the theme's private icon size and its
  // selection fill. Five couplings to a private layout, for two icons.
  const freeink::Icon* mark;
  const Item* items;
  int count;
  // Whether the footer shows this device's face and name. True for GAMES,
  // because the name exists for playing against somebody in the room and this
  // bar is the only way into PLAYER, where it is changed. False elsewhere,
  // where it would be a face with no job.
  bool showsDeviceName;
};

const Folder* folders();
int folderCount();

// Open folder `index` from Home. Out of range is a no-op and logged.
void openFolder(int index, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Open item `item` of folder `folder`, and remember which folder it came from
// so leave() can undo it. False if the item could not be created, in which case
// nothing was replaced and the caller still owns the screen.
bool openItem(int folder, int item, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Record, on the way into deep sleep, whether a shelf item is what the user is
// looking at. `currentActivityName` is the name of the activity on screen; an
// item counts as open only when that is the activity the item launched, so
// leaving a game by the Home gesture -- which never passes through leave() --
// does not leave a stale claim behind.
//
// Wake from deep sleep is a chip reset, so nothing survives it that was not
// written to the card first. This is the write.
void rememberForWake(const char* currentActivityName);

// Reopen whatever rememberForWake() recorded, and say whether it did. False
// when the device was not in a shelf item when it slept, and when the item has
// been renamed or removed by a firmware update since -- the caller goes Home,
// which is where every wake used to land.
bool resumeFromWake(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Open PLAYER, the one screen in the fork that is not a game and not a folder.
//
// It is reached from the footer bar rather than from a row, so it is not an
// Item and it is in no folder -- but it still has to come back where it came
// from, so it records the current folder exactly the way openItem does. That
// bookkeeping is the only reason this is not just a factory call at the tap
// site: leave() has to have somewhere to send it.
void openPlayer(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Open the item named by the CROSSPLAY_AUTOSTART environment variable, if it
// is set and matches an item title (case-insensitive). A no-op everywhere the
// variable does not exist, which is every real device: this is how the site's
// installer page boots its emulator straight into Study with the user's own
// deck, and how `CROSSPLAY_AUTOSTART=chess ./bin/sim` skips the shelf during
// development. Same getenv-in-firmware precedent as CROSSPLAY_SEED.
void autostartFromEnv(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Go back one level: an app returns to its folder, a folder returns to Home.
//
// The current folder is module state rather than something each app carries,
// and that is deliberate. `replaceActivity` means exactly one activity exists
// at a time, so there is exactly one current location -- one fact, stored once.
// Threading a parent through every factory would make six apps each keep their
// own copy of a thing the device only has one of, and any app that forgot to
// store it could not leave.
void leave(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Which shelf row Home should select on entry, or -1 if the shelf was not the
// last thing open. Home restores its own selection by matching the departing
// activity's name against its HomeMenuItem list, which has no idea our rows
// exist; this is how leaving GAMES puts the cursor back on GAMES.
int lastFolderOnHome();

// The row folder `index` should reopen on -- which is to say the PAGE it should
// reopen on, since the page is the row's. 0 if it has never been left anywhere.
//
// A folder is destroyed when it launches something and again when you walk out
// of it, so without this you come back from the third game with the cursor on
// the first, and you come back from a book to page one of a folder you had
// paged to the end of. Home has the same problem and the same answer; see
// lastFolderOnHome().
int resumeRowIn(int index);

// Remember that folder `index` is standing on `row`, so that is where it comes
// back. openItem() does this with the row it opened; the folder itself does it
// with the first row of every page it turns to, which is what makes leaving a
// folder WITHOUT opening anything come back to the page you were reading.
//
// Written when the page turns rather than on the way out, because there is no
// reliable way out to hook: the idle timeout deep-sleeps wherever you are, and
// wake is a chip reset. The write is ~20 bytes beside a full e-ink repaint.
void rememberRowIn(int index, int row);

// The icon for folder row `index`, for the Home seam to draw. Null-safe.
const freeink::Icon* folderMark(int index);

}  // namespace shelf
