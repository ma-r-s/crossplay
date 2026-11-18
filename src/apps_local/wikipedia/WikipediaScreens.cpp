#include "WikipediaScreens.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "../ui/ToyboxIcons.h"

// The home is "reading first", chosen from three rendered arrangements
// (docs/apps/wikipedia-plan.md, "After the second critic"): the keyboard rises
// on a tap of the field, CONTINUE is a filled card and the loudest thing on
// the page, RANDOM an outlined bar, RECENT a list, and the article's foot
// carries a progress rule.

namespace wikiui {
namespace {

constexpr int16_t kMargin = 16;
// A row is a thumb's height at least; a title that wraps makes its row taller.
constexpr int16_t kRowMin = 48;
constexpr int16_t kRowPad = 6;
// As many lines as a title takes. Three were said to hold any title, and the
// 100-byte "Borat - Kulturelle Lernung von Amerika, ..." needs four at the
// reading cut (titles run to 255 bytes); this is the wrap's own ceiling.
constexpr int16_t kMaxTitleLines = 16;
constexpr int16_t kFieldHeight = 52;
constexpr int16_t kCaptionHeight = 28;
// The footer's words sit kFooterPad under the page; under them the progress
// track and its band take kFooterBar, and the band ends kMargin above the
// glass. The footer is as tall as the small cut's line box needs, never a
// constant the cut can outgrow.
constexpr int16_t kFooterPad = 6;
constexpr int16_t kFooterBar = 8;  // the track 5 under the words, the band 2 above it and 3 below
// The count line at the foot of the home: a hairline, then the words.
constexpr int16_t kCountLine = 36;
// The line under the count, "TAP HERE FOR A NEWER PACK": the count and a
// door in one line did not fit the small cut on the panel ("GET NE...").
constexpr int16_t kDoorLine = 30;

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = colour;
  return style;
}

// All-caps chrome, ink-centred on the cap band.
void drawLabel(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
               const fui::TextAlign align, const toybox::CutMetrics& cut, const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cut), text, textStyle(font, align, colour));
}

// One line placed by its box. Only for text that is the reader's own (the
// query they typed) or a hint (the footer's section): titles go through
// fitTitle, which never cuts.
void drawLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
              const fui::TextAlign align = fui::TextAlign::Left, const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style = textStyle(font, align, colour);
  style.maxLines = 1;
  screen.target().text(box, text, style);
}

// Mixed-case prose, as many lines as the box holds.
void drawProse(toybox::Screen& screen, const fui::Rect& box, const char* text,
               const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style = textStyle(toybox::kBodyFont, align);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const int lines = lineHeight > 0 ? box.height / lineHeight : 1;
  style.maxLines = static_cast<uint8_t>(lines < 1 ? 1 : (lines > 16 ? 16 : lines));
  screen.target().text(box, text, style);
}

// U+00AD, the soft hyphen: where the activity says a word may break.
constexpr char kShy0 = '\xC2';
constexpr char kShy1 = '\xAD';

std::string withoutSoftHyphens(const char* text) {
  std::string out;
  for (const char* p = text ? text : ""; *p; ++p) {
    if (p[0] == kShy0 && p[1] == kShy1) {
      ++p;
      continue;
    }
    out += *p;
  }
  return out;
}

// What joins a piece of a title to the one before it, and so what a break
// there costs: a space goes, a soft hyphen becomes "-", and after a hyphen or
// a slash already in the title nothing is added.
enum class Glue : uint8_t { None, Space, Shy, After };

struct Piece {
  std::string text;
  Glue before;
};

std::deque<Piece> titlePieces(const char* text) {
  std::deque<Piece> out;
  std::string cur;
  Glue glue = Glue::None;
  const auto flush = [&](const Glue next) {
    if (!cur.empty()) {
      out.push_back(Piece{cur, out.empty() ? Glue::None : glue});
      cur.clear();
      glue = next;
    } else if (next == Glue::Space) {
      glue = Glue::Space;
    }
  };
  for (const char* p = text ? text : ""; *p; ++p) {
    if (*p == ' ') {
      flush(Glue::Space);
      continue;
    }
    if (p[0] == kShy0 && p[1] == kShy1) {
      flush(Glue::Shy);
      ++p;
      continue;
    }
    cur += *p;
    if ((*p == '-' || *p == '/') && p[1] != '\0' && p[1] != ' ') flush(Glue::After);
  }
  flush(Glue::None);
  return out;
}

std::string joinPieces(const std::vector<Piece>& line, const Glue next) {
  std::string s;
  for (size_t i = 0; i < line.size(); ++i) {
    if (i > 0 && line[i].before == Glue::Space) s += ' ';
    s += line[i].text;
  }
  if (next == Glue::Shy) s += '-';
  return s;
}

}  // namespace

// The device and the component wrap at spaces only, and a word wider than the
// line they split between any two letters with nothing to say so
// ("Hochleistungsfluessigkeitschrom" over "atographie"). So the breaks are
// chosen here, where the pack's language is known through the soft hyphens
// (U+00AD) the activity put in at its hyphenation points, the same patterns
// the article's own text breaks with, and handed over as spaces: a break at
// a soft hyphen becomes "- ", a break after a hyphen or a slash in the title
// a space after it, and every other soft hyphen goes. A word with no break
// that fits is split between letters, with a hyphen. Greedy, so the wrap that
// draws the result breaks exactly where this did: every line it is given
// fits, and no line can take the next piece, which would also bring a space
// this measure did not count.
std::string breakTitle(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text,
                       const int16_t width) {
  const std::string plain = withoutSoftHyphens(text);
  const auto fits = [&](const std::string& run) {
    return target.measureText(style.font, run.c_str(), style).width <= width;
  };
  if (width <= 0 || fits(plain)) return plain;
  std::deque<Piece> todo = titlePieces(text);
  std::vector<std::string> lines;
  std::vector<Piece> line;
  // A piece alone wider than the line: the widest run of whole letters that
  // fits beside a hyphen, at least one, and the rest goes on.
  const auto splitLetters = [&](Piece piece) {
    size_t cut = 0;
    size_t next = 0;
    while (next < piece.text.size()) {
      size_t after = next + 1;
      while (after < piece.text.size() && (static_cast<uint8_t>(piece.text[after]) & 0xC0) == 0x80) ++after;
      if (cut > 0 && !fits(piece.text.substr(0, after) + "-")) break;
      cut = after;
      next = after;
    }
    lines.push_back(piece.text.substr(0, cut) + (cut < piece.text.size() ? "-" : ""));
    if (cut < piece.text.size()) todo.push_front(Piece{piece.text.substr(cut), Glue::Shy});
  };
  while (!todo.empty()) {
    Piece piece = todo.front();
    todo.pop_front();
    if (line.empty()) {
      if (fits(piece.text)) {
        line.push_back(piece);
      } else {
        splitLetters(piece);
      }
      continue;
    }
    line.push_back(piece);
    if (fits(joinPieces(line, Glue::None))) continue;
    line.pop_back();
    // Break before this piece; at a soft hyphen the line must hold its "-"
    // too, and when it cannot the break moves back a piece.
    std::vector<Piece> carried{piece};
    while (!fits(joinPieces(line, carried.front().before)) && line.size() > 1) {
      carried.insert(carried.begin(), line.back());
      line.pop_back();
    }
    const bool closes = fits(joinPieces(line, carried.front().before));
    if (closes) lines.push_back(joinPieces(line, carried.front().before));
    const Piece alone = line.front();
    line.clear();
    for (auto it = carried.rbegin(); it != carried.rend(); ++it) todo.push_front(*it);
    // One piece whose "-" does not fit beside it: split it between letters,
    // its rest going on before what was carried.
    if (!closes) splitLetters(alone);
  }
  if (!line.empty()) lines.push_back(joinPieces(line, Glue::None));
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) out += ' ';
    out += lines[i];
  }
  return out;
}

namespace {

void hairline(toybox::Screen& screen, const int16_t y) {
  const fui::Rect body = screen.body();
  screen.target().fill(fui::makeRect(static_cast<int16_t>(body.x + kMargin), y,
                                     static_cast<int16_t>(body.width - kMargin * 2), toybox::kHairline),
                       fui::Paint::solid(fui::Color::Black));
}

// A button on the band: paper ink and a paper outline on the band's own
// black. Left at the defaults it is painted black on black and simply is not
// there (Hacker News paid for this first; see its bandOutlineStyles).
fui::StyleSet bandOutlineStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::Black);
  styles.normal.foreground = fui::Paint::solid(fui::Color::White);
  styles.normal.border = fui::Paint::solid(fui::Color::White);
  styles.normal.borderWidth = toybox::kHairline;
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  return styles;
}

// The band's props: the title, the chevron that goes back, and a trailing
// button that is a word or an icon. Shared by chrome() and articleTitleWhole(),
// so the width a title is measured against is the width it gets.
fui::HeaderProps bandProps(toybox::Screen& screen, const char* title, const char* trailingLabel,
                           const fui::ActionId trailingAction, const fui::ActionId backAction,
                           const freeink::Icon* trailingIcon) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  header.sidePadding = screen.theme().headerSidePadding;
  if (trailingIcon) {
    // An icon, square like the chevron: a word in the reader's small cut took
    // 151 of the band's 448 and left a one-word title cut.
    header.trailingIcon = fui::bitmapFromIcon(*trailingIcon);
    header.trailingAction = trailingAction;
    header.trailingStyles = bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  if (backAction != 0) {
    header.leadingIcon = fui::bitmapFromIcon(icon_wiki_back_32);
    header.leadingAction = backAction;
    header.leadingStyles = bandOutlineStyles();
    header.leadingRadius = toybox::kPillRadius / 2;
  }
  if (trailingLabel) {
    header.trailingLabel = trailingLabel;
    header.trailingAction = trailingAction;
    header.trailingStyles = bandOutlineStyles();
    // The small slot, not the theme's smallText (which is the body slot): a
    // "CONTENTS" pill at the reading size took 128 of the band's 448 and left
    // the title 200.
    header.trailingText = screen.theme().smallText;
    header.trailingText.font = toybox::kSmallFont;
    header.trailingText.color = fui::Color::White;
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  return header;
}

// An article's title in the band, bold in the reader's face: one line at the
// reading size when it fits, otherwise two lines of the reader's 12 (the most
// the band's 66 visible rows hold; two at the reading size are 80), broken
// where its language breaks words. `whole` is false when even that cuts it,
// the running head's one permitted cut, and the page then keeps its own h1
// (WikipediaActivity::stageArticle asks articleTitleWhole()). On these
// screens the pill is set in the small reader cut rather than Jersey: the
// activity binds the reader's faces so the title has real cuts to step
// through, and three slots is all there are.
struct BandTitle {
  fui::TextStyle style;
  std::string text;
  bool whole;
};

BandTitle bandTitle(toybox::Screen& screen, const fui::HeaderProps& header) {
  BandTitle out{screen.theme().titleText, std::string(), true};
  out.style.align = screen.theme().headerTitleAlign;
  out.style.bold = true;
  const int16_t width = toybox::bandTitleWidth(screen, header);
  out.text = withoutSoftHyphens(header.title);
  if (screen.target().measureText(out.style.font, out.text.c_str(), out.style).width <= width) return out;
  out.style.font = toybox::kSmallFont;
  out.style.maxLines = 2;
  out.text = breakTitle(screen.target(), out.style, header.title, width);
  fui::TextStyle all = out.style;
  all.maxLines = 16;
  const int16_t lineHeight = screen.target().lineHeight(out.style.font);
  const int16_t height = fui::measureWrappedText(screen.target(), out.text.c_str(), all, width).height;
  out.whole = lineHeight > 0 && height <= lineHeight * out.style.maxLines;
  return out;
}

// The band. `reading` marks a band that carries an article's title (see
// bandTitle); the others are the app's own words, in Jersey.
void chrome(toybox::Screen& screen, const char* title, const char* trailingLabel = nullptr,
            const fui::ActionId trailingAction = 0, const fui::ActionId backAction = 0, const bool reading = false,
            const freeink::Icon* trailingIcon = nullptr) {
  fui::HeaderProps header = bandProps(screen, title, trailingLabel, trailingAction, backAction, trailingIcon);
  BandTitle band;
  if (reading) {
    band = bandTitle(screen, header);
    header.titleText = band.style;
    header.title = band.text.c_str();
  }
  toybox::headerBand(screen, header);
}

// A title is never elided: it takes one line when it fits, wraps otherwise,
// and its row grows to hold it.
struct TitleFit {
  int16_t lines;
  int16_t height;    // the row's, kRowMin at least
  std::string text;  // as drawn: broken for the width (breakTitle)
};

TitleFit fitTitle(toybox::Screen& screen, const int16_t width, const char* title, const fui::FontId font) {
  fui::TextStyle style = textStyle(font, fui::TextAlign::Left);
  const int16_t lineHeight = screen.target().lineHeight(font);
  std::string text = breakTitle(screen.target(), style, title, width);
  int16_t lines = 1;
  if (screen.target().measureText(font, text.c_str(), style).width > width) {
    style.maxLines = kMaxTitleLines;
    const fui::Size size = fui::measureWrappedText(screen.target(), text.c_str(), style, width);
    lines = lineHeight > 0 ? static_cast<int16_t>((size.height + lineHeight - 1) / lineHeight) : 1;
    if (lines < 1) lines = 1;
    if (lines > kMaxTitleLines) lines = kMaxTitleLines;
  }
  const int16_t height = static_cast<int16_t>(lines * lineHeight + kRowPad * 2);
  return TitleFit{lines, height < kRowMin ? kRowMin : height, std::move(text)};
}

void drawTitle(toybox::Screen& screen, const fui::Rect& box, const fui::FontId font, const TitleFit& fit,
               const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style = textStyle(font, fui::TextAlign::Left, colour);
  style.maxLines = static_cast<uint8_t>(fit.lines);
  const int16_t textHeight = static_cast<int16_t>(fit.lines * screen.target().lineHeight(font));
  const int16_t top = static_cast<int16_t>(box.y + (box.height > textHeight ? (box.height - textHeight) / 2 : 0));
  screen.target().text(fui::Rect{box.x, top, box.width, textHeight}, fit.text.c_str(), style);
}

// A row of a title list, its whole width tappable. Returns the row's height,
// or 0 when it would not fit above `bottom` and so was not drawn. A redirect
// shows its own name; opening it lands on the target.
int16_t drawTitleRow(toybox::Screen& screen, const int16_t y, const int16_t bottom, const char* title,
                     const fui::ActionId action, const int value, const bool rule,
                     const fui::FontId font = toybox::kBodyFont) {
  const fui::Rect body = screen.body();
  const TitleFit fit = fitTitle(screen, static_cast<int16_t>(body.width - kMargin * 2), title, font);
  if (y + fit.height > bottom) return 0;
  const fui::Rect row{body.x, y, body.width, fit.height};
  drawTitle(screen, row.inset(fui::Insets{0, kMargin, 0, kMargin}), font, fit);
  if (rule) hairline(screen, static_cast<int16_t>(row.bottom() - 1));
  screen.frame().hit(row, action, static_cast<int16_t>(value));
  return fit.height;
}

void drawCaption(toybox::Screen& screen, const fui::Rect& row, const char* text) {
  const fui::Rect box = row.inset(fui::Insets{0, kMargin, 0, kMargin});
  drawLabel(screen, box, text, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
}

// One wide action across a row, where a thumb rests.
void drawAction(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                const bool filled) {
  if (filled) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
  }
  drawLabel(screen, box, label, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut,
            filled ? fui::Color::White : fui::Color::Black);
  screen.frame().hit(box, action);
}

// The field under the band. Its prompt is in the app's own voice (Jersey
// caps) and what the reader types is in the reading cut, so an empty field
// and a typed one never look alike. Tapping it is how the keyboard rises in
// the variants that hide it. `clear` draws the X: it clears the query, and in
// those variants it also puts the keyboard down.
fui::Rect drawField(toybox::Screen& screen, const SearchModel& model, const bool clear) {
  const fui::Rect body = screen.body();
  // toybox::kGutter below the band's rule, which the chrome probe holds every
  // screen to; the field's stroke is the first ink under the band.
  const fui::Rect field{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + toybox::kGutter + 2),
                        static_cast<int16_t>(body.width - kMargin * 2), kFieldHeight};
  screen.target().stroke(field, fui::Paint::solid(fui::Color::Black), 2);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const int16_t right = static_cast<int16_t>(clear ? 52 + 4 : 12);
  const fui::Rect text = field.inset(fui::Insets{0, right, 0, 12});
  if (empty) {
    drawLabel(screen, text, "SEARCH WIKIPEDIA", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
  } else {
    drawLine(screen, text, model.query, toybox::kBodyFont);
  }
  screen.frame().hit(field, ActionField);
  if (clear) {
    // A boxed X at the field's right end, so it reads as a control and not as
    // a letter somebody typed; the whole end of the field is its target.
    const fui::Rect box{static_cast<int16_t>(field.right() - 44), static_cast<int16_t>(field.y + 8), 36,
                        static_cast<int16_t>(field.height - 16)};
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
    drawLabel(screen, box, "X", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(fui::Rect{static_cast<int16_t>(field.right() - 52), field.y, 52, field.height}, ActionClear);
  }
  return field;
}

// The matches under the field, each carrying its row.
void drawResults(toybox::Screen& screen, const SearchModel& model, int16_t y, const int16_t bottom) {
  const fui::Rect body = screen.body();
  if (model.resultCount == 0 && model.noMatch) {
    drawProse(screen,
              fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 12),
                        static_cast<int16_t>(body.width - kMargin * 2), 60},
              "No article with that name");
  }
  // The list looks complete when it is not ("Where did Tokyo go?"): when
  // there are more matches than the panel holds, the last line says so,
  // and its room comes off the rows rather than the other way round.
  const int16_t rows = static_cast<int16_t>(model.moreResults ? bottom - kCaptionHeight - 4 : bottom);
  int drawn = 0;
  for (int i = 0; i < model.resultCount && i < kMaxResults; ++i) {
    const int16_t h = drawTitleRow(screen, y, rows, model.results[i].title, ActionResult, i, true);
    if (h == 0) break;
    y = static_cast<int16_t>(y + h);
    ++drawn;
  }
  if ((model.moreResults || drawn < model.resultCount) && y + kCaptionHeight <= bottom) {
    drawCaption(screen, fui::Rect{body.x, static_cast<int16_t>(y + 4), body.width, kCaptionHeight},
                "KEEP TYPING FOR MORE");
  }
}

// The row that goes back to the install screen, when the pack is only partly here.
int16_t drawPartsRow(toybox::Screen& screen, const SearchModel& model, const int16_t y) {
  if (!model.partsLine) return y;
  const fui::Rect body = screen.body();
  const fui::Rect row{body.x, y, body.width, kRowMin};
  drawCaption(screen, row, model.partsLine);
  screen.frame().hit(row, ActionInstall);
  return static_cast<int16_t>(y + kRowMin);
}

// The recent trail: as many as fit above `bottom`. The activity leaves out the
// article you are in, which CONTINUE already names.
int16_t drawRecent(toybox::Screen& screen, const SearchModel& model, int16_t y, const int16_t bottom) {
  if (model.recentCount == 0 || y + kCaptionHeight + kRowMin > bottom) return y;
  const fui::Rect body = screen.body();
  drawCaption(screen, fui::Rect{body.x, y, body.width, kCaptionHeight}, "RECENT");
  y = static_cast<int16_t>(y + kCaptionHeight);
  // Divided, not underlined: a rule goes between two rows, never under the
  // last, which would sit a few pixels over the count line's own hairline and
  // read as a double line.
  for (int i = 0; i < model.recentCount && i < kMaxRecent; ++i) {
    const int16_t h = drawTitleRow(screen, y, bottom, model.recent[i].title, ActionRecent, i, false);
    if (h == 0) break;
    if (i > 0) hairline(screen, static_cast<int16_t>(y - 1));
    y = static_cast<int16_t>(y + h);
  }
  return y;
}

// What is on the card, in the app's own voice, on the last line above the
// keyboard or the glass, with a hairline so it reads as the page's foot and
// not as a key.
// The count line is also the way to a newer pack: a tap opens the install
// screen, and the page then copies only the parts that changed. With a
// complete pack the app had no way back to that screen at all.
// `door`: the hint line under the count. Not while the keyboard is up: the
// person is typing, the rows above are what they came for, and the line
// would cost the one recent row that still fits.
void drawCountLine(toybox::Screen& screen, const int16_t bottom, const char* text, const bool door) {
  const fui::Rect body = screen.body();
  const int16_t height = static_cast<int16_t>(door ? kCountLine + kDoorLine : kCountLine);
  const int16_t top = static_cast<int16_t>(bottom - height);
  hairline(screen, top);
  const fui::Rect row{body.x, top, body.width, height};
  drawLabel(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 4),
                      static_cast<int16_t>(body.width - kMargin * 2), static_cast<int16_t>(kCountLine - 8)},
            text, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  if (door) {
    drawLabel(screen,
              fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + kCountLine - 4),
                        static_cast<int16_t>(body.width - kMargin * 2), static_cast<int16_t>(kDoorLine - 4)},
              "TAP HERE FOR A NEWER PACK", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  }
  screen.frame().hit(row, ActionInstall);
}

int16_t footerHeight(toybox::Screen& screen) {
  const int16_t lineHeight = screen.target().lineHeight(toybox::kSmallFont);
  return static_cast<int16_t>(kFooterPad + lineHeight + kFooterBar + kMargin);
}

}  // namespace

// The home. Nothing typed: the doors (CONTINUE, RANDOM ARTICLE, the recent
// trail) above the count line, and above the keyboard when that is up. With
// a query: the matches.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, model.bandTitle ? model.bandTitle : "WIKIPEDIA", nullptr, 0,
         model.languagesBack ? ActionLanguages : 0);
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const fui::Rect field = drawField(screen, model, !empty || model.keyboardHeight > 0);
  int16_t y = static_cast<int16_t>(field.bottom() + toybox::kGutter);
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);

  if (!empty) {
    drawResults(screen, model, y, bottom);
    return;
  }
  // The count line is reserved first; the trail gets what is left.
  const bool door = model.keyboardHeight == 0;
  const int16_t list = static_cast<int16_t>(bottom - kCountLine - (door ? kDoorLine : 0));
  y = drawPartsRow(screen, model, y);
  {
    // The card: filled, the loudest thing on the page, when there is an
    // article to go back to. Dimmed, not gone, when there is none yet, so the
    // page keeps its shape from the first open on.
    const char* title = model.continueTitle ? model.continueTitle : "Open any article and it waits here.";
    const TitleFit fit = fitTitle(screen, static_cast<int16_t>(inner - 28), title, toybox::kBodyFont);
    const int16_t textHeight = static_cast<int16_t>(fit.lines * screen.target().lineHeight(toybox::kBodyFont));
    const fui::Rect card{static_cast<int16_t>(body.x + kMargin), y, inner,
                         static_cast<int16_t>(12 + 24 + 6 + textHeight + 16)};
    if (card.bottom() <= list) {
      const bool live = model.continueTitle != nullptr;
      const fui::Color ink = live ? fui::Color::White : fui::Color::Black;
      screen.target().fill(card,
                           live ? fui::Paint::solid(fui::Color::Black) : fui::Paint::dither(fui::Color::LightGray));
      drawLabel(screen, fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 12), 220, 24},
                "CONTINUE", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut, ink);
      if (live && model.continuePage > 0) {
        // Where it lands, so the card says what it does before it is pressed.
        char page[24];
        snprintf(page, sizeof(page), "PAGE %d", model.continuePage);
        drawLabel(screen,
                  fui::Rect{static_cast<int16_t>(card.right() - 14 - 160), static_cast<int16_t>(card.y + 12), 160, 24},
                  page, toybox::kSmallFont, fui::TextAlign::Right, toybox::kButtonCut, ink);
      }
      drawTitle(screen,
                fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 42),
                          static_cast<int16_t>(inner - 28), textHeight},
                toybox::kBodyFont, fit, ink);
      if (live) screen.frame().hit(card, ActionContinue);
      y = static_cast<int16_t>(card.bottom() + toybox::kGutter);
    }
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, inner, 52};
    if (box.bottom() <= list) drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, false);
    y = static_cast<int16_t>(box.bottom() + 20);
  }
  // The count line before the recent trail: the frame holds 24 targets and
  // registers the rest silently, so with the keyboard up the door to a newer
  // pack must be in before the rows that may not fit.
  drawCountLine(screen, bottom, model.footer, door);
  drawRecent(screen, model, y, list);
}

bool articleTitleWhole(toybox::Screen& screen, const ArticleChromeModel& model) {
  return bandTitle(screen, bandProps(screen, model.title, nullptr, ActionContents, model.back ? ActionPrevious : 0,
                                     model.contents ? &icon_wiki_contents_32 : nullptr))
      .whole;
}

fui::Rect buildArticleChrome(toybox::Screen& screen, const ArticleChromeModel& model) {
  chrome(screen, model.title, nullptr, ActionContents, model.back ? ActionPrevious : 0, true,
         model.contents ? &icon_wiki_contents_32 : nullptr);
  const fui::Rect body = screen.body();
  return fui::Rect{body.x, body.y, body.width, static_cast<int16_t>(body.height - footerHeight(screen))};
}

// The section the page is in at the left, the page at the right, as a book's
// running foot. The words are one line box tall, kFooterPad under the page;
// under them the progress rule, a hairline track and a solid band along it as
// far as the page has come once the total is known, and then the page's
// margin.
void buildArticleFooter(toybox::Screen& screen, const ArticleFooterModel& model) {
  const fui::Rect body = screen.body();
  const int16_t lineHeight = screen.target().lineHeight(toybox::kSmallFont);
  const fui::Rect foot{static_cast<int16_t>(body.x + kMargin),
                       static_cast<int16_t>(body.bottom() - footerHeight(screen)),
                       static_cast<int16_t>(body.width - kMargin * 2), footerHeight(screen)};
  const int16_t textY = static_cast<int16_t>(foot.y + kFooterPad);
  const int16_t pageWidth = static_cast<int16_t>(foot.width / 3);
  drawLine(screen, fui::Rect{foot.x, textY, static_cast<int16_t>(foot.width - pageWidth - 8), lineHeight}, model.right,
           toybox::kSmallFont);
  drawLine(screen, fui::Rect{static_cast<int16_t>(foot.right() - pageWidth), textY, pageWidth, lineHeight}, model.left,
           toybox::kSmallFont, fui::TextAlign::Right);
  const int16_t trackY = static_cast<int16_t>(textY + lineHeight + 5);
  screen.target().fill(fui::Rect{foot.x, trackY, foot.width, toybox::kHairline}, fui::Paint::solid(fui::Color::Black));
  if (model.total > 0 && model.page > 0) {
    int16_t filled = static_cast<int16_t>(static_cast<int32_t>(foot.width) * model.page / model.total);
    if (filled < 8) filled = 8;
    screen.target().fill(fui::Rect{foot.x, static_cast<int16_t>(trackY - 2), filled, 5},
                         fui::Paint::solid(fui::Color::Black));
  }
}

int buildContents(toybox::Screen& screen, const ContentsModel& model) {
  chrome(screen, model.title, "CLOSE", ActionClose, 0, true);
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(body.y + 8);
  // Room for the window line under the rows.
  const int16_t bottom = static_cast<int16_t>(body.bottom() - 40);
  const int16_t numberWidth = 56;
  const int16_t textX = static_cast<int16_t>(body.x + kMargin + 16);
  const int16_t textWidth = static_cast<int16_t>(body.width - kMargin * 2 - 16 - numberWidth - 8);
  int shown = 0;
  for (int i = model.first; i < model.count; ++i) {
    // The section you are in is set bold, with a bar in the margin.
    const bool here = i == model.current;
    const fui::FontId font = here ? toybox::kDisplayFont : toybox::kBodyFont;
    const TitleFit fit = fitTitle(screen, textWidth, model.headings[i], font);
    if (y + fit.height > bottom) break;
    const fui::Rect row{body.x, y, body.width, fit.height};
    if (here) {
      screen.target().fill(fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 8), 4,
                                     static_cast<int16_t>(fit.height - 16)},
                           fui::Paint::solid(fui::Color::Black));
    }
    drawTitle(screen, fui::Rect{textX, y, textWidth, fit.height}, font, fit);
    if (model.pages && model.pages[i] >= 0) {
      char number[16];
      snprintf(number, sizeof(number), "%d", model.pages[i] + 1);
      drawLine(screen, fui::Rect{static_cast<int16_t>(row.right() - kMargin - numberWidth), y, numberWidth, fit.height},
               number, toybox::kSmallFont, fui::TextAlign::Right);
    }
    hairline(screen, static_cast<int16_t>(row.bottom() - 1));
    screen.frame().hit(row, ActionHeading, static_cast<int16_t>(i));
    y = static_cast<int16_t>(y + fit.height);
    ++shown;
  }
  // The window's place in the list, and where the rest is. Paged by the side
  // buttons or by tapping the line.
  if (model.first > 0 || model.first + shown < model.count) {
    char where[48];
    snprintf(where, sizeof(where), "%d-%d of %d", model.first + 1, model.first + shown, model.count);
    const fui::Rect line{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 34),
                         static_cast<int16_t>(body.width - kMargin * 2), 30};
    drawLine(screen, line, where, toybox::kSmallFont);
    drawLine(screen, line, model.first + shown < model.count ? "MORE >" : "< FIRST", toybox::kSmallFont,
             fui::TextAlign::Right);
    screen.frame().hit(line, ActionMore);
  }
  return shown;
}

// The first-open screen, top to bottom: the address, the sentence, the
// square the activity fills with the QR code, what the cable is doing, which
// language this is about, the parts on the card, and TRY AGAIN at the foot
// when the card could not be shared. Every block is as tall as its own words
// measure in the body face (the built-in reading cut, WikipediaActivity::
// render), and the square takes what the words leave, between a code a phone
// still reads and the size it has always had. Fixed boxes are what cut the
// language line at two lines and put the parts line under the button.
namespace {

constexpr int16_t kInstallTop = 20;
constexpr int16_t kAfterAddress = 10;
constexpr int16_t kAfterOpen = 12;
constexpr int16_t kAfterQr = 20;
constexpr int16_t kAfterStatus = 8;
constexpr int16_t kAfterLanguage = 4;
// The square: the code has 33 modules (QrUtils picks version 4), so 132 is
// four pixels a module and 232 seven.
constexpr int16_t kQrMin = 132;
constexpr int16_t kQrMax = 232;
constexpr int16_t kActionHeight = 56;
// TRY AGAIN's foot, and the air between it and the words above.
constexpr int16_t kActionFoot = 24;
constexpr int16_t kActionAir = 16;

// The height `text` wraps to at `width` in the body face, one line at least.
int16_t proseHeight(toybox::Screen& screen, const char* text, const int16_t width) {
  fui::TextStyle style = textStyle(toybox::kBodyFont, fui::TextAlign::Center);
  style.maxLines = 16;
  const int16_t lineHeight = screen.target().lineHeight(toybox::kBodyFont);
  const int16_t height = fui::measureWrappedText(screen.target(), text, style, width).height;
  return height > lineHeight ? height : lineHeight;
}

}  // namespace

fui::Rect buildInstall(toybox::Screen& screen, const InstallModel& model) {
  chrome(screen, "GET WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t lineHeight = screen.target().lineHeight(toybox::kBodyFont);
  const bool failed = model.stage == InstallModel::Stage::Failed;

  // The one string that has to be read off the glass and typed: the reading
  // face, bold (the built-in reading cut has no bold, so there it draws
  // regular). It has no space to wrap at, so one line of it ended in an
  // ellipsis on the panel ("crossplay.ma-r-s.com/wikipe..."); the host goes
  // on one line and the path on the next, each whole.
  const char* url = model.url ? model.url : "";
  const char* slash = strchr(url, '/');
  const bool split = slash != nullptr && slash != url;
  // No time here. It depends on the language and the route, which the page
  // knows and this screen does not, and the page states it beside the choice.
  const char* open = "Open this in Chrome or Edge on a computer.";
  const char* status = "Then plug the reader into the computer with its cable.";
  if (model.stage == InstallModel::Stage::Connected) status = "Connected. Follow the page on the computer.";
  if (failed) status = "The card could not be shared. Try again.";

  const int16_t addressHeight = static_cast<int16_t>(lineHeight * (split ? 2 : 1));
  const int16_t openHeight = proseHeight(screen, open, inner);
  const int16_t statusHeight = proseHeight(screen, status, inner);
  const int16_t languageHeight = model.languageLine ? proseHeight(screen, model.languageLine, inner) : 0;
  const int16_t partsHeight = model.partsLine ? kCaptionHeight : 0;
  const int16_t lowest = static_cast<int16_t>(failed ? body.bottom() - kActionFoot - kActionHeight - kActionAir
                                                     : body.bottom() - kActionAir);
  const int16_t words = static_cast<int16_t>(
      kInstallTop + addressHeight + kAfterAddress + openHeight + kAfterOpen + kAfterQr + statusHeight +
      (languageHeight ? kAfterStatus + languageHeight : 0) + (partsHeight ? kAfterLanguage + partsHeight : 0));
  int16_t side = static_cast<int16_t>(lowest - body.y - words);
  if (side > kQrMax) side = kQrMax;
  if (side < kQrMin) side = kQrMin;

  int16_t y = static_cast<int16_t>(body.y + kInstallTop);
  fui::TextStyle address = textStyle(toybox::kBodyFont, fui::TextAlign::Center);
  address.bold = true;
  address.maxLines = 1;
  if (split) {
    char host[64];
    const size_t n =
        static_cast<size_t>(slash - url) < sizeof(host) - 1 ? static_cast<size_t>(slash - url) : sizeof(host) - 1;
    memcpy(host, url, n);
    host[n] = '\0';
    screen.target().text(fui::Rect{x, y, inner, lineHeight}, host, address);
    screen.target().text(fui::Rect{x, static_cast<int16_t>(y + lineHeight), inner, lineHeight}, slash, address);
  } else {
    screen.target().text(fui::Rect{x, y, inner, lineHeight}, url, address);
  }
  y = static_cast<int16_t>(y + addressHeight + kAfterAddress);
  drawProse(screen, fui::Rect{x, y, inner, openHeight}, open, fui::TextAlign::Center);
  y = static_cast<int16_t>(y + openHeight + kAfterOpen);

  const fui::Rect qr{static_cast<int16_t>(body.x + (body.width - side) / 2), y, side, side};
  y = static_cast<int16_t>(qr.bottom() + kAfterQr);

  drawProse(screen, fui::Rect{x, y, inner, statusHeight}, status, fui::TextAlign::Center);
  y = static_cast<int16_t>(y + statusHeight);
  if (model.languageLine) {
    y = static_cast<int16_t>(y + kAfterStatus);
    drawProse(screen, fui::Rect{x, y, inner, languageHeight}, model.languageLine, fui::TextAlign::Center);
    y = static_cast<int16_t>(y + languageHeight);
  }
  if (model.partsLine) {
    y = static_cast<int16_t>(y + kAfterLanguage);
    drawLabel(screen, fui::Rect{x, y, inner, partsHeight}, model.partsLine, toybox::kSmallFont, fui::TextAlign::Center,
              toybox::kButtonCut);
  }
  if (failed) {
    const fui::Rect box{x, static_cast<int16_t>(body.bottom() - kActionFoot - kActionHeight), inner, kActionHeight};
    drawAction(screen, box, "TRY AGAIN", ActionRetry, true);
  }
  return qr;
}

// The languages on the card, as cards in the home's own idiom: the one opened
// last is filled like CONTINUE, the loudest thing on the page, and every card
// names the article its language was left on, so the choice is made by what
// you were reading as much as by the language's name. Chosen over a plain
// list and a grid of language codes, rendered side by side (the commit that
// added it says why).
namespace {

constexpr int16_t kAddLanguage = 52;
constexpr int16_t kCardPad = 12;
constexpr int16_t kCardDetail = 24;

}  // namespace

void buildChooser(toybox::Screen& screen, const ChooserModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t textWidth = static_cast<int16_t>(inner - 28);
  const int16_t addTop = static_cast<int16_t>(body.bottom() - toybox::kGutter - kAddLanguage);
  const int16_t bottom = static_cast<int16_t>(addTop - toybox::kGutter);
  const int16_t top = static_cast<int16_t>(body.y + toybox::kGutter + 2);
  const int16_t bodyLine = screen.target().lineHeight(toybox::kBodyFont);
  const int count = model.count < kMaxPacks ? model.count : kMaxPacks;
  // A card is its name, its count and the article it was left on; when that
  // many cards do not fit above ADD A LANGUAGE, every card drops the article
  // rather than any language dropping off the panel.
  const int16_t compact = static_cast<int16_t>(kCardPad + bodyLine + 4 + kCardDetail + kCardPad);
  bool withTitle = true;
  {
    int32_t need = 0;
    for (int i = 0; i < count; ++i) {
      const char* waits = model.rows[i].continueTitle ? model.rows[i].continueTitle : "Nothing opened yet.";
      const TitleFit fit = fitTitle(screen, textWidth, waits, toybox::kBodyFont);
      need += compact + 6 + fit.lines * bodyLine + (i ? toybox::kGutter : 0);
    }
    withTitle = need <= bottom - top;
  }
  int16_t y = top;
  for (int i = 0; i < count; ++i) {
    const PackRow& row = model.rows[i];
    const bool live = i == model.last;
    const char* waits = row.continueTitle ? row.continueTitle : "Nothing opened yet.";
    const TitleFit fit = fitTitle(screen, textWidth, waits, toybox::kBodyFont);
    const int16_t textHeight = static_cast<int16_t>(fit.lines * bodyLine);
    const fui::Rect card{static_cast<int16_t>(body.x + kMargin), y, inner,
                         static_cast<int16_t>(compact + (withTitle ? 6 + textHeight : 0))};
    if (card.bottom() > bottom) break;
    const fui::Color ink = live ? fui::Color::White : fui::Color::Black;
    if (live) {
      screen.target().fill(card, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), 2);
    }
    // The language's own name, in the reading face: a name like "Français"
    // is not in the app's caps cut.
    fui::TextStyle name = textStyle(toybox::kBodyFont, fui::TextAlign::Left, ink);
    name.maxLines = 1;
    name.bold = true;
    screen.target().text(
        fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + kCardPad), textWidth, bodyLine},
        row.name, name);
    int16_t ty = static_cast<int16_t>(card.y + kCardPad + bodyLine + 4);
    drawLabel(screen, fui::Rect{static_cast<int16_t>(card.x + 14), ty, textWidth, kCardDetail}, row.detail,
              toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut, ink);
    if (withTitle) {
      ty = static_cast<int16_t>(ty + kCardDetail + 6);
      drawTitle(screen, fui::Rect{static_cast<int16_t>(card.x + 14), ty, textWidth, textHeight}, toybox::kBodyFont, fit,
                ink);
    }
    screen.frame().hit(card, ActionPack, static_cast<int16_t>(i));
    y = static_cast<int16_t>(card.bottom() + toybox::kGutter);
  }
  drawAction(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), addTop, inner, kAddLanguage}, "ADD A LANGUAGE",
             ActionInstall, false);
}

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t top = static_cast<int16_t>(body.y + 96);
  drawLabel(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), top, inner, 64}, model.headline,
            toybox::kDisplayFont, fui::TextAlign::Center, toybox::kLargeCut);
  drawProse(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 88), inner, 200},
            model.body, fui::TextAlign::Center);
  if (model.actionLabel) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 80), inner, 56};
    drawAction(screen, box, model.actionLabel, model.action, true);
  }
}

}  // namespace wikiui
