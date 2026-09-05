#pragma once

// The Wallpapers screens. Freestanding builders in the XkcdScreens mould: a
// model in, a drawn frame out, no renderer and no Activity, so host-tests/ui/
// can assert what they drew and what they made tappable.
//
// Portrait, 480x800, like every other app on the device. The picker offers the
// wallpapers already on the card and pins the chosen one as the sleep screen,
// by copying it to the /sleep.bmp slot the sleep system already checks first.
//
// No live thumbnail: a wallpaper is a 1-bit image sized to the sleep canvas,
// and the renderer's 1-bit blit neither scales nor rotates, so a landscape
// wallpaper cannot be shown shrunk inside a portrait row without cropping to
// nonsense. The picker names the wallpapers and marks the live one; seeing one
// means setting it and letting the device sleep. A shrunk preview is a
// follow-up that needs the uploader to emit a portrait thumbnail.
//
// Chosen from three rendered arrangements (immediate / confirm-button /
// banner): a tap sets the wallpaper at once, and an inked banner under the
// chrome names the live one so "which is on?" is never a question even when it
// has scrolled off the list -- the a-silent-screen-reads-as-a-crash concern.
// The other two were built behind a WALLPAPERS_VARIANT macro, composed side by
// side, and deleted with the macro in the same commit.

#include "../ui/ToyboxScreen.h"

namespace wallpapersui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer owns the 200s, Hacker News the 300s, xkcd the
// 400s. Wallpapers takes the 800s.
enum : fui::ActionId {
  ActionPick = 800,  // a wallpaper row; value carries its index
};

// One wallpaper on the card. `active` is the one currently pinned as the sleep
// screen, marked so the user is never left guessing which is live.
struct Entry {
  const char* name = "";  // the file name
  bool active = false;
};

struct PickerModel {
  const char* title = "WALLPAPERS";
  const Entry* items = nullptr;
  int count = 0;
  int selected = 0;  // the highlighted wallpaper
  // "3 SAVED", drawn in the header's right label.
  const char* rightLabel = nullptr;
  // The free-space advisory. Left null when there is room; otherwise one short
  // line under the chrome that does NOT claim the card is full when the real
  // answer is "could not tell" (see WallpapersCore::roomFor). A warning, not a
  // wall: the picker still works, because pinning a wallpaper is a tiny write.
  const char* warning = nullptr;
};

void buildPicker(toybox::Screen& screen, const PickerModel& model);

// The empty state. A wallpaper app with nothing to show must SAY so and say how
// to fix it -- a blank body is indistinguishable from a crashed device (see the
// a-silent-screen-reads-as-a-crash memory).
struct EmptyModel {
  const char* title = "WALLPAPERS";
  const char* warning = nullptr;
};

void buildEmpty(toybox::Screen& screen, const EmptyModel& model);

}  // namespace wallpapersui
