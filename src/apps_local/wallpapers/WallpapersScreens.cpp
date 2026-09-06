#include "WallpapersScreens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "../ui/ToyboxText.h"
#include "WallpapersCore.h"

namespace wallpapersui {

namespace {

// The top of the body: below the header band AND the rule Toybox draws under
// it, which is what kChromeHeight names -- kHeaderHeight is the band alone, so
// this read as a gutter of clearance and was five pixels.
//
// NOT the same row as the shelf's, and the comment here used to claim it was.
// Every caller below adds safe.y to this (see gridGeom and buildEmpty), while
// the chrome it is measured from is pinned at panel row 0 by absoluteChrome --
// so on the X4 Pro this body starts ten pixels lower than Hacker News' or the
// shelf's, which do not add it. That predates card #248 and is left alone here
// rather than fixed blind: it is an alignment BETWEEN apps, the ui suite builds
// every screen with an empty safe area and cannot see it, and moving two apps
// up by ten pixels is a change to look at on the panel. Card #358.
constexpr int16_t kBodyTop = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter);
// A fixed strip under the chrome for the free-space advisory or the "nothing is
// set yet" hint. Fixed so the grid's top does not jump when a hint appears.
constexpr int16_t kHintH = 30;
// Clear space between the hint and the first row of thumbnails. Without it the
// grid began at exactly the hint's box bottom, so the sentence sat ON the
// artwork -- Mario's words were that it is too close to the wallpapers. Taken
// out of the grid's height (the cells are height-constrained), which costs each
// thumbnail a few pixels and buys the sentence room to read as a caption for
// the screen rather than a label on the first tile.
constexpr int16_t kHintGap = 16;
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

void drawButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action) {
  screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  fui::TextStyle style = screen.theme().smallText;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::White;
  style.maxLines = 1;
  screen.target().text(box, label, style);
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

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
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
  const int16_t gridTop = static_cast<int16_t>(safe.y + kBodyTop + kHintH + kHintGap);
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

int16_t hintTextWidth(const fui::Rect& safe) { return static_cast<int16_t>(safe.width - toybox::kMargin * 2); }

int16_t hintStripHeight() { return kHintH; }

void buildGridChrome(toybox::Screen& screen, const GridChromeModel& model) {
  chrome(screen, model.title, model.rightLabel);

  // The hint strip, at a fixed place so the grid below it never moves. One
  // line, and three things want it; the order is settled just below.
  const fui::Rect safe = screen.frame().safeRect();
  const int16_t hintY = static_cast<int16_t>(safe.y + kBodyTop);
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
  } else if (!model.hasActive) {
    // Short enough to fit the hint strip at the grid's cut. The longer form
    // ("Tap a wallpaper to set it as your sleep screen.") was cut mid-phrase.
    line = "Tap one to set your sleep screen.";
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
      "Add wallpapers from crossplay.ma-r-s.com/wallpapers, then copy them into the "
      "wallpapers folder on the card using File Transfer. They will show up here.\n\n"
      "Press Back to return.";
  detail.style = owned(screen.theme().bodyText, fui::TextAlign::Left);
  detail.showCaret = false;
  screen.textArea(detail, static_cast<int16_t>(screen.body().height - toybox::kGutter));
}

void buildHelp(toybox::Screen& screen) {
  chrome(screen, "ADD A WALLPAPER", nullptr);

  fui::TextAreaProps detail;
  detail.text =
      "Make a wallpaper from any picture in your browser at "
      "crossplay.ma-r-s.com/wallpapers (nothing is uploaded), then copy the file into "
      "the wallpapers folder on the card using File Transfer. It will appear here "
      "beside the built-in ones.\n\n"
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

void drawAddress(toybox::Screen& screen, const fui::Rect& box, const char* url) {
  // FONT_SLOT_SMALL, which readingAddressFaces binds to the bold reading cut.
  // Naming titleText here looked like asking for the display cut and was not:
  // no address fits it (a worst-case IPv4 URL measures 632 against 448), so the
  // ladder silently stepped this to the same serif 14 as the prose below it.
  // At bold 16 the longest possible address measures 399 and fits with room.
  fui::TextStyle style = onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1);
  style.font = fui::FONT_SLOT_SMALL;
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
constexpr const char* kProse = "Pick a photo and it lands here. Your phone has to be on this same WiFi.";
constexpr const char* kFoot = "BACK STOPS";

// Inherited from the Instapaper twin as `bottom() - 30, height 24`, which is
// correct THERE because it draws in toybox_10 (line box 21). Here the same box
// holds a 40px line box: DrawTarget::text clamps a negative centring offset to
// zero, so the line ran 758..798 -- five pixels BELOW body.bottom() and eleven
// from the panel edge, eating the whole page margin. The box came across from
// the twin and the font did not. Sized from the bound face now, so it cannot
// happen again when the face changes.
void drawFoot(toybox::Screen& screen, const fui::Rect& body) {
  fui::TextStyle style = onPaper(screen.theme().bodyText, fui::TextAlign::Center, 1);
  const int16_t lineH = screen.target().lineHeight(style.font);
  const fui::Rect box = fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - lineH), body.width, lineH);
  screen.target().text(box, toybox::fittedTitle(screen.target(), kFoot, box.width, style).c_str(), style);
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

fui::Rect buildAdd(toybox::Screen& screen, const AddModel& model) {
  // No right label. Any label at all costs the band its display cut: the widest
  // that fits is 62px, and "ADD A WALLPAPER" needs 433 of the 448 either way, so
  // the title steps from a 38px Jersey cap to a 21px serif one the moment a
  // count appears. A count belongs in the body, where it can also say a number
  // other than one.
  chrome(screen, "ADD A WALLPAPER", nullptr);
  const fui::Rect body = screen.body();

  // The pairing twin. Same skeleton as InstapaperScreens::buildPairQr, with the
  // address occupying the line its 8-character code does.
  constexpr int16_t kQrSide = 232;
  constexpr int16_t kHead = 48;
  // Every text block's height is asked of the face that will draw it, never
  // typed. The two literals here were 46 and 108 against line boxes of 45 and
  // 120, so the prose overran its own rect by twelve pixels -- and the numbers
  // were then wrong for anything laid out against them.
  const int16_t addrH = screen.target().lineHeight(fui::FONT_SLOT_SMALL);
  const int16_t proseLine = screen.target().lineHeight(fui::FONT_SLOT_BODY);
  const int16_t proseH = static_cast<int16_t>(proseLine * 3);

  // Centred in the body rather than hung from its top, so the leftover is
  // shared above and below instead of pooling into a dead band over the footer.
  const int16_t stack =
      static_cast<int16_t>(kHead + toybox::kMargin * 2 + kQrSide + toybox::kMargin + addrH + toybox::kGutter + proseH);
  int16_t y = static_cast<int16_t>(body.y + (body.height - proseLine - stack) / 2);
  if (y < body.y) y = body.y;

  drawHeadline(screen, fui::makeRect(body.x, y, body.width, kHead), "SCAN THIS CODE");
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                                     static_cast<int16_t>(y + kHead + toybox::kMargin * 2), kQrSide, kQrSide);

  const int16_t addrY = static_cast<int16_t>(qr.bottom() + toybox::kMargin);
  drawAddress(screen, fui::makeRect(body.x, addrY, body.width, addrH), model.url);
  drawAltAddress(screen, fui::makeRect(body.x, static_cast<int16_t>(addrY + addrH), body.width, proseLine),
                 model.altUrl);

  // FULL body width, not inset: the address is the longest unbreakable token on
  // this screen, and an inset that costs it two characters costs it silently.
  screen.target().text(
      fui::makeRect(body.x, static_cast<int16_t>(addrY + addrH + proseLine + toybox::kGutter), body.width, proseH),
      model.status != nullptr ? model.status : kProse, onPaper(screen.theme().bodyText, fui::TextAlign::Center, 3));
  drawFoot(screen, body);
  return qr;
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

  // The secondary route stays a sentence, never a second button: two buttons on
  // a "before" screen is two obvious actions, which is none.
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(buttonY + kButtonH + 8), width, 84),
            "Or make your own from any picture, in a browser.", fui::TextAlign::Center);
}

// DOWNLOADING. Painted from inside the blocking fetch, so it says what is
// happening, how far along, and that Back stops it -- the three things a person
// staring at a frozen-looking panel needs (a-silent-screen-reads-as-a-crash).
uint32_t gridMeaning(const int page, const int view, const int libraryCount, const int specialTiles) {
  uint32_t m = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(page));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(view));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(libraryCount));
  return paintclock::mixMeaning(m, static_cast<uint32_t>(specialTiles));
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

}  // namespace wallpapersui
