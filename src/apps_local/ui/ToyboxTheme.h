#pragma once

// The renderer-side half of the Toybox theme: binding real fonts to FreeInkUI's
// font slots, and the one place that notices an overflowing interaction table.
// The tokens themselves are freestanding, in ToyboxTokens.h.

#include <FreeInkUIGfxRenderer.h>
#include <Logging.h>

#include "ToyboxFonts.h"
#include "ToyboxScreen.h"
#include "ToyboxTokens.h"

namespace toybox {

// Which typefaces a screen speaks in.
//
// Toybox is the shapes -- the black header band, the pill buttons, the hairline
// rows, the proportions -- and those stay shared so the apps read as one system.
// The faces do not: a game gets its own, so you can tell what you are playing
// from across the room without reading a word.
//
// Three slots exist (GfxRendererTarget::FONT_SLOTS), so a game picks three:
// `small` is the dense cut for tiles, `body` is rows and labels, `title` is the
// header band.
struct Faces {
  int small = kTileFontId;
  int body = kUiFontId;
  int title = kDisplayFontId;
};

// Jersey throughout: the fork's default, used by the Games menu and Chess.
inline Faces toyboxFaces() { return Faces{}; }

// Connections speaks Instrument Serif -- chosen because it is the only elegant
// face also condensed enough to set a long word inside a square tile: Lora,
// Fraunces and Young Serif all need more than the tile's whole width for
// "ACTUALLY" at their smallest usable size, while this fits it at a 20px cap.
//
// The header band stays Jersey in every game. The top bar is the fork's chrome,
// not the game's voice, and a shared one is what makes two apps feel like one
// device.
//
// The board starts with Jersey in the body slot so its buttons and status read
// like every other app's, then the activity rebinds that slot to the smaller
// serif cut before drawing tiles. Rebinding costs one assignment, so the
// adapter's three slots are a working set, not a limit on how many faces a
// screen may use: GfxRenderer itself holds a map with no cap, and this fork
// already registers about fifteen.
inline Faces serifBoardFaces() { return Faces{kSerifTileFontId, kUiFontId, kDisplayFontId}; }
inline Faces serifMenuFaces() { return Faces{kSerifSmallFontId, kSerifTileFontId, kDisplayFontId}; }

// For an app whose own surface is a page of prose. The body slot steps down to
// the reading cut so a screenful is a screenful rather than a paragraph; the
// header band keeps the display cut, because the top bar is the fork's chrome
// and a shared one is what makes two apps feel like one device.
inline Faces readingFaces() { return Faces{kTileFontId, kReadingFontId, kDisplayFontId}; }
// Same, for a screen with buttons on it. A button is the device speaking rather
// than the app, so it keeps Jersey; the small slot carries it, because three
// slots is a working set and a list's dense footnote cut and a footer's button
// cut are never wanted on the same screen. Rebinding is one assignment.
inline Faces readingChromeFaces() { return Faces{kButtonFontId, kReadingFontId, kDisplayFontId}; }

// And for a screen whose header band carries a story's title rather than the
// app's name: the title slot takes the bold reading cut so a whole headline
// fits and still reads as a headline. The app's own name stays in the display
// cut on the screen where it *is* the title, which is the front page.
inline Faces readerFaces() { return Faces{kButtonFontId, kReadingFontId, kReadingBoldFontId}; }

inline fui::GfxRendererTarget makeTarget(const GfxRenderer& renderer, const Faces& faces = Faces{}) {
  fui::GfxRendererTarget target(renderer);
  // The small slot carries the dense cut, not a small UI face; see ToyboxTokens.h.
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, faces.small);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, faces.body);
  target.setFont(fui::GfxRendererTarget::FONT_TITLE, faces.title);
  return target;
}

// Overflow means a control drew but registered no hit rect, which reads to a
// user as a dead button and to a developer as nothing at all. Cheap to check
// once per paint; the alternative is finding it by tapping.
inline void reportOverflow(const Interactions& interactions, const char* screenName) {
  if (interactions.overflowed()) {
    LOG_ERR("TOYBOX", "%s registered more than %d interactions; some controls are not tappable", screenName,
            static_cast<int>(kMaxInteractions));
  }
}

// The rule under the header band. Kept for callers that still draw straight to
// the renderer; screen builders draw it through the DrawTarget instead.
inline void headerRule(const GfxRenderer& renderer) {
  renderer.fillRect(0, kHeaderHeight + 4, renderer.getScreenWidth(), kRule, true);
}

}  // namespace toybox
