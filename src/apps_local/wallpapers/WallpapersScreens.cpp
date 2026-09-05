#include "WallpapersScreens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"

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

fui::TextStyle onPaper(fui::TextStyle style, fui::TextAlign align) {
  style.align = align;
  style.color = fui::Color::Black;
  return style;
}

// Which arrangement of the BEFORE screen to build. Three are rendered side by
// side for Mario to choose from; the losers and this macro go in the same
// commit as the decision (docs/building-apps.md).
#ifndef WALLPAPERS_OFFER_VARIANT
#define WALLPAPERS_OFFER_VARIANT 1
#endif

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

[[maybe_unused]] void paintChecker(fui::DrawTarget& t, const fui::Rect& r, const int cell) {
  for (int gy = 0; gy * cell < r.height; ++gy) {
    for (int gx = 0; gx * cell < r.width; ++gx) {
      if (((gx + gy) & 1) == 0) continue;
      inkRect(t, r, r.x + gx * cell, r.y + gy * cell, cell, cell);
    }
  }
}

// The lattice from gen_geoB.py: translation vectors (2u, 2u) and (3u, u), with
// only the HORIZONTAL brick inked. The vertical brick is background -- inking
// both, as the first draft did, tiles to solid black at any u.
[[maybe_unused]] void paintHerringbone(fui::DrawTarget& t, const fui::Rect& r, const int u) {
  const int L = u * 2;
  const int span = (r.width + r.height) / u + 4;
  for (int i = -span; i <= span; ++i) {
    for (int j = -span; j <= span; ++j) {
      const int bx = r.x + i * (2 * u) + j * (3 * u);
      const int by = r.y + i * (2 * u) + j * u;
      if (bx > r.x + r.width || by > r.y + r.height || bx + L < r.x || by + u < r.y) continue;
      inkRect(t, r, bx, by, L, u);
    }
  }
}

// Quarter arcs, two orientations per cell -- the shipped truchet, sampled as
// short segments because there is no arc primitive here.
[[maybe_unused]] void paintTruchet(fui::DrawTarget& t, const fui::Rect& r, const int cell) {
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

// Dogtooth: a solid square with a stepped tail, which is what makes it read as
// houndstooth rather than as a check.
[[maybe_unused]] void paintHoundstooth(fui::DrawTarget& t, const fui::Rect& r, const int cell) {
  const int step = cell / 4 < 1 ? 1 : cell / 4;
  for (int gy = 0; gy * cell < r.height + cell; ++gy) {
    for (int gx = 0; gx * cell < r.width + cell; ++gx) {
      if (((gx + gy) & 1) != 0) continue;
      const int x0 = r.x + gx * cell;
      const int y0 = r.y + gy * cell;
      inkRect(t, r, x0, y0, cell, cell);
      for (int k = 0; k < 4; ++k) {
        inkRect(t, r, x0 + cell + k * step, y0 + k * step, step, step);
        inkRect(t, r, x0 - (k + 1) * step, y0 + cell - (k + 1) * step, step, step);
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

  // The one sentence that has to carry the size, so nobody taps a download of
  // unknown weight on a metered connection.
  char shortWeight[80];
  std::snprintf(shortWeight, sizeof(shortWeight), "%d wallpapers, about %s", remaining, size);

  char weight[96];
  if (model.alreadyHave > 0) {
    std::snprintf(weight, sizeof(weight), "The %d you are missing. About %s over WiFi.", remaining, size);
  } else {
    std::snprintf(weight, sizeof(weight), "About %s over WiFi, onto the card.", size);
  }

  const fui::Rect body = screen.body();
  const int16_t left = static_cast<int16_t>(body.x);
  const int16_t width = body.width;
  // Room under the button for TWO lines of the secondary sentence: at the
  // reading cut it does not fit on one, and a sentence cut with an ellipsis is
  // the defect this screen exists to avoid.
  const int16_t buttonY = static_cast<int16_t>(body.y + body.height - kButtonH - 96);

#if WALLPAPERS_OFFER_VARIANT == 1
  // A: POSTER. The artwork is the screen and the words sit on a card laid over
  // it. A wallpaper set on a panel whose whole appeal is how pictures look on
  // it should lead with a picture, and this one is real: the same herringbone
  // the user is being offered, drawn rather than described.
  paintHerringbone(screen.target(), body, 26);
  // The button and its footnote get a white mat: a black button on a black
  // motif is not a button, it is a hole.
  screen.target().fill(fui::makeRect(left, static_cast<int16_t>(buttonY - 14), width,
                                     static_cast<int16_t>(body.y + body.height - buttonY + 14)),
                       fui::Paint::solid(fui::Color::White));

  const fui::Rect card = fui::makeRect(static_cast<int16_t>(left + 6), static_cast<int16_t>(body.y + 92),
                                       static_cast<int16_t>(width - 12), 236);
  screen.target().fill(card, fui::Paint::solid(fui::Color::White));
  screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), 3);

  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Center);
  screen.target().text(fui::makeRect(static_cast<int16_t>(card.x + 10), static_cast<int16_t>(card.y + 22),
                                     static_cast<int16_t>(card.width - 20), 58),
                       headline, head);
  drawProse(screen,
            fui::makeRect(static_cast<int16_t>(card.x + 16), static_cast<int16_t>(card.y + 96),
                          static_cast<int16_t>(card.width - 32), 128),
            weight, fui::TextAlign::Center);

#elif WALLPAPERS_OFFER_VARIANT == 2
  // B: THE ARTWORK LEADS. Four real motifs at tile scale in the picker's own
  // two-column shape, captioned with their real names, and the count demoted to
  // a caption underneath. No headline: the pictures say what this is.
  //
  // This is what the rejected empty-frame version was reaching for. An outlined
  // box reads as an image that failed to load; a drawn motif reads as the
  // wallpaper it actually is.
  const int16_t tw = 100;
  const int16_t thh = 167;
  const int16_t gx = 24;
  const int16_t startX = static_cast<int16_t>(left + (width - (tw * 2 + gx)) / 2);
  // No per-tile caption: "Herringbone" needs about 157px in this cut and a tile
  // is 100, so it cut to "Herrin...". The names ride in one line underneath
  // instead, which fits and reads better than four cropped words.
  for (int i = 0; i < 4; ++i) {
    const int16_t cx = static_cast<int16_t>(startX + (i % 2) * (tw + gx));
    const int16_t cy = static_cast<int16_t>(body.y + 8 + (i / 2) * (thh + 16));
    const fui::Rect tile = fui::makeRect(cx, cy, tw, thh);
    if (i == 0) paintHerringbone(screen.target(), tile, 13);
    if (i == 1) paintChecker(screen.target(), tile, 17);
    if (i == 2) paintHoundstooth(screen.target(), tile, 24);
    if (i == 3) paintTruchet(screen.target(), tile, 33);
    screen.target().stroke(tile, fui::Paint::solid(fui::Color::Black), 1);
  }
  // Names, count and size in one sentence, because the pictures have already
  // said what this is and the facts still have to be on screen.
  char blurb[160];
  std::snprintf(blurb, sizeof(blurb), "Herringbone, checker, houndstooth, truchet and %d more. About %s.",
                remaining - 4, size);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 8 + 2 * thh + 16 + 14), width, 130), blurb,
            fui::TextAlign::Center);

#else
  // C: COVER. A full-bleed band of one motif across the top, type below it, the
  // way a book cover carries its title. The band is truchet, whose curves read
  // at a glance where a fine check would just look like grey.
  const fui::Rect band = fui::makeRect(left, body.y, width, 286);
  paintTruchet(screen.target(), band, 72);
  screen.target().stroke(band, fui::Paint::solid(fui::Color::Black), 2);

  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 306), width, 56), headline, head);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 372), width, 130), weight, fui::TextAlign::Left);
#endif

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
