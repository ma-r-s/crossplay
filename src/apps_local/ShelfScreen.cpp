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

// Shorter than a row on purpose. It carries no text and no face, only marks, and
// a full row of chrome under the player bar would put 148px of furniture along
// the bottom of a screen whose whole job is the list above it.
constexpr int kPageBarHeight = 44;

int pageBarHeight(const bool hasPages) { return hasPages ? kPageBarHeight + toybox::kGutter : 0; }

fui::Rect listBand(const fui::DeviceContext& device, const bool hasDeviceName, const bool hasPages) {
  const int top = toybox::kHeaderHeight + toybox::kGutter * 3;
  return fui::makeRect(toybox::kMargin, top, device.width - 2 * toybox::kMargin,
                       device.height - toybox::kMargin - top - footerHeight(hasDeviceName) - pageBarHeight(hasPages));
}

Paging pagingFor(const fui::DeviceContext& device, const fui::ThemeTokens& tokens, const bool hasDeviceName,
                 const int count) {
  // Asked without the bar first. A folder that fits keeps every row it has, and
  // never draws a control that would say "1/1".
  const int whole = fui::listVisibleRows(listBand(device, hasDeviceName, false), tokens.rowHeight, tokens.listRowGap);
  if (count <= whole) return Paging{whole > 0 ? whole : 1, 1};

  // It does not fit, so the bar exists, so the rows it costs come off. Asking in
  // this order is what stops the two answers depending on each other.
  const int paged = fui::listVisibleRows(listBand(device, hasDeviceName, true), tokens.rowHeight, tokens.listRowGap);
  if (paged <= 0) return Paging{1, count};
  return Paging{paged, (count + paged - 1) / paged};
}

int pageFor(const int selected, const int rowsPerPage) {
  if (rowsPerPage <= 0 || selected <= 0) return 0;
  return selected / rowsPerPage;
}

int pageCountFor(const int itemCount, const int rowsPerPage) {
  if (rowsPerPage <= 0 || itemCount <= 0) return 1;
  return (itemCount + rowsPerPage - 1) / rowsPerPage;
}

int pageStep(const int page, const int pageCount, const int delta) {
  if (pageCount <= 1) return 0;
  // Modulo of a negative left operand is negative in C++, so the step is
  // normalised into 0..pageCount-1 before it is added.
  const int step = ((delta % pageCount) + pageCount) % pageCount;
  const int from = page < 0 ? 0 : page % pageCount;
  return (from + step) % pageCount;
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  fui::HeaderProps header;
  header.title = model.title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  const fui::Rect panel = screen.device().screen();

  // Paper, because the header is a filled black band. Placed from the right
  // edge, where nothing else in the header goes.
  if (model.mark != nullptr) {
    const fui::Rect markRect =
        fui::makeRect(static_cast<int16_t>(panel.width - toybox::kIconSize - toybox::kMargin),
                      toybox::bandCenterY(screen, toybox::kIconSize), toybox::kIconSize, toybox::kIconSize);
    screen.target().bitmap(markRect, fui::bitmapFromIcon(*model.mark), fui::BitmapMode::Contain,
                           fui::Paint::solid(fui::Color::White));
  }
  toybox::headerRule(screen);
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

  // Taken after the player bar, so it sits above it, and only when there is more
  // than one page: a lone pip saying "you are on the only page" is furniture.
  //
  // **This is a position indicator that happens to be tappable, not a row of
  // buttons**, and the difference is the whole design. The first version tiled
  // the full width with page-sized targets, and it read as a control -- which
  // was wrong twice over. The device already has the control: the side buttons
  // step the cursor and the page follows, so drawing a second one is
  // reinventing hardware we already have. And what was actually missing was
  // never a control at all, it was the *signal* that more games exist.
  //
  // It stays tappable rather than becoming pure decoration because of a fact
  // about this hardware: `BaseTheme::drawButtonHints` returns immediately when
  // `gpio.hasTouch()`, and the X4 Pro has a GT911. So upstream deliberately
  // teaches nothing about the physical buttons on a touch device, which means a
  // touch user has no way to discover that Up and Down would page. Touch has to
  // stay complete. The iOS home screen resolves the same tension the same way:
  // dots that read as position and happen to accept a tap.
  //
  // So the marks are a small centred cluster with air around them rather than a
  // bar of slabs, and the targets are a thumb wide and contiguous *within the
  // cluster only* -- tapping the far edge of the screen does nothing, because
  // out there the user is not aiming at anything.
  if (model.pageCount > 1) {
    const fui::Rect bar = screen.takeBottom(kPageBarHeight, toybox::kGutter);

    // A thumb, and contiguous, so no gap between adjacent pages can swallow a
    // tap. Falls back to sharing the bar when there are so many pages that a
    // thumb-wide pitch would not fit; small honest targets beat overlapping
    // ones, which is why the component is not allowed to grow them either.
    constexpr int16_t kPitch = 44;
    constexpr int16_t kPip = 10;
    const int16_t pitch =
        kPitch * model.pageCount <= bar.width ? kPitch : static_cast<int16_t>(bar.width / model.pageCount);
    const int16_t clusterX = static_cast<int16_t>(bar.x + (bar.width - pitch * model.pageCount) / 2);

    // A hit region and nothing else. A StyleSet with no paints would be taken
    // for an unset one and quietly replaced by the default button look, which is
    // the filled slab the player bar wants and this does not; explicitlySet is
    // how the component is told the blankness is deliberate.
    fui::StyleSet invisible;
    invisible.explicitlySet = true;

    // A hairline capsule around the cluster, drawn on exactly the strip the
    // targets occupy so the outline cannot promise a hit where there is none.
    //
    // The marks say where you are; without this nothing said they can be
    // touched. Two cold testers found the taps by accident and used them as
    // their only reliable route; a third never tried them and concluded the
    // list could not be paged at all. A frame is the smallest thing in this
    // design language that reads as a control, and it costs no layout: the bar
    // was already this tall.
    const fui::Rect cluster = fui::makeRect(clusterX, bar.y, static_cast<int16_t>(pitch * model.pageCount), bar.height);
    screen.target().stroke(cluster, fui::Paint::solid(fui::Color::Black), 1, static_cast<uint8_t>(cluster.height / 2));

    for (int p = 0; p < model.pageCount; ++p) {
      const fui::Rect target =
          fui::makeRect(static_cast<int16_t>(clusterX + p * pitch), bar.y, pitch, static_cast<int16_t>(bar.height));
      fui::ButtonProps jump;
      jump.action = ActionGoToPage;
      jump.value = static_cast<int16_t>(p);
      jump.styles = invisible;
      jump.minTouchSize = 0;
      screen.button(jump, target);

      // Filled for where you are, outlined for where you are not. Ink rather
      // than a grey, because there is no grey on this panel: text() draws any
      // non-white colour solid black and a dither this small is mud.
      const fui::Rect pip = fui::makeRect(static_cast<int16_t>(target.x + (pitch - kPip) / 2),
                                          static_cast<int16_t>(target.y + (target.height - kPip) / 2), kPip, kPip);
      if (p == model.page) {
        screen.target().fill(pip, fui::Paint::solid(fui::Color::Black));
      } else {
        screen.target().stroke(pip, fui::Paint::solid(fui::Color::Black), 2);
      }
    }
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  // Always zero: this list is exactly one page, so it never overflows its band
  // and has nothing to scroll. See MenuModel::items.
  list.topIndex = 0;
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpen;

  const fui::Rect rows = listBand(screen.device(), model.playerName != nullptr, model.pageCount > 1);
  screen.list(list);

  // Icons sit at the right edge; see toybox::iconAtRowRight for why not the
  // list's own left-hand slot.
  if (model.icons != nullptr) {
    for (int i = 0; i < model.count; ++i) {
      if (model.icons[i] == nullptr) continue;
      toybox::iconAtRowRight(screen, rows, i, 0, *model.icons[i], i == model.selected);
    }
  }
}

}  // namespace shelfui
