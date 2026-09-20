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
  std::printf("theme: rowHeight=%d listRowGap=%d listTouchRowGap=%d listTouchMinRowHeight=%d\n",
              (int)tokens.rowHeight, (int)tokens.listRowGap, (int)tokens.listTouchRowGap,
              (int)tokens.listTouchMinRowHeight);

  // THE CLAIM UNDER TEST: on a touch device the gap the panel reserves
  // (tokens.listRowGap, what it hands nav_.syncToProps) equals the gap the list
  // actually draws with. Red today: 0 reserved, listTouchRowGap drawn.
  check(tokens.listTouchRowGap == tokens.listRowGap ||
            tokens.listTouchRowGap <= tokens.listRowGap,
        "touch row gap exceeds the raw listRowGap the reader panel reserves "
        "(reserved=" + std::to_string(tokens.listRowGap) +
            " drawn=" + std::to_string(tokens.listTouchRowGap) + ")");

  // And the consequence, in rows: a sheet sized for N rows at the reserved
  // stride cannot hold N rows at the drawn stride.
  const int16_t rowH = 44;
  const int rows = 6;
  const int reservedStride = rowH + tokens.listRowGap;
  const int drawnStride = rowH + (tokens.listTouchRowGap > tokens.listRowGap ? tokens.listTouchRowGap
                                                                             : tokens.listRowGap);
  const int reservedHeight = rows * reservedStride - tokens.listRowGap;
  const int neededHeight = rows * drawnStride - tokens.listTouchRowGap;
  std::printf("6-row panel: sheet reserves %dpx, list needs %dpx (short by %dpx)\n",
              reservedHeight, neededHeight, neededHeight - reservedHeight);
  check(neededHeight <= reservedHeight,
        "a 6-row reader panel needs more height than its sheet reserved");

  // The discriminator: button-only boards keep gap 0 and the geometry is exact.
  // That is why upstream, whose PR was titled "on button-only devices", cannot
  // see this. If this ever fails, the test is measuring the wrong thing.
  for (bool touch : {false, true}) {
    RowTarget target;
    fui::InteractionBuffer<24> interactions;
    const fui::InputSnapshot noInput{};
    const fui::DeviceContext ctx = deviceCtx(touch);
    fui::Frame frame(target, ctx, noInput, interactions);
    fui::Screen screen(frame, tokens);
    fui::ListProps props;
    static const char* kLabels[] = {"Contents", "Text", "More", "Bookmarks", "Search", "Sync"};
    std::vector<fui::ListItem> items;
    for (int i = 0; i < rows; ++i) {
      fui::ListItem it{};
      it.label = kLabels[i];
      items.push_back(it);
    }
    props.items = items.data();
    props.count = static_cast<uint16_t>(items.size());
    props.itemsWindowCount = static_cast<uint16_t>(items.size());
    props.rowHeight = rowH;   // explicit, as the reader sets it
    props.labelText = tokens.bodyText;
    // props.rowGap deliberately left at its -1 sentinel, as the reader leaves it.
    screen.list(props, static_cast<int16_t>(reservedHeight));
    int stride = 0;
    if (target.texts.size() >= 2) stride = target.texts[1].y - target.texts[0].y;
    std::printf("%s: drawn row stride = %d (reader reserved %d)\n",
                touch ? "touch " : "button", stride, reservedStride);
    if (!touch)
      check(stride == reservedStride, "button-only board: drawn stride must match the reserved one");
    else
      check(stride == reservedStride,
            "touch board: drawn stride must match the stride the panel reserved");
  }

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
