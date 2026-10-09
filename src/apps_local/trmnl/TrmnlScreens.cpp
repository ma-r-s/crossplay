#include "TrmnlScreens.h"

#include <string>

#include "../ui/ToyboxText.h"
#include "TrmnlIcons.h"

namespace trmnlui {
namespace {

constexpr int16_t kFooterHeight = toybox::kPillHeight;

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = maxLines;
  return style;
}

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);

// Header band, rule, page margin. `phone` puts the phone on the band's right,
// the way Workouts carries its pencil.
void chrome(toybox::Screen& screen, const bool phone = false) {
  fui::HeaderProps header;
  header.title = "TRMNL";
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.borderEdges = fui::EdgesNone;
  if (phone) {
    header.trailingIcon = fui::bitmapFromIcon(icon_t_phone_32);
    header.trailingAction = ActionUsePhone;
    header.trailingStyles = toybox::rowStyles();
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

void rowHit(toybox::Screen& screen, const fui::Rect& row, const fui::ActionId action) {
  fui::ButtonProps hit;
  hit.label = "";
  hit.action = action;
  hit.styles = toybox::rowStyles();
  hit.styles.normal.background = fui::Paint::none();
  hit.styles.normal.border = fui::Paint::none();
  screen.button(hit, row);
}

void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const fui::FontId font) {
  fui::TextStyle style = plain(font, align);
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

// Prose wrapped into a box, as many lines as it holds. Returns the height used.
int16_t prose(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
              const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style = plain(font, align);
  const int16_t lineHeight = screen.target().lineHeight(font);
  int lines = lineHeight > 0 ? box.height / lineHeight : 1;
  if (lines < 1) lines = 1;
  if (lines > 8) lines = 8;
  style.maxLines = static_cast<uint8_t>(lines);
  const std::string drawn = toybox::fitLines(screen.target(), text, box.width, style.maxLines, style);
  int used = 1;
  for (const char c : drawn) used += c == '\n' ? 1 : 0;
  screen.target().text(box, drawn.c_str(), style);
  return static_cast<int16_t>(used * lineHeight);
}

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

// The two footer buttons: the outlined one on the left
// asks, the filled one on the right is the reason the app exists.
void refreshAndShow(toybox::Screen& screen, const fui::Rect& footer) {
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "REFRESH", ActionRefresh, true);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
               "SHOW", ActionShow, false);
}

// The last picture, framed, in TRMNL's own 5:3 unless the box is narrower.
fui::Rect previewFrame(toybox::Screen& screen, const fui::Rect& box, const bool hasImage) {
  const fui::Rect frame = box;
  const fui::Paint rule = fui::Paint::solid(fui::Color::Black);
  screen.target().fill(fui::makeRect(frame.x, frame.y, frame.width, toybox::kRule), rule);
  screen.target().fill(
      fui::makeRect(frame.x, static_cast<int16_t>(frame.y + frame.height - toybox::kRule), frame.width, toybox::kRule),
      rule);
  screen.target().fill(fui::makeRect(frame.x, frame.y, toybox::kRule, frame.height), rule);
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(frame.x + frame.width - toybox::kRule), frame.y, toybox::kRule, frame.height),
      rule);
  const fui::Rect inside = fui::makeRect(
      static_cast<int16_t>(frame.x + toybox::kRule), static_cast<int16_t>(frame.y + toybox::kRule),
      static_cast<int16_t>(frame.width - 2 * toybox::kRule), static_cast<int16_t>(frame.height - 2 * toybox::kRule));
  if (!hasImage) {
    fittedLine(screen,
               fui::makeRect(inside.x, static_cast<int16_t>(inside.y + inside.height / 2 - 12), inside.width, 24),
               "NO SCREEN YET", fui::TextAlign::Center, toybox::kTileFont);
  }
  rowHit(screen, frame, ActionShow);
  return hasImage ? inside : fui::Rect{};
}

}  // namespace

// --- The app's own screen -----------------------------------------------------

// The last picture first, because it is the answer to "is it working".
fui::Rect buildHome(toybox::Screen& screen, const HomeModel& model) {
  chrome(screen, true);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;
  const fui::Rect frame =
      fui::makeRect(toybox::kMargin, toybox::kBodyTop, width, static_cast<int16_t>(width * 3 / 5 + 2 * toybox::kRule));
  const fui::Rect picture = previewFrame(screen, frame, model.hasImage);

  int16_t y = static_cast<int16_t>(frame.y + frame.height + toybox::kGutter);
  const int16_t tileHeight = screen.target().lineHeight(toybox::kTileFont);
  std::string where = std::string(model.server) + "  /  " + model.cadence;
  fittedLine(screen, fui::makeRect(toybox::kMargin, y, width, tileHeight), where.c_str(), fui::TextAlign::Left,
             toybox::kTileFont);
  y = static_cast<int16_t>(y + tileHeight + 4);
  fittedLine(screen, fui::makeRect(toybox::kMargin, y, width, tileHeight), model.device, fui::TextAlign::Left,
             toybox::kTileFont);
  y = static_cast<int16_t>(y + tileHeight + toybox::kGutter);
  prose(screen, fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(footer.y - toybox::kGutter - y)),
        model.status, toybox::kBodyFont);
  refreshAndShow(screen, footer);
  return picture;
}

// --- The phone ---------------------------------------------------------------

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen);
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
  const fui::Rect state =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, state, model.saved ? "SAVED FROM YOUR PHONE" : "WAITING FOR YOUR PHONE", fui::TextAlign::Center,
             toybox::kTileFont);

  footerButton(screen, footer, "DONE", ActionDismiss, false);
  return qr;
}

// --- Waiting, and refusals ------------------------------------------------------

void buildNotice(toybox::Screen& screen, const char* text) {
  chrome(screen);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  prose(screen,
        fui::makeRect(footer.x, toybox::kBodyTop, footer.width,
                      static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop)),
        text, toybox::kBodyFont);
  footerButton(screen, footer, "BACK", ActionDismiss, false);
}

void buildBusy(toybox::Screen& screen, const char* text) {
  chrome(screen);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t lineHeight = screen.target().lineHeight(toybox::kBodyFont);
  const fui::Rect box = fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height / 2 - lineHeight * 2), width,
                                      static_cast<int16_t>(lineHeight * 4));
  prose(screen, box, text, toybox::kBodyFont, fui::TextAlign::Center);
}

}  // namespace trmnlui
