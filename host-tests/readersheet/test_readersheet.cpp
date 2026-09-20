// The reader's toolbar panel (src/activities/reader/ReaderToolbarUi.cpp) sizes
// its bottom sheet for a whole number of rows and syncs its ListNav with
// tokens.listRowGap -- the RAW theme value. It never sets ListProps::rowGap,
// which defaults to the sentinel -1, and Screen::list() resolves that sentinel
// through resolveListProps(), which on a TOUCH device raises the gap to
// theme.listTouchRowGap. So the nav paginates on one stride and the renderer
// draws on another.
//
// The SDK's own Screen::syncToList() gets this right -- it resolves first, then
// syncs with props.rowGap. The reader calls nav_.syncToProps() directly and
// skips the resolve. This asks the real SDK, not a copy of its arithmetic.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/activities/reader/ReaderPanelRows.h"
#include "FreeInkApp.h"
#include "FreeInkUICore.h"

namespace fui = freeink::ui;

namespace {
int failures = 0;
int checks = 0;
void check(bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

class RowTarget final : public fui::DrawTarget {
 public:
  std::vector<fui::Rect> texts;
  fui::Size measureText(fui::FontId, const char* t, fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(8 * (t ? std::string(t).size() : 0)), 20};
  }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = fui::CornersAll) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = fui::CornersAll) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect r, const char*, fui::TextStyle) override { texts.push_back(r); }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = fui::Paint::solid(fui::Color::Black),
              fui::Rotation = fui::Rotation::None) override {}
};

fui::DeviceContext deviceCtx(bool touch) {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = touch;
  ctx.hasButtons = true;
  return ctx;
}
}  // namespace

int main() {
  const fui::ThemeTokens tokens = fui::themeTokensForLineHeight(20);
  std::printf("theme: rowHeight=%d listRowGap=%d listTouchRowGap=%d\n", (int)tokens.rowHeight, (int)tokens.listRowGap,
              (int)tokens.listTouchRowGap);

  // Representative panel chrome and budget. The numbers only have to be
  // plausible: what is under test is whether the sheet and the renderer agree
  // about the row stride, which is independent of how tall the chrome is.
  const int16_t rowH = 44;
  const int chrome = 220;
  const int safeH = 790;  // 800 less the X4 Pro's measured top bezel inset
  const int items = 6;

  for (bool touch : {false, true}) {
    const char* who = touch ? "touch " : "button";
    // Ask the REAL SDK what gap it resolves for a panel that leaves rowGap at
    // its sentinel, by measuring what it actually draws.
    RowTarget target;
    fui::InteractionBuffer<24> interactions;
    const fui::InputSnapshot noInput{};
    const fui::DeviceContext ctx = deviceCtx(touch);
    fui::Frame frame(target, ctx, noInput, interactions);
    fui::Screen screen(frame, tokens);
    static const char* kLabels[] = {"Contents", "Text", "More", "Bookmarks", "Search", "Sync"};
    std::vector<fui::ListItem> list;
    for (int i = 0; i < items; ++i) {
      fui::ListItem it{};
      it.label = kLabels[i];
      list.push_back(it);
    }
    fui::ListProps props;
    props.items = list.data();
    props.count = static_cast<uint16_t>(list.size());
    props.itemsWindowCount = static_cast<uint16_t>(list.size());
    props.rowHeight = rowH;
    props.labelText = tokens.bodyText;
    // rowGap deliberately left at its -1 sentinel, exactly as the panel leaves it.
    screen.list(props, static_cast<int16_t>(items * rowH));
    check(target.texts.size() >= 2, std::string(who) + ": the list drew rows to measure");
    const int drawnGap = target.texts.size() >= 2 ? (target.texts[1].y - target.texts[0].y) - rowH : 0;
    std::printf("%s: SDK draws rows with gap %d (raw theme token is %d)\n", who, drawnGap, (int)tokens.listRowGap);

    // THE CONTRACT: a sheet sized with the gap the renderer will use must hold
    // its own rows. This calls the shipped helper, not a copy of it.
    const readerpanel::Geometry good = readerpanel::panelGeometry(safeH, rowH, drawnGap, chrome, items, 62, 72);
    const int need = readerpanel::rowsDrawnHeight(good.rows, rowH, drawnGap);
    const int band = good.sheetHeight - chrome;
    std::printf("%s: sized with the drawn gap -> %d rows, band %dpx, rows need %dpx\n", who, good.rows, band, need);
    check(need <= band, std::string(who) + ": a panel sized with the DRAWN gap must hold its rows");

    // THE REGRESSION GUARD: sizing with the raw theme token -- what this code
    // did until card #546 -- must be detectably wrong wherever the two gaps
    // differ, and harmless where they do not. Without this, reverting the fix
    // would leave the suite green.
    const readerpanel::Geometry raw = readerpanel::panelGeometry(safeH, rowH, tokens.listRowGap, chrome, items, 62, 72);
    const int rawBand = raw.sheetHeight - chrome;
    const int rawNeed = readerpanel::rowsDrawnHeight(raw.rows, rowH, drawnGap);
    if (drawnGap == tokens.listRowGap) {
      check(rawNeed <= rawBand, std::string(who) + ": no touch gap here, so the raw token is the drawn gap and fits");
    } else {
      std::printf("%s: sized with the RAW token -> band %dpx but rows need %dpx (short by %d)\n", who, rawBand, rawNeed,
                  rawNeed - rawBand);
      check(rawNeed > rawBand, std::string(who) +
                                   ": the raw-token sizing must still be provably short, or this "
                                   "suite would not catch the bug coming back");
    }
  }

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
