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
// Same again, for a screen whose body carries ONE STRING that has to be read off
// the glass and typed -- an address, a code. The small slot takes the bold
// reading cut so that string has somewhere to land that is neither the display
// cut (which no address fits: a worst-case IPv4 URL measures 632 against a
// 448px body) nor the same serif 14 as the paragraph under it. Without this the
// fitting ladder steps the headline, the address, the prose and the footer all
// onto the identical cut, and the one line the reader has to copy is the third
// line of a paragraph.
inline Faces readingAddressFaces() { return Faces{kReadingBoldFontId, kReadingFontId, kDisplayFontId}; }

// And for a screen whose header band carries a story's title rather than the
// app's name: the title slot takes the bold reading cut so a whole headline
// fits and still reads as a headline, and the SMALL slot takes the small
// reading cut so a headline too long for the bold cut steps DOWN through real
// reading faces (bold 16 -> 14 -> 11) rather than falling straight to an
// ellipsis. The step used to be the Jersey tile cut (kButtonFontId), which is
// a pixel display face with no business setting prose -- so a long headline was
// either cut at reading_serif_14 or shrunk into a 21px Jersey line box, and the
// fork's own shrink rule had nothing worth stepping down to (card #268). The
// app's own name stays in the display cut on the screen where it *is* the
// title, which is the front page.
inline Faces readerFaces() { return Faces{kReadingSmallFontId, kReadingFontId, kReadingBoldFontId}; }

// A screen whose content is ONE WORD, sized to be read across a room. All three
// slots carry a different size of the same face, largest in the title slot,
// because the card picks the biggest cut the word fits in and a freestanding
// builder has no way to ask for a rebind mid-layout: whatever it can measure is
// whatever is bound. The 30px cut doubles as this screen's chrome face, which
// is why there is no UI cut here and no need for one -- the only other text is
// two edge labels and a score.
inline Faces cardFaces() { return Faces{kLargeFontId, kDisplayFontId, kHugeFontId}; }

// A front door with a secondary line under its headline and a record line under
// the rule. Both are sentences rather than labels, and at the 20px UI cut a
// sentence runs off a 480px panel and is truncated with an ellipsis Jersey does
// not carry -- so it draws as nothing and the line simply stops. The small slot
// takes the button cut, which fits about half again as much.
inline Faces proseMenuFaces() { return Faces{kButtonFontId, kUiFontId, kDisplayFontId}; }

// A results screen: a header band, a list of words, and one very large number.
// The title slot stays the display cut so the band is not clipped by a face
// meant for a card; the huge cut goes in the BODY slot, which is the only slot
// the score needs, and the list takes the UI cut in the small slot.
inline Faces bigNumberFaces() { return Faces{kButtonFontId, kHugeFontId, kDisplayFontId}; }

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

// Vertical top for an element of height h centred in the VISIBLE part of the
// header band, for an Activity drawing straight to the renderer. The twin of
// toybox::bandCenterY(Screen&, int16_t), and it exists so the band's height is
// named in exactly one place: chess open-coded this arithmetic, which is one
// more copy of the chrome's geometry living in an app file. See
// host-tests/chromeguard.
inline int bandCenterY(const GfxRenderer& renderer, const int elementH) {
  int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  (void)viewRight;
  (void)viewBottom;
  (void)viewLeft;
  return viewTop + (kHeaderHeight - viewTop - elementH) / 2;
}

// The band's own rect at renderer coordinates: absolute chrome puts it at the
// panel's top-left corner, full width. For hit rects and decorations an
// Activity registers over the band itself.
inline Rect headerBandRect(const GfxRenderer& renderer) { return Rect{0, 0, renderer.getScreenWidth(), kHeaderHeight}; }

}  // namespace toybox
