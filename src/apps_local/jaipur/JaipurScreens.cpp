#include "JaipurScreens.h"

#include <cstdio>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxFormat.h"
#include "JaipurArt.h"
#include "JaipurCore.h"
#include "JaipurGoods.h"

namespace jaipurui {

namespace {

// The header band and its offset rule, as every Toybox screen wears them. A
// local copy rather than a shared helper, for the reason LinkScreens gives: a
// copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, not trailingText, and Screen
  // substitutes the theme's smallText when it is unset -- which is black, on a
  // solid black band. The label is then invisible and indistinguishable from
  // never having been set. Insider paid for this discovery.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  toybox::headerRule(screen);

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A small left-aligned line. The alignment is named even though the component
// would apply it anyway: a style whose only field is FONT_SLOT_SMALL reads as
// unset, and the theme quietly puts the full-size style back.
void smallLine(toybox::Screen& screen, const fui::Rect& where, const char* text,
               const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style;
  style.font = toybox::kTileFont;
  style.align = align;
  screen.target().text(where, text, style);
}

// --- the tutorial's own drawing kit -----------------------------------------
//
// Local copies rather than a shared helper, the same call LinkScreens makes: a
// copy is cheaper than a header dependency between two apps.

fui::TextStyle styled(const fui::FontId font, const fui::TextAlign align) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  return style;
}

// Wraps `text` inside `box`, measuring the assembled line rather than adding up
// word widths. Insider's copy of this carries the scars: a line measured by
// summing words overflows, and an overflowing line is ellipsised into U+2026 --
// a glyph the Toybox face does not have, so the tail of the sentence simply
// vanishes with nothing on screen to say so.
// Pass draw = false to measure without drawing, which is the only way to place
// a block of prose whose height depends on the face: the caption is then
// centred in the space it actually has rather than started at a guessed y and
// left to run into the page dots.
int16_t paragraph(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextStyle& style,
                  const bool draw = true) {
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  char line[96] = {};
  int fill = 0;
  int16_t y = box.y;

  const char* at = text;
  while (true) {
    while (*at == ' ') ++at;
    if (*at == '\0') break;
    int n = 0;
    while (at[n] != '\0' && at[n] != ' ') ++n;

    const int kept = fill;
    if (fill != 0 && fill + 1 < static_cast<int>(sizeof(line))) line[fill++] = ' ';
    for (int i = 0; i < n && fill + 1 < static_cast<int>(sizeof(line)); ++i) line[fill++] = at[i];
    line[fill] = '\0';

    if (kept != 0 && screen.target().measureText(style.font, line, style).width > box.width) {
      line[kept] = '\0';
      if (draw) screen.target().text(fui::makeRect(box.x, y, box.width, lineHeight), line, style);
      y = static_cast<int16_t>(y + lineHeight);
      fill = 0;
      for (int i = 0; i < n && fill + 1 < static_cast<int>(sizeof(line)); ++i) line[fill++] = at[i];
      line[fill] = '\0';
    }
    at += n;
  }
  if (fill != 0) {
    if (draw) screen.target().text(fui::makeRect(box.x, y, box.width, lineHeight), line, style);
    y = static_cast<int16_t>(y + lineHeight);
  }
  return static_cast<int16_t>(y - box.y);
}

// The caption, centred in everything the diagram and the footer left it.
void caption(toybox::Screen& screen, const fui::Rect& body, const int16_t top, const char* text) {
  // The footer's own two rows: the tap line and the dots.
  const int16_t floorY = static_cast<int16_t>(body.bottom() - 56);
  const int16_t room = static_cast<int16_t>(floorY - top);
  const fui::Rect probe = fui::makeRect(body.x, top, body.width, room);

  // Measured at the reading cut, and dropped to the small one if that does not
  // fit. A caption that does not fit does not shrink on its own -- it just
  // keeps drawing, straight through the page dots, which is what the six-line
  // selling caption did.
  fui::TextStyle style = styled(toybox::kUiFont, fui::TextAlign::Center);
  int16_t used = paragraph(screen, probe, text, style, false);
  if (used > room) {
    style = styled(toybox::kTileFont, fui::TextAlign::Center);
    used = paragraph(screen, probe, text, style, false);
  }
  const int16_t y = used < room ? static_cast<int16_t>(top + (room - used) / 2) : top;
  paragraph(screen, fui::makeRect(body.x, y, body.width, used), text, style);
}

// A card, exactly as the market draws one: knocked out, stroked, one mark in
// the middle. `dim` is the board's own "you cannot have this" dither.
void cardWithMark(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& mark, const bool dim = false) {
  screen.target().fill(box, fui::Paint::solid(fui::Color::White), 8);
  if (dim) screen.target().fill(box.inset(fui::Insets{4, 4, 4, 4}), fui::Paint::dither(fui::Color::LightGray));
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);
  const int16_t side = static_cast<int16_t>(box.width < box.height ? box.width - 16 : box.height - 16);
  screen.target().bitmap(fui::makeRect(static_cast<int16_t>(box.x + (box.width - side) / 2),
                                       static_cast<int16_t>(box.y + (box.height - side) / 2), side, side),
                         fui::bitmapFromIcon(mark), fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
}

// A goods token: a rupee value, as a chip. Solid when it is the one being
// taken, so a diagram can say "this one, off the top".
void tokenChip(toybox::Screen& screen, const fui::Rect& box, const int value, const bool taken) {
  char text[8];
  std::snprintf(text, sizeof(text), "%d", value);
  if (taken) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black), 8);
  } else {
    screen.target().fill(box, fui::Paint::solid(fui::Color::White), 8);
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);
  }
  fui::TextStyle style = styled(toybox::kUiFont, fui::TextAlign::Center);
  if (taken) style.color = fui::Color::White;
  // Measured, not guessed. A fixed 26px band clipped every digit top and
  // bottom: the cut draws taller than the number looks.
  const int16_t lh = screen.target().lineHeight(style.font);
  screen.target().text(fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - lh) / 2), box.width, lh), text,
                       style);
}

void arrowRight(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t len) {
  const auto ink = fui::Paint::solid(fui::Color::Black);
  screen.target().fill(fui::makeRect(x, static_cast<int16_t>(y - 1), static_cast<int16_t>(len - 8), toybox::kRule),
                       ink);
  screen.target().triangle(fui::Point{static_cast<int16_t>(x + len - 10), static_cast<int16_t>(y - 9)},
                           fui::Point{static_cast<int16_t>(x + len), y},
                           fui::Point{static_cast<int16_t>(x + len - 10), static_cast<int16_t>(y + 9)}, ink);
}

void arrowLeft(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t len) {
  const auto ink = fui::Paint::solid(fui::Color::Black);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(x + 8), static_cast<int16_t>(y - 1),
                                     static_cast<int16_t>(len - 8), toybox::kRule),
                       ink);
  screen.target().triangle(fui::Point{static_cast<int16_t>(x + 10), static_cast<int16_t>(y - 9)}, fui::Point{x, y},
                           fui::Point{static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 9)}, ink);
}

// A trade, which is the one move that goes both ways. Two arrows stacked say
// that; one arrow says "these become those", which is not what happens.
void arrowSwap(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t len) {
  arrowRight(screen, x, static_cast<int16_t>(y - 11), len);
  arrowLeft(screen, x, static_cast<int16_t>(y + 11), len);
}

void arrowDown(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t len) {
  const auto ink = fui::Paint::solid(fui::Color::Black);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(x - 1), y, toybox::kRule, static_cast<int16_t>(len - 8)),
                       ink);
  screen.target().triangle(fui::Point{static_cast<int16_t>(x - 9), static_cast<int16_t>(y + len - 10)},
                           fui::Point{x, static_cast<int16_t>(y + len)},
                           fui::Point{static_cast<int16_t>(x + 9), static_cast<int16_t>(y + len - 10)}, ink);
}

// One item of the small print: a mark, then a line that wraps under itself
// rather than under the mark. Returns where the next item starts.
int16_t smallPrint(toybox::Screen& screen, const fui::Rect& body, const int16_t y, const char* text) {
  constexpr int16_t kMark = 10;
  constexpr int16_t kIndent = 26;
  const fui::TextStyle style = styled(toybox::kTileFont, fui::TextAlign::Left);
  const fui::Rect box =
      fui::makeRect(static_cast<int16_t>(body.x + kIndent), y, static_cast<int16_t>(body.width - kIndent), 200);
  screen.target().fill(fui::makeRect(body.x, static_cast<int16_t>(y + 8), kMark, kMark),
                       fui::Paint::solid(fui::Color::Black), 5);
  const int16_t used = paragraph(screen, box, text, style);
  return static_cast<int16_t>(y + used + 12);
}

// A label over a diagram row, small and centred.
void note(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  screen.target().text(box, text, styled(toybox::kTileFont, fui::TextAlign::Center));
}

// The title band is a full line height. Insider's copy of this comment was
// written after placing things at +42 twice and landing on top of the title
// both times; the display cut draws taller than it looks.
constexpr int16_t kTitleBand = 46;
// Where a diagram may start. The title band is where the text is laid out, not
// where it stops drawing -- the display cut overhangs it, and on three pages a
// card placed at kTitleBand + 14 sat on top of the title. One number, so
// fixing it fixes every page.
constexpr int16_t kDiagramTop = kTitleBand + 34;

void pageTitle(toybox::Screen& screen, const fui::Rect& body, const char* title) {
  screen.target().text(fui::makeRect(body.x, body.y, body.width, kTitleBand), title,
                       styled(toybox::kDisplayFont, fui::TextAlign::Center));
}

// Page dots and the tap affordance, at the foot.
void tutorialFooter(toybox::Screen& screen, const fui::Rect& body, const int page, const int pages) {
  constexpr int16_t kDot = 14;
  constexpr int16_t kDotGap = 10;
  const int16_t row = static_cast<int16_t>(pages * kDot + (pages - 1) * kDotGap);
  const int16_t x = static_cast<int16_t>(body.x + (body.width - row) / 2);
  const int16_t y = static_cast<int16_t>(body.bottom() - kDot);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect at = fui::makeRect(static_cast<int16_t>(x + i * (kDot + kDotGap)), y, kDot, kDot);
    if (i == page) {
      screen.target().fill(at, fui::Paint::solid(fui::Color::Black), 7);
    } else {
      screen.target().stroke(at, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 7);
    }
  }
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(y - 34), body.width, 22),
                       page + 1 == pages ? "TAP TO FINISH" : "TAP TO CONTINUE",
                       styled(toybox::kTileFont, fui::TextAlign::Center));
}

const freeink::Icon& diamond32 = icon_good_diamond_32;
const freeink::Icon& spice32 = icon_good_spice_32;
const freeink::Icon& cloth32 = icon_good_cloth_32;
const freeink::Icon& leather32 = icon_good_leather_32;

}  // namespace

int startRows(const StartModel& model) {
  return model.hasSavedGame ? static_cast<int>(StartRow::Count) : static_cast<int>(StartRow::Count) - 1;
}

StartRow startRowAt(const StartModel& model, const int visibleIndex) {
  const int count = startRows(model);
  const int clamped = visibleIndex < 0 ? 0 : (visibleIndex >= count ? count - 1 : visibleIndex);
  // With no game to continue, the first row is NEW GAME and everything shifts.
  return static_cast<StartRow>(model.hasSavedGame ? clamped : clamped + 1);
}

const char* startRowLabel(const StartRow row) {
  switch (row) {
    case StartRow::Continue:
      return "CONTINUE";
    case StartRow::NewGame:
      return "NEW GAME";
    case StartRow::PlayNearby:
      // The same words chess and battleship use. "Tap where it says
      // multiplayer" only works if something says it, and NEARBY is what it is.
      return "PLAY NEARBY";
    case StartRow::HowToPlay:
      return "HOW TO PLAY";
    default:
      return "";
  }
}

fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model) {
  toyboxChrome(screen, "JAIPUR");

  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.played, model.won);
  const fui::Rect line = screen.takeTop(26);
  smallLine(screen, line, record);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(StartRow::Count)] = {};
  const int count = startRows(model);
  for (int i = 0; i < count; ++i) {
    const StartRow row = startRowAt(model, i);
    rows[i].label = startRowLabel(row);
    if (row == StartRow::Continue) rows[i].value = model.continueDetail;
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionStartRow;
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  // One symbol wherever two devices talk to each other, so it is learned once
  // and recognised everywhere.
  for (int i = 0; i < count; ++i) {
    if (startRowAt(model, i) != StartRow::PlayNearby) continue;
    toybox::iconAtRowRight(screen, listBand, i, 0, linkui::nearbyMark(), i == model.selected);
  }

  return screen.body();
}

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model) {
  toyboxChrome(screen, "JAIPUR");

  // The narration sits at the top, under the rule: it is read once and then the
  // eye belongs to the market. One element acts, one element reports.
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  smallLine(screen, line, model.report);

  fui::ButtonProps status;
  status.label = model.status;
  // Registering no action is what makes the capsule inert while it is only
  // reporting: with NO_ACTION the component draws it and adds nothing to the
  // hit table, so there is no tappable region to drift out of step with the
  // label. It is a button exactly when it says something you can press.
  // Three things the capsule can be, in the order they take precedence: the
  // round has ended and there are scores to see, the selection is a legal move,
  // or it is only reporting.
  // Cast, the way BattleshipScreens does: GCC calls an enumerated and a
  // non-enumerated type in one conditional a -Wextra error, and clang does
  // not, so this only appears once a GCC suite compiles the file.
  status.action = model.roundOver ? static_cast<fui::ActionId>(ActionScores)
                                  : (model.canCommit ? static_cast<fui::ActionId>(ActionCommit) : fui::NO_ACTION);
  status.borderEdges = fui::EdgesNone;
  if (!model.roundOver && !model.canCommit) status.styles = toybox::disabledButtonStyles();
  screen.button(status, linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName));

  return screen.body();
}

fui::Rect buildRoundOver(toybox::Screen& screen, const RoundModel& model) {
  char title[32];
  std::snprintf(title, sizeof(title), "ROUND %d", model.round);
  toyboxChrome(screen, model.matchOver ? "JAIPUR" : title);

  char waiting[40];
  fui::ButtonProps go;
  if (model.waitingOnThem) {
    // Named, because "WAITING" alone reads as the app being busy rather than as
    // a person being asked for something.
    if (model.theirShortName != nullptr && model.theirShortName[0] != '\0') {
      std::snprintf(waiting, sizeof(waiting), "%s DEALS", model.theirShortName);
    } else {
      std::snprintf(waiting, sizeof(waiting), "THEY DEAL");
    }
    go.label = waiting;
    go.action = fui::NO_ACTION;
    go.styles = toybox::disabledButtonStyles();
  } else {
    go.label = model.matchOver ? "PLAY AGAIN" : "NEXT ROUND";
    go.action = model.matchOver ? ActionPlayAgain : ActionContinue;
  }
  go.borderEdges = fui::EdgesNone;
  screen.button(go, linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName));

  return screen.body();
}

int tutorialPages() { return 8; }

void buildTutorial(toybox::Screen& screen, const TutorialModel& model) {
  const int pages = tutorialPages();
  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", model.page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  // The whole page is the button. There is one gesture here and it is the same
  // one the board uses to do everything else.
  screen.frame().hit(body, ActionAdvance, 0);
  tutorialFooter(screen, body, model.page, pages);

  // Where a caption may start: below the tallest diagram on any page.
  const int16_t capTop = static_cast<int16_t>(body.y + 366);
  const int16_t midX = static_cast<int16_t>(body.x + body.width / 2);

  switch (model.page) {
    case 0: {
      // What the whole thing is for. Seals first, because every other rule is
      // in service of them and a player who does not know what they are
      // collecting cannot tell a good turn from a bad one.
      pageTitle(screen, body, "THE GOAL");
      const int16_t top = static_cast<int16_t>(body.y + kDiagramTop);
      const int16_t col = static_cast<int16_t>(body.width / 2);
      const char* who[2] = {"YOU", "THEM"};
      const char* rupees[2] = {"74", "61"};
      for (int i = 0; i < 2; ++i) {
        const int16_t x = static_cast<int16_t>(body.x + i * col);
        note(screen, fui::makeRect(x, top, col, 24), who[i]);
        screen.target().text(fui::makeRect(x, static_cast<int16_t>(top + 30), col, 60), rupees[i],
                             styled(toybox::kDisplayFont, fui::TextAlign::Center));
      }
      arrowDown(screen, static_cast<int16_t>(body.x + col / 2), static_cast<int16_t>(top + 104), 40);
      // Two slots a side, one of them won: the same row the scoring screen
      // draws, so it is recognised there rather than met for the first time.
      const int16_t pip = 26;
      const int16_t span = static_cast<int16_t>(jaipur::kSealsToWin * pip + (jaipur::kSealsToWin - 1) * 10);
      for (int seat = 0; seat < 2; ++seat) {
        const int16_t x0 = static_cast<int16_t>(body.x + seat * col + (col - span) / 2);
        for (int i = 0; i < jaipur::kSealsToWin; ++i) {
          const fui::Rect at =
              fui::makeRect(static_cast<int16_t>(x0 + i * (pip + 10)), static_cast<int16_t>(top + 158), pip, pip);
          if (seat == 0 && i == 0) {
            screen.target().fill(at, fui::Paint::solid(fui::Color::Black), 13);
          } else {
            screen.target().stroke(at, fui::Paint::solid(fui::Color::Black), toybox::kRule, 13);
          }
        }
      }
      caption(screen, body, capTop,
              "THE RICHER TRADER TAKES A SEAL OF EXCELLENCE. TWO SEALS WINS THE GAME, SO A MATCH IS TWO OR THREE "
              "ROUNDS.");
      break;
    }

    case 1: {
      // The one rule that shapes every turn, and the one people get wrong.
      pageTitle(screen, body, "YOUR TURN");
      const int16_t top = static_cast<int16_t>(body.y + kDiagramTop);
      const int16_t panel = static_cast<int16_t>((body.width - 24) / 2);
      const int16_t cardW = 72;
      const int16_t cardH = 100;

      note(screen, fui::makeRect(body.x, top, panel, 24), "TAKE");
      cardWithMark(screen,
                   fui::makeRect(static_cast<int16_t>(body.x + (panel - cardW) / 2), static_cast<int16_t>(top + 32),
                                 cardW, cardH),
                   diamond32);
      arrowDown(screen, static_cast<int16_t>(body.x + panel / 2), static_cast<int16_t>(top + 144), 40);
      note(screen, fui::makeRect(body.x, static_cast<int16_t>(top + 196), panel, 24), "INTO YOUR HAND");

      const int16_t rightX = static_cast<int16_t>(body.x + panel + 24);
      note(screen, fui::makeRect(rightX, top, panel, 24), "OR SELL");
      cardWithMark(screen,
                   fui::makeRect(static_cast<int16_t>(rightX + (panel - cardW) / 2), static_cast<int16_t>(top + 32),
                                 cardW, cardH),
                   spice32);
      arrowDown(screen, static_cast<int16_t>(rightX + panel / 2), static_cast<int16_t>(top + 144), 40);
      tokenChip(screen,
                fui::makeRect(static_cast<int16_t>(rightX + (panel - 60) / 2), static_cast<int16_t>(top + 188), 60, 46),
                5, true);

      caption(screen, body, capTop,
              "EVERY TURN IS ONE OR THE OTHER. TAKE CARDS FROM THE MARKET, OR SELL FROM YOUR HAND.");
      break;
    }

    case 2: {
      // Three ways to take, listed as rows because they are alternatives rather
      // than a sequence.
      pageTitle(screen, body, "TAKING");
      const int16_t rowH = 88;
      const int16_t cardW = 46;
      const int16_t cardH = 64;
      const int16_t labelW = static_cast<int16_t>(body.width / 3);
      const char* label[3] = {"ONE GOOD", "EVERY CAMEL", "OR TRADE"};
      for (int row = 0; row < 3; ++row) {
        const int16_t y = static_cast<int16_t>(body.y + kDiagramTop + row * rowH);
        // The label sits on the cards' centre line, not their top edge.
        note(screen, fui::makeRect(body.x, static_cast<int16_t>(y + (cardH - 22) / 2), labelW, 22), label[row]);
        const int16_t x0 = static_cast<int16_t>(body.x + labelW);
        if (row == 0) {
          cardWithMark(screen, fui::makeRect(x0, y, cardW, cardH), cloth32);
        } else if (row == 1) {
          for (int i = 0; i < 3; ++i) {
            cardWithMark(screen, fui::makeRect(static_cast<int16_t>(x0 + i * (cardW + 6)), y, cardW, cardH),
                         icon_camel_32);
          }
        } else {
          // Two from the market against two of yours. Both sides are drawn the
          // same, because both are real cards; it is the arrows that say which
          // way each pair is going.
          cardWithMark(screen, fui::makeRect(x0, y, cardW, cardH), diamond32);
          cardWithMark(screen, fui::makeRect(static_cast<int16_t>(x0 + cardW + 6), y, cardW, cardH), leather32);
          const int16_t swapX = static_cast<int16_t>(x0 + 2 * (cardW + 6) + 2);
          arrowSwap(screen, swapX, static_cast<int16_t>(y + cardH / 2), 30);
          cardWithMark(screen, fui::makeRect(static_cast<int16_t>(swapX + 38), y, cardW, cardH), spice32);
          cardWithMark(screen, fui::makeRect(static_cast<int16_t>(swapX + 38 + cardW + 6), y, cardW, cardH),
                       icon_camel_32);
        }
      }
      caption(screen, body, capTop,
              "TAKE ONE GOOD, OR EVERY CAMEL AT ONCE, OR TRADE AT LEAST TWO FOR TWO. CAMELS COUNT AS PAYMENT.");
      break;
    }

    case 3: {
      // Where the rupees actually come from, and the reason to sell early.
      pageTitle(screen, body, "SELLING");
      const int16_t top = static_cast<int16_t>(body.y + kDiagramTop);
      const int16_t cardW = 62;
      const int16_t cardH = 84;

      // The sale itself: two of one good, out of your hand.
      const int16_t pairW = static_cast<int16_t>(2 * cardW + 10);
      for (int i = 0; i < 2; ++i) {
        cardWithMark(screen,
                     fui::makeRect(static_cast<int16_t>(body.x + (body.width - pairW) / 2 + i * (cardW + 10)), top,
                                   cardW, cardH),
                     diamond32);
      }
      arrowDown(screen, midX, static_cast<int16_t>(top + cardH + 10), 38);

      // The pile, drawn as what it physically is: every diamond token there
      // will ever be, laid out highest first. This is the object the board's
      // middle row shows, and the page it was missing from -- "the pile" meant
      // nothing until you could see one.
      note(screen, fui::makeRect(body.x, static_cast<int16_t>(top + cardH + 60), body.width, 22),
           "THE DIAMOND PILE, HIGHEST FIRST");
      const int16_t chipW = 54;
      const int16_t chipH = 44;
      const int depth = jaipur::kPileDepth[static_cast<int>(jaipur::Good::Diamond)];
      const int16_t span = static_cast<int16_t>(depth * chipW + (depth - 1) * 8);
      for (int i = 0; i < depth; ++i) {
        // Straight out of the rulebook table rather than typed in again: the
        // diagram cannot go stale against the game it is teaching.
        tokenChip(screen,
                  fui::makeRect(static_cast<int16_t>(body.x + (body.width - span) / 2 + i * (chipW + 8)),
                                static_cast<int16_t>(top + cardH + 88), chipW, chipH),
                  jaipur::kGoodsTokens[static_cast<int>(jaipur::Good::Diamond)][i], i < 2);
      }
      note(screen, fui::makeRect(body.x, static_cast<int16_t>(top + cardH + 142), body.width, 22),
           "THE TOP TWO ARE NOW YOURS");

      caption(screen, body, capTop,
              "SELL ANY NUMBER OF ONE GOOD. ITS TOKENS COME OFF THE TOP, SO SELLING EARLY PAYS MORE.");
      break;
    }

    case 4: {
      // The reason to hold a run instead of cashing it in pairs.
      pageTitle(screen, body, "THE BONUS");
      const int16_t top = static_cast<int16_t>(body.y + kDiagramTop);
      const char* how[3] = {"SELL 3", "SELL 4", "SELL 5+"};
      const int16_t chipW = 118;
      const int16_t gap = 16;
      const int16_t span = static_cast<int16_t>(3 * chipW + 2 * gap);
      for (int i = 0; i < 3; ++i) {
        const int16_t x = static_cast<int16_t>(body.x + (body.width - span) / 2 + i * (chipW + gap));
        const fui::Rect box = fui::makeRect(x, top, chipW, 120);
        screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule, 8);
        screen.target().bitmap(
            fui::makeRect(static_cast<int16_t>(x + (chipW - 44) / 2), static_cast<int16_t>(top + 18), 44, 44),
            fui::bitmapFromIcon(icon_bonus_token_44), fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
        note(screen, fui::makeRect(x, static_cast<int16_t>(top + 76), chipW, 24), how[i]);
      }
      note(screen, fui::makeRect(body.x, static_cast<int16_t>(top + 156), body.width, 24), "BIGGER SALE, BIGGER TOKEN");
      caption(screen, body, capTop,
              "SELL THREE OR MORE OF ONE GOOD AT ONCE AND TAKE A BONUS TOKEN. IT STAYS FACE DOWN UNTIL THE ROUND IS "
              "SCORED.");
      break;
    }

    case 5: {
      // The piece that behaves like nothing else in the game.
      pageTitle(screen, body, "CAMELS");
      const int16_t top = static_cast<int16_t>(body.y + kDiagramTop);
      const int16_t cardW = 46;
      const int16_t cardH = 66;
      const int16_t col = static_cast<int16_t>(body.width / 2);
      const int herd[2] = {4, 2};
      for (int seat = 0; seat < 2; ++seat) {
        note(screen, fui::makeRect(static_cast<int16_t>(body.x + seat * col), top, col, 22),
             seat == 0 ? "YOU" : "THEM");
        // Laid out clear of each other rather than fanned. Overlapping them
        // covered each camel with the next card and the herd turned to mush;
        // four is a number you count, so the four have to be countable.
        const int16_t step = static_cast<int16_t>(cardW + 4);
        const int16_t span = static_cast<int16_t>(herd[seat] * step - 4);
        for (int i = 0; i < herd[seat]; ++i) {
          cardWithMark(screen,
                       fui::makeRect(static_cast<int16_t>(body.x + seat * col + (col - span) / 2 + i * step),
                                     static_cast<int16_t>(top + 30), cardW, cardH),
                       icon_camel_32);
        }
      }
      arrowDown(screen, static_cast<int16_t>(body.x + col / 2), static_cast<int16_t>(top + 116), 40);
      tokenChip(screen,
                fui::makeRect(static_cast<int16_t>(body.x + (col - 68) / 2), static_cast<int16_t>(top + 166), 68, 46),
                jaipur::kCamelTokenValue, true);
      caption(screen, body, capTop,
              "CAMELS NEVER SELL AND NEVER COUNT AGAINST YOUR HAND. THE BIGGER HERD TAKES FIVE RUPEES WHEN THE ROUND "
              "ENDS.");
      break;
    }

    case 6: {
      // The round's clock. Both triggers, because a player who only knows about
      // the deck will not see the other one coming.
      pageTitle(screen, body, "THE END");
      const int16_t top = static_cast<int16_t>(body.y + kDiagramTop);
      note(screen, fui::makeRect(body.x, top, body.width, 22), "THREE PILES RUN OUT");
      const int16_t cardW = 58;
      const int16_t cardH = 80;
      const int16_t span = static_cast<int16_t>(3 * cardW + 2 * 12);
      const freeink::Icon* spent[3] = {&diamond32, &cloth32, &spice32};
      for (int i = 0; i < 3; ++i) {
        cardWithMark(screen,
                     fui::makeRect(static_cast<int16_t>(body.x + (body.width - span) / 2 + i * (cardW + 12)),
                                   static_cast<int16_t>(top + 30), cardW, cardH),
                     *spent[i], true);
      }
      note(screen, fui::makeRect(body.x, static_cast<int16_t>(top + 130), body.width, 22), "OR THE DECK RUNS DRY");
      const fui::Rect deck = fui::makeRect(static_cast<int16_t>(body.x + (body.width - cardW) / 2),
                                           static_cast<int16_t>(top + 160), cardW, cardH);
      screen.target().stroke(deck, fui::Paint::dither(fui::Color::DarkGray), toybox::kRule, 8);
      caption(screen, body, capTop,
              "EITHER ONE ENDS THE ROUND ON THE SPOT. COUNT UP, TAKE THE SEAL, AND DEAL AGAIN, WITH THE LOSER "
              "GOING FIRST.");
      break;
    }

    case 7:
    default: {
      // Everything a diagram would have to caveat, in one place, at the end.
      //
      // These are not obscure: the hand limit and the two-at-a-time rule both
      // bite in the first round. They are here rather than on the pages they
      // belong to because a page that teaches one thing and then qualifies it
      // twice teaches neither -- and because a list is what you come back to.
      // The board also says most of them at the moment they matter: the capsule
      // dims and states the rule it is failing rather than a price it will not
      // honour.
      pageTitle(screen, body, "THE SMALL PRINT");
      int16_t y = static_cast<int16_t>(body.y + kDiagramTop);
      y = smallPrint(screen, body, y, "SEVEN CARDS IN HAND, NO MORE. CAMELS SIT IN YOUR HERD AND NEVER COUNT.");
      y = smallPrint(screen, body, y, "DIAMOND, GOLD AND SILVER GO TWO OR MORE AT A TIME. NEVER ONE ON ITS OWN.");
      y = smallPrint(screen, body, y, "A TRADE CANNOT TAKE AND GIVE THE SAME GOOD.");
      y = smallPrint(screen, body, y, "CAMELS CAN BE GIVEN IN A TRADE. THEY CAN NEVER BE TAKEN IN ONE.");
      y = smallPrint(screen, body, y, "TAKING CAMELS TAKES EVERY CAMEL IN THE MARKET.");
      y = smallPrint(screen, body, y, "A PILE WITH TOO FEW TOKENS PAYS WHAT IS LEFT, AND STILL PAYS THE BONUS.");
      y = smallPrint(screen, body, y, "EQUAL HERDS: NOBODY TAKES THE FIVE RUPEES.");
      // Nothing else in the deck says this, and it is why a trade does not
      // bring the end of the round any closer.
      smallPrint(screen, body, y, "TAKING REFILLS THE MARKET FROM THE DECK. TRADING DOES NOT.");
      break;
    }
  }
}

}  // namespace jaipurui
