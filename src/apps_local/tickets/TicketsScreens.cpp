#include "TicketsScreens.h"

#include <string>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"

namespace ticketsui {

namespace fui = freeink::ui;

// Footer band: fixed height at the bottom of the screen, above the panel margin.
fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), toybox::kPillHeight);
}

// Footer button: draws a button in the footer area.
void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  if (outlined) button.styles = toybox::rowStyles();
  screen.button(button, box);
}

// Draws one line that must not overflow its box, set at the largest cut that holds it.
// Toybox's rule is that nothing is elided, and the cuts above toybox_10 carry no
// ellipsis glyph at all -- an overflow there draws as a sentence that stops at a
// plausible place, and the screenshot looks fine.
void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const fui::FontId font) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

// Header band, rule, page margin. The title is fitted by headerBand() itself.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel, const freeink::Icon* trailing = nullptr,
            void (*trailingAction)() = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.font = toybox::kTileFont;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  if (trailing != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(*trailing);
    header.trailingAction = trailingAction ? ActionDeleteTicket : fui::NO_ACTION;
    header.trailingStyles = toybox::rowStyles();
  }
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The page between the chrome and the panel's bottom margin. Derived from the
// device alone, so listCapacity() can answer the same rect without a Screen.
fui::Rect bodyRect(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - toybox::kMargin - toybox::kBodyTop));
}

// Two centred lines in the middle of the body: what went wrong, and what to do
// about it. Both fitted, never elided.
void emptyState(toybox::Screen& screen, const char* headline, const char* message) {
  const fui::Rect body = screen.body();
  fui::TextStyle headlineStyle = screen.theme().titleText;
  headlineStyle.color = fui::Color::Black;
  headlineStyle.align = fui::TextAlign::Center;
  fui::TextStyle messageStyle = screen.theme().bodyText;
  messageStyle.color = fui::Color::Black;
  messageStyle.align = fui::TextAlign::Center;
  messageStyle.maxLines = 3;

  const std::string headlineDrawn = toybox::fittedTitle(screen.target(), headline, body.width, headlineStyle);
  const std::string messageDrawn = toybox::fitLines(screen.target(), message, body.width, 3, messageStyle);

  const int16_t headlineH = screen.target().lineHeight(headlineStyle.font);
  const int16_t messageH = static_cast<int16_t>(screen.target().lineHeight(messageStyle.font) * 3);
  const int16_t gap = toybox::kGutter;
  int16_t y = static_cast<int16_t>(body.y + (body.height - headlineH - gap - messageH) / 2);
  screen.target().text(fui::makeRect(body.x, y, body.width, headlineH), headlineDrawn.c_str(), headlineStyle);
  y = static_cast<int16_t>(y + headlineH + gap);
  screen.target().text(fui::makeRect(body.x, y, body.width, messageH), messageDrawn.c_str(), messageStyle);
}

void buildList(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, "TICKETS", model.pageLabel);

  if (model.count <= 0) {
    emptyState(screen, model.emptyHeadline != nullptr ? model.emptyHeadline : "NO TICKETS",
               model.emptyMessage != nullptr ? model.emptyMessage : "");
    // Empty state already drawn; add button below it in the footer
    const fui::Rect footer = footerBand(screen.device());
    footerButton(screen, footer, "ADD TICKET", ActionOpenUpload, false);
    return;
  }

  // List of tickets (no ADD TICKET row in the list)
  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.firstVisible);
  list.action = ActionOpenTicket;
  list.labelText = screen.theme().bodyText;
  list.subtitleText = screen.theme().smallText;
  list.subtitleText.color = fui::Color::Black;
  screen.list(list);

  // Footer bar with ADD TICKET button (like Notes + LIST/+ PAGE)
  const fui::Rect footer = footerBand(screen.device());
  footerButton(screen, footer, "ADD TICKET", ActionOpenUpload, false);
}

int listCapacity(const fui::DeviceContext& device) {
  const fui::ThemeTokens& theme = toybox::themeTokens();
  return fui::listVisibleRows(bodyRect(device), theme.listMinRowHeight, theme.listRowGap);
}

fui::Rect buildTicket(toybox::Screen& screen, const TicketModel& model) {
  chrome(screen, model.name, nullptr, &icon_go_trash_32, model.onDelete);
  const fui::Rect body = screen.body();

  // The subtitle gets a strip at the bottom of the body; the QR takes the
  // largest square above it. A ticket is read by a scanner, not a person, so
  // the code gets the panel and the words get what is left.
  int16_t subtitleH = 0;
  fui::TextStyle subtitleStyle = screen.theme().bodyText;
  subtitleStyle.font = toybox::kTileFont;
  subtitleStyle.color = fui::Color::Black;
  subtitleStyle.align = fui::TextAlign::Center;
  if (model.subtitle != nullptr && model.subtitle[0] != '\0') {
    subtitleH = static_cast<int16_t>(screen.target().lineHeight(subtitleStyle.font) + toybox::kGutter);
  }

  const int16_t roomW = body.width;
  const int16_t roomH = static_cast<int16_t>(body.height - subtitleH);
  const int16_t side = roomW < roomH ? roomW : roomH;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - side) / 2),
                                     static_cast<int16_t>(body.y + (roomH - side) / 2), side, side);

  if (subtitleH > 0) {
    const std::string drawn = toybox::fittedTitle(screen.target(), model.subtitle, body.width, subtitleStyle);
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + body.height - subtitleH + toybox::kGutter),
                                       body.width, subtitleH),
                         drawn.c_str(), subtitleStyle);
  }
  return qr;
}

fui::Rect buildUpload(toybox::Screen& screen, const UploadModel& model) {
  chrome(screen, model.title, nullptr);
  const fui::Rect body = screen.body();

  const int16_t lineH = screen.target().lineHeight(toybox::kTileFont);
  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + toybox::kGutter), body.width, lineH);
  fittedLine(screen, caption, "POINT YOUR PHONE CAMERA HERE", fui::TextAlign::Center, toybox::kTileFont);

  const int16_t room = static_cast<int16_t>(body.y + body.height - toybox::kMargin - toybox::kPillHeight -
                                            toybox::kGutter * 2 - caption.y - caption.height - lineH * 5);
  int16_t side = room < body.width ? room : body.width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr =
      fui::makeRect(static_cast<int16_t>((body.x + body.width - side) / 2),
                    static_cast<int16_t>(caption.y + caption.height + toybox::kGutter * 2), side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), body.width, lineH);
  fittedLine(screen, url, model.readable, fui::TextAlign::Center, toybox::kTileFont);

  // Show WiFi status
  if (!model.wifiConnected) {
    const fui::Rect wifiStatus =
        fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), body.width, lineH);
    fittedLine(screen, wifiStatus, "WIFI DISCONNECTED -- ENABLE WIFI TO UPLOAD", fui::TextAlign::Center,
               toybox::kTileFont);
  } else if (model.status_ != nullptr && model.status_[0] != '\0') {
    const fui::Rect status =
        fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), body.width, lineH);
    fittedLine(screen, status, model.status_, fui::TextAlign::Center, toybox::kTileFont);
  }

  // Footer bar with DONE button (with dark background like Notes)
  const fui::Rect footer = fui::makeRect(
      toybox::kMargin, static_cast<int16_t>(screen.device().height - toybox::kMargin - toybox::kPillHeight),
      static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin), toybox::kPillHeight);
  // Footer background (dark band like Notes)
  screen.target().fill(footer, fui::Paint::solid(fui::Color::Black));
  footerButton(screen, footer, "DONE", ActionDismiss, false);
  return qr;
}

}  // namespace ticketsui
