#include "ShelfScreen.h"

#include <FreeInkUIIcon.h>

#include "player/PlayerAvatar.h"
#include "ui/ToyboxIcons.h"

namespace shelfui {

// The footer holds the device's name and is the way into changing it, so the
// list has to stop above it. Shared with the builder, or the scroll maths would
// think it has a row's more room than it does and style a selection on a row
// that is never drawn.
//
// A row's height rather than a pill's: it carries a 48px face now, and a pill
// would leave two pixels of air above and below it.
int footerHeight(const bool hasDeviceName) { return hasDeviceName ? toybox::kRowHeight + toybox::kGutter : 0; }

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

  const fui::Rect panel = screen.device().screen();

  // Paper, because the header is a filled black band. Placed from the right
  // edge, where nothing else in the header goes.
  if (model.mark != nullptr) {
    const fui::Rect markRect = fui::makeRect(static_cast<int16_t>(panel.width - toybox::kIconSize - toybox::kMargin),
                                             static_cast<int16_t>((toybox::kHeaderHeight - toybox::kIconSize) / 2),
                                             toybox::kIconSize, toybox::kIconSize);
    screen.target().bitmap(markRect, fui::bitmapFromIcon(*model.mark), fui::BitmapMode::Contain,
                           fui::Paint::solid(fui::Color::White));
  }
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, panel.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Taken before the list, so the list can never grow into it.
  //
  // Who this device is, and the way to change it. The bar is a single control:
  // face, name and chevron are one hit region, because three targets in a strip
  // this size would each be smaller than a thumb and two of them would do the
  // same thing.
  //
  // There is no keyboard anywhere in this fork and this is deliberately not the
  // reason to build one: a name here is a label to be recognised, not a message
  // to be written, and rolling one word at a time is faster and more fun than
  // typing on a panel that repaints in half a second.
  if (model.playerName != nullptr) {
    const fui::Rect bar = screen.takeBottom(toybox::kRowHeight, toybox::kGutter);

    // The slab and the hit region, with NO label. The name is drawn separately
    // below, into a band that excludes the face and the chevron.
    //
    // Handing the button its label was the obvious thing and it was wrong: the
    // component centres the text across the whole rect, so at the widest name
    // the lists can roll -- twenty characters -- it ran straight through the
    // face on one side and the chevron on the other. Three things cannot share
    // one centre line.
    fui::ButtonProps you;
    you.action = ActionOpenPlayer;
    screen.button(you, bar);

    // Both marks are drawn ON the bar, which the button has just filled solid
    // black, so both are paper. Drawn in ink they would be invisible and
    // nothing would warn -- the multiplayer mark went black-on-black once
    // already.
    const fui::Paint paper = fui::Paint::solid(fui::Color::White);
    const int16_t face = player::avatarPixels(player::AvatarSize::Row);
    const int16_t inset = static_cast<int16_t>((toybox::kRowHeight - face) / 2);
    const fui::Rect faceRect =
        fui::makeRect(static_cast<int16_t>(bar.x + inset), static_cast<int16_t>(bar.y + inset), face, face);
    const fui::Rect chevron = fui::makeRect(static_cast<int16_t>(bar.right() - toybox::kIconSize - toybox::kGutter),
                                            static_cast<int16_t>(bar.y + (toybox::kRowHeight - toybox::kIconSize) / 2),
                                            toybox::kIconSize, toybox::kIconSize);
    player::drawAvatar(screen.target(), faceRect, model.playerName, player::AvatarSize::Row, fui::Color::White);
    screen.target().bitmap(chevron, fui::bitmapFromIcon(icon_opens_32), fui::BitmapMode::Contain, paper);

    // What is left between them, and the name has to live inside it whatever it
    // says. The dense cut, because twenty characters do not fit this band at UI
    // size and the alternative is the component quietly truncating -- with an
    // ellipsis glyph Jersey does not even have. Smaller is also the right
    // hierarchy here: on a door, the face says who and the name only confirms
    // it. The PLAYER screen is where the name is the subject.
    fui::TextStyle label;
    label.font = toybox::kSmallFont;
    label.align = fui::TextAlign::Center;
    label.color = fui::Color::White;
    const int16_t left = static_cast<int16_t>(faceRect.right() + toybox::kGutter);
    screen.target().text(
        fui::makeRect(left, bar.y, static_cast<int16_t>(chevron.x - toybox::kGutter - left), toybox::kRowHeight),
        model.playerName, label);
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpen;

  const fui::Rect rows = listBand(screen.device(), model.playerName != nullptr);
  screen.list(list);

  // Icons sit at the right edge; see toybox::iconAtRowRight for why not the
  // list's own left-hand slot.
  if (model.icons != nullptr) {
    for (int i = 0; i < model.count; ++i) {
      if (model.icons[i] == nullptr) continue;
      toybox::iconAtRowRight(screen, rows, i, *model.icons[i], i == model.selected);
    }
  }
}

}  // namespace shelfui
