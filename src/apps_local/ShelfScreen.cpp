#include "ShelfScreen.h"

namespace shelfui {

// The footer holds the device's name and is the only way to change it, so the
// list has to stop above it. Shared with the builder, or the scroll maths would
// think it has a row's more room than it does and style a selection on a row
// that is never drawn.
int footerHeight(const bool hasDeviceName) { return hasDeviceName ? toybox::kPillHeight + toybox::kGutter : 0; }

fui::Rect listBand(const fui::DeviceContext& device, const bool hasDeviceName) {
  const int top = toybox::kHeaderHeight + toybox::kGutter * 3;
  return fui::makeRect(toybox::kMargin, top, device.width - 2 * toybox::kMargin,
                       device.height - toybox::kMargin - top - footerHeight(hasDeviceName));
}

int topIndexFor(const fui::Rect& body, const fui::ThemeTokens& tokens, const int selected, const int topIndex,
                const int count) {
  const uint16_t visible = fui::listVisibleRows(body, tokens.rowHeight, tokens.listRowGap);
  return fui::listTopIndexFor(static_cast<int16_t>(selected), static_cast<uint16_t>(topIndex), visible,
                              static_cast<uint16_t>(count));
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  fui::HeaderProps header;
  header.title = model.title;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);

  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Taken before the list, so the list can never grow into it.
  //
  // Tapping it rolls a new name. There is no keyboard anywhere in this fork and
  // this is deliberately not the reason to build one: a name here is a label to
  // be recognised, not a message to be written, and rolling one is faster and
  // more fun than typing on a panel that repaints in half a second.
  if (model.playerName != nullptr) {
    fui::ButtonProps you;
    you.label = model.playerName;
    you.action = ActionRerollName;
    screen.button(you, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
  }

  fui::ListProps list;
  // 32 into a 62px row: big enough to read a silhouette, small enough that the
  // label is still the thing you see first.
  list.iconSize = 32;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpen;
  screen.list(list);
}

}  // namespace shelfui
