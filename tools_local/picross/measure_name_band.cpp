// How wide is the band the Picross win screen gives the puzzle's name?
//
//     tools_local/picross/measure_name_band.sh
//
// The naming tool (site/picross-names/) tells Mario, while he is typing,
// whether a name will fit on the panel. That answer is a width in pixels, and
// a width copied by hand into a browser is a width that stops being true the
// next time somebody moves the win screen's layout. So it is measured HERE,
// from the real screen builder, and written to name_band.txt for
// gen_name_tool.py to read.
//
// It measures rather than derives. The band is `screen.takeBottom(52, kGutter)`
// against a content rect three layers of chrome deep, and reasoning it out from
// the page margins is exactly the mistake ToyboxScreen.h's headerTitleWidth()
// comment describes: a guess at a box is not a smaller version of the box, it
// is a different number.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/picross/PicrossScreens.h"

namespace fui = freeink::ui;

namespace {

// Records geometry and nothing else. The metrics are a stand-in: the answer
// wanted here is the RECT, which the layout decides without asking the font.
class GeometryTarget final : public fui::DrawTarget {
 public:
  struct TextRun {
    fui::Rect rect;
    std::string text;
  };
  std::vector<TextRun> texts;

  fui::Size measureText(const fui::FontId, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(text == nullptr ? 0 : std::char_traits<char>::length(text) * 10), 20};
  }
  int16_t lineHeight(const fui::FontId) const override { return 20; }
  void fill(const fui::Rect, const fui::Paint, const uint8_t = 0, const uint8_t = 0xFF) override {}
  void stroke(const fui::Rect, const fui::Paint, const uint8_t, const uint8_t = 0, const uint8_t = 0xFF) override {}
  void line(const fui::Point, const fui::Point, const uint8_t, const fui::Paint) override {}
  void triangle(const fui::Point, const fui::Point, const fui::Point, const fui::Paint) override {}
  void text(const fui::Rect rect, const char* text, const fui::TextStyle) override {
    if (text != nullptr) texts.push_back(TextRun{rect, text});
  }
  void bitmap(const fui::Rect, const fui::BitmapRef, const fui::BitmapMode, const fui::Paint = {},
              const fui::Rotation = fui::Rotation::None) override {}
};

}  // namespace

int main() {
  const picross::Puzzle* first = nullptr;
  for (int i = 0; i < picross::kPuzzleCount; ++i) {
    if (picross::kPuzzles[i].size == 10) {
      first = &picross::kPuzzles[i];
      break;
    }
  }
  if (first == nullptr) {
    std::fprintf(stderr, "no 10x10 puzzle in the bank\n");
    return 1;
  }

  fui::DeviceContext ctx;
  ctx.width = 480;  // the X4 Pro's logical frame, portrait
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;

  GeometryTarget target;
  toybox::Interactions interactions;
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, ctx, noInput, interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  picrossui::WinModel model;
  model.cleared = first;
  model.mistakes = 0;
  model.solvedCount = 1;
  model.total = picross::kPuzzleCount;
  model.moreToPlay = true;
  picrossui::buildWin(screen, model);

  // The name is the only run drawn with the puzzle's own name in it. Found by
  // its text rather than by position, so a reordered screen still answers.
  for (const auto& run : target.texts) {
    if (run.text == first->name) {
      std::printf("%d\n", static_cast<int>(run.rect.width));
      return 0;
    }
  }
  std::fprintf(stderr, "the win screen did not draw the puzzle's name -- has it been renamed or removed?\n");
  return 1;
}
