#include "InstapaperScreens.h"

#include <FreeInkUIIcon.h>

#include "../ui/ToyboxText.h"

namespace instapaperui {
namespace {

// The top of any body: below the header band and the rule Toybox draws under
// it. Shared by every screen here so they line up with each other and with the
// shelf the reader just came from.
constexpr int kBodyTop = toybox::kHeaderHeight + toybox::kGutter * 3;

constexpr int kFooterHeight = toybox::kPillHeight;

// Header band, rule, and the page margin.
//
// `rightLabel` is drawn in paper, not ink. The band is solid black and the
// header component draws rightLabel with `subtitleText`, whose default colour
// is Black -- so a label left at the default is painted black on black and
// simply is not there. Styling `trailingText` instead does nothing: the
// component only consults that for its trailing BUTTON. Study and Hacker News
// each paid for this once.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel,
            const fui::TextStyle* titleText = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (titleText != nullptr) header.titleText = *titleText;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// Plain text into a rect, with the defaults every line on these screens wants.
fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const fui::Color color = fui::Color::Black, const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  style.maxLines = maxLines;
  return style;
}

fui::TextStyle rowSubtitleStyle(const fui::ThemeTokens& tokens) {
  fui::TextStyle subtitle = tokens.smallText;
  subtitle.font = toybox::kTileFont;
  // FONT_SLOT_SMALL is 0, and textStyleUnset() reads a style whose font is 0
  // and whose every other field is default as "the caller did not set this",
  // so Screen::list() would put the theme's value back and the row would draw
  // at full size. Naming the alignment the component applies anyway is what
  // marks this style as owned.
  subtitle.align = fui::TextAlign::Left;
  return subtitle;
}

fui::TextStyle rowValueStyle(const fui::ThemeTokens& tokens) {
  fui::TextStyle value = tokens.smallText;
  value.font = toybox::kTileFont;
  value.align = fui::TextAlign::Right;
  return value;
}

}  // namespace

// --- The queue -----------------------------------------------------------

fui::Rect queueBand(const fui::DeviceContext& device) {
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

int16_t queueRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens) {
  // One line of title and one of subtitle, plus air. ONE line of title, not
  // two, and that is forced rather than chosen: a row's title band is one line
  // tall whenever a subtitle is set, so a wrapping label draws straight
  // through the subtitle beneath it. The Activity fits the title to one line
  // for the same reason.
  return static_cast<int16_t>(target.lineHeight(tokens.bodyText.font) + target.lineHeight(toybox::kTileFont) +
                              toybox::kGutter);
}

int16_t queueTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                        const fui::ThemeTokens& tokens) {
  // What the label actually gets: the row less its side padding on both edges,
  // less the widest value the row will ever show and the gap before it.
  // Derived from the numbers buildQueue hands the component, so a title is
  // measured against the space it is really drawn into.
  const fui::TextStyle value = rowValueStyle(tokens);
  const int16_t valueWidth = target.measureText(value.font, "100%", value).width;
  return static_cast<int16_t>(queueBand(device).width - 2 * tokens.listSidePadding - valueWidth - toybox::kGutter);
}

void buildQueue(toybox::Screen& screen, const QueueModel& model) {
  // An empty lastSync passes nullptr, not "": the header styles its right
  // label only when there is one, and an empty string would still reserve the
  // room that pushed the title off the band.
  chrome(screen, "INSTAPAPER",
         model.lastSync != nullptr && model.lastSync[0] != '\0' ? model.lastSync : nullptr);

  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);

  // Taken first so the list can never grow into it. The door is always here,
  // including on an empty queue -- an empty queue is precisely when a reader
  // wants to pull, and a screen whose only control appears once there is
  // something to do teaches nobody where it lives.
  fui::ButtonProps sync;
  sync.label = "SYNC";
  sync.action = ActionSync;
  screen.button(sync, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight));

  if (model.count <= 0) {
    // Both boxes ASK the target for their line height rather than carrying a
    // number. The first version hardcoded a 40px box and put the sentence 48px
    // below it; the display cut's advanceY is 63, so the headline overflowed
    // its box by 23px and the sentence began 15px INSIDE its glyphs. On the
    // panel the two lines touched. The simulator never showed it because this
    // screen -- the one a new user meets first -- had never been rendered:
    // the host test asserts the words are present, not where they land.
    const int16_t headlineH = screen.target().lineHeight(toybox::kDisplayFont);
    const int16_t bodyH = screen.target().lineHeight(toybox::kUiFont);
    const int16_t top = static_cast<int16_t>(kBodyTop + toybox::kMargin * 2);
    screen.target().text(fui::makeRect(toybox::kMargin, top, width, headlineH), "NOTHING TO READ",
                         plain(toybox::kDisplayFont, fui::TextAlign::Center));
    screen.target().text(
        fui::makeRect(toybox::kMargin, static_cast<int16_t>(top + headlineH + toybox::kGutter), width,
                      static_cast<int16_t>(bodyH * 2)),
        "Save something to Instapaper, then sync.",
        plain(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 2));
    return;
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpenArticle;
  list.rowHeight = queueRowHeight(screen.target(), screen.theme());
  list.labelText = screen.theme().bodyText;
  list.labelText.maxLines = 1;
  list.subtitleText = rowSubtitleStyle(screen.theme());
  list.valueText = rowValueStyle(screen.theme());
  // Off, or a title is capped at 60% of the row so it sits prettily beside its
  // value. That balance is right for a settings row with a short value; here
  // the title is the content and the position is a footnote.
  list.balanceWrappedLabelWithValue = false;
  screen.list(list, static_cast<int16_t>(queueBand(device).height), fui::LayoutAnchor::Top);
}

// --- The reader ----------------------------------------------------------

fui::Rect readerBody(const fui::DeviceContext& device) {
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

void buildReader(toybox::Screen& screen, const ReaderModel& model) {
  // The band carries the article's own title. Within this app chrome is Jersey
  // and content is the reading face, and a title is content -- somebody's
  // sentence, in its own case -- so the band borrows the reading cut in paper,
  // the way a book's running header borrows the text's.
  fui::TextStyle bandTitle = screen.theme().bodyText;
  bandTitle.color = fui::Color::White;
  bandTitle.maxLines = 1;
  const fui::TextStyle& labelStyle = screen.theme().smallText;
  const int16_t labelWidth =
      model.pageLabel != nullptr && model.pageLabel[0] != '\0'
          ? static_cast<int16_t>(screen.target().measureText(labelStyle.font, model.pageLabel, labelStyle).width +
                                 toybox::kGutter)
          : 0;
  const int16_t room = static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin - labelWidth);
  const std::string headline = toybox::fitLines(screen.target(), model.title, room, 1, bandTitle);
  chrome(screen, headline.c_str(), model.pageLabel, &bandTitle);

  const fui::DeviceContext& device = screen.device();

  // Three controls across the bottom: back a page, archive, forward a page.
  // Each button registers the rect it was drawn into, so a control cannot be
  // live anywhere its pixels are not. The wide middle is the deliberate one --
  // it is the only control here that changes anything outside this screen, and
  // it is the one a thumb finds without aiming.
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const int16_t usable = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t arrow = static_cast<int16_t>((usable - 2 * toybox::kGutter) / 4);
  const int16_t middle = static_cast<int16_t>(usable - 2 * arrow - 2 * toybox::kGutter);

  const auto footerButton = [&screen, footerY](const char* label, const fui::ActionId action, const int16_t x,
                                               const int16_t width, const bool enabled) {
    fui::ButtonProps button;
    button.label = label;
    button.action = enabled ? action : fui::NO_ACTION;
    // Dimmed rather than removed: a control that vanishes moves its
    // neighbours, and on e-ink that costs a repaint of the whole bar.
    if (!enabled) button.styles = toybox::disabledButtonStyles();
    screen.button(button, fui::makeRect(x, footerY, width, kFooterHeight));
  };

  const int16_t left = toybox::kMargin;
  footerButton("<", ActionPagePrev, left, arrow, model.canPagePrev);
  footerButton("ARCHIVE", ActionArchive, static_cast<int16_t>(left + arrow + toybox::kGutter), middle, true);
  footerButton(">", ActionPageNext, static_cast<int16_t>(left + arrow + middle + 2 * toybox::kGutter), arrow,
               model.canPageNext);

  // Drawn into readerBody() rather than into the Screen's running content
  // rect, because the Activity counts the lines that fit in this exact rect to
  // decide what a page turn does AND to compute the reading position it sends
  // back to Instapaper. Two ways of arriving at the same rectangle is how a
  // page turn starts eating a line.
  fui::TextAreaProps body;
  body.text = model.text;
  body.topLine = model.topLine;
  body.showCaret = false;
  body.style = screen.theme().bodyText;
  fui::textArea(screen.frame(), readerBody(device), body);
}

// --- Notices -------------------------------------------------------------

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  // The band says the app, always, and never repaints between notices. What a
  // particular notice is about goes in the headline below it, where a sentence
  // has room to be one: at the display cut the band holds about fourteen
  // characters.
  chrome(screen, "INSTAPAPER", nullptr);

  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);

  // Bottom-anchored, and taken first so the body can never grow into it.
  if (model.actionLabel != nullptr) {
    fui::ButtonProps action;
    action.label = model.actionLabel;
    action.action = ActionNotice;
    screen.button(action, fui::makeRect(toybox::kMargin,
                                        static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight),
                                        width, toybox::kPillHeight));
  }

  int16_t y = kBodyTop;

  if (model.mark != nullptr) {
    const int16_t markSize = toybox::kIconSize * 2;
    // Black on paper: a 1-bpp mask painted in one colour is invisible on a
    // background of that colour, so it has to stay off the header band, which
    // is why it starts at kBodyTop.
    screen.target().bitmap(fui::makeRect(toybox::kMargin, y, markSize, markSize), fui::bitmapFromIcon(*model.mark),
                           fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + markSize + toybox::kGutter);
  }

  if (model.headline != nullptr && model.headline[0] != '\0') {
    fui::TextStyle headline = screen.theme().titleText;
    headline.color = fui::Color::Black;  // off the band now, so it has to be ink
    headline.align = fui::TextAlign::Left;
    headline.maxLines = 2;
    const int16_t headlineHeight = static_cast<int16_t>(2 * screen.target().lineHeight(headline.font));
    screen.target().text(fui::makeRect(toybox::kMargin, y, width, headlineHeight), model.headline, headline);
    y = static_cast<int16_t>(y + headlineHeight + toybox::kGutter);

    screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);
  }

  if (model.message != nullptr && model.message[0] != '\0') {
    // Through textArea because it wraps and centeredText does not.
    const int16_t reserved =
        model.actionLabel != nullptr ? static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter) : 0;
    const int16_t bottom = static_cast<int16_t>(device.height - toybox::kMargin - reserved);
    fui::TextAreaProps message;
    message.text = model.message;
    message.showCaret = false;
    message.style = screen.theme().bodyText;
    fui::textArea(screen.frame(), fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(bottom - y)), message);
  }
}

// --- Pairing -------------------------------------------------------------

fui::Rect buildPairQr(toybox::Screen& screen, const char* code) {
  chrome(screen, "INSTAPAPER", nullptr);
  const fui::Rect body = screen.body();

  screen.target().text(fui::makeRect(body.x, body.y, body.width, 48), "PAIR THIS READER",
                       plain(toybox::kDisplayFont, fui::TextAlign::Center));

  constexpr int16_t kQrSide = 232;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                                     static_cast<int16_t>(body.y + 48 + toybox::kMargin * 2), kQrSide, kQrSide);

  // The code, said twice on purpose: the QR for phones, the letters for the
  // person typing it into the pair page by hand.
  const int codeY = qr.bottom() + toybox::kMargin;
  screen.target().text(fui::makeRect(body.x, codeY, body.width, 48), code,
                       plain(toybox::kDisplayFont, fui::TextAlign::Center));

  // FULL body width, not inset by a margin on each side, and that is not
  // tidiness: the host is the longest unbreakable token on this screen and at
  // the inset width it wrapped one character short, drawing
  // "read.crossplay.ma-r-s.com/pai" with the renderer's U+2026 invisible
  // because this cut has no glyph for it. A reader would have typed an address
  // that does not exist and had no way to see why.
  screen.target().text(
      fui::makeRect(body.x, codeY + 48 + toybox::kMargin, body.width, 116),
      "Sign in at read.crossplay.ma-r-s.com/pair, then scan this code.",
      plain(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));
  // Overflow is invisible in these cuts: the renderer appends U+2026 and the
  // face has no glyph for it, so a line too long simply stops mid-word rather
  // than showing it was cut. Every string on this screen is short for that
  // reason and not for taste.
  screen.target().text(fui::makeRect(body.x, body.bottom() - 30, body.width, 24), "CODE LASTS 5 MIN, BACK STOPS",
                       plain(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
  return qr;
}

PairConfirmLayout buildPairConfirm(toybox::Screen& screen) {
  chrome(screen, "INSTAPAPER", nullptr);
  const fui::Rect body = screen.body();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  screen.target().text(fui::makeRect(body.x, body.y + toybox::kMargin, body.width, 48), "IS THIS YOU?",
                       plain(toybox::kDisplayFont, fui::TextAlign::Center));

  PairConfirmLayout layout;
  layout.username =
      fui::makeRect(body.x, static_cast<int16_t>(body.y + toybox::kMargin + 48 + toybox::kMargin), body.width, 80);

  screen.target().text(fui::makeRect(body.x + toybox::kMargin, layout.username.bottom() + toybox::kMargin,
                                     body.width - toybox::kMargin * 2, 84),
                       "Nothing is stored yet.",
                       plain(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));

  // The confirm target is a pill rather than a key press: on the Sticky the
  // Confirm button is the power button, and four of the X4 Pro's six logical
  // buttons are unassigned pins. A thumb has to be able to reach this.
  const int16_t pillH = 56;
  layout.pill = fui::makeRect(static_cast<int16_t>(body.x + 44), static_cast<int16_t>(body.bottom() - pillH - 48),
                              static_cast<int16_t>(body.width - 88), pillH);
  screen.target().stroke(layout.pill, ink, toybox::kRule, static_cast<uint8_t>(pillH / 2));
  screen.target().text(fui::makeRect(layout.pill.x, layout.pill.y + (pillH - 26) / 2, layout.pill.width, 26),
                       "YES, PAIR IT", plain(toybox::kUiFont, fui::TextAlign::Center));
  screen.target().text(fui::makeRect(body.x, layout.pill.y - 34, body.width, 24), "NOT ME? PRESS BACK",
                       plain(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
  return layout;
}

}  // namespace instapaperui
