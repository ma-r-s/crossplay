#include "RssScreens.h"

#include "../hackernews/HackerNewsScreens.h"
#include "../ui/ToyboxText.h"

namespace rssui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;

// The most a site's name may take at the right of a post row: a footnote, in
// the dense cut, cut short past this. Room for "Daring Fireball" and its like.
constexpr int16_t kSiteWidth = 132;

// A 12px dot, and the same square left empty. Mask1: a set bit is paper.
constexpr uint8_t kDotBits[] = {0xFF, 0xFF, 0xE0, 0x7F, 0xC0, 0x3F, 0x80, 0x1F, 0x80, 0x1F, 0x80, 0x1F,
                                0x80, 0x1F, 0x80, 0x1F, 0x80, 0x1F, 0xC0, 0x3F, 0xE0, 0x7F, 0xFF, 0xFF};
constexpr uint8_t kBlankBits[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr int16_t kMarkSize = 12;
// What the list puts between the mark and the headline.
constexpr int16_t kMarkGap = 8;
// The scroll track the list draws beside its rows once they page, which takes
// its width out of every row. Reserved whether or not it is drawn: a headline
// fitted a few pixels too wide is cut mid-word by the component instead.
constexpr int16_t kScrollRoom = 8;

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The three places, as segments rather than tabs that move: each names where it
// goes, the one you are in is filled and inert, and none of them ever vanishes,
// because a segment that disappears moves its neighbours.
void segments(toybox::Screen& screen, const Tab tab, const bool narrowed) {
  const fui::Rect strip = screen.takeBottom(kFooterHeight, toybox::kGutter);
  const int16_t third = static_cast<int16_t>((strip.width - 2 * toybox::kGutter) / 3);
  struct Segment {
    const char* label;
    fui::ActionId action;
    Tab tab;
  };
  const Segment all[] = {
      {"ALL", ActionShowAll, Tab::All}, {"FEEDS", ActionShowFeeds, Tab::Feeds}, {"SAVED", ActionShowSaved, Tab::Saved}};
  int16_t x = strip.x;
  for (int i = 0; i < 3; ++i) {
    const bool here = all[i].tab == tab;
    // The last segment takes whatever the division left over, so the strip
    // ends exactly at the margin.
    const int16_t width = i == 2 ? static_cast<int16_t>(strip.x + strip.width - x) : third;
    fui::ButtonProps button;
    button.label = all[i].label;
    // Filled is where you are, and inert there -- except over a narrowed list,
    // where FEEDS is where you came from and the way back to it.
    button.action = here && !narrowed ? fui::NO_ACTION : all[i].action;
    button.styles = here ? toybox::invertedStyles() : toybox::rowStyles();
    screen.button(button, fui::makeRect(x, strip.y, width, kFooterHeight));
    x = static_cast<int16_t>(x + width + toybox::kGutter);
  }
}

fui::Rect bandAbove(const fui::DeviceContext& device, const int reserved) {
  const int bottom = toybox::kMargin + reserved;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

void list(toybox::Screen& screen, const ListModel& model, const fui::ActionId action, const int16_t rowHeight,
          const int16_t iconSize, const int labelLines) {
  fui::ListProps props;
  props.items = model.items;
  props.count = static_cast<uint16_t>(model.count);
  props.itemsWindowFirst = static_cast<uint16_t>(model.windowFirst);
  props.itemsWindowCount = static_cast<uint16_t>(model.windowCount);
  props.topIndex = static_cast<uint16_t>(model.topIndex);
  props.selectedIndex = static_cast<int16_t>(model.selected);
  props.action = action;
  props.rowHeight = rowHeight;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = static_cast<uint8_t>(labelLines);
  props.valueText = footnoteStyle(screen.theme());
  props.iconSize = iconSize;
  if (iconSize > 0) props.textGap = kMarkGap;
  // The headline is the content and the site a footnote beside it; Hacker
  // News turns the balancing off for the same reason.
  props.balanceWrappedLabelWithValue = false;
  screen.list(props);
}

}  // namespace

// --- Geometry ------------------------------------------------------------

fui::Rect postsBand(const fui::DeviceContext& device) { return bandAbove(device, kFooterHeight + toybox::kGutter); }

fui::Rect feedsBand(const fui::DeviceContext& device) {
  // The way to the phone sits between the rows and the segments.
  return bandAbove(device, 2 * (kFooterHeight + toybox::kGutter));
}

int16_t postRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens) {
  return static_cast<int16_t>(2 * target.lineHeight(tokens.bodyText.font) + toybox::kGutter);
}

int16_t feedRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens) {
  return static_cast<int16_t>(target.lineHeight(tokens.bodyText.font) + 2 * toybox::kGutter);
}

fui::TextStyle footnoteStyle(const fui::ThemeTokens& tokens) {
  fui::TextStyle style = tokens.smallText;
  style.font = toybox::kTileFont;
  // FONT_SLOT_SMALL is 0, and a style whose font is 0 and every other field
  // default reads as unset, so Screen::list() would put the theme's value
  // style back. Naming the alignment marks it as owned. See HackerNewsScreens.
  style.align = fui::TextAlign::Right;
  return style;
}

int16_t postSiteWidth() { return kSiteWidth; }

int16_t postTitleWidth(const fui::DeviceContext& device, const fui::ThemeTokens& tokens, const int16_t siteWidth) {
  return static_cast<int16_t>(postsBand(device).width - kScrollRoom - 2 * tokens.listSidePadding - kMarkSize -
                              kMarkGap - (siteWidth > 0 ? siteWidth + kMarkGap : 0));
}

int16_t feedTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                       const fui::ThemeTokens& tokens) {
  const fui::TextStyle count = footnoteStyle(tokens);
  const int16_t countWidth = target.measureText(count.font, "FAILED", count).width;
  return static_cast<int16_t>(feedsBand(device).width - kScrollRoom - 2 * tokens.listSidePadding - countWidth -
                              toybox::kGutter);
}

fui::BitmapRef unreadMark() { return fui::BitmapRef{kDotBits, kMarkSize, kMarkSize, fui::BitmapFormat::Mask1}; }
fui::BitmapRef readMark() { return fui::BitmapRef{kBlankBits, kMarkSize, kMarkSize, fui::BitmapFormat::Mask1}; }

// --- The lists -----------------------------------------------------------

Empty emptyFor(const Tab tab, const bool haveFeeds, const bool oneFeed) {
  Empty empty;
  if (tab == Tab::Saved) {
    // Hacker News's sentence for the same shelf, so one idea reads one way.
    empty.headline = "NOTHING SAVED YET";
    empty.message = "Tap SAVE while you read.";
    return empty;
  }
  if (!haveFeeds) {
    // The ordinary first screen, and the whole of how feeds arrive: there is
    // no keyboard worth typing an address on, so the phone does it.
    empty.headline = "NO FEEDS YET";
    empty.message = "Add the sites you read from your phone.";
    empty.actionLabel = "ADD FEEDS";
    empty.action = ActionAddFeeds;
    return empty;
  }
  if (oneFeed) {
    empty.headline = "NOTHING HERE";
    empty.message = "This feed had no posts when it was last read.";
    return empty;
  }
  empty.headline = "NOTHING YET";
  empty.message = "Feeds are read each time RSS opens, over Wi-Fi.";
  return empty;
}

void buildPosts(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, model.title);
  segments(screen, model.tab, model.narrowed);
  if (model.count <= 0) {
    hnui::emptyBlock(screen, model.emptyHeadline, model.emptyMessage, model.emptyActionLabel, model.emptyAction);
    return;
  }
  list(screen, model, ActionOpenPost, postRowHeight(screen.target(), screen.theme()), kMarkSize, 2);
}

void buildFeeds(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, model.title);
  segments(screen, model.tab, model.narrowed);
  if (model.count <= 0) {
    hnui::emptyBlock(screen, model.emptyHeadline, model.emptyMessage, model.emptyActionLabel, model.emptyAction);
    return;
  }
  // Adding and removing both happen on the phone, so one door says both.
  {
    const fui::Rect strip = screen.takeBottom(kFooterHeight, toybox::kGutter);
    fui::ButtonProps button;
    button.label = "ADD OR REMOVE";
    button.action = ActionAddFeeds;
    screen.button(button, fui::makeRect(strip.x, strip.y, strip.width, kFooterHeight));
  }
  list(screen, model, ActionOpenFeed, feedRowHeight(screen.target(), screen.theme()), 0, 1);
}

// --- The phone -----------------------------------------------------------

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, "ADD FEEDS");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);

  {
    fui::ButtonProps done;
    done.label = "DONE";
    done.action = ActionPhoneDone;
    screen.button(done, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight));
  }

  fui::TextStyle line = screen.theme().bodyText;
  line.align = fui::TextAlign::Center;
  line.maxLines = 1;
  const int16_t lineHeight = screen.target().lineHeight(line.font);

  const auto fitted = [&screen, &line, width](const int16_t y, const char* text, const int lines) {
    fui::TextStyle style = line;
    style.maxLines = static_cast<uint8_t>(lines);
    const std::string fit = toybox::fitLines(screen.target(), text, width, lines, style);
    screen.target().text(fui::makeRect(toybox::kMargin, y, width, lines * screen.target().lineHeight(style.font)),
                         fit.c_str(), style);
  };

  int16_t y = static_cast<int16_t>(kBodyTop + toybox::kGutter);
  fitted(y, "Scan this with your phone.", 1);
  y = static_cast<int16_t>(y + lineHeight + toybox::kGutter * 2);

  // The code takes about half the panel, so the address under it -- the one
  // string somebody may have to type -- is not pushed into a corner.
  const int16_t room = static_cast<int16_t>(footerY - toybox::kGutter * 3 - y - lineHeight * 5);
  int16_t side = room < width ? room : width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), y, side, side);

  y = static_cast<int16_t>(qr.y + qr.height + toybox::kGutter);
  fitted(y, model.readable, 1);
  y = static_cast<int16_t>(y + lineHeight + toybox::kGutter);
  // The state says whether anything has happened. A screen whose whole promise
  // is "add it over there and it appears here" has to answer "did it", in the
  // same face as the address rather than a footnote under it. Two lines,
  // because it carries a feed's own name and some names are long.
  fitted(y, model.status, 2);
  return qr;
}

}  // namespace rssui
