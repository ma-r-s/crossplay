#include "WallpapersScreens.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../ui/ToyboxText.h"
#include "../ui/ToyboxTokens.h"

namespace wallpapersui {

namespace {

// The top of any body: below the header band and the rule Toybox draws under
// it, matching xkcd so the picker lines up with the shelf it came from.
constexpr int16_t kBodyTop = static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 3);

fui::TextStyle owned(fui::TextStyle style, fui::TextAlign align) {
  style.align = align;
  return style;
}

fui::TextStyle onPaper(fui::TextStyle style, fui::TextAlign align) {
  style.align = align;
  style.color = fui::Color::Black;
  return style;
}

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
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

// A short line under the chrome, from the given top. Returns the y just below
// it so the caller stacks the next thing under it rather than through it.
// `emphasis` fills the line background (used for the live-wallpaper strip; a
// plain advisory leaves the paper alone).
int16_t noticeLine(toybox::Screen& screen, const fui::Rect& safe, int16_t top, const char* text, bool emphasis) {
  if (text == nullptr || text[0] == '\0') return top;
  const int16_t height = 40;
  const fui::Rect line = fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), top,
                                       static_cast<int16_t>(safe.width - toybox::kMargin * 2), height);
  if (emphasis) {
    screen.target().fill(line, fui::Paint::solid(fui::Color::Black));
  }
  fui::TextStyle style = owned(screen.theme().smallText, fui::TextAlign::Left);
  style.color = emphasis ? fui::Color::White : fui::Color::Black;
  const fui::Rect textRect =
      fui::makeRect(static_cast<int16_t>(line.x + (emphasis ? toybox::kGutter : 0)), line.y,
                    static_cast<int16_t>(line.width - (emphasis ? toybox::kGutter * 2 : 0)), height);
  std::string fitted = toybox::fittedTitle(screen.target(), text, textRect.width, style);
  screen.target().text(textRect, fitted.c_str(), style);
  return static_cast<int16_t>(top + height + toybox::kGutter);
}

// The band the list rows are laid into, from `top` down to the safe bottom.
fui::Rect listBand(const fui::Rect& safe, int16_t top) {
  return fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), top,
                       static_cast<int16_t>(safe.width - toybox::kMargin * 2),
                       static_cast<int16_t>(safe.bottom() - top));
}

// The list shared by all three variants. Names are fitted here, with an ASCII
// ellipsis, so a long file name is never handed to the component long enough
// for it to truncate with U+2026 -- a glyph the Toybox faces lack, which draws
// as nothing and silently eats the end of the name.
void drawList(toybox::Screen& screen, const PickerModel& model, const fui::Rect& band) {
  fui::TextStyle labelStyle = owned(screen.theme().bodyText, fui::TextAlign::Left);
  const int16_t labelWidth = static_cast<int16_t>(band.width - toybox::kMargin * 2 - 84);

  std::vector<std::string> labels;
  std::vector<fui::ListItem> items;
  labels.reserve(static_cast<size_t>(model.count));
  items.reserve(static_cast<size_t>(model.count));
  for (int i = 0; i < model.count; ++i) {
    fui::TextStyle probe = labelStyle;
    labels.push_back(toybox::fittedTitle(screen.target(), model.items[i].name, labelWidth, probe));
  }
  for (int i = 0; i < model.count; ++i) {
    fui::ListItem item;
    item.label = labels[static_cast<size_t>(i)].c_str();
    item.value = model.items[i].active ? "ON" : nullptr;
    // The absolute index rides on the row, so a tap reports WHICH wallpaper it
    // was rather than a bare 0. Without this the list defaults every row to 0.
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(model.count);
  props.selectedIndex = static_cast<int16_t>(model.selected);
  props.action = ActionPick;
  props.labelText = labelStyle;
  props.valueText = owned(screen.theme().smallText, fui::TextAlign::Right);

  // Scroll so the highlighted wallpaper is visible: the list does not do this
  // for us (topIndex is the caller's job), and the overflow track is not
  // tappable (shelf.md), so a library taller than the panel would otherwise
  // strand every row past the first screenful. Centre the selection in the
  // window; clamp to a full last page.
  const int16_t rowHeight = screen.theme().rowHeight > 0 ? screen.theme().rowHeight : 44;
  const int16_t rowGap = screen.theme().listRowGap;
  const int visible = std::max(1, band.height / std::max<int>(1, rowHeight + rowGap));
  if (model.count > visible) {
    int top = model.selected - visible / 2;
    top = std::max(0, std::min(top, model.count - visible));
    props.topIndex = static_cast<uint16_t>(top);
  }

  const fui::Rect panel = screen.device().screen();
  screen.setContentMarginAbsolute(fui::Insets{band.y, static_cast<int16_t>(panel.width - band.right()),
                                              static_cast<int16_t>(panel.height - band.bottom()), band.x});
  screen.list(props, band.height);
}

}  // namespace

void buildPicker(toybox::Screen& screen, const PickerModel& model) {
  chrome(screen, model.title, model.rightLabel);
  const fui::Rect safe = screen.frame().safeRect();
  int16_t top = static_cast<int16_t>(safe.y + kBodyTop);

  // The free-space advisory always comes first when present.
  top = noticeLine(screen, safe, top, model.warning, /*emphasis=*/false);

  // The live wallpaper, named on an inked strip, so which one is on is
  // unmissable rather than an easily-skimmed badge -- and it names it even when
  // it has scrolled off the list.
  const char* live = nullptr;
  char banner[160];  // "SLEEP SCREEN:  " + a name up to kNameMax
  for (int i = 0; i < model.count; ++i) {
    if (model.items[i].active) {
      snprintf(banner, sizeof(banner), "SLEEP SCREEN:  %s", model.items[i].name);
      live = banner;
      break;
    }
  }
  top = noticeLine(screen, safe, top, live != nullptr ? live : "SLEEP SCREEN:  none chosen yet", /*emphasis=*/true);

  drawList(screen, model, listBand(safe, top));
}

void buildEmpty(toybox::Screen& screen, const EmptyModel& model) {
  chrome(screen, model.title);

  // Content flows down the content rect chrome() just inset, the same as
  // buildNotice: a warning line (only when there is one), the headline, then
  // the instructions filling what is left.
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

}  // namespace wallpapersui
