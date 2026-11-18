#include "WeatherScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxInk.h"
#include "../ui/ToyboxText.h"
#include "WeatherIcons.h"

namespace weatherui {
namespace {

constexpr int16_t kMargin = toybox::kMargin;
constexpr int16_t kGutter = toybox::kGutter;

int16_t max16(const int a, const int b) { return static_cast<int16_t>(a > b ? a : b); }

// A style this screen owns outright. The alignment is always named: the small
// slot is font 0, and a style with font 0 and every other field at its default
// reads as unset to the components, which would put the theme's back.
fui::TextStyle ink(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left, const uint8_t lines = 1,
                   const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.maxLines = lines;
  style.color = color;
  return style;
}

int16_t lineOf(toybox::Screen& screen, const fui::FontId font) { return screen.target().lineHeight(font); }

int16_t widthOf(toybox::Screen& screen, const fui::FontId font, const char* text) {
  return screen.target().measureText(font, text, ink(font)).width;
}

// Where a capital's ink sits inside its own line box, from the cut's measured
// metrics (ToyboxTokens.h). The fallback is for a target that is not one of
// the fork's cuts, which in practice is the host tests' fake.
struct Cap {
  int16_t line;
  int16_t top;     // line-box top to cap top
  int16_t height;  // cap height
};

Cap capOf(toybox::Screen& screen, const fui::FontId font) {
  const int16_t line = lineOf(screen, font);
  if (const toybox::CutMetrics* cut = toybox::cutForLineHeight(line)) {
    return Cap{line, static_cast<int16_t>(cut->ascender - cut->inkHeight), cut->inkHeight};
  }
  return Cap{line, static_cast<int16_t>(line / 5), static_cast<int16_t>(line * 3 / 5)};
}

// --- Degrees -----------------------------------------------------------------
// No Jersey cut carries U+00B0, so the sign is drawn: a ring at the cap line,
// sized from the cap height so it scales with the number it belongs to.

int16_t ringRadius(const int16_t capHeight) { return max16(3, (capHeight + 4) / 7); }
int16_t ringWeight(const int16_t capHeight) { return max16(2, capHeight / 18); }
int16_t ringGap(const int16_t capHeight) { return max16(2, capHeight / 12); }

bool isTemperature(const std::string& text) { return text != "--"; }

int16_t tempWidth(toybox::Screen& screen, const fui::FontId font, const std::string& text) {
  int16_t width = widthOf(screen, font, text.c_str());
  if (isTemperature(text)) {
    const Cap cap = capOf(screen, font);
    width = static_cast<int16_t>(width + ringGap(cap.height) + 2 * ringRadius(cap.height));
  }
  return width;
}

// Draws "7" and its ring with the line box's top at `lineTop`.
void drawTemp(toybox::Screen& screen, const int16_t x, const int16_t lineTop, const fui::FontId font,
              const std::string& text, const fui::Color color = fui::Color::Black) {
  const Cap cap = capOf(screen, font);
  const int16_t width = widthOf(screen, font, text.c_str());
  screen.target().text(fui::makeRect(x, lineTop, static_cast<int16_t>(width + 1), cap.line), text.c_str(),
                       ink(font, fui::TextAlign::Left, 1, color));
  if (!isTemperature(text)) return;
  const int16_t r = ringRadius(cap.height);
  const int16_t cx = static_cast<int16_t>(x + width + ringGap(cap.height) + r);
  const int16_t cy = static_cast<int16_t>(lineTop + cap.top + r);
  toybox::ring(screen, cx, cy, r, ringWeight(cap.height), color,
               color == fui::Color::Black ? fui::Color::White : fui::Color::Black);
}

std::string number(const int value) { return std::to_string(value); }
std::string highText(const DayRow& day) { return day.hasHigh ? number(day.high) : std::string("--"); }
std::string lowText(const DayRow& day) { return day.hasLow ? number(day.low) : std::string("--"); }
std::string rainLabel(const int rain) { return rain < 0 ? std::string() : number(rain) + "%"; }

void blit(toybox::Screen& screen, const freeink::Icon& icon, const int16_t x, const int16_t y,
          const fui::Color color = fui::Color::Black) {
  screen.target().bitmap(fui::makeRect(x, y, static_cast<int16_t>(icon.w), static_cast<int16_t>(icon.h)),
                         fui::bitmapFromIcon(icon), fui::BitmapMode::Contain, fui::Paint::solid(color));
}

void hairline(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t width) {
  screen.target().fill(fui::makeRect(x, y, width, toybox::kHairline), fui::Paint::solid(fui::Color::Black));
}

// --- The band ------------------------------------------------------------------

// `insetTop` leaves the body three gutters under the rule, as most screens
// here do; the main screen places its own composition by ink and takes none.
void band(toybox::Screen& screen, const char* title, const fui::ActionId trailing, const char* trailingLabel,
          const bool insetTop = true) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  header.sidePadding = screen.theme().headerSidePadding;
  if (trailing == ActionMenu) {
    header.trailingIcon = fui::bitmapFromIcon(icon_go_settings_32);
  } else if (trailingLabel != nullptr) {
    header.trailingLabel = trailingLabel;
    header.trailingText = ink(toybox::kSmallFont, fui::TextAlign::Center, 1, fui::Color::White);
  }
  if (trailing != fui::NO_ACTION) {
    header.trailingAction = trailing;
    header.trailingStyles = toybox::bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{static_cast<int16_t>(insetTop ? kGutter * 3 : 0), kMargin, kMargin, kMargin});
}

// "OFFLINE. UPDATED 14:07": what the radio did, and when the reading is from.
std::string statusLine(const MainModel& model) {
  std::string line = model.status;
  if (model.updated[0] != '\0') {
    if (!line.empty()) line += ' ';
    line += model.updated;
  }
  return line;
}

// --- The main screen, by ink ---------------------------------------------------------
//
// Every gap below is between INKS, cap top to baseline (ui/ToyboxInk.h), and
// comes from one scale chosen by relationship: lines of one reading a gutter
// apart, the week's rows two, the three groups (the place, the reading, the
// week) three. The composition is then centred between the band's rule and the
// panel's foot, so the space it does not need is placed, equally above and
// below, instead of being handed to whichever element came last.
constexpr int16_t kLineGap = kGutter;
constexpr int16_t kRowGap = kGutter * 2;
constexpr int16_t kGroupGap = kGutter * 3;
// The place, four lines of reading, the rule, the rows; or the place and the
// four blocks of an empty screen.
constexpr int kBlocks = 1 + 4 + 1 + kRows;
using Column = toybox::InkColumn<kBlocks>;

// Where the composition may sit: from the rule under the band to the foot of
// the panel, never closer than a margin to either.
int16_t spanTop() { return static_cast<int16_t>(toybox::kChromeHeight); }
int16_t spanBottom(toybox::Screen& screen) { return screen.device().height; }

// Draws `text` with its cap top at inkTop.
void textAt(toybox::Screen& screen, const int16_t x, const int16_t inkTop, const int16_t width, const fui::FontId font,
            const char* text, const uint8_t lines = 1) {
  screen.target().text(toybox::inkRect(screen.target(), font, x, inkTop, width, lines), text,
                       ink(font, fui::TextAlign::Left, lines));
}

// A temperature and its ring with the cap top at inkTop.
void tempAt(toybox::Screen& screen, const int16_t x, const int16_t inkTop, const fui::FontId font,
            const std::string& text) {
  drawTemp(screen, x, static_cast<int16_t>(inkTop - capOf(screen, font).top), font, text);
}

// An icon whose ink is centred on (centreX, centreY): the drawn shape, not the
// square around it, which carries different padding from icon to icon.
void iconCentred(toybox::Screen& screen, const freeink::Icon& icon, const int16_t centreX, const int16_t centreY) {
  const fui::Rect drawn = toybox::iconInk(icon);
  blit(screen, icon, static_cast<int16_t>(centreX - drawn.width / 2 - drawn.x),
       static_cast<int16_t>(centreY - drawn.height / 2 - drawn.y));
}

// No forecast to draw: a headline, a sentence, the radio's state, and when it
// helps a way to try again, left-aligned like everything else on the screen.
int16_t emptyBlock(toybox::Screen& screen, const MainModel& model, const int16_t placeInk) {
  const fui::Rect body = screen.body();
  const auto lines = [&screen, &body](const char* text, const fui::FontId font, const int most) {
    fui::TextStyle style = ink(font, fui::TextAlign::Left, static_cast<uint8_t>(most));
    const int16_t height = fui::measureWrappedText(screen.target(), text, style, body.width).height;
    const int16_t line = screen.target().lineHeight(font);
    return line > 0 ? max16(1, height / line) : 1;
  };
  const std::string status = statusLine(model);
  const int headLines = lines(model.emptyHeadline, toybox::kUiFont, 2);
  const int messageLines = model.emptyMessage[0] ? lines(model.emptyMessage, toybox::kSmallFont, 4) : 0;
  Column column;
  const int place = column.add(0, placeInk);
  const int head = column.add(kGroupGap, toybox::inkHeight(screen.target(), toybox::kUiFont, headLines));
  const int message =
      messageLines ? column.add(kLineGap, toybox::inkHeight(screen.target(), toybox::kSmallFont, messageLines)) : -1;
  const int state = status.empty() ? -1 : column.add(kLineGap, toybox::inkHeight(screen.target(), toybox::kSmallFont));
  const int retry = model.offerRetry ? column.add(kGroupGap, toybox::kPillHeight) : -1;
  column.centre(spanTop(), spanBottom(screen), kMargin);

  textAt(screen, body.x, column.top(head), body.width, toybox::kUiFont, model.emptyHeadline,
         static_cast<uint8_t>(headLines));
  if (message >= 0) {
    textAt(screen, body.x, column.top(message), body.width, toybox::kSmallFont, model.emptyMessage,
           static_cast<uint8_t>(messageLines));
  }
  if (state >= 0) textAt(screen, body.x, column.top(state), body.width, toybox::kSmallFont, status.c_str());
  if (retry >= 0) {
    fui::ButtonProps button;
    button.label = "TRY AGAIN";
    button.action = ActionRetry;
    button.text = toybox::buttonText(screen.theme());
    button.radius = toybox::kPillRadius;
    screen.button(button,
                  fui::makeRect(body.x, column.top(retry), static_cast<int16_t>(body.width / 2), toybox::kPillHeight));
  }
  return column.top(place);
}

// "H 9  L 3  RAIN 20%" (with rings), measured once and drawn from the same
// numbers.
struct RangeLine {
  std::string high;
  std::string low;
  std::string rain;
  int16_t hWidth = 0;
  int16_t lWidth = 0;
  int16_t highWidth = 0;
  int16_t lowWidth = 0;
  int16_t space = 0;
};

RangeLine rangeLine(toybox::Screen& screen, const DayRow& today, const fui::FontId font) {
  RangeLine line;
  line.high = highText(today);
  line.low = lowText(today);
  line.rain = today.rain >= 0 ? "RAIN " + rainLabel(today.rain) : std::string();
  // Jersey's space is a couple of pixels, so the gaps are set, not typed.
  const int16_t letterGap = static_cast<int16_t>(kGutter / 3);
  line.hWidth = static_cast<int16_t>(widthOf(screen, font, "H") + letterGap);
  line.lWidth = static_cast<int16_t>(widthOf(screen, font, "L") + letterGap);
  line.highWidth = tempWidth(screen, font, line.high);
  line.lowWidth = tempWidth(screen, font, line.low);
  line.space = static_cast<int16_t>(kGutter * 2);
  return line;
}

void drawRangeLine(toybox::Screen& screen, const RangeLine& line, int16_t x, const int16_t inkTop,
                   const fui::FontId font) {
  textAt(screen, x, inkTop, widthOf(screen, font, "H"), font, "H");
  x = static_cast<int16_t>(x + line.hWidth);
  tempAt(screen, x, inkTop, font, line.high);
  x = static_cast<int16_t>(x + line.highWidth + line.space);
  textAt(screen, x, inkTop, widthOf(screen, font, "L"), font, "L");
  x = static_cast<int16_t>(x + line.lWidth);
  tempAt(screen, x, inkTop, font, line.low);
  x = static_cast<int16_t>(x + line.lowWidth + line.space);
  if (!line.rain.empty()) textAt(screen, x, inkTop, widthOf(screen, font, line.rain.c_str()), font, line.rain.c_str());
}

std::string nowText(const MainModel& model) { return model.hasNow ? number(model.now) : std::string("--"); }

// The week's columns, measured from THIS week's strings, a gutter apart:
// day | sky | chance of rain | low | the bar | high. Nothing is reserved for a
// string that is not on screen, so no dead band opens between two columns.
struct WeekColumns {
  int16_t nameX = 0;
  int16_t skyX = 0;  // the sky column, as wide as this week's widest sky ink
  int16_t skyW = 0;
  int16_t rainX = 0;
  int16_t lowRight = 0;  // lows are right-aligned on this edge
  int16_t barX = 0;
  int16_t barW = 0;
  int16_t highRight = 0;  // and highs on this one, the screen's right edge
};

WeekColumns weekColumns(toybox::Screen& screen, const MainModel& model, const int16_t left, const int16_t right) {
  int16_t nameW = 0, skyW = 0, rainW = 0, lowW = 0, highW = 0;
  for (int i = 0; i < model.dayCount && i < kRows; ++i) {
    const DayRow& day = model.days[i];
    nameW = max16(nameW, widthOf(screen, toybox::kUiFont, day.name));
    skyW = max16(skyW, toybox::iconInk(skyBitmap(day.icon, 32)).width);
    rainW = max16(rainW, widthOf(screen, toybox::kSmallFont, rainLabel(day.rain).c_str()));
    lowW = max16(lowW, tempWidth(screen, toybox::kUiFont, lowText(day)));
    highW = max16(highW, tempWidth(screen, toybox::kUiFont, highText(day)));
  }
  WeekColumns c;
  c.nameX = left;
  c.skyX = static_cast<int16_t>(left + nameW + kGutter);
  c.skyW = skyW;
  c.rainX = static_cast<int16_t>(c.skyX + skyW + kGutter);
  const int16_t lowX = rainW > 0 ? static_cast<int16_t>(c.rainX + rainW + kGutter) : c.rainX;
  c.lowRight = static_cast<int16_t>(lowX + lowW);
  c.highRight = right;
  c.barX = static_cast<int16_t>(c.lowRight + kGutter);
  c.barW = static_cast<int16_t>(right - highW - kGutter - c.barX);
  return c;
}

int16_t mainBody(toybox::Screen& screen, const MainModel& model, const int16_t placeInk) {
  const fui::Rect body = screen.body();
  const int16_t right = static_cast<int16_t>(body.x + body.width);
  const fui::DrawTarget& target = screen.target();
  const std::string status = statusLine(model);
  const int days = model.dayCount < kRows ? model.dayCount : kRows;
  // A row is its text's capitals: the rhythm is the words', and each sky is
  // centred on them (a 32px sky's ink is at most 29, so it reaches two pixels
  // into the row gap either side and never near a neighbour).
  const int16_t rowBand = toybox::inkHeight(target, toybox::kUiFont);

  // The whole composition, gaps first; when it will not fit with a margin to
  // spare, the rows close up to a gutter, then the groups to two.
  int rowsAt[kRows] = {};
  int place = 0, hero = 0, sky = -1, range = 0, state = -1, rule = -1;
  Column column;
  const int16_t room = static_cast<int16_t>(spanBottom(screen) - spanTop() - 2 * kMargin);
  for (const int16_t tighten : {int16_t{0}, int16_t{1}, int16_t{2}}) {
    const int16_t rowGap = tighten ? kLineGap : kRowGap;
    const int16_t groupGap = tighten > 1 ? kRowGap : kGroupGap;
    column = Column{};
    place = column.add(0, placeInk);
    hero = column.add(groupGap, toybox::inkHeight(target, toybox::kDisplayFont));
    sky = model.sky[0] ? column.add(kLineGap, toybox::inkHeight(target, toybox::kUiFont)) : -1;
    range = column.add(kLineGap, toybox::inkHeight(target, toybox::kUiFont));
    state = status.empty() ? -1 : column.add(kLineGap, toybox::inkHeight(target, toybox::kSmallFont));
    rule = days > 0 ? column.add(groupGap, toybox::kHairline) : -1;
    for (int i = 0; i < days; ++i) rowsAt[i] = column.add(i == 0 ? groupGap : rowGap, rowBand);
    if (column.height() <= room) break;
  }
  column.centre(spanTop(), spanBottom(screen), kMargin);

  // The reading: the number at the left edge, its sky with its ink centred on
  // the number's capitals at the right edge, then the lines under it.
  const std::string now = nowText(model);
  const int16_t heroTop = column.top(hero);
  const int16_t heroCap = toybox::inkHeight(target, toybox::kDisplayFont);
  tempAt(screen, body.x, heroTop, toybox::kDisplayFont, now);
  {
    const freeink::Icon& icon = skyBitmap(model.nowIcon, 88);
    const fui::Rect drawn = toybox::iconInk(icon);
    iconCentred(screen, icon, static_cast<int16_t>(right - drawn.width / 2),
                static_cast<int16_t>(heroTop + heroCap / 2));
  }
  if (sky >= 0) textAt(screen, body.x, column.top(sky), body.width, toybox::kUiFont, model.sky);
  drawRangeLine(screen, rangeLine(screen, model.today, toybox::kUiFont), body.x, column.top(range), toybox::kUiFont);
  if (state >= 0) textAt(screen, body.x, column.top(state), body.width, toybox::kSmallFont, status.c_str());
  if (rule < 0) return column.top(place);
  hairline(screen, body.x, column.top(rule), body.width);

  // One scale for the whole week, today included, so every bar is comparable.
  int lo = 0, hi = 0;
  bool any = false;
  const auto take = [&lo, &hi, &any](const DayRow& d) {
    if (d.hasLow) {
      lo = any ? (d.low < lo ? d.low : lo) : d.low;
      hi = any ? (d.low > hi ? d.low : hi) : d.low;
      any = true;
    }
    if (d.hasHigh) {
      lo = any ? (d.high < lo ? d.high : lo) : d.high;
      hi = any ? (d.high > hi ? d.high : hi) : d.high;
      any = true;
    }
  };
  take(model.today);
  for (int i = 0; i < days; ++i) take(model.days[i]);
  if (hi == lo) ++hi;

  // The week: text on one baseline per row, skies and bars centred on its caps.
  const WeekColumns cols = weekColumns(screen, model, body.x, right);
  const int16_t uiCap = toybox::inkHeight(target, toybox::kUiFont);
  const int16_t smallCap = toybox::inkHeight(target, toybox::kSmallFont);
  for (int i = 0; i < days; ++i) {
    const DayRow& day = model.days[i];
    const int16_t top = column.top(rowsAt[i]);
    const int16_t mid = static_cast<int16_t>(top + rowBand / 2);
    const int16_t baseline = static_cast<int16_t>(top + uiCap);
    textAt(screen, cols.nameX, static_cast<int16_t>(baseline - uiCap), widthOf(screen, toybox::kUiFont, day.name),
           toybox::kUiFont, day.name);
    iconCentred(screen, skyBitmap(day.icon, 32), static_cast<int16_t>(cols.skyX + cols.skyW / 2), mid);
    const std::string rain = rainLabel(day.rain);
    if (!rain.empty()) {
      textAt(screen, cols.rainX, static_cast<int16_t>(baseline - smallCap),
             widthOf(screen, toybox::kSmallFont, rain.c_str()), toybox::kSmallFont, rain.c_str());
    }
    const std::string low = lowText(day);
    const std::string high = highText(day);
    tempAt(screen, static_cast<int16_t>(cols.lowRight - tempWidth(screen, toybox::kUiFont, low)),
           static_cast<int16_t>(baseline - uiCap), toybox::kUiFont, low);
    tempAt(screen, static_cast<int16_t>(cols.highRight - tempWidth(screen, toybox::kUiFont, high)),
           static_cast<int16_t>(baseline - uiCap), toybox::kUiFont, high);
    if (cols.barW <= 0) continue;
    screen.target().fill(fui::makeRect(cols.barX, static_cast<int16_t>(mid - 3), cols.barW, 6),
                         fui::Paint::dither(fui::Color::LightGray));
    if (day.hasLow && day.hasHigh) {
      const int16_t x0 = static_cast<int16_t>(cols.barX + static_cast<long>(cols.barW) * (day.low - lo) / (hi - lo));
      const int16_t x1 = static_cast<int16_t>(cols.barX + static_cast<long>(cols.barW) * (day.high - lo) / (hi - lo));
      screen.target().fill(fui::makeRect(x0, static_cast<int16_t>(mid - 6), max16(8, x1 - x0), 12),
                           fui::Paint::solid(fui::Color::Black), 6);
    }
  }
  return column.top(place);
}

// --- Search ----------------------------------------------------------------------

int16_t searchRowHeight(toybox::Screen& screen) {
  return static_cast<int16_t>(lineOf(screen, toybox::kBodyFont) + lineOf(screen, toybox::kSmallFont) + 8);
}

}  // namespace

// --- Main --------------------------------------------------------------------------

MainPlace buildMainTop(toybox::Screen& screen, const MainModel& model) {
  band(screen, "WEATHER", ActionMenu, nullptr, false);
  const fui::Rect body = screen.body();
  MainPlace place;
  place.style = ink(toybox::kBodyFont, fui::TextAlign::Left, 2);
  place.text = toybox::fittedTitle(screen.target(), model.place, body.width, place.style);
  const int16_t height = fui::measureWrappedText(screen.target(), place.text.c_str(), place.style, body.width).height;
  const int16_t line = screen.target().lineHeight(place.style.font);
  place.lines = line > 0 ? max16(1, height / line) : 1;
  place.inkHeight = toybox::inkHeight(screen.target(), place.style.font, place.lines);
  return place;
}

int16_t buildMainBody(toybox::Screen& screen, const MainModel& model, const MainPlace& place) {
  if (!model.hasForecast) return emptyBlock(screen, model, place.inkHeight);
  return mainBody(screen, model, place.inkHeight);
}

void drawMainPlace(toybox::Screen& screen, const MainPlace& place, const int16_t inkTop) {
  const fui::Rect body = screen.body();
  screen.target().text(toybox::inkRect(screen.target(), place.style.font, body.x, inkTop, body.width, place.lines),
                       place.text.c_str(), place.style);
}

// --- Search ---------------------------------------------------------------------------

int buildSearch(toybox::Screen& screen, const SearchModel& model) {
  band(screen, model.firstRun ? "CHOOSE A CITY" : "CHANGE CITY", fui::NO_ACTION, nullptr);
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.y + body.height - model.keyboardHeight);
  const int16_t bodyLine = lineOf(screen, toybox::kBodyFont);
  const int16_t smallLine = lineOf(screen, toybox::kSmallFont);

  // The field: an outlined box with what has been typed, and a caret while
  // the keyboard is up. Tapping it raises the keyboard.
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const fui::Rect field = fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(bodyLine + 16));
  screen.target().stroke(field, fui::Paint::solid(fui::Color::Black), 2, toybox::kPillRadius / 2);
  const int16_t textX = static_cast<int16_t>(field.x + 14);
  const int16_t textW = static_cast<int16_t>(field.width - 28);
  if (empty && model.keyboardHeight == 0) {
    screen.target().text(fui::makeRect(textX, field.y, textW, field.height), "Tap here to type a city",
                         ink(toybox::kSmallFont));
  } else {
    fui::TextStyle typed = ink(toybox::kBodyFont);
    const std::string shown = toybox::fittedTitle(screen.target(), model.query, static_cast<int16_t>(textW - 8), typed);
    const int16_t typedW = screen.target().measureText(typed.font, shown.c_str(), typed).width;
    screen.target().text(fui::makeRect(textX, field.y, static_cast<int16_t>(typedW + 1), field.height), shown.c_str(),
                         typed);
    if (model.keyboardHeight > 0) {
      const Cap cap = capOf(screen, typed.font);
      const int16_t lineTop = static_cast<int16_t>(field.y + max16(0, (field.height - cap.line) / 2));
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(textX + typedW + 3), static_cast<int16_t>(lineTop + cap.top - 2), 3,
                        static_cast<int16_t>(cap.height + 6)),
          fui::Paint::solid(fui::Color::Black));
    }
  }
  screen.frame().hit(field, ActionField);

  int16_t y = static_cast<int16_t>(field.y + field.height + kGutter);
  const int16_t width = body.width;
  const auto sentence = [&screen, &y, width, body](const char* text, const fui::FontId font) {
    fui::TextStyle style = ink(font, fui::TextAlign::Left, 3);
    const int16_t h = fui::measureWrappedText(screen.target(), text, style, width).height;
    screen.target().text(fui::makeRect(body.x, y, width, h), text, style);
    y = static_cast<int16_t>(y + h + kGutter / 2);
  };

  switch (model.state) {
    case SearchModel::State::Typing:
      sentence(model.firstRun ? "Which city is the forecast for? Type at least two letters of its name, then GO."
                              : "Type at least two letters of a city, then GO.",
               toybox::kSmallFont);
      return 0;
    case SearchModel::State::Searching:
      sentence("Searching...", toybox::kBodyFont);
      return 0;
    case SearchModel::State::NoMatch: {
      std::string head = std::string("No place called \"") + model.query + "\".";
      sentence(head.c_str(), toybox::kBodyFont);
      sentence("Check the spelling, or try a larger town nearby.", toybox::kSmallFont);
      return 0;
    }
    case SearchModel::State::Failed:
      sentence("Could not search.", toybox::kBodyFont);
      sentence(model.failure, toybox::kSmallFont);
      return 0;
    case SearchModel::State::Results:
      break;
  }

  const int16_t rowH = searchRowHeight(screen);
  const int16_t moreH = static_cast<int16_t>(smallLine + kGutter);
  int shown = 0;
  for (int i = 0; i < model.rowCount; ++i) {
    const bool last = i + 1 == model.rowCount;
    // The announcement of what did not fit is reserved before the row that
    // would take its place.
    if (y + rowH + (last ? 0 : moreH) > bottom) break;
    const fui::Rect row = fui::makeRect(body.x, y, body.width, rowH);
    fui::TextStyle name = ink(toybox::kBodyFont);
    const std::string fittedName = toybox::fittedTitle(screen.target(), model.rows[i].name, row.width, name);
    screen.target().text(fui::makeRect(row.x, static_cast<int16_t>(row.y + 2), row.width, bodyLine), fittedName.c_str(),
                         name);
    if (model.rows[i].region[0]) {
      fui::TextStyle region = ink(toybox::kSmallFont);
      const std::string fittedRegion = toybox::fittedTitle(screen.target(), model.rows[i].region, row.width, region);
      screen.target().text(fui::makeRect(row.x, static_cast<int16_t>(row.y + 2 + bodyLine), row.width, smallLine),
                           fittedRegion.c_str(), region);
    }
    hairline(screen, row.x, static_cast<int16_t>(row.y + row.height - 1), row.width);
    screen.frame().hit(row, ActionMatch, static_cast<int16_t>(i));
    y = static_cast<int16_t>(y + rowH);
    ++shown;
  }
  if (shown < model.rowCount) {
    y = static_cast<int16_t>(y + kGutter / 2);
    sentence("Not here? Type more of the name.", toybox::kSmallFont);
  }
  return shown;
}

// --- Menu ------------------------------------------------------------------------------

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  band(screen, "WEATHER", ActionCloseMenu, "DONE");
  const fui::Rect body = screen.body();
  const int16_t smallLine = lineOf(screen, toybox::kSmallFont);
  const int16_t bodyLine = lineOf(screen, toybox::kBodyFont);
  int16_t y = body.y;

  const auto caption = [&screen, &y, body, smallLine](const char* text) {
    screen.target().text(fui::makeRect(body.x, y, body.width, smallLine), text, ink(toybox::kSmallFont));
    y = static_cast<int16_t>(y + smallLine + 4);
  };

  // The city: the whole row is the door back to the search.
  caption("CITY");
  {
    const int16_t rowH = static_cast<int16_t>(bodyLine + 20);
    const fui::Rect row = fui::makeRect(body.x, y, body.width, rowH);
    screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), toybox::kHairline, toybox::kPillRadius / 2);
    const int16_t chevronX = static_cast<int16_t>(row.x + row.width - kGutter - 32);
    fui::TextStyle name = ink(toybox::kBodyFont);
    const int16_t nameW = static_cast<int16_t>(chevronX - row.x - kGutter * 2);
    const std::string fitted = toybox::fittedTitle(screen.target(), model.place, nameW, name);
    screen.target().text(fui::makeRect(static_cast<int16_t>(row.x + kGutter), row.y, nameW, row.height), fitted.c_str(),
                         name);
    blit(screen, icon_opens_32, chevronX, static_cast<int16_t>(row.y + (row.height - 32) / 2));
    screen.frame().hit(row, ActionChangeCity);
    y = static_cast<int16_t>(y + rowH + kGutter * 2);
  }

  // Celsius or Fahrenheit: two segments, the one in force filled and inert.
  caption("UNITS");
  {
    const int16_t half = static_cast<int16_t>((body.width - kGutter) / 2);
    struct Segment {
      const char* label;
      fui::ActionId action;
      bool on;
    };
    const Segment segments[] = {{"CELSIUS", ActionCelsius, !model.fahrenheit},
                                {"FAHRENHEIT", ActionFahrenheit, model.fahrenheit}};
    for (int i = 0; i < 2; ++i) {
      fui::ButtonProps button;
      button.label = segments[i].label;
      button.action = segments[i].on ? fui::NO_ACTION : segments[i].action;
      button.styles = segments[i].on ? toybox::invertedStyles() : toybox::rowStyles();
      button.text = toybox::buttonText(screen.theme());
      button.radius = toybox::kPillRadius / 2;
      screen.button(button, fui::makeRect(static_cast<int16_t>(body.x + i * (half + kGutter)), y,
                                          i == 0 ? half : static_cast<int16_t>(body.width - half - kGutter),
                                          toybox::kPillHeight));
    }
    y = static_cast<int16_t>(y + toybox::kPillHeight + kGutter * 2);
  }

  // A refresh, and when the forecast was last read.
  caption("FORECAST");
  {
    fui::ButtonProps refresh;
    refresh.label = "REFRESH NOW";
    refresh.action = ActionRefresh;
    refresh.styles = toybox::rowStyles();
    refresh.text = toybox::buttonText(screen.theme());
    refresh.radius = toybox::kPillRadius / 2;
    screen.button(refresh, fui::makeRect(body.x, y, body.width, toybox::kPillHeight));
    y = static_cast<int16_t>(y + toybox::kPillHeight + 6);
    if (model.updated[0]) {
      screen.target().text(fui::makeRect(body.x, y, body.width, smallLine), model.updated, ink(toybox::kSmallFont));
    }
  }
}

// --- Skies ---------------------------------------------------------------------------------

const freeink::Icon& skyBitmap(const weather::Icon icon, const int size) {
  // One row per sky, one column per generated size: 32 for the rows, 88 for
  // the reading, whose ink then fits inside the number's capitals.
  static const freeink::Icon* const kTable[][2] = {
      {&icon_wx_sun_32, &icon_wx_sun_88},
      {&icon_wx_moon_32, &icon_wx_moon_88},
      {&icon_wx_cloud_sun_32, &icon_wx_cloud_sun_88},
      {&icon_wx_cloud_moon_32, &icon_wx_cloud_moon_88},
      {&icon_wx_cloud_32, &icon_wx_cloud_88},
      {&icon_wx_fog_32, &icon_wx_fog_88},
      {&icon_wx_drizzle_32, &icon_wx_drizzle_88},
      {&icon_wx_rain_32, &icon_wx_rain_88},
      {&icon_wx_snow_32, &icon_wx_snow_88},
      {&icon_wx_thunder_32, &icon_wx_thunder_88},
  };
  static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(weather::Icon::Thunder) + 1,
                "one row per weather::Icon");
  return *kTable[static_cast<int>(icon)][size >= 88 ? 1 : 0];
}

}  // namespace weatherui
