#include "WallpapersScreens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"
#include "WallpapersCore.h"

namespace wallpapersui {

namespace {

// The top of the body: below the header band AND the rule Toybox draws under
// it, which is what toybox::kBodyTop derives from chromeBelow().
//
// This app was wrong twice, and card 358 closes both. It read
// kHeaderHeight + kGutter -- the band alone plus one gutter, five pixels of
// clearance under a rule it never counted, where every other app has
// twenty-nine. And every caller below (gridGeom, buildGridChrome) then added
// safe.y on top, while the chrome it measures from is pinned at panel row 0 by
// absoluteChrome and already covers the rows the glass hides -- so the hint and
// the grid started ten pixels below Hacker News' body and the shelf's list.
// The comment here used to claim the opposite on both counts.
constexpr int16_t kBodyTop = static_cast<int16_t>(toybox::kBodyTop);
// A fixed strip under the chrome for the free-space advisory or the "nothing is
// set yet" hint. Fixed so the grid's top does not jump when a hint appears.
constexpr int16_t kHintH = 30;
// Clear space between the hint and the first row of thumbnails. Without it the
// grid began at exactly the hint's box bottom, so the sentence sat ON the
// artwork -- Mario's words were that it is too close to the wallpapers. Taken
// out of the grid's height (the cells are height-constrained), which costs each
// thumbnail a few pixels and buys the sentence room to read as a caption for
// the screen rather than a label on the first tile.
// The gap under the hint strip, unchanged: the grid must not move. Every row
// given to the top of this screen comes out of the THUMBNAILS, because the
// grid is height-bound between the strip and the page dots and gridGeom()
// re-fits the cells into whatever is left.
constexpr int16_t kHintGap = 16;

// Where the hint strip sits: centred between the rule and the grid, rather
// than hung off kBodyTop.
//
// kBodyTop is chromeBelow() plus the whole 36px body gutter, which put the
// sentence 42px under the rule and 18 over the tiles -- it read as a caption
// stuck to the grid instead of a line placed between the two. Centring it
// moves ONLY the strip: gridTop is still kBodyTop + kHintH + kHintGap, so not
// a pixel of thumbnail is spent on it.
//
// The hint is chrome, not body. Wallpapers' body is its grid, and
// testEveryAppsBodyStartsOnTheSameRow used this strip as a proxy for a body
// top that this screen does not otherwise export.
constexpr int16_t kGridTop = static_cast<int16_t>(kBodyTop + kHintH + kHintGap);
constexpr int16_t kHintTop =
    static_cast<int16_t>(toybox::kChromeHeight + (kGridTop - toybox::kChromeHeight - kHintH) / 2);
// The page-dot strip at the very bottom, reserved whether or not it is used, so
// the grid height is the same on a one-page library as on a ten-page one.
constexpr int16_t kPageStripH = 28;
constexpr int16_t kBottomMargin = 12;
// Wide enough to hold the selection marker in the padding with white space
// on both sides of it, which is what keeps the marker off the artwork.
constexpr int16_t kGap = 24;
// The grid: two columns, two rows a page. Decided from rendered candidates --
// at three rows a page the fine-line engravings turn to grey mush.
constexpr int kRows = 2;
constexpr int16_t kCaptionH = 22;
// Clearance under the thumbnail so the selection marker, which lives in the
// padding, cannot land on the caption.
constexpr int16_t kMarkerRoom = 12;
// The bracket marker's own dimensions. kMarkerRoom must exceed
// kMarkerGap + kMarkerWeight or the brackets reach into the caption's line box;
// host-tests/wallcaption asserts exactly that, for every name and every slot.
constexpr int16_t kMarkerGap = 5;
constexpr int16_t kMarkerWeight = 4;
constexpr int16_t kBracketArm = 30;

// The hold sheet's button stack, measured DOWN from the body top rather than up
// from the panel bottom: the confirm screen puts a third button below the
// sheet's two (see confirmDeleteRect) and anchoring to the bottom would have no
// room left for it. Panel-absolute at safe.y = 0: 470..534, 554..618, 638..702.
//
// 470 rather than the 380 this was first drawn at. At 380 the confirm's prose
// had five 42px lines for a sentence that needs six, and the clause it dropped
// was the SECOND one -- "it stays on your sleep screen until you pick another"
// -- so the confirm for the wallpaper actually in use silently lost the only
// line that was about it (a-warning-that-can-vanish). Found in a render; no
// suite could see it, which is why host-tests/wallcaption now measures every
// combination of that sentence against the box below in the real face.
constexpr int16_t kSheetStackTop = 470;
constexpr int16_t kSheetButtonGap = 20;

// The headline the sheet and the confirm both hang their prose off: two lines
// of the display cut (63px each on this panel) plus slack. Two because a
// wallpaper the user added is named by its FILE, and file names are the one
// string on these screens nobody chose for its width.
constexpr int16_t kSheetHeadH = 132;

// The wallpaper's own shape. Sleep wallpapers are portrait 480x800 on this
// device (verified: a 480x800 image fills the sleep screen), so the cells are
// too and a thumbnail of a matching wallpaper fills its cell with no letterbox.
constexpr float kCellAspectWoverH = 480.0f / 800.0f;

fui::TextStyle owned(fui::TextStyle style, fui::TextAlign align) {
  style.align = align;
  return style;
}

fui::TextStyle onPaper(fui::TextStyle style, fui::TextAlign align, uint8_t maxLines = 0) {
  style.align = align;
  style.color = fui::Color::Black;
  if (maxLines > 0) style.maxLines = maxLines;
  return style;
}

// A button: filled black, white label, hit-tested. 64 tall because that is the
// fork's finger target and the reason the offer is not a 30px strip.
constexpr int16_t kButtonH = 64;

// `hit` defaults to the drawn box and differs in exactly one place: the Live
// revoke confirm's KEEP, whose hit rect has to cover the whole strip the sender
// rows occupied while its INK stays a button. Drawing what it hits would put a
// 276px slab of solid black on an e-ink panel -- slow to refresh, and it ghosts.
void drawButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                const fui::Rect* hit = nullptr) {
  screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  fui::TextStyle style = screen.theme().smallText;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::White;
  style.maxLines = 1;
  // Through the ladder, like every other string in this file. The labels here
  // were all short enough while every button on these screens was the full body
  // wide; the Live screen puts two of them side by side in half that, and an
  // unfitted label in a narrow button is cut rather than shrunk. Labels that
  // already fit are untouched -- fitting only ever goes down.
  screen.target().text(box, toybox::fittedTitle(screen.target(), label, box.width, style).c_str(), style);
  screen.frame().hit(hit == nullptr ? box : *hit, action);
}

// The same finger target as drawButton, stroked rather than filled: a control
// that is clearly a control and clearly not the primary one.
void drawOutlineButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 3);
  fui::TextStyle style = screen.theme().smallText;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::Black;
  style.maxLines = 1;
  screen.target().text(box, toybox::fittedTitle(screen.target(), label, box.width, style).c_str(), style);
  screen.frame().hit(box, action);
}

void drawProse(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align) {
  fui::TextStyle style = onPaper(screen.theme().bodyText, align);
  const int16_t lineH = screen.target().lineHeight(style.font);
  const int lines = lineH > 0 ? box.height / lineH : 1;
  style.maxLines = static_cast<uint8_t>(lines < 1 ? 1 : (lines > 16 ? 16 : lines));
  screen.target().text(box, text, style);
}

// "1.0 MB". Tenths, rounded, so a 0.96MB set does not render as "0 MB" -- which
// is what a plain >>20 gives and would read as "nothing to download".
void formatSize(const uint64_t bytes, char* out, const size_t n) {
  const unsigned tenths = static_cast<unsigned>((bytes * 10 + (1u << 19)) >> 20);
  std::snprintf(out, n, "%u.%u MB", tenths / 10, tenths % 10);
}

// ---------------------------------------------------------------------------
// Drawn wallpaper motifs.
//
// A third of the set is algorithmic, so the offer screen can show REAL artwork
// with no asset, no flash cost and no empty frame: what the user sees is the
// motif they are being offered, drawn by the same rules that generated the BMP
// (tools_local/wallpapers/gen_geoA.py, gen_geoB.py). Deterministic -- every
// choice comes from a hash of the cell, never from rand() -- so the screen is
// the same every time it paints, which an e-ink panel needs and a screenshot
// test relies on.
//
// Everything clamps to `r`: there is no clip stack here, and a motif that drew
// one pixel past its box would land on the type.
uint32_t motifHash(const int x, const int y) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

void inkRect(fui::DrawTarget& t, const fui::Rect& clip, int x, int y, int w, int h) {
  int x0 = x < clip.x ? clip.x : x;
  int y0 = y < clip.y ? clip.y : y;
  int x1 = x + w > clip.x + clip.width ? clip.x + clip.width : x + w;
  int y1 = y + h > clip.y + clip.height ? clip.y + clip.height : y + h;
  if (x1 <= x0 || y1 <= y0) return;
  t.fill(fui::makeRect(static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0),
                       static_cast<int16_t>(y1 - y0)),
         fui::Paint::solid(fui::Color::Black));
}

// Quarter arcs, two orientations per cell -- the shipped truchet, sampled as
// short segments because there is no arc primitive here.
void paintTruchet(fui::DrawTarget& t, const fui::Rect& r, const int cell) {
  const int thick = cell / 5 < 2 ? 2 : cell / 5;
  const int steps = 12;
  for (int gy = 0; gy * cell < r.height; ++gy) {
    for (int gx = 0; gx * cell < r.width; ++gx) {
      const int x0 = r.x + gx * cell;
      const int y0 = r.y + gy * cell;
      const bool flip = (motifHash(gx, gy) & 1u) != 0u;
      for (int corner = 0; corner < 2; ++corner) {
        // Centres at opposite corners: (0,0)+(1,1), or (1,0)+(0,1) when flipped.
        const int cx = x0 + ((corner == 0) == !flip ? 0 : cell);
        const int cy = y0 + (corner == 0 ? 0 : cell);
        for (int s = 0; s <= steps; ++s) {
          const float a = 1.5707963f * static_cast<float>(s) / static_cast<float>(steps);
          const int px =
              cx + static_cast<int>((cx == x0 ? 1.0f : -1.0f) * (static_cast<float>(cell) / 2.0f) * std::cos(a));
          const int py =
              cy + static_cast<int>((cy == y0 ? 1.0f : -1.0f) * (static_cast<float>(cell) / 2.0f) * std::sin(a));
          inkRect(t, r, px - thick / 2, py - thick / 2, thick, thick);
        }
      }
    }
  }
}

// The chip takes toybox::bandOutlineStyles(), and only ever that half: this
// chip is an ACTION rather than a state, and a filled one would read as
// "already on". The filled half is for a control that has two states, which is
// the shelf's chooser and Hacker News's save mark. So the two modes are two
// GLYPHS in one outline chip, never one glyph in two fills.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel, const bool showChip = false,
            const bool choosing = false) {
  fui::HeaderProps header;
  header.title = title;
  if (showChip) {
    // A glyph, not a word. An icon-only trailing button is square (band height
    // less 8) where "CHOOSE" measured 91px at the BODY cut, and that width came
    // straight out of the room the title is fitted to on a band that also
    // carries the page count.
    header.trailingIcon = fui::bitmapFromIcon(chooseChipIcon(choosing));
    header.trailingAction = ActionChoose;
    header.trailingStyles = toybox::bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
  }
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

GridGeom gridGeom(const fui::DeviceContext& device) {
  const fui::Rect safe = device.safeRect();
  GridGeom g;
  g.cols = 2;
  g.rows = kRows;
  g.perPage = g.cols * g.rows;
  g.captionH = kCaptionH;
  g.markerRoom = kMarkerRoom;
  g.gapX = kGap;
  g.gapY = kGap;

  const int16_t gridLeft = static_cast<int16_t>(safe.x + toybox::kMargin);
  const int16_t gridW = static_cast<int16_t>(safe.width - toybox::kMargin * 2);
  // Sides and bottom off the safe rect; the top absolute, because the header
  // band already covers the rows the glass hides. See toybox::kBodyTop.
  const int16_t gridTop = kGridTop;
  const int16_t gridBottom = static_cast<int16_t>(safe.bottom() - kPageStripH - kBottomMargin);
  const int16_t gridH = static_cast<int16_t>(gridBottom - gridTop);

  // The largest a cell may be in each axis, then the wallpaper's aspect fitted
  // inside that box so the thumbnail is never stretched.
  const int16_t maxCellW = static_cast<int16_t>((gridW - g.gapX * (g.cols - 1)) / g.cols);
  const int16_t maxCellH =
      static_cast<int16_t>((gridH - g.gapY * (g.rows - 1) - (g.captionH + g.markerRoom) * g.rows) / g.rows);
  int16_t cellW = std::min<int16_t>(maxCellW, static_cast<int16_t>(maxCellH * kCellAspectWoverH));
  if (cellW < 1) cellW = 1;
  int16_t cellH = static_cast<int16_t>(cellW / kCellAspectWoverH);
  if (cellH > maxCellH) {
    cellH = maxCellH;
    cellW = static_cast<int16_t>(cellH * kCellAspectWoverH);
  }
  g.cellW = cellW;
  g.cellH = cellH;

  // Centre the columns horizontally; top-align the rows.
  const int16_t usedW = static_cast<int16_t>(g.cols * cellW + (g.cols - 1) * g.gapX);
  g.originX = static_cast<int16_t>(gridLeft + (gridW - usedW) / 2);
  g.originY = gridTop;
  g.pageDotsY = static_cast<int16_t>(safe.bottom() - kPageStripH + 6);
  return g;
}

fui::Rect thumbRect(const GridGeom& g, int slot) {
  const int col = slot % g.cols;
  const int row = slot / g.cols;
  const int16_t x = static_cast<int16_t>(g.originX + col * (g.cellW + g.gapX));
  const int16_t y = static_cast<int16_t>(g.originY + row * (g.cellH + g.markerRoom + g.captionH + g.gapY));
  return fui::makeRect(x, y, g.cellW, g.cellH);
}

fui::Rect cellRect(const GridGeom& g, int slot) {
  const fui::Rect t = thumbRect(g, slot);
  return fui::makeRect(t.x, t.y, t.width, static_cast<int16_t>(t.height + g.markerRoom + g.captionH));
}

fui::Rect captionRect(const GridGeom& g, int slot) {
  if (g.captionH <= 0) return fui::makeRect(0, 0, 0, 0);
  const fui::Rect t = thumbRect(g, slot);
  return fui::makeRect(t.x, static_cast<int16_t>(t.y + t.height + g.markerRoom), t.width, g.captionH);
}

int cellAt(const GridGeom& g, int x, int y) {
  for (int slot = 0; slot < g.perPage; ++slot) {
    const fui::Rect c = cellRect(g, slot);
    if (x >= c.x && x < c.right() && y >= c.y && y < c.bottom()) return slot;
  }
  return -1;
}

int pageCountFor(const int specialTiles, const int libraryCount, const int perPage) {
  if (perPage <= 0) return 1;
  const int tiles = (specialTiles < 0 ? 0 : specialTiles) + (libraryCount < 0 ? 0 : libraryCount);
  if (tiles <= 0) return 1;
  return (tiles + perPage - 1) / perPage;
}

int16_t hintTextWidth(const fui::Rect& safe) { return static_cast<int16_t>(safe.width - toybox::kMargin * 2); }

int16_t hintStripHeight() { return kHintH; }

const freeink::Icon& chooseChipIcon(const bool choosing) {
  // Four squares for "several", a tick for "that is my several". Both at 24 so
  // the chip's ink does not change weight when the mode does; the lucide
  // grid-2x2 glyph is borrowed for its shape, not for the app it is named for.
  return choosing ? icon_tick_24 : icon_connections_24;
}

const char* chooseHint() { return "Tap the grid button to pick several."; }

const char* liveTileCaption() { return "Your phone"; }

// Measured, not guessed: 31 characters at the strip's pinned SMALL cut, one
// shorter than "Tap one to set your sleep screen." which it displaces.
// host-tests/wallcaption walks it through fittedTitle in the real face with the
// rest of the strip's sentences.
const char* liveStripLine() { return "Your phone is your sleep screen."; }

void buildGridChrome(toybox::Screen& screen, const GridChromeModel& model) {
  // The title says which mode this is, in the biggest type on the screen. The
  // chip alone could not, and carries even less of that load now that it is a
  // glyph: a person who has not been watching cannot tell a control they may
  // press from one they already pressed.
  chrome(screen, model.choosing ? "CHOOSE A SET" : model.title, model.rightLabel, true, model.choosing);

  // The hint strip, at a fixed place so the grid below it never moves. One
  // line, and three things want it; the order is settled just below.
  const fui::Rect safe = screen.frame().safeRect();
  // Absolute, like every other app's chrome: see kHintTop.
  const int16_t hintY = kHintTop;
  const char* line = nullptr;
  // The sleep-screen note wins, ahead of the free-space advisory. The honest
  // statement of that trade: on a filling card with a sleep-screen note to
  // show, the space advisory does not appear on THIS screen for the rest of the
  // session, because warning_ is computed once per onEnter and never refreshed.
  //
  // It wins anyway because the advisory has other voices and the note has none.
  // A pin that actually fails now raises the Notice screen, and buildOffer and
  // buildEmpty draw the same warning in their own full body width. A wallpaper
  // that cannot reach the glass is contradicted by the marker drawn beside it
  // and by nothing else, which is card #354 exactly.
  if (model.note != nullptr && model.note[0] != '\0') {
    line = model.note;
  } else if (model.warning != nullptr && model.warning[0] != '\0') {
    line = model.warning;
  } else if (model.liveOn) {
    // THIRD, and the position is the whole decision.
    //
    // Below the note and the warning, because both are NEWS -- something just
    // changed behind the user's back, or the card is filling -- and Live being
    // on is a standing state that will still be true on the next paint. A
    // standing line that outranked either would suppress it for the whole
    // session, which is the shape of #354 itself.
    //
    // Above the two below it, because both of those are false while Live is
    // showing. "Tap one to set your sleep screen." says nothing is set beside a
    // tile wearing the selection marker, which is #354 exactly: the marker and
    // the words disagreeing with nothing to say why. And chooseHint is an
    // affordance, which never outranks a fact.
    line = liveStripLine();
  } else if (!model.hasActive) {
    // Short enough to fit the hint strip at the grid's cut. The longer form
    // ("Tap a wallpaper to set it as your sleep screen.") was cut mid-phrase.
    line = "Tap one to set your sleep screen.";
  } else if (!model.choosing) {
    // Something IS set, nothing is wrong, and the strip would otherwise be
    // blank -- which is where the one affordance nobody would guess belongs.
    // Four tiles fit a page, so a chip in the band is the only thing on this
    // screen that says a set is possible at all.
    line = chooseHint();
  }
  if (line != nullptr) {
    const fui::Rect rect =
        fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), hintY, hintTextWidth(safe), kHintH);
    fui::TextStyle style = onPaper(screen.theme().smallText, fui::TextAlign::Left);
    // The SMALL slot, explicitly -- the same override drawGrid() makes for the
    // captions, for the same reason. themeTokens().smallText.font is kUiFont =
    // FONT_SLOT_BODY (ToyboxTokens.h says so out loud), and in this screen's
    // face set that is toybox_20, whose advanceY is 42 in a strip that is
    // kHintH = 30 tall. Worse, fittedTitle steps DOWN from the style's font, so
    // WHICH cut a sentence landed in depended on its length: short lines
    // overflowed the strip at 20px and long ones quietly dropped to 14px.
    // toybox_14's advanceY is 29, so pinning the slot makes every sentence fit
    // and makes the measurement in host-tests/wallcaption mean something.
    style.font = fui::FONT_SLOT_SMALL;
    std::string fitted = toybox::fittedTitle(screen.target(), line, rect.width, style);
    screen.target().text(rect, fitted.c_str(), style);
  }
}

void buildEmpty(toybox::Screen& screen, const EmptyModel& model) {
  chrome(screen, model.title, nullptr);

  if (model.warning != nullptr && model.warning[0] != '\0') {
    fui::TextStyle warn = onPaper(screen.theme().smallText, fui::TextAlign::Left);
    std::string fitted = toybox::fittedTitle(screen.target(), model.warning, screen.body().width, warn);
    screen.target().text(screen.takeTop(30, toybox::kGutter), fitted.c_str(), warn);
  }

  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(screen.takeTop(44, toybox::kGutter), "NO WALLPAPERS", head);

  fui::TextAreaProps detail;
  detail.text =
      "Tap + Add a wallpaper to put one here from your phone: scan the code, pick a "
      "picture, done. The built-in set can be downloaded too.\n\n"
      "Press Back to return.";
  detail.style = owned(screen.theme().bodyText, fui::TextAlign::Left);
  detail.showCaret = false;
  screen.textArea(detail, static_cast<int16_t>(screen.body().height - toybox::kGutter));
}

// ---------------------------------------------------------------------------
// ADD FROM A PHONE. Chosen from three rendered arrangements: the pairing twin,
// a numbered three-step rail, and a bare oversized code. This one won on its
// headline -- it is the only one that says what the black square IS before you
// have looked at it, which matters on a screen whose whole failure mode is
// "my phone is on the wrong network".
//
// Two facts here are load-bearing rather than decorative: the address in words
// (a QR tells a person nothing, and it is the only thing to fall back on) and
// that Back stops it.
namespace {

// The address, at the largest cut that holds it. NEVER handed straight to
// text(): it is one unbreakable token, and an overflowing token in these cuts
// does not arrive clipped or ellipsised -- the faces above toybox_10 carry no
// U+2026 glyph, so it simply stops at a plausible place. That is how a pairing
// screen once printed "read.crossplay.ma-r-s.com/pai" and nobody could see why
// the address did not work.
// "http://" is encoded in the QR and NOT drawn. It costs seven characters, and
// seven characters is the difference between the address holding the bold cut
// and stepping down onto the same serif 14 as the paragraph under it -- which
// is precisely the hierarchy defect the UI review had just removed, measured
// out of the render as an ink band falling from 27px to 24px the moment the
// hostname grew per-device. Browsers supply the scheme, and HTTPS-First does
// not interfere: Chromium exempts "non-unique hostnames, local IP addresses,
// and single-label hostnames", which covers both lines on this screen.
std::string_view withoutScheme(const char* url) {
  std::string_view v(url == nullptr ? "" : url);
  constexpr std::string_view kHttp = "http://";
  if (v.size() > kHttp.size() && v.compare(0, kHttp.size(), kHttp) == 0) v.remove_prefix(kHttp.size());
  return v;
}

// `font` because this screen is no longer the only caller. The Add screen's
// address is a device-local URL that can be an IPv4 literal, and it takes the
// default; the Live screen's is one fixed short hostname on a face set with no
// bold cut in SMALL, so it asks for the display cut instead. The SLOT varies,
// the two rules do not: strip the scheme, and never hand the token to text().
void drawAddress(toybox::Screen& screen, const fui::Rect& box, const char* url,
                 const fui::FontId font = fui::FONT_SLOT_SMALL, const bool bold = false) {
  // FONT_SLOT_SMALL, which readingAddressFaces binds to the bold reading cut.
  // Naming titleText here looked like asking for the display cut and was not:
  // no address fits it (a worst-case IPv4 URL measures 632 against 448), so the
  // ladder silently stepped this to the same serif 14 as the prose below it.
  // At bold 16 the longest possible address measures 399 and fits with room.
  fui::TextStyle style = onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1);
  style.font = font;
  style.bold = bold;
  const std::string shown(withoutScheme(url));
  const std::string fitted = toybox::fittedTitle(screen.target(), shown.c_str(), box.width, style);
  screen.target().text(box, fitted.c_str(), style);
}

// The numeric address, under the name. Quieter than the name on purpose: it is
// the fallback, and a reader who can use the QR should never need to read it.
// It is drawn rather than hidden because the two fail in opposite conditions --
// the name dies on a router that filters mDNS or a phone on a VPN, the address
// dies when DHCP moves this device -- and neither failure puts anything on
// screen to explain itself.
void drawAltAddress(toybox::Screen& screen, const fui::Rect& box, const char* url) {
  if (url == nullptr || url[0] == '\0') return;
  fui::TextStyle style = onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1);
  style.color = fui::Color::DarkGray;
  const std::string shown(withoutScheme(url));
  screen.target().text(box, toybox::fittedTitle(screen.target(), shown.c_str(), box.width, style).c_str(), style);
}

// The same-WiFi requirement is this screen's ENTIRE error handling, so it lives
// in the prose rather than the footer: nothing on the device can detect that the
// phone went out over cellular instead, and the browser's own message ("cannot
// reach this site") names no cause. It was in the footer for one render and came
// out as "PHONE MUST BE ON THE SAM..." -- the failure explanation, truncated.
constexpr const char* kProse = "Pick a photo and it lands here. Phone or computer, on this same WiFi.";
constexpr const char* kFoot = "BACK STOPS";

// WHAT THE SCREEN SAYS ONCE A PICTURE HAS LANDED.
//
// The route ends where the person was going: their photo is on the sleep
// screen before they look up from the phone, so every sentence here reports
// that rather than offering it. "BACK RETURNS", not "BACK STOPS": once
// something has arrived, leaving is finishing, and a word that reads as
// cancelling would be the screen describing its own success as an abort.
// SHORT ENOUGH TO KEEP THE DISPLAY CUT, which is the whole reason it is not
// anything longer: "ON YOUR SLEEP SCREEN" and "SLEEP SCREEN SET" both measure
// past the 448px body at toybox_30, so
// the ladder steps it down to the same serif as the prose under it and the
// screen loses its hierarchy without ever looking broken -- the same trap
// "SCAN WITH YOUR PHONE" fell into on this screen's first render. The picture
// directly below says what was set.
constexpr const char* kArrivedHead = "SLEEP SCREEN";
constexpr const char* kArrivedHere = "On your sleep screen.";
constexpr const char* kArrivedAgain = "Send another to replace it.";
constexpr const char* kAnother = "SEND ANOTHER";
constexpr const char* kArrivedFoot = "BACK RETURNS";
// The square, shared by the code and by the picture that replaces it.
constexpr int16_t kAddPictureSide = 232;

// Inherited from the Instapaper twin as `bottom() - 30, height 24`, which is
// correct THERE because it draws in toybox_10 (line box 21). Here the same box
// holds a 40px line box: DrawTarget::text clamps a negative centring offset to
// zero, so the line ran 758..798 -- five pixels BELOW body.bottom() and eleven
// from the panel edge, eating the whole page margin. The box came across from
// the twin and the font did not. Sized from the bound face now, so it cannot
// happen again when the face changes.
void drawFoot(toybox::Screen& screen, const fui::Rect& body, const char* label = kFoot) {
  fui::TextStyle style = onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1);
  const int16_t lineH = screen.target().lineHeight(style.font);
  const fui::Rect box = fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - lineH), body.width, lineH);
  screen.target().text(box, toybox::fittedTitle(screen.target(), label, box.width, style).c_str(), style);
}

// The headline is the only thing on this screen that gets the display cut, and
// it only keeps it by being short enough: "SCAN WITH YOUR PHONE" measures 579
// against a 448px body at toybox_30, so the ladder stepped it down to the same
// serif 14 as everything else and the screen lost its hierarchy without ever
// looking broken. "SCAN THIS CODE" fits, so the three levels are real -- Jersey
// 30 headline, bold serif 16 address, serif 14 prose.
void drawHeadline(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  fui::TextStyle style = onPaper(screen.theme().titleText, fui::TextAlign::Center, 1);
  screen.target().text(box, toybox::fittedTitle(screen.target(), text, box.width, style).c_str(), style);
}

}  // namespace

const char* addArrivedHeadline() { return kArrivedHead; }
const char* addAnotherLabel() { return kAnother; }
const char* addArrivedLine() { return kArrivedHere; }
const char* addFootWaiting() { return kFoot; }
const char* addFootArrived() { return kArrivedFoot; }
const char* addAgainLine() { return kArrivedAgain; }

int16_t addPictureSide() { return kAddPictureSide; }

AddRects buildAdd(toybox::Screen& screen, const AddModel& model) {
  // No right label. Any label at all costs the band its display cut: the widest
  // that fits is 62px, and "ADD A WALLPAPER" needs 433 of the 448 either way, so
  // the title steps from a 38px Jersey cap to a 21px serif one the moment a
  // count appears. A count belongs in the body, where it can also say a number
  // other than one.
  chrome(screen, "ADD A WALLPAPER", nullptr);
  const fui::Rect body = screen.body();
  const bool arrived = model.arrived != nullptr && model.arrived[0] != '\0';
  AddRects out;

  // The pairing twin. Same skeleton as InstapaperScreens::buildPairQr, with the
  // address occupying the line its 8-character code does. The arrival's picture
  // takes the same square, which is why one constant serves both.
  constexpr int16_t kQrSide = kAddPictureSide;
  constexpr int16_t kHead = 48;
  // Every text block's height is asked of the face that will draw it, never
  // typed. The two literals here were 46 and 108 against line boxes of 45 and
  // 120, so the prose overran its own rect by twelve pixels -- and the numbers
  // were then wrong for anything laid out against them.
  const int16_t addrH = screen.target().lineHeight(fui::FONT_SLOT_SMALL);
  const int16_t proseLine = screen.target().lineHeight(fui::FONT_SLOT_BODY);

  // ONCE A PICTURE HAS LANDED, THE PICTURE IS THE SCREEN.
  //
  // Chosen from three rendered arrangements (the other two: the code screen
  // keeping its shape with one sentence carrying the news, and the code
  // shrinking to make room for a thumbnail strip beside it). The reason this
  // one won is a fact about the route rather than a preference: every upload is
  // renamed w0001.bmp, w0002.bmp and so on by
  // CrossPointWebServer::nextWallpaperPath, and the phone's own name is
  // discarded there by design. So the NAME can never confirm anything -- "w0007
  // is on your sleep screen" tells a person nothing about which picture they
  // just sent -- and the only honest confirmation available is the picture
  // itself, at the size the square was already spending.
  //
  // The name is drawn under it anyway, small: it is what the grid's caption
  // will say, so somebody looking for this wallpaper again has the word for it.
  if (arrived) {
    const int16_t proseH = proseLine;
    const int16_t stack = static_cast<int16_t>(kHead + toybox::kMargin * 2 + kQrSide + toybox::kMargin + addrH +
                                               toybox::kGutter + proseH + toybox::kGutter + kButtonH);
    int16_t y = static_cast<int16_t>(body.y + (body.height - proseLine - stack) / 2);
    if (y < body.y) y = body.y;

    drawHeadline(screen, fui::makeRect(body.x, y, body.width, kHead), kArrivedHead);
    out.thumb = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                              static_cast<int16_t>(y + kHead + toybox::kMargin * 2), kQrSide, kQrSide);

    const int16_t nameY = static_cast<int16_t>(out.thumb.bottom() + toybox::kMargin);
    // FONT_SLOT_SMALL, which readingAddressFaces binds to the bold reading cut:
    // the one step of hierarchy this face set has between the prose and the
    // headline. A `bold = true` on the BODY slot is not it -- that face has no
    // bold and the flag draws the same regular.
    fui::TextStyle nameStyle = onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1);
    nameStyle.font = fui::FONT_SLOT_SMALL;
    screen.target().text(fui::makeRect(body.x, nameY, body.width, addrH),
                         toybox::fittedTitle(screen.target(), model.arrived, body.width, nameStyle).c_str(), nameStyle);

    const int16_t proseY = static_cast<int16_t>(nameY + addrH + toybox::kGutter);
    screen.target().text(fui::makeRect(body.x, proseY, body.width, proseH), kArrivedAgain,
                         onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1));
    // The one interaction slot this screen spends, and it buys the code back.
    // Without it the screen is a dead end one picture into a route whose whole
    // point is sending pictures.
    drawButton(screen,
               fui::makeRect(body.x, static_cast<int16_t>(proseY + proseH + toybox::kGutter), body.width, kButtonH),
               kAnother, ActionAddAnother);
    drawFoot(screen, body, kArrivedFoot);
    return out;
  }

  // Centred in the body rather than hung from its top, so the leftover is
  // shared above and below instead of pooling into a dead band over the footer.
  const int16_t proseH = static_cast<int16_t>(proseLine * 3);
  const int16_t stack =
      static_cast<int16_t>(kHead + toybox::kMargin * 2 + kQrSide + toybox::kMargin + addrH + toybox::kGutter + proseH);
  int16_t y = static_cast<int16_t>(body.y + (body.height - proseLine - stack) / 2);
  if (y < body.y) y = body.y;

  drawHeadline(screen, fui::makeRect(body.x, y, body.width, kHead), "SCAN THIS CODE");
  out.qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                         static_cast<int16_t>(y + kHead + toybox::kMargin * 2), kQrSide, kQrSide);

  const int16_t addrY = static_cast<int16_t>(out.qr.bottom() + toybox::kMargin);
  drawAddress(screen, fui::makeRect(body.x, addrY, body.width, addrH), model.url);
  drawAltAddress(screen, fui::makeRect(body.x, static_cast<int16_t>(addrY + addrH), body.width, proseLine),
                 model.altUrl);

  // FULL body width, not inset: the address is the longest unbreakable token on
  // this screen, and an inset that costs it two characters costs it silently.
  screen.target().text(
      fui::makeRect(body.x, static_cast<int16_t>(addrY + addrH + proseLine + toybox::kGutter), body.width, proseH),
      model.status != nullptr ? model.status : kProse, onPaper(screen.theme().bodyText, fui::TextAlign::Center, 3));
  drawFoot(screen, body, kFoot);
  return out;
}

// ---------------------------------------------------------------------------
// LIVE. The "Your phone" tile's destination.
//
// STATIC IN THIS SLICE: no networking, no pairing protocol, nothing persisted.
// Every string below arrives in the model from a compile-time stub, which is
// what makes the two renders (one arrangement x two states) reproducible.
//
// The two states bind DIFFERENT FACES, and it is not decoration. Unpaired, the
// screen's content is a six-digit code read down a telephone, so the Activity
// binds toybox::pairingCodeFaces() and the code gets the 82px capital. Paired,
// the content is three facts and three buttons, so it binds the same
// readingChromeFaces() the offer and the sheet use and there is no huge cut on
// the screen at all. buildLive is told which state it is in by the model and
// asks for slots, never for sizes, so neither half can reach a cut the other
// one bound.
//
// Hierarchy here is SIZE and INVERSION, never colour. GfxRendererTarget::text()
// decides ink with `style.color != Color::White`, so every non-white colour
// draws solid black (ToyboxTokens.h says the same thing about dimming a glyph):
// a label told apart from its value by a grey would be told apart by nothing.
namespace {

// MEASURED, not estimated. reading_serif_14 sets about 26 characters across a
// 448px body, so the rule these two are cut to is TWO LINES of it and no more
// -- the code is the screen and the sentence is a caption under it.
//
// Both were half again as long. The first said "Send a picture from your
// phone, or keep it Live so more arrive on their own", which is three lines
// explaining a feature to somebody who is holding a six-digit number and
// wants to know where to type it.
constexpr const char* kLiveWhat = "Type this code on your phone to send pictures.";
// The SAME box, for a code that means the opposite thing.
//
// Both halves of this screen show six digits under the word LIVE, and only this
// sentence says which of the two is on the panel: one makes a fridge, the other
// adds a phone to the one that already exists. It lives in the PROSE rather
// than the line under it because that line is the status, and during a join the
// status is always saying something ("Asking for a code to share.", "Waiting
// for a phone.") -- a note put there would be a note nobody ever sees. The
// person who needs this is the one about to worry they are replacing the
// message already on the glass.
constexpr const char* kLiveJoinWhat = "Give this code to somebody else. They can send too.";
// The ten-minute note is a LINE OF THE BODY and not the footer. It went in the
// footer first, where it drew as "CODE LASTS 10 MINUTES,..." -- drawFoot fits to
// one line and on this face set there is no cut below reading_serif_14 to step
// down to, so the ellipsis was the only move left. The footer says the one
// thing short enough to survive there.
constexpr const char* kLiveExpiry = "Code lasts ten minutes.";
constexpr const char* kLiveFoot = "BACK RETURNS";
// WHAT STANDS WHERE THE CODE GOES WHILE THERE IS NOT ONE.
//
// Emphatically not a plausible six-digit number. This screen used to fall back
// to a hardcoded "482 160" -- the screenshot harness's stub -- and the QR
// beside it encoded that same stub, so a reader still waiting on the service,
// or one that could not reach it at all, put a code somebody could read down a
// telephone and a square somebody could scan on the glass, both of them
// fiction. The first person through this path scanned the square, was told by
// the website that the code did not work, and typed the real one in by hand.
//
// Hyphens, and they are a real glyph in this cut: toybox_64 is generated from
// jersey25-ascii.ttf. If it were ever missing the line would draw as nothing,
// which is still not a code, which is the property that matters.
constexpr const char* kLiveNoCode = "--- ---";
// The caption under it, in place of "Type this code on your phone", which is
// an instruction about something that is not there.
constexpr const char* kLiveNoCodeWhat = "The code appears here when Live answers.";

// THE "Your phone" DESTINATION'S WORDS. See buildPhone for why the screen
// exists at all.
//
// Each route gets a verb and a sentence, and the sentence answers the one
// question the verb cannot: where the picture ends up. "SEND A PICTURE" and
// "LIVE" are both true of both routes read loosely enough, so the prose is
// what actually tells them apart, and it is measured rather than trimmed to
// look short -- reading_serif_14 sets about 26 characters across a 448px body,
// so three lines is 78 and both of these are inside it.
constexpr const char* kPhoneLede = "Two ways to put a picture on this reader.";
constexpr const char* kPhoneSend = "SEND A PICTURE";
constexpr const char* kPhoneSendWhat = "From a phone in this room, over Wi-Fi. Kept on the card like any wallpaper.";
constexpr const char* kPhoneLive = "LIVE";
constexpr const char* kPhoneLiveWhat = "From anywhere. The reader wakes now and then to see what arrived.";
// What Live is when nobody has set it up. The Activity sends the schedule note
// instead once there is one.
constexpr const char* kPhoneLiveIdle = "Not set up yet.";
// ONE WORD PER CONTROL, and each of them beside a mark.
//
// The three used to be "CHECK NOW", "TURN IT OFF" and "ADD SOMEBODY", stacked
// two-and-one under two labelled fact pairs, and the screen spent seven
// headings saying what six words and four glyphs say here. Mario's words:
// be concise, rely on icons, clear intuitive stuff.
//
// A WORD AS WELL AS THE MARK, never the mark alone. This panel has no hover and
// no tooltip -- the Add screen 400 lines up draws its address in words beside
// the QR for exactly that reason and says so -- and a rotating arrow is only
// obviously "ask the website now" to somebody who already knows what this
// screen does. The word costs one line of a 64px button that was going to be
// empty either way.
constexpr const char* kLiveCheck = "CHECK";
constexpr const char* kLiveAdd = "ADD";
// The last sentence of the empty list, and it is two sentences on purpose: the
// first says what the screen is, the second is what keeps it from reading as a
// broken one. A reader whose last phone was just removed is in exactly this
// state and nothing is wrong with it.
//
// THREE FACTS, and it needs all three. Nobody can send; here is the control
// that changes that; and nothing is broken. The middle one is new: with the
// list unheaded there is no longer anything on the screen that says what ADD
// adds, so a reader in this state had a button with no antecedent.
//
// NOT trimmed to fit. It was cut to "Nobody can send yet" on the assumption it
// would not hold two lines, and then measured at 888px against a 448px box,
// which is two with room; "yet" is false as well, since a reader whose last
// phone was just revoked has had senders. This one is 894px over three lines,
// and the box takes three because it sits inside a band reserved for four
// 69px rows. Measure before cutting.
constexpr const char* kLiveNobody = "Nobody can send. Press ADD to let a phone in. The picture stays.";
// The word over the date on the confirm, and the only place on the device that
// says what a bare "12 Sep" beside a name MEANS. The rows cannot afford it --
// it would be the same word four times over a list whose whole point is that it
// is short -- and without it somewhere the date reads as when that phone last
// sent, which is a different fact and the one a person would act on.
//
// Stacked OVER the date rather than written in front of it: "Added 12 Sep" on
// one line is 147px at the button cut, which leaves 285px for the name, and
// "Abuela phone" is 315px at the display cut. Putting it inline would have
// stepped the name down a rung on the one screen where the name is the point.
constexpr const char* kLiveAddedLabel = "Added";
// The confirm's headline and its two labels. THEM, not IT: the wallpaper
// confirm removes a file and this one removes a person's access, and the
// pronoun is the only thing on the screen that says which kind of thing is
// about to happen.
constexpr const char* kLiveRevokeTitle = "REMOVE A PHONE";
constexpr const char* kLiveRevokeKeep = "KEEP THEM";
constexpr const char* kLiveRevokeGo = "REMOVE THEM";
constexpr const char* kLiveRevokeFoot = "BACK KEEPS THEM";
// What it costs, and every clause of it is a fact somebody standing at the
// reader cannot otherwise know. They are not told: the service sends nothing.
// It cannot be undone from here: there is no un-revoke call. The way back is a
// new code, which needs somebody at this reader again -- and the person losing
// access is in another country, which is the whole reason revoking lives on the
// device at all.
//
// THREE LINES, and that is the budget rather than a style choice. The prose box
// runs from under the name down to KEEP, and KEEP is the list's own band -- so
// the room here is whatever the band leaves, which is 124px at the prose cut.
// The first version of this sentence was four lines and arrived as "...means a
// new", with the clause about how to undo it missing: the fact a person most
// needs before pressing REMOVE was the fact that fell off.
constexpr const char* kLiveRevokeWhy =
    "They stop being able to send. Nobody tells them, and coming back needs a new code.";

// QrUtils pins version 4 for every payload under 114 bytes, so this screen's
// code is 33 modules whatever the link says. A side that is a whole multiple of
// 33 spends no pixels on the centring slack QrUtils would otherwise leave.
constexpr int16_t kLiveQrSide = 165;  // five module pixels a side

// The marks, at 24 and not at toybox::kIconSize.
//
// 32 is the size a mark is drawn at when it is ALONE on a shelf row. Every one
// of these is beside a word -- three inside a 64px button, one at the end of a
// list row -- and at 32 the pair stops reading as a pair and starts reading as
// a picture with a caption stuck to it.
constexpr int16_t kLiveIconSize = 24;

// The unpaired screen's three cuts, asked for by SLOT so the paired half cannot
// accidentally reach one of them: on its face set FONT_SLOT_SMALL is the button
// cut, not the huge one.
constexpr fui::FontId kLiveCodeSlot = fui::FONT_SLOT_SMALL;     // toybox_64 while unpaired
constexpr fui::FontId kLiveAddressSlot = fui::FONT_SLOT_TITLE;  // toybox_30, both states
// THE ADDRESS ON THE UNPAIRED SCREEN IS NOT AT THE DISPLAY CUT, and cannot be.
// kLiveAddress names a host and a path now, and no string that long fits a
// 448px body at toybox_30 -- the bare host does not either. fittedTitle answers
// that by stepping DOWN, and the only rung under the display cut on this face
// set is the prose face: the one line a person has to read off the glass and
// type into a phone, set exactly like the paragraph beneath it, with nothing
// anywhere reporting the change. So it is CHOSEN here rather than arrived at,
// one rung down and bold, which is the same trick readingAddressFaces plays for
// the Add screen in a slot this one has already spent on the code.
constexpr fui::FontId kLiveUrlSlot = fui::FONT_SLOT_BODY;    // reading_serif_14, bold
constexpr fui::FontId kLiveProseSlot = fui::FONT_SLOT_BODY;  // reading_serif_14, both states
// The headline on the paired screen: WHEN THE NEXT CHECK IS, and it is the same
// cut the unpaired half gives the address. The biggest thing on a screen should
// be the thing the screen is opened to find out, and that is this.
constexpr fui::FontId kLiveHeroSlot = fui::FONT_SLOT_TITLE;  // toybox_30, 63px

fui::TextStyle liveCut(toybox::Screen& screen, const fui::FontId font, const fui::TextAlign align,
                       const fui::Color colour = fui::Color::Black, const uint8_t lines = 1) {
  fui::TextStyle style = screen.theme().bodyText;
  style.font = font;
  style.align = align;
  style.color = colour;
  style.maxLines = lines;
  return style;
}

// Every label and value on this screen goes through here and not through
// text(). At each of these cuts an overflowing string arrives neither clipped
// nor ellipsised -- the faces above toybox_10 carry no U+2026, so the line
// simply stops at a plausible place -- and the one string here that MUST be
// read character by character is the code.
void drawFitted(toybox::Screen& screen, const fui::Rect& box, const char* text, fui::TextStyle style) {
  if (text == nullptr || text[0] == '\0') return;
  screen.target().text(box, toybox::fittedTitle(screen.target(), text, box.width, style).c_str(), style);
}

// A mark, drawn square and centred in the box it is given.
void drawLiveIcon(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& icon, const fui::Color ink) {
  const fui::Rect where =
      fui::makeRect(static_cast<int16_t>(box.x + (box.width - kLiveIconSize) / 2),
                    static_cast<int16_t>(box.y + (box.height - kLiveIconSize) / 2), kLiveIconSize, kLiveIconSize);
  screen.target().bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain, fui::Paint::solid(ink));
}

// A CONTROL THAT IS A MARK AND A WORD, and never one of them.
//
// The mark is what lets three controls share the row two used to need. The word
// is what stops the mark being a guess: there is no hover and no tooltip on
// this panel, and the Add screen already draws its address in words beside the
// QR for that exact reason. A rotating arrow says "again" to somebody who
// already knows what this screen does and nothing at all to anybody else.
//
// The pair is centred as ONE GROUP rather than the mark sitting at a fixed
// indent, so a three-letter word and a five-letter one are both balanced in
// their own button.
//
// The label is fitted against WHAT IS LEFT after the mark, not against the
// button: a label measured on the whole width would be declared to fit and then
// drawn into a box the mark had already taken a third of, and at these cuts an
// overflowing string neither clips nor ellipsises -- it stops somewhere
// plausible.
void drawIconButton(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& icon, const char* label,
                    const fui::ActionId action, const bool primary) {
  const fui::Color ink = primary ? fui::Color::White : fui::Color::Black;
  if (primary) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 3);
  }
  // PINNED to the button cut, not taken from theme().smallText, and the three
  // buttons in two different faces is why. smallText resolves to the prose face
  // here, so fittedTitle's ladder -- which steps down and only down -- kept
  // "STOP" and "ADD" in the serif because they fitted, and shrank "CHECK" to
  // the button cut because it did not. One row, two typefaces, and nothing
  // failed: every label fitted its box. Asking for the cut directly leaves the
  // ladder one rung, so all three are the same face whatever the words become.
  fui::TextStyle style = liveCut(screen, fui::FONT_SLOT_SMALL, fui::TextAlign::Left, ink);
  const int16_t room = static_cast<int16_t>(box.width - kLiveIconSize - toybox::kGutter * 2);
  const std::string fitted = toybox::fittedTitle(screen.target(), label, room, style);
  const int16_t textW = screen.target().measureText(style.font, fitted.c_str(), style).width;
  const int16_t groupW = static_cast<int16_t>(kLiveIconSize + toybox::kGutter + textW);
  const int16_t left = static_cast<int16_t>(box.x + (box.width - groupW) / 2);
  drawLiveIcon(screen, fui::makeRect(left, box.y, kLiveIconSize, box.height), icon, ink);
  // The text box runs to the button's own edge rather than stopping at the
  // measured width. The string is already fitted; a box cut to the exact
  // measurement is a box one rounding away from cutting what it just fitted.
  const int16_t textX = static_cast<int16_t>(left + kLiveIconSize + toybox::kGutter);
  screen.target().text(fui::makeRect(textX, box.y, static_cast<int16_t>(box.right() - textX), box.height),
                       fitted.c_str(), style);
  screen.frame().hit(box, action);
}

// One sender: who, and when they were let in. Side by side where the row can
// take both, stacked where it cannot, and the choice is MEASURED rather than
// assumed from the body width: a row laid out for the wide case drew
// "Mario's..." in a narrower one -- a list of people with a person's name cut
// out of it. Returns the height it used, so a caller advances by what was drawn
// instead of by what it assumed.
// Whether THE WHOLE LIST has to stack, asked once and applied to every row. Per
// row it would be per row: one name long enough to stack and the next one short
// enough not to, which is a list that changes shape halfway down. The question
// is about the widest name, so it is answered about the widest name.
// The room a row has for words: everything but the column the remove mark sits
// in. Derived rather than typed, because three things read it -- the stack
// decision, the name box and the date box -- and a fourth number would be a
// fourth thing to edit alone.
int16_t senderTextWidth(const int16_t rowWidth) {
  return static_cast<int16_t>(rowWidth - kLiveIconSize - toybox::kMargin);
}

bool sendersStack(toybox::Screen& screen, const int16_t width, const LiveModel& model) {
  const fui::TextStyle prose = liveCut(screen, kLiveProseSlot, fui::TextAlign::Left);
  for (int i = 0; i < model.senderCount && i < LiveModel::kMaxSenders; ++i) {
    const LiveModel::Sender& sender = model.senders[i];
    if (sender.who == nullptr) continue;
    const int16_t whoW = screen.target().measureText(prose.font, sender.who, prose).width;
    const int16_t sinceW =
        sender.since == nullptr ? 0 : screen.target().measureText(prose.font, sender.since, prose).width;
    if (whoW + sinceW + toybox::kMargin > width) return true;
  }
  return false;
}

// The height of ONE row, and it is the same for every row on every list.
//
// It does not depend on the names, on how many there are, or on whether they
// stack, for three reasons that all point the same way: the confirm has to be
// able to place KEEP from an index alone (RevokeModel carries no list), the
// reserved band must not change shape as phones come and go
// (same-pixel-different-action), and a row is a CONTROL now -- the 22px the
// prose cut needs is half a finger, and a 22px target is a control that misses.
// So it is the tallest thing a row can draw, floored at minTouchSize.
int16_t senderRowHeight(toybox::Screen& screen) {
  const int16_t drawn = static_cast<int16_t>(screen.target().lineHeight(kLiveProseSlot) +
                                             screen.target().lineHeight(fui::FONT_SLOT_SMALL));
  const int16_t floor = screen.frame().device().minTouchSize;
  return drawn > floor ? drawn : floor;
}

// One sender: who, when they were let in, and an X at the end of the row.
//
// The X IS THE HEADING, and that is the whole trade. The list used to carry
// "WHO CAN SEND" over it and "TAP TO REMOVE" beside that, two headings for a
// list whose rows are a name and a date -- and the only screen either of them
// was protecting was the one where a person cannot tell a fact from a control.
// A mark at the end of a row is what every list in the world puts there, and
// the thing behind it is a confirm that NAMES the person, so discovering it by
// pressing one costs nothing and destroys nothing.
//
// The mark is drawn once per row and is NOT its own hit rect. The whole row
// stays the target: it is bigger than the mark, it is what the confirm's KEEP
// is laid out against, and a second target inside the first would be a second
// place for the same finger to land.
void drawSender(toybox::Screen& screen, const fui::Rect& row, const LiveModel::Sender& sender, const bool stacked) {
  if (sender.who == nullptr) return;
  const fui::TextStyle prose = liveCut(screen, kLiveProseSlot, fui::TextAlign::Left);
  const int16_t lineH = screen.target().lineHeight(kLiveProseSlot);
  const int16_t sinceH = screen.target().lineHeight(fui::FONT_SLOT_SMALL);
  const int16_t textW = senderTextWidth(row.width);
  const int16_t sinceW =
      sender.since == nullptr ? 0 : screen.target().measureText(prose.font, sender.since, prose).width;
  // Centred in the row rather than pinned to its top. The row is sized for the
  // stacked case and floored at a finger, so an unstacked name pinned to row.y
  // would sit against the name above it with all the slack below -- a list
  // whose rows look like pairs.
  const int16_t drawn = static_cast<int16_t>(stacked ? lineH + sinceH : lineH);
  int16_t y = static_cast<int16_t>(row.y + (row.height - drawn) / 2);
  if (y < row.y) y = row.y;

  drawLiveIcon(screen,
               fui::makeRect(static_cast<int16_t>(row.right() - kLiveIconSize), row.y, kLiveIconSize, row.height),
               icon_live_remove_24, fui::Color::Black);

  if (!stacked) {
    drawFitted(screen, fui::makeRect(row.x, y, static_cast<int16_t>(textW - sinceW - toybox::kMargin), lineH),
               sender.who, prose);
    drawFitted(screen, fui::makeRect(static_cast<int16_t>(row.x + textW - sinceW), y, sinceW, lineH), sender.since,
               liveCut(screen, kLiveProseSlot, fui::TextAlign::Right));
    return;
  }
  // Stacked. The date takes the button cut under the name rather than the prose
  // one beside it: smaller says "about the line above" on a face set with no
  // second colour to say it with.
  drawFitted(screen, fui::makeRect(row.x, y, textW, lineH), sender.who, prose);
  drawFitted(screen, fui::makeRect(row.x, static_cast<int16_t>(y + lineH), textW, sinceH), sender.since,
             liveCut(screen, fui::FONT_SLOT_SMALL, fui::TextAlign::Left));
}

// The band's state word, and the label of the control that changes it.
//
// "ON" / "OFF" rather than "LIVE IS ON": the band it sits in already says LIVE,
// and a status badge that repeats the title of the screen it is on is three
// words doing one word's work.
//
// The BUTTON is a verb and the BAND is a state, and the two vocabularies are
// deliberately different. A button reading "OFF" under a band reading "ON" asks
// a person to work out which of the two is a fact and which is an offer -- on a
// panel that takes up to two seconds to repaint, with one of them inside a box
// and the other not. It also cost the test its teeth: with one vocabulary both
// words are on the screen in both states, so an assertion that each is drawn
// passes however the two are wired together (a-test-not-seen-fail).
//
// Still two readings of ONE BOOL in ONE PLACE, which is why this is a pair of
// functions and not four literals at the call sites.
const char* liveStateWord(const bool on) { return on ? "ON" : "OFF"; }
const char* liveToggleLabel(const bool on) { return on ? "STOP" : "START"; }

// A hairline the width of a column, used as a divider between stacked facts.
void liveRule(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t width) {
  screen.target().fill(fui::makeRect(x, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
}

// ONE arithmetic for the paired screen, read by three things that must not
// disagree: the drawing, the hit table, and the confirm that stands in front of
// the destructive half. It is the same rule the grid follows -- the geometry is
// shared between the draw and the hit-test -- and it is the rule that has caught
// more bugs in this fork than any other.
//
// Every height is asked of the face that will draw it. The Add screen shipped
// two typed literals and everything laid out against them inherited the error.
struct PairedYs {
  fui::Rect body;
  int16_t lineH = 0;   // the prose cut, for a sender's name
  int16_t heroH = 0;   // the display cut, for the next check
  int16_t labelH = 0;  // the button cut, for the cadence and a sender's date
  int16_t heroY = 0;   // WHEN THE NEXT CHECK IS, and nothing above it
  int16_t cadenceY = 0;
  int16_t controlsY = 0;  // CHECK, the toggle and ADD, three across
  int16_t thirdW = 0;     // a third of the body, less the gutters between them
  int16_t ruleY = 0;
  int16_t rowsY = 0;  // the first sender row
  int16_t rowH = 0;
  int16_t afterY = 0;  // below the WHOLE band, full or not: where the confirm puts REMOVE
};

PairedYs pairedYs(toybox::Screen& screen) {
  PairedYs out;
  out.body = screen.body();
  out.lineH = screen.target().lineHeight(kLiveProseSlot);
  out.heroH = screen.target().lineHeight(kLiveHeroSlot);
  out.labelH = screen.target().lineHeight(fui::FONT_SLOT_SMALL);
  out.rowH = senderRowHeight(screen);

  // THREE CONTROLS ACROSS ONE ROW, and it is what buys the headline its size.
  //
  // The screen was two labelled fact pairs, then two buttons side by side, then
  // two more headings, then the rows, then a full-width ADD SOMEBODY under all
  // of it. Seven headings for three facts. Folding the third button up beside
  // the other two frees a whole 64px control row and its margin, and the
  // headline spends it: the next check is the display cut now, which is the
  // only thing on this screen anybody opens it to find out.
  //
  // MEASURED, not estimated: on this face set the display cut is 63px and the
  // button cut 29, not the 38 and 22 a guess would have given, and a guess is
  // what made the first version of this screen fit on paper and not on glass.
  // The three labels are one word each and the widest measures 72px inside a
  // 141px third, mark and gutter included.
  out.thirdW = static_cast<int16_t>((out.body.width - toybox::kGutter * 2) / 3);
  int16_t y = out.body.y;
  out.heroY = y;
  y = static_cast<int16_t>(y + out.heroH);
  // Directly under the headline with no gap: the cadence is a subtitle to it,
  // not a second fact of its own. That is the whole difference between this and
  // the pair of labelled columns it replaces, which said "In about 24 hours"
  // beside "Every 24 hours" and read as one fact typed twice.
  out.cadenceY = y;
  y = static_cast<int16_t>(y + out.labelH + toybox::kMargin * 2);
  out.controlsY = y;
  y = static_cast<int16_t>(y + kButtonH + toybox::kMargin);
  out.ruleY = y;
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter);
  out.rowsY = y;
  // The band is RESERVED, not grown, so letting a phone in -- or taking one out
  // -- never slides anything under a finger already travelling towards it
  // (same-pixel-different-action), and the confirm's REMOVE can be placed once
  // instead of per list length.
  y = static_cast<int16_t>(y + LiveModel::kMaxSenders * out.rowH + toybox::kGutter);
  out.afterY = y;
  return out;
}

// The report at the foot of the paired screen, and why it is not one line.
//
// One line was enough while everything it could say was one of OURS: the
// LiveStatus sentences are enumerated, short, and walked by a test in the box
// that draws them. A SERVICE's refusal is neither. It arrives verbatim -- the
// device does not get to reword a decision somebody else made -- and the
// service's longest is 64 characters, which at the prose cut is not one line
// and never was. Drawn as one, "This reader already has 4 phones. Remove one
// first." reached the panel as "This reader already has 4 phones...." with the
// only actionable half of it gone.
//
// So: the prose cut when it fits on a line, exactly as before, and otherwise
// the condensed cut and up to three of them. Through fitLines either way, so a
// sentence longer than even that is marked with the "..." this face can really
// draw rather than a U+2026 that would be a hole.
//
// BOTH HALVES OF THE SCREEN GO THROUGH THIS NOW. The unpaired half drew its
// status with drawFitted, which is one row and no step down, so a transport's
// sentence reached the panel as "Could not reach the sync..." -- with the half
// that says what to do about it gone. That is the same defect this function
// was written for, on the other state of the same screen (fix-the-twin-too).
// THE RUNG IT STEPS DOWN TO IS THE CALLER'S, and that is not a preference.
// The two halves of this screen bind DIFFERENT FACES TO THE SAME SLOTS: paired,
// FONT_SLOT_SMALL is toybox_14 and a genuine condensed rung; unpaired,
// pairingCodeFaces puts the EIGHTY-TWO PIXEL code face in that same slot. A
// report that stepped "down" to FONT_SLOT_SMALL on the unpaired half therefore
// drew a transport's sentence three words high, off the bottom of the panel and
// through the footer -- which is what the simulator showed the first time this
// helper was shared between the two (fui-font-slot-fallback, one layer out).
//
// The unpaired half has no rung below its prose cut at all, so it passes the
// prose slot here and wraps instead of shrinking.
int16_t liveReportHeight(toybox::Screen& screen, const int16_t width, const char* text, const fui::FontId smaller) {
  if (text == nullptr || text[0] == '\0') return 0;
  const fui::TextStyle prose = liveCut(screen, kLiveProseSlot, fui::TextAlign::Center);
  if (screen.target().measureText(prose.font, text, prose).width <= width) {
    return screen.target().lineHeight(kLiveProseSlot);
  }
  return static_cast<int16_t>(screen.target().lineHeight(smaller) * 3);
}

void drawLiveReportIn(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId smaller) {
  if (text == nullptr || text[0] == '\0') return;
  const fui::TextStyle prose = liveCut(screen, kLiveProseSlot, fui::TextAlign::Center);
  const int16_t proseH = screen.target().lineHeight(kLiveProseSlot);
  if (screen.target().measureText(prose.font, text, prose).width <= box.width) {
    screen.target().text(fui::makeRect(box.x, box.y, box.width, proseH), text, prose);
    return;
  }
  const fui::TextStyle small = liveCut(screen, smaller, fui::TextAlign::Center, fui::Color::Black, 3);
  screen.target().text(box, toybox::fitLines(screen.target(), text, box.width, 3, small).c_str(), small);
}

void drawLiveReport(toybox::Screen& screen, const fui::Rect& body, const char* text) {
  if (text == nullptr || text[0] == '\0') return;
  const int16_t height = liveReportHeight(screen, body.width, text, fui::FONT_SLOT_SMALL);
  drawLiveReportIn(screen, fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - height), body.width, height),
                   text, fui::FONT_SLOT_SMALL);
}

// -------------------------------------------------------------------------
// THE STACK. The code dominant and centred, everything else under it in
// reading order. Its unpaired half has a single axis: a person holding the
// reader up to a phone camera or reading digits aloud never has to choose
// where to look first.
fui::Rect buildLiveStackPaired(toybox::Screen& screen, const LiveModel& model) {
  const PairedYs g = pairedYs(screen);
  const fui::Rect body = g.body;

  // THE HEADLINE, with no label over it.
  //
  // "NEXT CHECK" in the button cut above "In about 24 hours" in the prose cut
  // was a heading explaining a sentence that is already a sentence. The one
  // thing somebody opens this screen to learn is when the next check is, so it
  // is the biggest thing on the screen and nothing stands in front of it.
  //
  // The cadence sits directly under it at the small cut, as a subtitle rather
  // than as a second labelled column. It answers a different question and it
  // reads as an aside to the line above, which is what it is.
  drawFitted(screen, fui::makeRect(body.x, g.heroY, body.width, g.heroH), model.nextCheck,
             liveCut(screen, kLiveHeroSlot, fui::TextAlign::Left));

  drawFitted(screen, fui::makeRect(body.x, g.cadenceY, body.width, g.labelH), model.cadence,
             liveCut(screen, fui::FONT_SLOT_SMALL, fui::TextAlign::Left));

  // Three controls, three marks, three words. CHECK is the filled one: it is
  // the thing this screen is for, and the headline above it is the answer it
  // changes.
  const int16_t middleX = static_cast<int16_t>(body.x + g.thirdW + toybox::kGutter);
  const int16_t rightX = static_cast<int16_t>(body.right() - g.thirdW);
  drawIconButton(screen, fui::makeRect(body.x, g.controlsY, g.thirdW, kButtonH), icon_live_check_24, kLiveCheck,
                 ActionLiveCheck, true);
  drawIconButton(screen, fui::makeRect(middleX, g.controlsY, g.thirdW, kButtonH), icon_live_power_24,
                 liveToggleLabel(model.on), ActionLiveToggle, false);
  drawIconButton(screen, fui::makeRect(rightX, g.controlsY, g.thirdW, kButtonH), icon_live_add_24, kLiveAdd,
                 ActionLiveAdd, false);

  liveRule(screen, body.x, g.ruleY, body.width);

  if (model.senderCount <= 0) {
    // NOT an empty region under a rule. A reader whose last phone was just
    // removed is here, nothing is wrong with it, and the recovery is the ADD
    // button above -- so it says both of those in words. An empty space where
    // content belongs is this fork's most repeated user-visible failure and was
    // twice reported as a crash by cold testers. This is the one place on the
    // screen where a sentence is still worth its room, because there is nothing
    // else here to read.
    drawProse(screen, fui::makeRect(body.x, g.rowsY, body.width, static_cast<int16_t>(g.lineH * 3)), kLiveNobody,
              fui::TextAlign::Left);
  } else {
    const bool stacked = sendersStack(screen, senderTextWidth(body.width), model);
    for (int i = 0; i < model.senderCount && i < LiveModel::kMaxSenders; ++i) {
      // The SAME rect the confirm reads back for its KEEP, and the same rect
      // the hit table gets. Three readers, one rectangle.
      const fui::Rect row = liveSenderRowRect(screen, i);
      drawSender(screen, row, model.senders[i], stacked);
      // A row is a control. The INDEX travels as the action value, because the
      // id the service named is not a thing a screen may hold: it is a token
      // hash, the Activity owns it, and a screen that carried it would be a
      // second copy of the one fact that decides whose access is destroyed.
      screen.frame().hit(row, ActionLiveSender, static_cast<int16_t>(i));
    }
  }

  // The status takes the FOOT's line when there is one, rather than a line of
  // its own. This stack is laid out against a measured height and an inserted
  // row pushes the last control off the bottom; the foot is already one fitted
  // sentence in the one place a short report belongs.
  //
  // It has to be drawn somewhere. CHECK's honest answer is usually "nothing
  // new", and on this panel a control that reports nothing and a touch that was
  // dropped look exactly alike -- which is the confusion the Live tile was
  // logged for before this screen existed.
  drawLiveReport(screen, body, (model.status != nullptr && model.status[0] != '\0') ? model.status : kLiveFoot);
  return fui::makeRect(0, 0, 0, 0);
}

fui::Rect buildLiveStack(toybox::Screen& screen, const LiveModel& model) {
  if (!liveShowsCode(model)) return buildLiveStackPaired(screen, model);

  const fui::Rect body = screen.body();
  // NO CODE IS A STATE, not a case to paper over. It is what the screen is in
  // from the moment it opens until the service answers -- which on a device is
  // a Wi-Fi join and a TLS handshake, seconds, with somebody already holding a
  // phone up to the glass -- and it is where a reader that cannot reach Live
  // at all stays. Everything below keeps its place either way, so nothing
  // moves when the digits arrive; only what is IN the code line, the caption
  // and the square changes.
  const bool haveCode = model.code != nullptr && model.code[0] != '\0';
  const int16_t codeH = screen.target().lineHeight(kLiveCodeSlot);
  const int16_t addrH = screen.target().lineHeight(kLiveUrlSlot);
  const int16_t lineH = screen.target().lineHeight(kLiveProseSlot);
  // TWO LINES, down from three, because both sentences were cut to two. The
  // box is what wraps them, so a box left at three lines would be a blank line
  // in the middle of a centred stack rather than harmless slack.
  const int16_t proseH = static_cast<int16_t>(lineH * 2);
  const char* report = (model.status != nullptr && model.status[0] != '\0') ? model.status : kLiveExpiry;
  // ASKED, not assumed. This row holds the service's refusals and the
  // transport's sentences as well as the screen's own, and the longest of them
  // is 84 characters -- three wrapped lines, not one, and a stack laid out
  // against one line while three are drawn puts the QR under the last of them.
  //
  // kLiveProseSlot as the rung to fall to, which means it does not fall: this
  // face set binds the EIGHTY-TWO PIXEL code cut to FONT_SLOT_SMALL, so the
  // paired half's step down is a step up here. See liveReportHeight.
  const int16_t reportH = liveReportHeight(screen, body.width, report, kLiveProseSlot);

  // Centred in what is left AFTER the foot, rather than in the body: the foot
  // is drawn from body.bottom() up, so a stack centred in the whole body puts
  // its last element under a line it cannot see. Every height above is asked of
  // the face that will draw it; the Add screen's two typed literals were both
  // wrong and everything laid out against them inherited the error.
  const int16_t stack = static_cast<int16_t>(codeH + toybox::kGutter + toybox::kRule + toybox::kGutter + addrH +
                                             toybox::kGutter + proseH + reportH + toybox::kMargin + kLiveQrSide);
  const int16_t room = static_cast<int16_t>(body.height - lineH - toybox::kGutter);
  int16_t y = static_cast<int16_t>(body.y + (room - stack) / 2);
  if (y < body.y) y = body.y;

  drawFitted(screen, fui::makeRect(body.x, y, body.width, codeH), haveCode ? model.code : kLiveNoCode,
             liveCut(screen, kLiveCodeSlot, fui::TextAlign::Center));
  y = static_cast<int16_t>(y + codeH + toybox::kGutter);
  // A short rule, not a full-width one: it separates the code from the address
  // without reading as the bottom of a panel the code is inside.
  const int16_t ruleW = static_cast<int16_t>(body.width / 2);
  liveRule(screen, static_cast<int16_t>(body.x + (body.width - ruleW) / 2), y, ruleW);
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter);

  drawAddress(screen, fui::makeRect(body.x, y, body.width, addrH), model.url, kLiveUrlSlot, /*bold=*/true);
  y = static_cast<int16_t>(y + addrH + toybox::kGutter);
  drawProse(screen, fui::makeRect(body.x, y, body.width, proseH),
            haveCode ? (model.joining ? kLiveJoinWhat : kLiveWhat) : kLiveNoCodeWhat, fui::TextAlign::Center);
  y = static_cast<int16_t>(y + proseH);
  drawLiveReportIn(screen, fui::makeRect(body.x, y, body.width, reportH), report, kLiveProseSlot);
  y = static_cast<int16_t>(y + reportH + toybox::kMargin);

  const fui::Rect qr =
      fui::makeRect(static_cast<int16_t>(body.x + (body.width - kLiveQrSide) / 2), y, kLiveQrSide, kLiveQrSide);
  drawFoot(screen, body, kLiveFoot);
  // AN EMPTY FRAME, NOT AN EMPTY REGION, and a zero rect back to the caller so
  // nothing is drawn in it. A blank quarter of a screen reads as a crash -- it
  // has been reported as one twice by cold testers -- and a hairline square
  // says "the picture of the code goes here" without pretending there is one.
  if (!haveCode) {
    screen.target().stroke(qr, fui::Paint::solid(fui::Color::Black), 1);
    return fui::makeRect(0, 0, 0, 0);
  }
  return qr;
}

}  // namespace

const char* liveStatusLine(const LiveStatus status) {
  // Short on purpose. This line is ONE fitted row in both halves of the screen
  // -- under the address when unpaired, the foot's row when paired -- and the
  // longest of these is what sizes them all.
  switch (status) {
    case LiveStatus::AskingForCode:
      return "Asking for a code.";
    case LiveStatus::AskingToShare:
      return "Asking for a code to share.";
    case LiveStatus::WaitingForPhone:
      return "Waiting for a phone.";
    case LiveStatus::Connected:
      return "Connected. Fetching.";
    case LiveStatus::Checking:
      return "Asking Live now.";
    case LiveStatus::NothingNew:
      return "Checked. Nothing new.";
    case LiveStatus::NewMessage:
      return "A new message arrived.";
    case LiveStatus::Removed:
      return "That phone can no longer send.";
    case LiveStatus::Disconnected:
      return "Disconnected. Set up again.";
    case LiveStatus::TellingLiveOff:
      return "Telling Live it is off.";
    case LiveStatus::kCount:
      break;
  }
  return "";
}

bool liveShowsCode(const LiveModel& model) { return !model.configured || model.joining; }

const char* phoneLiveIdle() { return kPhoneLiveIdle; }
const char* phoneSendLabel() { return kPhoneSend; }
const char* phoneLiveLabel() { return kPhoneLive; }

const char* liveNobodySends() { return kLiveNobody; }
const char* liveJoinPrompt() { return kLiveJoinWhat; }
const char* liveRevokeConsequence() { return kLiveRevokeWhy; }
const char* liveAddedLabel() { return kLiveAddedLabel; }

// THIS translation unit's copy, and that is the whole reason it is a function.
// ToyboxIcons.h declares every icon `static const`, so a test that included the
// header would hold a different object with a different bits pointer and could
// not tell the mark this file drew from any other (everyone-picks-the-same-name
// is the same shape of problem one level up).
const freeink::Icon& liveRemoveMark() { return icon_live_remove_24; }

fui::Rect liveSenderRowRect(toybox::Screen& screen, const int index) {
  const PairedYs g = pairedYs(screen);
  const int clamped = index < 0 ? 0 : (index >= LiveModel::kMaxSenders ? LiveModel::kMaxSenders - 1 : index);
  return fui::makeRect(g.body.x, static_cast<int16_t>(g.rowsY + clamped * g.rowH), g.body.width, g.rowH);
}

fui::Rect liveSendersBand(toybox::Screen& screen) {
  const PairedYs g = pairedYs(screen);
  return fui::makeRect(g.body.x, g.rowsY, g.body.width, static_cast<int16_t>(LiveModel::kMaxSenders * g.rowH));
}

// Deliberately the SAME rect as the whole band of rows. Not a coincidence to be
// tidied away: it is the whole defence, and host-tests/wallcaption asserts it
// covers every row rather than merely sitting near them.
fui::Rect liveKeepRect(toybox::Screen& screen) { return liveSendersBand(screen); }

fui::Rect liveRevokeRect(toybox::Screen& screen) {
  // Under the WHOLE band, and since the third control moved up into the header
  // row this is a strip of the list screen that carries NOTHING. It used to be
  // ADD SOMEBODY's pixels, which was accepted with an argument about the reveal
  // gate; the argument is no longer needed, and host-tests/wallcaption asserts
  // the strip is clear rather than taking this comment's word for it.
  const PairedYs g = pairedYs(screen);
  return fui::makeRect(g.body.x, g.afterY, g.body.width, kButtonH);
}

void buildLiveRevoke(toybox::Screen& screen, const RevokeModel& model) {
  // Its own band title, because the two screens must not be mistaken for one
  // another during a 0.3-2s repaint: this is the only screen in Live that
  // destroys anything, and the word at the top is the fastest thing to read.
  chrome(screen, kLiveRevokeTitle, nullptr);
  const fui::Rect body = screen.body();
  const int16_t headH = screen.target().lineHeight(kLiveAddressSlot);
  const fui::Rect keep = liveKeepRect(screen);

  // WHO, on its own line and at the biggest cut this face set has here, with
  // when they were let in beside it. A name folded into a sentence would have
  // to be composed, and a line composed inside a paint is a line no test can
  // walk -- which is precisely the line you would want walked before a screen
  // takes somebody's access away.
  //
  // Beside rather than under, because the two share this row with the prose
  // box's ceiling: an extra line here comes straight out of the sentence that
  // says what removing them costs, and the date is the less load-bearing half.
  const fui::TextStyle dateCut = liveCut(screen, fui::FONT_SLOT_SMALL, fui::TextAlign::Right);
  const bool dated = model.since != nullptr && model.since[0] != '\0';
  // The column is the WIDER of the two stacked lines, so neither can be drawn
  // through the name beside it.
  const int16_t labelW = screen.target().measureText(dateCut.font, kLiveAddedLabel, dateCut).width;
  const int16_t dateW = dated ? screen.target().measureText(dateCut.font, model.since, dateCut).width : 0;
  const int16_t sinceW =
      dated ? static_cast<int16_t>((labelW > dateW ? labelW : dateW) + toybox::kMargin) : static_cast<int16_t>(0);
  drawFitted(screen, fui::makeRect(body.x, body.y, static_cast<int16_t>(body.width - sinceW), headH), model.who,
             liveCut(screen, kLiveAddressSlot, fui::TextAlign::Left));
  if (sinceW > 0) {
    // ADDED over the date, both in the column. The list's rows cannot carry
    // this word -- four copies of it over a list whose point is brevity -- so
    // this is the one place on the device that says what a bare date beside a
    // name means. Without it somewhere it reads as when that phone last SENT,
    // which is a different fact and the one somebody would act on.
    const int16_t small = screen.target().lineHeight(fui::FONT_SLOT_SMALL);
    const int16_t columnX = static_cast<int16_t>(body.right() - sinceW);
    const int16_t stackY = static_cast<int16_t>(body.y + (headH - small * 2) / 2);
    drawFitted(screen, fui::makeRect(columnX, stackY > body.y ? stackY : body.y, sinceW, small), kLiveAddedLabel,
               dateCut);
    drawFitted(screen,
               fui::makeRect(columnX, static_cast<int16_t>((stackY > body.y ? stackY : body.y) + small), sinceW, small),
               model.since, dateCut);
  }
  const int16_t y = static_cast<int16_t>(body.y + headH + toybox::kGutter);

  // Derived from BOTH ends, the way the wallpaper confirm's prose is: the box
  // runs from under the name down to the first control, so moving either
  // shrinks the box rather than letting a sentence run under a button. KEEP's
  // top is the list's band, which comes out of the face's line heights, so this
  // cannot be a height typed here.
  const int16_t proseBottom = static_cast<int16_t>(keep.y - toybox::kGutter);
  drawProse(screen, fui::makeRect(body.x, y, body.width, static_cast<int16_t>(proseBottom > y ? proseBottom - y : 0)),
            kLiveRevokeWhy, fui::TextAlign::Left);

  // KEEP is HIT over every pixel a row occupies and DRAWN as an ordinary
  // button inside that strip. A repeat of the press that got here is a cancel
  // by construction rather than by luck -- the confirm does not know which row
  // the finger came from, so the safe half has to answer for all four -- and
  // the ink stays a button, because a 276px slab of black is slow on this panel
  // and ghosts after it.
  const fui::Rect keepInk =
      fui::makeRect(keep.x, static_cast<int16_t>(keep.y + (keep.height - kButtonH) / 2), keep.width, kButtonH);
  drawButton(screen, keepInk, kLiveRevokeKeep, ActionLiveKeep, &keep);
  drawOutlineButton(screen, liveRevokeRect(screen), kLiveRevokeGo, ActionLiveRevoke);
  drawFoot(screen, body, kLiveRevokeFoot);
}

fui::Rect buildLive(toybox::Screen& screen, const LiveModel& model) {
  // The band says LIVE in both states. It is the shortest true name for the
  // destination and it fits the display cut with room, which "YOUR PHONE" does
  // not need to be tested against because it would be a second name for one
  // thing (the tile's caption is the tile's business).
  //
  // AND, once there is a phone attached, whether Live is on. It was a 63px
  // headline in the body and the body is the one thing this screen ran out of:
  // four tappable sender rows need the room, and a STATE is what a header band
  // carries everywhere else in this fork. It stays one reading of one bool --
  // liveStateWord here, liveToggleLabel on the button -- so a screen saying ON
  // beside a control offering to turn it on is still impossible.
  chrome(screen, "LIVE", liveShowsCode(model) ? nullptr : liveStateWord(model.on));
  return buildLiveStack(screen, model);
}

// ---------------------------------------------------------------------------
// YOUR PHONE. The tile's destination, and the whole of what was missing.
//
// docs/apps/fridge.md settled this before the tile was built: "one tile
// captioned Your phone that leads to a destination offering both intents: add
// a wallpaper, or set up Live... The destination names both intents; the tile
// names only where you are going." The tile shipped and the destination did
// not -- it went straight to Live -- so the local upload server was left
// reachable from exactly one place: USE MY OWN PHOTO on the offer screen,
// which only exists while the built-in set is missing. On any reader that has
// wallpapers, which is every reader after the first fetch, sending a picture
// from a phone had no way in at all.
//
// TWO ROUTES, and they are genuinely different things rather than two spellings
// of one. One is a page this reader serves over the local Wi-Fi, ending in a
// file on the card that behaves like every other wallpaper. The other is a
// fridge somebody feeds from another country on a schedule. A person standing
// here has to be able to tell which they want, so each says what it does in a
// sentence, and Live says what state it is in besides -- the answer to "is it
// already doing something?" is the fact that decides the tap.
//
// NO SECOND GRID TILE. Mario chose the combined tile from three rendered
// arrangements; this screen is where the choice it defers gets made.
void buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, "YOUR PHONE", nullptr);
  const fui::Rect body = screen.body();
  const int16_t lineH = screen.target().lineHeight(fui::FONT_SLOT_BODY);
  const int16_t smallH = screen.target().lineHeight(fui::FONT_SLOT_SMALL);

  int16_t y = body.y;
  drawProse(screen, fui::makeRect(body.x, y, body.width, static_cast<int16_t>(lineH * 2)), kPhoneLede,
            fui::TextAlign::Left);
  y = static_cast<int16_t>(y + lineH * 2 + toybox::kMargin);

  // FILLED IS THE ORDINARY ONE. Sending a picture is what somebody with a
  // photo on their phone came here for, and it is the route that works with no
  // account, no service and nobody in another country.
  drawButton(screen, fui::makeRect(body.x, y, body.width, kButtonH), kPhoneSend, ActionAddOwn);
  y = static_cast<int16_t>(y + kButtonH + toybox::kGutter);
  drawProse(screen, fui::makeRect(body.x, y, body.width, static_cast<int16_t>(lineH * 3)), kPhoneSendWhat,
            fui::TextAlign::Left);
  y = static_cast<int16_t>(y + lineH * 3 + toybox::kMargin * 2);

  drawOutlineButton(screen, fui::makeRect(body.x, y, body.width, kButtonH), kPhoneLive, ActionLiveOpen);
  y = static_cast<int16_t>(y + kButtonH + toybox::kGutter);
  drawProse(screen, fui::makeRect(body.x, y, body.width, static_cast<int16_t>(lineH * 3)), kPhoneLiveWhat,
            fui::TextAlign::Left);
  // A gutter under the sentence before the state line. Without it the two run
  // together and the state reads as a fourth line of the description rather
  // than as an answer to a different question.
  y = static_cast<int16_t>(y + lineH * 3 + toybox::kGutter);
  // WHAT LIVE IS DOING, composed by the Activity from live::scheduleNote so it
  // is the same sentence the Live screen itself carries. A second phrasing
  // here would be a second copy of a fact the schedule owns.
  drawFitted(screen, fui::makeRect(body.x, y, body.width, smallH), model.liveState,
             liveCut(screen, fui::FONT_SLOT_SMALL, fui::TextAlign::Left));

  drawFoot(screen, body, kLiveFoot);
}

MarkerRects markerRects(const fui::Rect& thumb) {
  const int o = kMarkerGap + kMarkerWeight;
  const int x = thumb.x - o, y = thumb.y - o;
  const int w = thumb.width + o * 2, h = thumb.height + o * 2;
  const int a = kBracketArm, t = kMarkerWeight;
  const auto R = [](int rx, int ry, int rw, int rh) {
    return fui::makeRect(static_cast<int16_t>(rx), static_cast<int16_t>(ry), static_cast<int16_t>(rw),
                         static_cast<int16_t>(rh));
  };
  MarkerRects m{};
  m.r[0] = R(x, y, a, t);          // top-left, horizontal arm
  m.r[1] = R(x, y, t, a);          // top-left, vertical arm
  m.r[2] = R(x + w - a, y, a, t);  // top-right
  m.r[3] = R(x + w - t, y, t, a);
  m.r[4] = R(x, y + h - t, a, t);  // bottom-left
  m.r[5] = R(x, y + h - a, t, a);
  m.r[6] = R(x + w - a, y + h - t, a, t);  // bottom-right
  m.r[7] = R(x + w - t, y + h - a, t, a);
  return m;
}

int markerBottomExtent(const fui::Rect& thumb) {
  const MarkerRects m = markerRects(thumb);
  int lowest = thumb.y + thumb.height;
  for (const fui::Rect& r : m.r) lowest = std::max(lowest, static_cast<int>(r.y + r.height));
  return lowest;
}

// ---------------------------------------------------------------------------
// BEFORE: the built-in set is not on the card.
//
// Three arrangements, all obeying the same contract: name the set, say how many
// and roughly how big, and offer exactly ONE primary action. None of them can
// show the wallpapers themselves -- they are not downloaded yet, and embedding
// previews is the flash cost this whole download exists to avoid -- so the set
// is sold with its NAMES, which cost nothing and are the actual draw.
void buildOffer(toybox::Screen& screen, const OfferModel& model) {
  chrome(screen, "WALLPAPERS", nullptr);

  if (model.warning != nullptr && model.warning[0] != '\0') {
    fui::TextStyle warn = onPaper(screen.theme().smallText, fui::TextAlign::Left);
    std::string fitted = toybox::fittedTitle(screen.target(), model.warning, screen.body().width, warn);
    screen.target().text(screen.takeTop(kHintH, toybox::kGutter), fitted.c_str(), warn);
  }

  const int remaining = model.count - model.alreadyHave;
  char size[24];
  formatSize(model.bytes, size, sizeof(size));

  char headline[40];
  std::snprintf(headline, sizeof(headline), "%d WALLPAPERS", remaining);

  const fui::Rect body = screen.body();
  const int16_t left = static_cast<int16_t>(body.x);
  const int16_t width = body.width;
  // Room under the button for TWO lines of the secondary sentence: at the
  // reading cut it does not fit on one, and a sentence cut with an ellipsis is
  // the defect this screen exists to avoid.
  const int16_t buttonY = static_cast<int16_t>(body.y + body.height - kButtonH - 96);

  // THE COVER. A band of real artwork across the top, type below it, the way a
  // book cover carries its title. Mario picked this over a full-bleed poster and
  // over an artwork-led grid: it leads with the most beautiful thing in the set
  // at a scale that suits the panel, and still keeps a real headline and a
  // scannable layout underneath.
  //
  // Truchet because its curves read at a glance; a fine check at this size just
  // looks like grey. Drawn by gen_geoA.py's rules rather than shipped as an
  // asset, which is the whole reason this screen costs no flash.
  const fui::Rect band = fui::makeRect(left, body.y, width, 260);
  paintTruchet(screen.target(), band, 72);
  screen.target().stroke(band, fui::Paint::solid(fui::Color::Black), 2);

  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 276), width, 56), headline, head);

  // What is actually IN the pack. The cover shows one motif, and without this
  // line it undersells twenty-one: a band of curves says nothing about the
  // Duerer woodcut or the star chart, which are the strongest things here.
  // Names come from displayName() so they match the captions in the picker.
  char blurb[192];
  std::snprintf(blurb, sizeof(blurb), "%s, %s, %s and %d more. About %s over WiFi.",
                wallpapers::displayName("durer-horsemen").full.c_str(),
                wallpapers::displayName("celestial").full.c_str(), wallpapers::displayName("bauhaus").full.c_str(),
                remaining - 3, size);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 340), width, 134), blurb, fui::TextAlign::Left);

  drawButton(screen, fui::makeRect(left, buttonY, width, kButtonH), "GET THEM", ActionGetSet);

  // The second route is now a CONTROL, outlined rather than filled.
  //
  // It was a sentence, on the reasoning that two buttons on a "before" screen
  // is two obvious actions, which is none. That reasoning is sound about two
  // EQUAL buttons and it made this feature unreachable on a factory device:
  // an empty library shows only this screen, and its one control fetches a 1MB
  // pack behind a 12MB floor and the WiFi picker. A person who just wants their
  // own photo on their new reader had a sentence and no way to act on it --
  // and ActionAddOwn was routed but drawn by NOTHING, so even a hopeful tap
  // did nothing at all (nothing-calls-it).
  //
  // Outlined keeps the hierarchy the sentence was protecting: one filled
  // button is still the obvious action, and this is plainly the other one.
  const int16_t secondY = static_cast<int16_t>(buttonY + kButtonH + toybox::kGutter);
  drawOutlineButton(screen, fui::makeRect(left, secondY, width, kButtonH), "USE MY OWN PHOTO", ActionAddOwn);
}

// DOWNLOADING. Painted from inside the blocking fetch, so it says what is
// happening, how far along, and that Back stops it -- the three things a person
// staring at a frozen-looking panel needs (a-silent-screen-reads-as-a-crash).
uint32_t gridMeaning(const int page, const int view, const int libraryCount, const int specialTiles,
                     const bool choosing) {
  uint32_t m = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(page));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(view));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(libraryCount));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(specialTiles));
  return paintclock::mixMeaning(m, choosing ? 1u : 0u);
}

BarSpan fetchBarSpan(const FetchingModel& model) {
  BarSpan span;
  const int phases = model.phaseCount < 1 ? 1 : model.phaseCount;
  span.units = model.total > 0 ? model.total * phases : 1;
  const int done = model.done < 0 ? 0 : (model.done > model.total ? model.total : model.done);
  const int phase = model.phase < 0 ? 0 : (model.phase >= phases ? phases - 1 : model.phase);
  span.at = phase * model.total + done;
  if (span.at > span.units) span.at = span.units;
  return span;
}

void buildFetching(toybox::Screen& screen, const FetchingModel& model) {
  chrome(screen, "WALLPAPERS", nullptr);
  const fui::Rect body = screen.body();
  const int16_t left = body.x;

  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 40), body.width, 52),
                       model.cancelling ? "STOPPING" : "GETTING THEM", head);

  char line[96];
  if (model.cancelling) {
    std::snprintf(line, sizeof(line), "Finishing the current one, then stopping.");
  } else {
    std::snprintf(line, sizeof(line), "Wallpaper %d of %d.",
                  model.done + 1 > model.total ? model.total : model.done + 1, model.total);
  }
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 104), body.width, 40), line,
            fui::TextAlign::Left);

  // A bar, because "7 of 21" is a number and a bar is a glance. Drawn as an
  // outline with a filled portion so a 1-bit panel shows both ends of it.
  const int16_t barY = static_cast<int16_t>(body.y + 158);
  const fui::Rect bar = fui::makeRect(left, barY, body.width, 26);
  screen.target().stroke(bar, fui::Paint::solid(fui::Color::Black), 2);
  // ONE sweep across BOTH phases: the download fills the first half, the unpack
  // the second. Two phases each filling the whole bar is what made it look like
  // it restarted.
  if (model.total > 0) {
    const BarSpan span = fetchBarSpan(model);
    const int16_t inner = static_cast<int16_t>(body.width - 8);
    const int16_t filled = static_cast<int16_t>(static_cast<int>(inner) * span.at / span.units);
    if (filled > 0) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(left + 4), static_cast<int16_t>(barY + 4), filled, 18),
                           fui::Paint::solid(fui::Color::Black));
    }
  }

  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(barY + 56), body.width, 200),
            "They go onto the card, not into the app. Press Back to stop; what has arrived is kept.",
            fui::TextAlign::Left);
}

// FAILED, and every other "here is what happened". Always has a button.
void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "WALLPAPERS", nullptr);
  const fui::Rect body = screen.body();
  const int16_t left = body.x;

  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 48), body.width, 52), model.headline, head);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 116), body.width, 200), model.body,
            fui::TextAlign::Left);

  if (model.actionLabel != nullptr) {
    drawButton(screen,
               fui::makeRect(left, static_cast<int16_t>(body.y + body.height - kButtonH - 44), body.width, kButtonH),
               model.actionLabel, model.action);
  }
}

// ---------------------------------------------------------------------------
// THE HOLD SHEET, AND THE ONE DESTRUCTIVE BUTTON IN THIS APP.
//
// Reached by holding a cell, never by tapping one. The header says WALLPAPER
// (singular) so the screen names its own subject: the grid behind it says
// WALLPAPERS, and two screens with one title is how a user loses track of which
// one they are on.

namespace {
// One derivation, four rects. Every one of them hangs off the two constants
// above, so moving the stack moves the confirm with it and the "keep is where
// delete was" identity cannot be broken by editing one of them.
//
// Card 358: the ROW is absolute, the sides are not. kSheetStackTop is measured
// down from the body top, and that body top is pinned at panel row 0 by
// absoluteChrome -- the header band already paints over the rows the glass
// hides. Adding safe.y here was the same double count card 358 took out of
// gridGeom and buildGridChrome, and it put these two screens ten pixels below
// every other app's body. Left and width still come off the safe rect, because
// the bezel really does cover a column on each side.
int16_t stackSlot(const fui::DeviceContext& device, const int index) {
  (void)device;
  return static_cast<int16_t>(kSheetStackTop + index * (kSheetButtonH + kSheetButtonGap));
}
fui::Rect stackRect(const fui::DeviceContext& device, const int index) {
  const fui::Rect safe = device.safeRect();
  return fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), stackSlot(device, index),
                       static_cast<int16_t>(safe.width - toybox::kMargin * 2), kSheetButtonH);
}
}  // namespace

fui::Rect sheetHeadRect(const fui::DeviceContext& device) {
  const fui::Rect safe = device.safeRect();
  // Absolute row, safe-rect sides. See stackSlot and toybox::kBodyTop.
  return fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), static_cast<int16_t>(kBodyTop + toybox::kGutter),
                       static_cast<int16_t>(safe.width - toybox::kMargin * 2), kSheetHeadH);
}

// The prose box on each screen: from under the headline down to the first
// control. Derived from BOTH ends rather than given a height, so moving the
// button stack or the headline cannot leave a sentence overlapping a button --
// it makes the box shorter, and the box is what host-tests/wallcaption measures
// the sentence against.
namespace {
fui::Rect proseTo(const fui::DeviceContext& device, const fui::Rect& firstControl) {
  const fui::Rect head = sheetHeadRect(device);
  const int16_t top = static_cast<int16_t>(head.bottom() + toybox::kGutter);
  const int16_t bottom = static_cast<int16_t>(firstControl.y - toybox::kGutter);
  return fui::makeRect(head.x, top, head.width, static_cast<int16_t>(bottom > top ? bottom - top : 0));
}
}  // namespace

fui::Rect sheetPreviewRect(const fui::DeviceContext& device) { return stackRect(device, 0); }
fui::Rect sheetDeleteRect(const fui::DeviceContext& device) { return stackRect(device, 1); }
// Deliberately the SAME rect as the sheet's DELETE. Not a coincidence to be
// tidied away: it is the whole defence, and host-tests/wallcaption asserts the
// two are identical rather than merely close.
fui::Rect confirmKeepRect(const fui::DeviceContext& device) { return sheetDeleteRect(device); }
fui::Rect confirmDeleteRect(const fui::DeviceContext& device) { return stackRect(device, 2); }

fui::Rect sheetProseRect(const fui::DeviceContext& device) { return proseTo(device, sheetPreviewRect(device)); }
fui::Rect confirmProseRect(const fui::DeviceContext& device) { return proseTo(device, confirmKeepRect(device)); }

namespace {
// The name, at the largest cut that holds it on two lines.
//
// fittedTitle, NOT a bare fitLines at the display cut. A wallpaper the user
// added is named by its FILE, which is the one string on these screens nobody
// chose the width of, and an ordinary phone name -- "SCREENSHOT 2026 09 05 AT
// 14 23 07" -- overflows two lines of the display cut. A bare fitLines answers
// that by appending U+2026, and NO Toybox cut above toybox_10 carries that
// glyph: it draws as a HOLE, so the name stops mid-word with a gap after it on
// the screen that is about to delete it (typography-fold). fittedTitle steps
// the cut DOWN first and only marks at the smallest, which is the one that can
// really draw the mark. Same rule the add screen's address arrived at.
std::string drawSheetName(toybox::Screen& screen, const fui::Rect& box, const char* name) {
  fui::TextStyle style = onPaper(screen.theme().titleText, fui::TextAlign::Left, 2);
  const std::string drawn = toybox::fittedTitle(screen.target(), name, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
  return drawn;
}
}  // namespace

const char* sheetInstruction(const bool isActive) {
  return isActive ? "This one is on your sleep screen now. Preview fills the panel; tap it to come back."
                  : "Preview fills the panel exactly as the sleep screen draws it; tap it to come back.";
}

void buildSheet(toybox::Screen& screen, const SheetModel& model) {
  chrome(screen, "WALLPAPER", nullptr);
  const fui::DeviceContext device = screen.frame().device();

  // Everything on these two screens is laid out against the published rects
  // rather than against screen.body(), for the same reason the grid is: the
  // safety proof in host-tests/wallcaption has to read the SAME rectangles the
  // drawing does.
  drawSheetName(screen, sheetHeadRect(device), model.name);

  // What each button does, BEFORE it is pressed. Preview leaves no chrome on
  // the panel on purpose -- it is the sleep screen, and a hint band drawn over
  // it would be a preview of something the sleep screen never shows -- so the
  // way back out has to be said here instead, where there is room for it.
  drawProse(screen, sheetProseRect(device), sheetInstruction(model.isActive), fui::TextAlign::Left);

  drawButton(screen, sheetPreviewRect(device), "PREVIEW", ActionPreview);
  drawButton(screen, sheetDeleteRect(device), "DELETE", ActionDelete);
  drawFoot(screen, screen.body(), "BACK RETURNS");
}

void buildConfirm(toybox::Screen& screen, const ConfirmModel& model) {
  chrome(screen, "DELETE WALLPAPER", nullptr);
  const fui::DeviceContext device = screen.frame().device();

  drawSheetName(screen, sheetHeadRect(device), model.name);

  drawProse(screen, confirmProseRect(device), model.consequence, fui::TextAlign::Left);

  // KEEP IT first, and on the pixels the sheet's DELETE occupied. A repeat of
  // the press that got here is a cancel, by construction rather than by luck.
  drawButton(screen, confirmKeepRect(device), "KEEP IT", ActionKeep);
  drawButton(screen, confirmDeleteRect(device), "DELETE IT", ActionConfirmDelete);
  drawFoot(screen, screen.body(), "BACK KEEPS IT");
}

}  // namespace wallpapersui
