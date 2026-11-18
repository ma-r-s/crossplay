#include "WeatherScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"
#include "WeatherIcons.h"

namespace weatherui {
namespace {

constexpr int16_t kMargin = toybox::kMargin;
constexpr int16_t kGutter = toybox::kGutter;
constexpr const char* kSource = "WEATHER BY OPEN-METEO.COM";

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

// The same, placed by its baseline, so two cuts in one row share one.
void drawTempOnBaseline(toybox::Screen& screen, const int16_t x, const int16_t baseline, const fui::FontId font,
                        const std::string& text, const fui::Color color = fui::Color::Black) {
  const Cap cap = capOf(screen, font);
  drawTemp(screen, x, static_cast<int16_t>(baseline - cap.top - cap.height), font, text, color);
}

std::string number(const int value) { return std::to_string(value); }
std::string highText(const DayRow& day) { return day.hasHigh ? number(day.high) : std::string("--"); }
std::string lowText(const DayRow& day) { return day.hasLow ? number(day.low) : std::string("--"); }
std::string rainLabel(const int rain) { return rain < 0 ? std::string() : number(rain) + "%"; }

// A line of text placed by its baseline.
void textOnBaseline(toybox::Screen& screen, const int16_t x, const int16_t baseline, const fui::FontId font,
                    const char* text, const fui::Color color = fui::Color::Black) {
  const Cap cap = capOf(screen, font);
  const int16_t width = widthOf(screen, font, text);
  screen.target().text(fui::makeRect(x, static_cast<int16_t>(baseline - cap.top - cap.height),
                                     static_cast<int16_t>(width + 1), cap.line),
                       text, ink(font, fui::TextAlign::Left, 1, color));
}

void blit(toybox::Screen& screen, const freeink::Icon& icon, const int16_t x, const int16_t y,
          const fui::Color color = fui::Color::Black) {
  screen.target().bitmap(fui::makeRect(x, y, static_cast<int16_t>(icon.w), static_cast<int16_t>(icon.h)),
                         fui::bitmapFromIcon(icon), fui::BitmapMode::Contain, fui::Paint::solid(color));
}

void hairline(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t width) {
  screen.target().fill(fui::makeRect(x, y, width, toybox::kHairline), fui::Paint::solid(fui::Color::Black));
}

// One line fitted to `width` in the largest cut that holds it whole, wrapping
// to `lines` before anything is cut. Returns the height it took.
int16_t fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
                   const fui::TextAlign align, const uint8_t lines = 1, const fui::Color color = fui::Color::Black) {
  fui::TextStyle style = ink(font, align, lines, color);
  const std::string fitted = toybox::fittedTitle(screen.target(), text, box.width, style);
  const int16_t height = fui::measureWrappedText(screen.target(), fitted.c_str(), style, box.width).height;
  screen.target().text(fui::makeRect(box.x, box.y, box.width, height), fitted.c_str(), style);
  return height;
}

// --- The band ------------------------------------------------------------------

void band(toybox::Screen& screen, const char* title, const fui::ActionId trailing, const char* trailingLabel) {
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
  screen.insetContent(fui::Insets{static_cast<int16_t>(kGutter * 3), kMargin, kMargin, kMargin});
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

// The forecast's source, at the foot of the page. Open-Meteo asks for it
// (CC BY 4.0), and it is the one place the screen says where the numbers came
// from.
void sourceLine(toybox::Screen& screen) {
  const fui::Rect foot = screen.takeBottom(lineOf(screen, toybox::kSmallFont), kGutter);
  screen.target().text(foot, kSource, ink(toybox::kSmallFont, fui::TextAlign::Left));
}

// No forecast to draw: a headline, a sentence, and when it helps a way to try
// again. Laid out as one block and centred as one.
void emptyBlock(toybox::Screen& screen, const MainModel& model) {
  sourceLine(screen);
  const fui::Rect body = screen.body();
  fui::TextStyle head = ink(toybox::kUiFont, fui::TextAlign::Center, 2);
  fui::TextStyle message = ink(toybox::kSmallFont, fui::TextAlign::Center, 4);
  const std::string status = statusLine(model);
  const int16_t headH = fui::measureWrappedText(screen.target(), model.emptyHeadline, head, body.width).height;
  const int16_t messageH =
      model.emptyMessage[0] ? fui::measureWrappedText(screen.target(), model.emptyMessage, message, body.width).height
                            : 0;
  const int16_t statusH = status.empty() ? 0 : lineOf(screen, toybox::kSmallFont);
  const int16_t buttonH = model.offerRetry ? toybox::kPillHeight : 0;
  const int16_t total = static_cast<int16_t>(headH + (messageH ? kGutter + messageH : 0) +
                                             (statusH ? kGutter + statusH : 0) + (buttonH ? kGutter * 2 + buttonH : 0));
  int16_t y = static_cast<int16_t>(body.y + max16(0, (body.height - total) / 2));
  screen.target().text(fui::makeRect(body.x, y, body.width, headH), model.emptyHeadline, head);
  y = static_cast<int16_t>(y + headH);
  if (messageH) {
    y = static_cast<int16_t>(y + kGutter);
    screen.target().text(fui::makeRect(body.x, y, body.width, messageH), model.emptyMessage, message);
    y = static_cast<int16_t>(y + messageH);
  }
  if (statusH) {
    y = static_cast<int16_t>(y + kGutter);
    screen.target().text(fui::makeRect(body.x, y, body.width, statusH), status.c_str(),
                         ink(toybox::kSmallFont, fui::TextAlign::Center));
    y = static_cast<int16_t>(y + statusH);
  }
  if (buttonH) {
    y = static_cast<int16_t>(y + kGutter * 2);
    const int16_t width = static_cast<int16_t>(body.width / 2);
    fui::ButtonProps retry;
    retry.label = "TRY AGAIN";
    retry.action = ActionRetry;
    retry.text = toybox::buttonText(screen.theme());
    retry.radius = toybox::kPillRadius;
    screen.button(retry, fui::makeRect(static_cast<int16_t>(body.x + (body.width - width) / 2), y, width, buttonH));
  }
}

// "H 9  L 3  RAIN 20%" (with rings), the pieces measured once so a line can be
// placed left, centred or inside a card from the same arithmetic.
struct RangeLine {
  std::string high;
  std::string low;
  std::string rain;
  int16_t hWidth = 0;
  int16_t lWidth = 0;
  int16_t highWidth = 0;
  int16_t lowWidth = 0;
  int16_t rainWidth = 0;
  int16_t space = 0;
  int16_t width = 0;
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
  line.rainWidth = line.rain.empty() ? 0 : widthOf(screen, font, line.rain.c_str());
  line.space = static_cast<int16_t>(kGutter * 2);
  line.width = static_cast<int16_t>(line.hWidth + line.highWidth + line.space + line.lWidth + line.lowWidth +
                                    (line.rain.empty() ? 0 : line.space + line.rainWidth));
  return line;
}

void drawRangeLine(toybox::Screen& screen, const RangeLine& line, int16_t x, const int16_t lineTop,
                   const fui::FontId font, const fui::Color color = fui::Color::Black) {
  const int16_t height = lineOf(screen, font);
  const fui::TextStyle style = ink(font, fui::TextAlign::Left, 1, color);
  screen.target().text(fui::makeRect(x, lineTop, widthOf(screen, font, "H"), height), "H", style);
  x = static_cast<int16_t>(x + line.hWidth);
  drawTemp(screen, x, lineTop, font, line.high, color);
  x = static_cast<int16_t>(x + line.highWidth + line.space);
  screen.target().text(fui::makeRect(x, lineTop, widthOf(screen, font, "L"), height), "L", style);
  x = static_cast<int16_t>(x + line.lWidth);
  drawTemp(screen, x, lineTop, font, line.low, color);
  x = static_cast<int16_t>(x + line.lowWidth + line.space);
  if (!line.rain.empty()) {
    screen.target().text(fui::makeRect(x, lineTop, static_cast<int16_t>(line.rainWidth + 1), height), line.rain.c_str(),
                         style);
  }
}

// The big number's line-box top that centres its capitals in [top, top + h).
int16_t bigLineTop(toybox::Screen& screen, const int16_t top, const int16_t height) {
  const Cap cap = capOf(screen, toybox::kDisplayFont);
  return static_cast<int16_t>(top + (height - cap.height) / 2 - cap.top);
}

std::string nowText(const MainModel& model) { return model.hasNow ? number(model.now) : std::string("--"); }

// The widest day name this week, measured: the sky column starts after it, so
// a wide MON or WED never runs into its own icon.
int16_t nameColumn(toybox::Screen& screen, const MainModel& model) {
  int16_t widest = 0;
  for (int i = 0; i < model.dayCount && i < kRows; ++i) {
    widest = max16(widest, widthOf(screen, toybox::kUiFont, model.days[i].name));
  }
  return widest;
}

// --- The reading and the week ------------------------------------------------------
// The reading centred, and each day as a bar from its low to its high on one
// scale for the week, so a cold snap is a shape rather than six numbers: the
// ornament is the forecast itself (docs/design-language.md, "Ornament").

void mainBody(toybox::Screen& screen, const MainModel& model) {
  sourceLine(screen);
  const fui::Rect body = screen.body();
  const int16_t right = static_cast<int16_t>(body.x + body.width);
  const int16_t centre = static_cast<int16_t>(body.x + body.width / 2);
  constexpr int16_t kHero = 96;
  int16_t y = body.y;

  const std::string now = nowText(model);
  const int16_t heroWidth = static_cast<int16_t>(96 + kGutter * 2 + tempWidth(screen, toybox::kDisplayFont, now));
  const int16_t heroX = static_cast<int16_t>(centre - heroWidth / 2);
  blit(screen, skyBitmap(model.nowIcon, 96), heroX, y);
  drawTemp(screen, static_cast<int16_t>(heroX + 96 + kGutter * 2), bigLineTop(screen, y, kHero), toybox::kDisplayFont,
           now);
  y = static_cast<int16_t>(y + kHero + kGutter / 2);

  y = static_cast<int16_t>(y + fittedLine(screen, fui::makeRect(body.x, y, body.width, 0), model.sky, toybox::kUiFont,
                                          fui::TextAlign::Center));
  const RangeLine range = rangeLine(screen, model.today, toybox::kUiFont);
  drawRangeLine(screen, range, static_cast<int16_t>(centre - range.width / 2), y, toybox::kUiFont);
  y = static_cast<int16_t>(y + lineOf(screen, toybox::kUiFont));
  const std::string status = statusLine(model);
  screen.target().text(fui::makeRect(body.x, y, body.width, lineOf(screen, toybox::kSmallFont)), status.c_str(),
                       ink(toybox::kSmallFont, fui::TextAlign::Center));
  y = static_cast<int16_t>(y + lineOf(screen, toybox::kSmallFont) + kGutter);
  hairline(screen, body.x, y, body.width);
  y = static_cast<int16_t>(y + toybox::kHairline + kGutter / 2);

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
  for (int i = 0; i < model.dayCount && i < kRows; ++i) take(model.days[i]);
  if (hi == lo) ++hi;

  const int16_t rowH = static_cast<int16_t>((body.y + body.height - y) / kRows);
  const Cap ui = capOf(screen, toybox::kUiFont);
  const int16_t iconX = static_cast<int16_t>(body.x + nameColumn(screen, model) + kGutter / 2);
  // The low column starts after the widest rain label this screen can draw.
  const int16_t lowRight = static_cast<int16_t>(iconX + 32 + kGutter / 2 + widthOf(screen, toybox::kSmallFont, "100%") +
                                                kGutter + tempWidth(screen, toybox::kUiFont, "-88"));
  const int16_t highWidth = tempWidth(screen, toybox::kUiFont, "-88");
  const int16_t barX = static_cast<int16_t>(lowRight + 10);
  const int16_t barW = static_cast<int16_t>(right - highWidth - 10 - barX);
  for (int i = 0; i < model.dayCount && i < kRows; ++i) {
    const DayRow& day = model.days[i];
    const int16_t top = static_cast<int16_t>(y + i * rowH);
    const int16_t baseline = static_cast<int16_t>(top + (rowH + ui.height) / 2);
    const int16_t mid = static_cast<int16_t>(top + rowH / 2);
    textOnBaseline(screen, body.x, baseline, toybox::kUiFont, day.name);
    blit(screen, skyBitmap(day.icon, 32), iconX, static_cast<int16_t>(mid - 16));
    const std::string rain = rainLabel(day.rain);
    if (!rain.empty())
      textOnBaseline(screen, static_cast<int16_t>(iconX + 32 + kGutter / 2), baseline, toybox::kSmallFont,
                     rain.c_str());
    const std::string low = lowText(day);
    const std::string high = highText(day);
    drawTempOnBaseline(screen, static_cast<int16_t>(lowRight - tempWidth(screen, toybox::kUiFont, low)), baseline,
                       toybox::kUiFont, low);
    drawTempOnBaseline(screen, static_cast<int16_t>(right - highWidth), baseline, toybox::kUiFont, high);
    screen.target().fill(fui::makeRect(barX, static_cast<int16_t>(mid - 3), barW, 6),
                         fui::Paint::dither(fui::Color::LightGray));
    if (day.hasLow && day.hasHigh) {
      const int16_t x0 = static_cast<int16_t>(barX + static_cast<long>(barW) * (day.low - lo) / (hi - lo));
      const int16_t x1 = static_cast<int16_t>(barX + static_cast<long>(barW) * (day.high - lo) / (hi - lo));
      screen.target().fill(fui::makeRect(x0, static_cast<int16_t>(mid - 6), max16(8, x1 - x0), 12),
                           fui::Paint::solid(fui::Color::Black), 6);
    }
  }
}

// --- Search ----------------------------------------------------------------------

int16_t searchRowHeight(toybox::Screen& screen) {
  return static_cast<int16_t>(lineOf(screen, toybox::kBodyFont) + lineOf(screen, toybox::kSmallFont) + 8);
}

}  // namespace

// --- Main --------------------------------------------------------------------------

void buildMainTop(toybox::Screen& screen, const MainModel& model) {
  band(screen, "WEATHER", ActionMenu, nullptr);
  const fui::Rect body = screen.body();
  // Centred, over the centred reading it names.
  fui::TextStyle name = ink(toybox::kBodyFont, fui::TextAlign::Center, 2);
  const std::string fitted = toybox::fittedTitle(screen.target(), model.place, body.width, name);
  const int16_t height = fui::measureWrappedText(screen.target(), fitted.c_str(), name, body.width).height;
  const fui::Rect line = screen.takeTop(height, kGutter);
  screen.target().text(line, fitted.c_str(), name);
}

void buildMainBody(toybox::Screen& screen, const MainModel& model) {
  if (!model.hasForecast) {
    emptyBlock(screen, model);
    return;
  }
  mainBody(screen, model);
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

  // Where the numbers come from, at the foot: Open-Meteo asks to be named,
  // and the second line is the promise the app keeps about the battery.
  {
    fui::TextStyle source = ink(toybox::kSmallFont, fui::TextAlign::Left, 4);
    const char* const text =
        "WEATHER DATA BY OPEN-METEO.COM, UNDER CC BY 4.0. READ ONLY WHEN THIS APP OPENS OR YOU REFRESH, NEVER IN THE "
        "BACKGROUND.";
    const int16_t h = fui::measureWrappedText(screen.target(), text, source, body.width).height;
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + body.height - h), body.width, h), text,
                         source);
  }
}

// --- Skies ---------------------------------------------------------------------------------

const freeink::Icon& skyBitmap(const weather::Icon icon, const int size) {
  // One row per sky, one column per generated size: 32 for the rows, 96 for
  // the reading.
  static const freeink::Icon* const kTable[][2] = {
      {&icon_wx_sun_32, &icon_wx_sun_96},
      {&icon_wx_moon_32, &icon_wx_moon_96},
      {&icon_wx_cloud_sun_32, &icon_wx_cloud_sun_96},
      {&icon_wx_cloud_moon_32, &icon_wx_cloud_moon_96},
      {&icon_wx_cloud_32, &icon_wx_cloud_96},
      {&icon_wx_fog_32, &icon_wx_fog_96},
      {&icon_wx_drizzle_32, &icon_wx_drizzle_96},
      {&icon_wx_rain_32, &icon_wx_rain_96},
      {&icon_wx_snow_32, &icon_wx_snow_96},
      {&icon_wx_thunder_32, &icon_wx_thunder_96},
  };
  static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(weather::Icon::Thunder) + 1,
                "one row per weather::Icon");
  return *kTable[static_cast<int>(icon)][size >= 96 ? 1 : 0];
}

}  // namespace weatherui
