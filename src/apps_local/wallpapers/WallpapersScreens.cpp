#include "WallpapersScreens.h"

#include <algorithm>
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
  // A: the hero. One big name, one paragraph, one button. Nothing else on the
  // screen competes for the tap.
  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 12), width, 56), headline, head);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 78), width, 300),
            "Woodcuts, engravings and patterns, cut for this screen: Durer's Four Horsemen, "
            "a celestial chart, Bauhaus, Penrose tiling, herringbone and more.",
            fui::TextAlign::Left);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 392), width, 96), weight, fui::TextAlign::Left);

#elif WALLPAPERS_OFFER_VARIANT == 2
  // B: show the shape of what arrives. Two empty frames at the picker's OWN
  // cell size, captioned with real names, so the screen previews the grid
  // rather than describing it. Sized from gridGeom instead of from the
  // wallpaper's aspect: a cell derived from 480x800 is taller than the body and
  // lands on the button, which is how the first draft of this looked.
  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Center);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 2), width, 56), headline, head);

  const GridGeom preview = gridGeom(screen.device());
  const int16_t cellW = preview.cellW;
  const int16_t cellH = preview.cellH;
  const int16_t pairW = static_cast<int16_t>(cellW * 2 + preview.gapX);
  const int16_t startX = static_cast<int16_t>(left + (width - pairW) / 2);
  const int16_t gridY = static_cast<int16_t>(body.y + 66);
  // The SHORT forms. This screen is drawn in the reading faces, where
  // FONT_SLOT_SMALL is a wider serif than the grid's toybox_10, so a name that
  // fits a real cell does not fit this preview of one.
  const char* names[2] = {"Horsemen", "Celestial"};
  for (int i = 0; i < 2; ++i) {
    const int16_t cx = static_cast<int16_t>(startX + i * (cellW + preview.gapX));
    screen.target().stroke(fui::makeRect(cx, gridY, cellW, cellH), fui::Paint::solid(fui::Color::Black), 1);
    fui::TextStyle cap = onPaper(screen.theme().smallText, fui::TextAlign::Center);
    cap.font = fui::FONT_SLOT_SMALL;
    cap.maxLines = 1;
    screen.target().text(
        fui::makeRect(cx, static_cast<int16_t>(gridY + cellH + preview.markerRoom), cellW, preview.captionH), names[i],
        cap);
  }
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(buttonY - 96), width, 88), weight, fui::TextAlign::Center);

#else
  // C: the list. The names ARE the pitch, so give them the room and keep the
  // headline small. Row pitch comes from the real line height rather than a
  // guessed 34, which overlapped at the reading cut.
  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(body.y + 10), width, 50), headline, head);
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(body.y + 66), width, 44),
            "In one download:", fui::TextAlign::Left);

  const char* rows[6] = {"Durer, The Four Horsemen",   "Durer, Adam and Eve",         "A celestial chart",
                         "Bauhaus and Penrose tiling", "Herringbone and houndstooth", "and eleven more"};
  fui::TextStyle rowStyle = onPaper(screen.theme().bodyText, fui::TextAlign::Left);
  rowStyle.maxLines = 1;
  const int16_t pitch = static_cast<int16_t>(screen.target().lineHeight(rowStyle.font) + 4);
  for (int i = 0; i < 6; ++i) {
    screen.target().text(fui::makeRect(static_cast<int16_t>(left + 14), static_cast<int16_t>(body.y + 118 + i * pitch),
                                       static_cast<int16_t>(width - 14), pitch),
                         rows[i], rowStyle);
  }
  drawProse(screen, fui::makeRect(left, static_cast<int16_t>(buttonY - 96), width, 88), weight, fui::TextAlign::Left);

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
