#include "WalletScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"
#include "WalletIcons.h"

namespace walletui {
namespace {

constexpr int16_t kFooterHeight = toybox::kPillHeight;

// The list rows, in Workouts' card proportions so the apps read as one family:
// a black badge, the title beside it, the caption under the title.
constexpr int16_t kRowHeight = 84;
constexpr int16_t kRowGap = 12;
constexpr int16_t kRowPitch = kRowHeight + kRowGap;
constexpr int16_t kBadgeSide = 64;

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = maxLines;
  return style;
}

// Header band, rule, page margin -- Workouts' chrome. headerBand() fits the
// title to the band, so a long card title steps down a cut rather than running
// under the counter. `trailing` puts one icon button on the band's right, with
// the counter just left of it.
void chrome(toybox::Screen& screen, const char* title, const char* counter = nullptr,
            const freeink::Icon* trailing = nullptr, const fui::ActionId trailingAction = fui::NO_ACTION) {
  fui::HeaderProps header;
  header.title = title;
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.borderEdges = fui::EdgesNone;
  int16_t trailingW = 0;
  if (trailing != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(*trailing);
    header.trailingAction = trailingAction;
    header.trailingStyles = toybox::rowStyles();
    // The component's button is the band's height less 8, plus an 8px gap
    // (headerTitleWidth()), which comes to the band's height.
    trailingW = screen.theme().headerHeight;
  }
  fui::TextStyle counterStyle;
  counterStyle.font = toybox::kUiFont;
  counterStyle.align = fui::TextAlign::Right;
  counterStyle.color = fui::Color::White;  // paper, on the black band
  if (counter != nullptr) {
    header.rightReserve = static_cast<int16_t>(
        screen.target().measureText(counterStyle.font, counter, counterStyle).width + toybox::kGutter);
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  if (counter != nullptr) {
    const int16_t right = static_cast<int16_t>(screen.device().width - toybox::kMargin - trailingW);
    const fui::Rect box =
        fui::makeRect(0, toybox::bandCenterY(screen, toybox::kUiCut.inkHeight), right, toybox::kUiCut.inkHeight);
    screen.target().text(toybox::inkCentred(box, toybox::kUiCut), counter, counterStyle);
  }
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The whole row is the target, invisible.
void rowHit(toybox::Screen& screen, const fui::Rect& row, const fui::ActionId action, const int index) {
  fui::ButtonProps hit;
  hit.label = "";
  hit.action = action;
  hit.value = static_cast<int16_t>(index);
  hit.styles = toybox::rowStyles();
  hit.styles.normal.background = fui::Paint::none();
  hit.styles.normal.border = fui::Paint::none();
  screen.button(hit, row);
}

// One line set at the largest cut that holds it, never above the one asked for.
void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const fui::FontId font) {
  fui::TextStyle style = plain(font, align);
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

// Up to `lines` lines of prose, centred on the band's middle.
void centredProse(toybox::Screen& screen, const fui::Rect& band, const char* text, const int lines,
                  const fui::FontId font = toybox::kBodyFont) {
  fui::TextStyle style = plain(font, fui::TextAlign::Center, static_cast<uint8_t>(lines));
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const std::string drawn = toybox::fitLines(screen.target(), text, band.width, lines, style);
  int used = 1;
  for (const char c : drawn) used += c == '\n' ? 1 : 0;
  const fui::Rect box = fui::makeRect(band.x, static_cast<int16_t>(band.y + (band.height - lineHeight * used) / 2),
                                      band.width, static_cast<int16_t>(lineHeight * used));
  screen.target().text(box, drawn.c_str(), style);
}

void icon(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& mark, const fui::Color colour) {
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(box.x + (box.width - mark.w) / 2),
                                        static_cast<int16_t>(box.y + (box.height - mark.h) / 2),
                                        static_cast<int16_t>(mark.w), static_cast<int16_t>(mark.h));
  screen.target().bitmap(where, fui::bitmapFromIcon(mark), fui::BitmapMode::Contain, fui::Paint::solid(colour));
}

// The bar every screen's actions live on, at Notes' y and height.
fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), kFooterHeight);
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  if (outlined) button.styles = toybox::rowStyles();
  screen.button(button, box);
}

// The room between the band and the footer.
fui::Rect bodyAboveFooter(const fui::DeviceContext& device) {
  const fui::Rect footer = footerBand(device);
  return fui::makeRect(footer.x, static_cast<int16_t>(toybox::kBodyTop), footer.width,
                       static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop));
}

// --- List geometry -------------------------------------------------------

fui::Rect listBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - toybox::kMargin - toybox::kBodyTop));
}

int rowsVisible(const fui::Rect& band) {
  const int visible = (band.height + kRowGap) / kRowPitch;
  return visible < 1 ? 1 : visible;
}

void listRow(toybox::Screen& screen, const fui::Rect& box, const ListRow& item, const int index) {
  const fui::Rect badge =
      fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - kBadgeSide) / 2), kBadgeSide, kBadgeSide);
  screen.target().fill(badge, kInk, 0);
  icon(screen, badge, item.barcode ? icon_wallet_bars_32 : icon_wallet_qr_32, fui::Color::White);

  const int16_t textX = static_cast<int16_t>(box.x + kBadgeSide + toybox::kGutter);
  const int16_t textW = static_cast<int16_t>(box.x + box.width - textX);
  const int16_t titleLine = screen.target().lineHeight(toybox::kBodyFont);
  const int16_t smallLine = screen.target().lineHeight(toybox::kTileFont);
  const bool captioned = item.caption != nullptr && item.caption[0] != '\0';
  const int16_t block = static_cast<int16_t>(titleLine + (captioned ? toybox::kGutter / 2 + smallLine : 0));
  const int16_t top = static_cast<int16_t>(box.y + (box.height - block) / 2);
  fittedLine(screen, fui::makeRect(textX, top, textW, titleLine), item.title, fui::TextAlign::Left, toybox::kBodyFont);
  if (captioned) {
    fittedLine(screen,
               fui::makeRect(textX, static_cast<int16_t>(top + titleLine + toybox::kGutter / 2), textW, smallLine),
               item.caption, fui::TextAlign::Left, toybox::kTileFont);
  }
  rowHit(screen, box, ActionOpenCard, index);
}

// --- Card geometry -------------------------------------------------------

// The code's square: as wide as the panel allows and no taller than the room
// left once the caption has its lines.
fui::Rect codeSquare(const fui::Rect& room, const int16_t below) {
  int16_t side = static_cast<int16_t>(room.height - below);
  if (side > room.width) side = room.width;
  if (side < 0) side = 0;
  return fui::makeRect(static_cast<int16_t>(room.x + (room.width - side) / 2), room.y, side, side);
}

// The open card's footer: PREV, a square moon, NEXT. The delete confirm uses
// the same split so that what a stray second tap lands on is always KEEP IT.
struct CardFooter {
  fui::Rect prev;
  fui::Rect moon;
  fui::Rect next;
};

CardFooter cardFooter(const fui::Rect& footer) {
  const int16_t side = footer.height;
  const int16_t half = static_cast<int16_t>((footer.width - side - toybox::kGutter * 2) / 2);
  const int16_t moonX = static_cast<int16_t>(footer.x + half + toybox::kGutter);
  const int16_t nextX = static_cast<int16_t>(moonX + side + toybox::kGutter);
  return CardFooter{
      fui::makeRect(footer.x, footer.y, half, footer.height), fui::makeRect(moonX, footer.y, side, side),
      fui::makeRect(nextX, footer.y, static_cast<int16_t>(footer.x + footer.width - nextX), footer.height)};
}

// The moon puts this card on the sleep screen: outlined while it is not
// there, filled while it is.
void footerControls(toybox::Screen& screen, const fui::Rect& footer, const CardModel& model) {
  const CardFooter parts = cardFooter(footer);
  if (model.hasPrev) footerButton(screen, parts.prev, "PREV", ActionPrev, true);
  fui::ButtonProps moon;
  moon.icon = fui::bitmapFromIcon(icon_wallet_sleep_32);
  moon.action = ActionSleep;
  if (!model.shownAsleep) moon.styles = toybox::rowStyles();
  screen.button(moon, parts.moon);
  if (model.hasNext) footerButton(screen, parts.next, "NEXT", ActionNext, true);
}

}  // namespace

// --- The list ------------------------------------------------------------

int listCapacity(const fui::DeviceContext& device) { return rowsVisible(listBand(device)); }

void buildList(toybox::Screen& screen, const ListModel& model) {
  // The phone is where cards are made, and the pencil on the band is the way
  // there, for the empty app and the full one alike.
  chrome(screen, "CARDS", model.pageLabel, &icon_wallet_edit_32, ActionUsePhone);
  const fui::Rect band = listBand(screen.device());
  if (model.count == 0) {
    centredProse(screen, band, "No cards yet. Tap the pencil to add one from your phone.", 4);
    return;
  }
  const int visible = rowsVisible(band);
  int16_t y = band.y;
  for (int i = model.firstVisible; i < model.count && i - model.firstVisible < visible; i++) {
    listRow(screen, fui::makeRect(band.x, y, band.width, kRowHeight), model.rows[i], i);
    y = static_cast<int16_t>(y + kRowPitch);
  }
}

// --- One card ------------------------------------------------------------

namespace {

constexpr int kMinModulePx = 2;

// A barcode's rectangle in `room`, with the number printed under it. The text
// takes the display face, large enough for a cashier to type it in when the
// scanner gives up.
fui::Rect barcodeIn(toybox::Screen& screen, const fui::Rect& room, const CardModel& model) {
  const int16_t textLine = screen.target().lineHeight(toybox::kDisplayFont);
  const int16_t gap = toybox::kGutter;
  const int modules = model.barModules > 0 ? model.barModules : 1;
  const int16_t barsRoom = static_cast<int16_t>(room.height - gap - textLine);
  // Across the page, the way every till expects to see one; down it only when
  // a long code would otherwise get modules too thin to scan.
  const bool rotate = room.width / modules < kMinModulePx;
  fui::Rect bars;
  if (barsRoom / modules < 1) {
    // Longer than the page at one pixel a module: the caller says so in the
    // square a QR code would have had.
    return codeSquare(room, static_cast<int16_t>(gap + textLine));
  }
  if (!rotate) {
    const int16_t px = static_cast<int16_t>(room.width / modules);
    const int16_t width = static_cast<int16_t>(px * modules);
    int16_t height = static_cast<int16_t>(room.width / 2);
    if (height > barsRoom) height = barsRoom;
    // Centred in the room, bars and number together: a barcode is much
    // shorter than the square a QR code takes, and hung from the band it
    // leaves the page's middle empty.
    const int16_t top = static_cast<int16_t>(room.y + (room.height - height - gap - textLine) / 2);
    bars = fui::makeRect(static_cast<int16_t>(room.x + (room.width - width) / 2), top, width, height);
  } else {
    const int16_t px = static_cast<int16_t>(barsRoom / modules);
    const int16_t length = static_cast<int16_t>(px * modules);
    int16_t across = static_cast<int16_t>(room.width * 3 / 5);
    if (across >= length) across = static_cast<int16_t>(length - 1);
    bars = fui::makeRect(static_cast<int16_t>(room.x + (room.width - across) / 2), room.y, across, length);
  }
  if (model.barText != nullptr && model.barText[0] != '\0') {
    const fui::Rect line =
        fui::makeRect(room.x, static_cast<int16_t>(bars.y + bars.height + gap), room.width, textLine);
    fittedLine(screen, line, model.barText, fui::TextAlign::Center, toybox::kDisplayFont);
  }
  return bars;
}

}  // namespace

fui::Rect buildCard(toybox::Screen& screen, const CardModel& model) {
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const bool captioned = model.caption != nullptr && model.caption[0] != '\0';
  const bool barcode = model.barModules > 0;
  const int16_t bodyLine = screen.target().lineHeight(toybox::kBodyFont);

  // The title is the band, as a pass's name is its header; the code fills the
  // width under it and the caption sits beneath.
  if (model.asleep) {
    chrome(screen, model.title);
  } else {
    chrome(screen, model.title, model.position, &icon_wallet_trash_32, ActionDelete);
  }
  fui::Rect room = bodyAboveFooter(device);
  const int16_t below = captioned ? static_cast<int16_t>(toybox::kGutter * 2 + bodyLine * 2) : 0;
  if (model.asleep) room.height = static_cast<int16_t>(device.height - toybox::kMargin - room.y);
  fui::Rect code;
  if (barcode) {
    code =
        barcodeIn(screen, fui::makeRect(room.x, room.y, room.width, static_cast<int16_t>(room.height - below)), model);
  } else {
    code = codeSquare(room, below);
    if (model.asleep) {
      // No footer asleep, so the code and its caption sit in the middle of the
      // whole page instead of hanging from the band.
      code.y = static_cast<int16_t>(room.y + (room.height - code.height - below) / 2);
    }
  }
  if (captioned) {
    fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Center, 2);
    // Under the code and centred in the room it leaves, so it reads with the
    // code rather than with the buttons.
    int16_t top = static_cast<int16_t>(code.y + code.height + toybox::kGutter);
    if (barcode) top = static_cast<int16_t>(top + toybox::kGutter + screen.target().lineHeight(toybox::kDisplayFont));
    const int16_t bottom = model.asleep && !barcode ? static_cast<int16_t>(top + below) : room.y + room.height;
    const fui::Rect box = fui::makeRect(room.x, top, room.width, static_cast<int16_t>(bottom - top));
    const std::string drawn = toybox::fitLines(screen.target(), model.caption, box.width, 2, style);
    screen.target().text(box, drawn.c_str(), style);
  }
  if (!model.asleep) footerControls(screen, footer, model);
  return code;
}

void buildCardFailure(toybox::Screen& screen, const fui::Rect& square, const char* prose) {
  screen.target().stroke(square, kInk, toybox::kHairline, 0);
  const fui::Rect inner = fui::makeRect(static_cast<int16_t>(square.x + toybox::kGutter), square.y,
                                        static_cast<int16_t>(square.width - toybox::kGutter * 2), square.height);
  centredProse(screen, inner, prose, 4);
}

// --- The delete confirm --------------------------------------------------

void buildDeleteConfirm(toybox::Screen& screen, const char* title, const char* prose) {
  chrome(screen, title);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  centredProse(screen, bodyAboveFooter(device), prose, 5);
  const CardFooter parts = cardFooter(footer);
  footerButton(screen, parts.prev, "DELETE IT", ActionDeleteConfirm, true);
  const int16_t keepW = static_cast<int16_t>(parts.next.x + parts.next.width - parts.moon.x);
  footerButton(screen, fui::makeRect(parts.moon.x, footer.y, keepW, footer.height), "KEEP IT", ActionDeleteKeep, false);
}

// --- The phone -----------------------------------------------------------

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, "CARDS");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;

  const int16_t lineHeight = screen.target().lineHeight(toybox::kTileFont);
  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop + toybox::kGutter), width, lineHeight);
  fittedLine(screen, caption, "POINT YOUR PHONE CAMERA HERE", fui::TextAlign::Center, toybox::kTileFont);

  // Notes' arrangement: the code about half the panel, the address under it,
  // and whether anything has arrived.
  const int16_t top = static_cast<int16_t>(caption.y + caption.height + toybox::kGutter * 2);
  const int16_t room = static_cast<int16_t>(footer.y - toybox::kGutter * 2 - top - lineHeight * 5);
  int16_t side = room < width ? room : width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), top, side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, url, model.readable, fui::TextAlign::Center, toybox::kTileFont);
  char state[40];
  if (model.added <= 0) {
    std::snprintf(state, sizeof(state), "WAITING FOR YOUR PHONE");
  } else {
    std::snprintf(state, sizeof(state), "%d %s ADDED", model.added, model.added == 1 ? "CARD" : "CARDS");
  }
  const fui::Rect line =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, line, state, fui::TextAlign::Center, toybox::kTileFont);

  footerButton(screen, footer, "DONE", ActionDismiss, false);
  return qr;
}

void buildNotice(toybox::Screen& screen, const char* prose) {
  chrome(screen, "CARDS");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const fui::Rect band = bodyAboveFooter(device);
  fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Left, 6);
  const std::string drawn = toybox::fitLines(screen.target(), prose, band.width, 6, style);
  screen.target().text(band, drawn.c_str(), style);
  footerButton(screen, footer, "BACK", ActionDismiss, false);
}

}  // namespace walletui
