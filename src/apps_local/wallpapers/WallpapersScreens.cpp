#include "WallpapersScreens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"
#include "WallpapersCore.h"

namespace wallpapersui {

namespace {

// The top of the body: below the header band and the rule Toybox draws under
// it, matching the other apps so the grid lines up with the shelf it came from.
constexpr int16_t kBodyTop = static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter);
// A fixed strip under the chrome for the free-space advisory or the "nothing is
// set yet" hint. Fixed so the grid's top does not jump when a hint appears.
constexpr int16_t kHintH = 30;
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
  toybox::headerRule(screen);
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
  const int16_t gridTop = static_cast<int16_t>(safe.y + kBodyTop + kHintH);
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

void buildGridChrome(toybox::Screen& screen, const GridChromeModel& model) {
  chrome(screen, model.title, model.rightLabel);

  // The hint strip, at a fixed place so the grid below it never moves. The
  // free-space advisory wins the strip when present; otherwise, if nothing is
  // set yet, it says so -- a grid with no thick border and no words reads as a
  // selection that failed to draw.
  const fui::Rect safe = screen.frame().safeRect();
  const int16_t hintY = static_cast<int16_t>(safe.y + kBodyTop);
  const char* line = nullptr;
  if (model.warning != nullptr && model.warning[0] != '\0') {
    line = model.warning;
  } else if (!model.hasActive) {
    // Short enough to fit the hint strip at the grid's cut. The longer form
    // ("Tap a wallpaper to set it as your sleep screen.") was cut mid-phrase.
    line = "Tap one to set your sleep screen.";
  }
  if (line != nullptr) {
    const fui::Rect rect = fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), hintY,
                                         static_cast<int16_t>(safe.width - toybox::kMargin * 2), kHintH);
    fui::TextStyle style = onPaper(screen.theme().smallText, fui::TextAlign::Left);
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
// ADD FROM A PHONE. Three arrangements; the winner survives and this macro goes
// in the same commit.
//
//   1  the pairing twin      -- Instapaper's screen, which Mario named as the
//                               precedent, with the address where its code sits
//   2  the three steps       -- the site's numbered rail, on the panel
//   3  the code, mostly      -- the largest QR the body will take, two lines
//
// All three carry the same two facts, because both are load-bearing: the
// address in words (a QR tells a person nothing, and the one failure this
// screen has is a phone on the wrong network) and that Back stops it.
#ifndef WALLADD_VARIANT
#define WALLADD_VARIANT 1
#endif

namespace {

// The address, at the largest cut that holds it. NEVER handed straight to
// text(): it is one unbreakable token, and an overflowing token in these cuts
// does not arrive clipped or ellipsised -- the faces above toybox_10 carry no
// U+2026 glyph, so it simply stops at a plausible place. That is how a pairing
// screen once printed "read.crossplay.ma-r-s.com/pai" and nobody could see why
// the address did not work.
void drawAddress(toybox::Screen& screen, const fui::Rect& box, const char* url) {
  fui::TextStyle style = onPaper(screen.theme().titleText, fui::TextAlign::Center);
  const std::string fitted = toybox::fittedTitle(screen.target(), url, box.width, style);
  screen.target().text(box, fitted.c_str(), style);
}

// The same-WiFi requirement is this screen's ENTIRE error handling, so it lives
// in the prose rather than the footer: nothing on the device can detect that the
// phone went out over cellular instead, and the browser's own message ("cannot
// reach this site") names no cause. It was in the footer for one render and came
// out as "PHONE MUST BE ON THE SAM..." -- the failure explanation, truncated.
constexpr const char* kProse = "Pick a photo and it lands here. Your phone has to be on this same WiFi.";
constexpr const char* kFoot = "BACK STOPS";

void drawFoot(toybox::Screen& screen, const fui::Rect& body) {
  fui::TextStyle style = onPaper(screen.theme().smallText, fui::TextAlign::Center, 1);
  const fui::Rect box = fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - 26), body.width, 24);
  screen.target().text(box, toybox::fittedTitle(screen.target(), kFoot, box.width, style).c_str(), style);
}

// Every headline on this screen goes through the ladder. "SCAN WITH YOUR PHONE"
// at the title cut is four pixels too wide for the body and rendered as
// "SCAN WITH YOUR..." -- and it only showed the ellipsis at all because the
// reading faces happen to carry U+2026. At the menu cuts it would simply have
// stopped after "YOUR".
void drawHeadline(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  fui::TextStyle style = onPaper(screen.theme().titleText, fui::TextAlign::Center, 1);
  screen.target().text(box, toybox::fittedTitle(screen.target(), text, box.width, style).c_str(), style);
}

#if WALLADD_VARIANT == 2
// A numbered step: a filled square holding the numeral, the words beside it.
// The site's three-step rail says the same thing with circles and a connector;
// neither survives at this size in one bit, so the number carries the order.
void drawStep(toybox::Screen& screen, const fui::Rect& row, const int n, const char* text) {
  constexpr int16_t kDot = 34;
  const fui::Rect dot = fui::makeRect(row.x, static_cast<int16_t>(row.y + (row.height - kDot) / 2), kDot, kDot);
  screen.target().fill(dot, fui::Paint::solid(fui::Color::Black));
  char numeral[12];
  std::snprintf(numeral, sizeof(numeral), "%d", n);
  // The body cut, not the small one: at the UI cut the numeral was a speck near
  // the top of the fill rather than a number in a box. Centred by the line box,
  // which is the vertical clamp these faces actually obey.
  fui::TextStyle num = screen.theme().bodyText;
  num.align = fui::TextAlign::Center;
  num.color = fui::Color::White;
  num.maxLines = 1;
  const int16_t lineH = screen.target().lineHeight(num.font);
  screen.target().text(fui::makeRect(dot.x, static_cast<int16_t>(dot.y + (kDot - lineH) / 2), dot.width, lineH),
                       numeral, num);

  const int16_t textX = static_cast<int16_t>(dot.right() + 12);
  fui::TextStyle style = onPaper(screen.theme().bodyText, fui::TextAlign::Left);
  style.maxLines = 1;
  const fui::Rect box = fui::makeRect(textX, row.y, static_cast<int16_t>(row.right() - textX), row.height);
  const std::string fitted = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, fitted.c_str(), style);
}
#endif  // WALLADD_VARIANT == 2

}  // namespace

fui::Rect buildAdd(toybox::Screen& screen, const AddModel& model) {
  chrome(screen, "ADD A WALLPAPER", model.added > 0 ? "1 ADDED" : nullptr);
  const fui::Rect body = screen.body();

#if WALLADD_VARIANT == 1
  // The pairing twin. Same skeleton as InstapaperScreens::buildPairQr, with the
  // address occupying the line its 8-character code does.
  constexpr int16_t kQrSide = 232;
  constexpr int16_t kHead = 48;
  constexpr int16_t kAddr = 46;
  constexpr int16_t kProseH = 108;
  // The stack centred in the body rather than hung from its top: at 232 the
  // four pieces leave 180px over, and all of it used to pool above the footer.
  const int16_t stack =
      static_cast<int16_t>(kHead + toybox::kMargin * 2 + kQrSide + toybox::kMargin + kAddr + toybox::kGutter + kProseH);
  int16_t y = static_cast<int16_t>(body.y + (body.height - 26 - stack) / 2);
  if (y < body.y) y = body.y;

  drawHeadline(screen, fui::makeRect(body.x, y, body.width, kHead), "SCAN WITH YOUR PHONE");
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                                     static_cast<int16_t>(y + kHead + toybox::kMargin * 2), kQrSide, kQrSide);

  drawAddress(screen, fui::makeRect(body.x, static_cast<int16_t>(qr.bottom() + toybox::kMargin), body.width, kAddr),
              model.url);

  // FULL body width, not inset: the address is the longest unbreakable token on
  // this screen, and an inset that costs it two characters costs it silently.
  screen.target().text(
      fui::makeRect(body.x, static_cast<int16_t>(qr.bottom() + toybox::kMargin + kAddr + toybox::kGutter), body.width,
                    kProseH),
      model.status != nullptr ? model.status : kProse, onPaper(screen.theme().bodyText, fui::TextAlign::Center, 3));
  drawFoot(screen, body);
  return qr;

#elif WALLADD_VARIANT == 2
  // The three steps. The panel's version of the site's numbered rail: what will
  // happen, before it happens, so the QR is not the only thing on screen that
  // has to be understood.
  constexpr int16_t kQrSide = 200;
  const fui::Rect qr =
      fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2), body.y, kQrSide, kQrSide);

  drawAddress(screen, fui::makeRect(body.x, static_cast<int16_t>(qr.bottom() + toybox::kGutter), body.width, 40),
              model.url);

  int16_t y = static_cast<int16_t>(qr.bottom() + toybox::kGutter + 40 + toybox::kGutter);
  constexpr int16_t kRow = 46;
  const fui::Rect rowBox =
      fui::makeRect(static_cast<int16_t>(body.x + 24), y, static_cast<int16_t>(body.width - 48), kRow);
  drawStep(screen, rowBox, 1, "Scan it with your camera");
  y = static_cast<int16_t>(y + kRow);
  drawStep(screen, fui::makeRect(rowBox.x, y, rowBox.width, kRow), 2, "Pick a photo");
  y = static_cast<int16_t>(y + kRow);
  drawStep(screen, fui::makeRect(rowBox.x, y, rowBox.width, kRow), 3, "It appears here");
  y = static_cast<int16_t>(y + kRow + toybox::kGutter * 2);
  screen.target().text(fui::makeRect(body.x, y, body.width, 76),
                       model.status != nullptr ? model.status : "Your phone has to be on this same WiFi.",
                       onPaper(screen.theme().bodyText, fui::TextAlign::Center, 2));
  drawFoot(screen, body);
  return qr;

#else
  // The code, mostly. The largest square the body will take under a single
  // line, on the theory that the only thing a phone has to do is see it.
  constexpr int16_t kQrSide = 300;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                                     static_cast<int16_t>(body.y + toybox::kGutter), kQrSide, kQrSide);

  drawAddress(screen, fui::makeRect(body.x, static_cast<int16_t>(qr.bottom() + toybox::kGutter * 2), body.width, 44),
              model.url);

  screen.target().text(
      fui::makeRect(body.x, static_cast<int16_t>(qr.bottom() + toybox::kGutter * 2 + 44 + toybox::kGutter), body.width,
                    72),
      model.status != nullptr ? model.status : kProse, onPaper(screen.theme().bodyText, fui::TextAlign::Center, 3));
  drawFoot(screen, body);
  return qr;
#endif
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
  if (model.total > 0 && model.done > 0) {
    const int16_t inner = static_cast<int16_t>(body.width - 8);
    const int16_t filled = static_cast<int16_t>(static_cast<int>(inner) * model.done / model.total);
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
