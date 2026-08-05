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
  UIIcon icon;
  std::unique_ptr<Activity> (*create)(GfxRenderer&, MappedInputManager&);
};

// A titled list of items. Note what is absent: a Folder cannot contain a
// Folder. That is rule 1, expressed as a type rather than a convention.
struct Folder {
  const char* title;  // "GAMES" -- capitals, because Toybox chrome is capitals
  UIIcon icon;
  const Item* items;
  int count;
  // Whether the footer shows this device's name. True for GAMES, because the
  // name exists for playing against somebody in the room and this footer is the
  // only place it can be rerolled. False elsewhere, where it would be a word
  // with no job. Move this out when Settings owns the name properly.
  bool showsDeviceName;
};

const Folder* folders();
int folderCount();

// Open folder `index` from Home. Out of range is a no-op and logged.
void openFolder(int index, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Open item `item` of folder `folder`, and remember which folder it came from
// so leave() can undo it.
void openItem(int folder, int item, GfxRenderer& renderer, MappedInputManager& mappedInput);

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

}  // namespace shelf
