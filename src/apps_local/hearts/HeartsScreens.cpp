#include "HeartsScreens.h"

#include <cstdio>

// THE TABLE IS A RAIL AND A DIAMOND, and it was chosen by rendering three.
//
// The other two put the scores in a band under the header and used the space
// below it for the trick. Both lost on the same measurement:
//
//   DIAMOND UNDER A BAND  the band leaves 174px for two rows of cards, so the
//                         cards come out 64x86 and the corner index is the only
//                         thing on them. Unreadable at arm's length.
//   A ROW IN PLAY ORDER   full-size cards and a name under each, and genuinely
//                         the clearest of the three to a beginner -- but a card
//                         at position 3 means "played third", so nothing on
//                         screen says where anybody sits, and 160px of white
//                         sits either side of the row.
//
// The rail wins because position means SEAT. That is how every card table
// works, it is what makes "West is out of clubs" something you saw rather than
// something you were told, and taking the band off the top is what pays for
// cards big enough to read.

namespace heartsui {
namespace {

namespace c = cards;
using namespace hearts;

constexpr int16_t kScreenW = 800;
constexpr int16_t kScreenH = 480;

constexpr int16_t kHandH = cardart::kCardH;
constexpr int16_t kHandBottomMargin = 12;
constexpr int16_t kHandTop = kScreenH - kHandBottomMargin - kHandH;  // 346
constexpr int16_t kStatusH = 34;
constexpr int16_t kStatusTop = kHandTop - kStatusH - 4;  // 308
// DERIVED FROM THE CHROME, not typed. The band is 56, its rule sits 4 below it
// and is 3 tall, and the fork's gutter is 12 more -- so content starts at 75,
// not at the 62 this was. Six pixels under the band looked fine and put the
// table three pixels INSIDE the rule, which host-tests/ui's chrome probe
// reported to the pixel and no screenshot of mine had caught.
constexpr int16_t kTableTop = static_cast<int16_t>(toybox::chromeBelow(kHeaderBand) + toybox::kGutter);
constexpr int16_t kTableBottom = kStatusTop - 4;

constexpr int16_t kPageMargin = 16;

// An outline on the black band: present, legible, clearly not available.
fui::StyleSet outlinedOnBlackStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::none();
  styles.normal.foreground = fui::Paint::solid(fui::Color::White);
  styles.normal.borderWidth = toybox::kHairline;
  styles.selected = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

fui::StyleSet knockedOutStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.selected = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

// A label that STEPS ITS CUT DOWN rather than running over its neighbour.
//
// label() below draws at the cut it is given and the text layer does not clip,
// so a word wider than its box is simply painted across whatever is next to it:
// "NORTH" at the UI cut is 107px in a 108px slot and its plaque's points pill
// starts at 126, so the two overlapped by nine pixels and the rail read
// "NORTH+4". fittedTitle is the fork's own answer -- pick the largest cut the
// string fits in, and only break a word when the smallest still overflows.
void fittedLabel(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
                 const fui::TextAlign align, const bool white) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = white ? fui::Color::White : fui::Color::Black;
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  // The cut fittedTitle chose decides where the ink has to sit, so the box is
  // re-centred against THAT cut rather than the one we asked for.
  const toybox::CutMetrics& cut = style.font == toybox::kSmallFont
                                      ? toybox::kTileCut
                                      : (style.font == toybox::kDisplayFont ? toybox::kDisplayCut : toybox::kUiCut);
  screen.target().text(toybox::inkCentred(box, cut), drawn.c_str(), style);
}

void label(toybox::Screen& screen, const fui::Rect& box, const char* text, const toybox::CutMetrics& cut,
           const fui::FontId font, const fui::TextAlign align, const bool white) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  // A non-white colour draws solid black whatever it is, so the only two inks
  // that exist are black and knocked-out white.
  style.color = white ? fui::Color::White : fui::Color::Black;
  screen.target().text(toybox::inkCentred(box, cut), text, style);
}

// A seat's plaque: who, what this hand has cost them, and the race to a hundred.
//
// The seat ON TURN is inverted. Solid black is the loudest thing this panel has
// and it is normally unaffordable on a surface that repaints, but a plaque is
// 224x54 and changes once per play: the rule is area times frequency, and this
// is small enough to spend.
//
// A SEAT THAT HAS TAKEN NOTHING SHOWS NOTHING. The first version drew a "-" in
// an outlined pill beside a "0" total, so every plaque opened the hand reading
// "YOU (-) 0" -- two placeholders for the same absence, side by side, and at a
// glance it was impossible to tell which number was the score. The pill now
// appears only when it has something to say, which also makes the first heart
// of a hand an event you SEE rather than a dash turning into a digit.
void seatPlaque(toybox::Screen& screen, const fui::Rect& box, const SeatView& seat) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);
  const fui::Paint white = fui::Paint::solid(fui::Color::White);
  const bool invert = seat.isTurn;

  target.fill(box, invert ? black : white, 8);
  target.stroke(box, black, invert ? 0 : toybox::kHairline, 8);

  // The name gets whatever the numbers leave it, and steps its cut down if that
  // is not enough. Derived from the box rather than typed, so widening the rail
  // widens the name instead of leaving a gap nobody notices.
  const int16_t pad = 12;
  const int16_t numbersW = static_cast<int16_t>(46 + 10 + 46 + pad);
  fittedLabel(screen, fui::makeRect(box.x + pad, box.y, static_cast<int16_t>(box.width - pad - numbersW), box.height),
              seat.name, toybox::kUiFont, fui::TextAlign::Left, invert);

  char running[16];
  std::snprintf(running, sizeof(running), "%d", seat.total);
  label(screen, fui::makeRect(static_cast<int16_t>(box.right() - pad - 46), box.y, 46, box.height), running,
        toybox::kUiCut, toybox::kUiFont, fui::TextAlign::Right, invert);

  if (seat.taken > 0) {
    char points[16];
    std::snprintf(points, sizeof(points), "+%d", seat.taken);
    const int16_t pillW = 46;
    const fui::Rect pill =
        fui::makeRect(static_cast<int16_t>(box.right() - pad - 46 - 10 - pillW), box.y + 10, pillW, box.height - 20);
    target.fill(pill, invert ? white : black, static_cast<uint8_t>(pill.height / 2));
    label(screen, pill, points, toybox::kButtonCut, toybox::kSmallFont, fui::TextAlign::Center, !invert);
  }
}

// The table's ground.
//
// A BORDERED WHITE PANEL, and the dither goes on the four PLACES rather than
// across the whole thing. Filling the panel was the first version and it was
// wrong twice over: 760x340 of dither is a grey slab that reads as a dead
// region rather than a surface, and it puts the heaviest texture on screen
// behind the four cards that are supposed to be the subject.
//
// White panel, dithered places: the places say "a card belongs here" while they
// are empty, a card drawn over one is white on grey and separates without
// needing a shadow, and the ink is spent in proportion to what changes.
void drawTablePanel(toybox::Screen& screen, const fui::Rect& rect) {
  auto& target = screen.target();
  target.fill(rect, fui::Paint::solid(fui::Color::White), 14);
  target.stroke(rect, fui::Paint::solid(fui::Color::Black), toybox::kRule, 14);
}

// Where a seat's card sits, as a compass. Written once so the empty marker, the
// played card and the seat tag cannot disagree about it.
fui::Rect trickSlot(const fui::Rect& felt, const Seat seat, const int16_t cw, const int16_t ch, const int16_t gap) {
  const int16_t cx = static_cast<int16_t>(felt.x + (felt.width - cw) / 2);
  const int16_t cy = static_cast<int16_t>(felt.y + (felt.height - ch) / 2);
  switch (seat) {
    case Seat::North:
      return fui::makeRect(cx, felt.y + gap, cw, ch);
    case Seat::South:
      return fui::makeRect(cx, felt.bottom() - gap - ch, cw, ch);
    case Seat::West:
      return fui::makeRect(static_cast<int16_t>(cx - cw - gap * 2), cy, cw, ch);
    case Seat::East:
      return fui::makeRect(static_cast<int16_t>(cx + cw + gap * 2), cy, cw, ch);
  }
  return fui::makeRect(cx, cy, cw, ch);
}

// A seat's place on the table, while it is empty.
//
// Card-shaped and dithered, carrying ONE LETTER. It used to carry the seat's
// name inside a circle about fifty pixels across, which the text layer cut to
// an ellipsis: every empty place on the table read "...". Y, W, N and E are all
// distinct, which is the only reason a single letter works at all here.
//
// The seat on turn gets a heavy frame. That is the whole of "whose go is it"
// on this screen, so it has to be visible from across a desk.
void drawPlace(toybox::Screen& screen, const fui::Rect& rect, const char initial, const bool onTurn) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);
  target.fill(rect, fui::Paint::dither(fui::Color::DarkGray), cardart::kRadius);
  target.stroke(rect, black, onTurn ? toybox::kFrame : toybox::kHairline, cardart::kRadius);

  // The letter sits in a knocked-out disc so it is readable on the dither,
  // which a glyph drawn straight onto 50% texture is not.
  const int16_t d = static_cast<int16_t>((rect.width < rect.height ? rect.width : rect.height) * 3 / 5);
  const fui::Rect disc = fui::makeRect(rect.x + (rect.width - d) / 2, rect.y + (rect.height - d) / 2, d, d);
  target.fill(disc, fui::Paint::solid(fui::Color::White), static_cast<uint8_t>(d / 2));
  target.stroke(disc, black, toybox::kHairline, static_cast<uint8_t>(d / 2));
  const char text[2] = {initial, '\0'};
  label(screen, disc, text, toybox::kUiCut, toybox::kUiFont, fui::TextAlign::Center, false);
}

// The pass, given the whole table to itself.
//
// During Phase::Passing there is no trick, and drawing four empty places for
// one was the single worst thing on this screen: the largest region on the
// panel showed nothing, about nothing, for the one phase where the player has
// a decision to make and no idea what it does. Now the three slots you are
// filling are the table, at full card size, with the direction said in words.
void drawPassPanel(toybox::Screen& screen, const fui::Rect& panel, const BoardModel& model, const char* direction) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);
  if (model.game == nullptr) return;
  const Hand& hand = model.game->hands[seatIndex(Seat::South)];

  char heading[48];
  std::snprintf(heading, sizeof(heading), "PASSING %s", direction);
  fittedLabel(screen, fui::makeRect(panel.x + 28, static_cast<int16_t>(panel.y + 14), panel.width - 56, 40), heading,
              toybox::kUiFont, fui::TextAlign::Left, false);

  const int16_t cw = cardart::kCardW;
  const int16_t ch = cardart::kCardH;
  const int16_t gap = 22;
  const int16_t span = static_cast<int16_t>(cw * kPassCount + gap * (kPassCount - 1));
  const int16_t sx = static_cast<int16_t>(panel.x + (panel.width - span) / 2);
  const int16_t sy = static_cast<int16_t>(panel.bottom() - 20 - ch);

  // Which of the chosen cards goes in which slot is not a decision, so they
  // fill left to right in hand order.
  uint8_t chosen[kPassCount] = {kNoCard, kNoCard, kNoCard};
  int n = 0;
  for (int i = 0; i < hand.count && n < kPassCount; ++i) {
    if (model.picked[i]) chosen[n++] = hand.at(i);
  }

  for (int i = 0; i < kPassCount; ++i) {
    const fui::Rect slot = fui::makeRect(static_cast<int16_t>(sx + i * (cw + gap)), sy, cw, ch);
    if (chosen[i] == kNoCard) {
      target.fill(slot, fui::Paint::dither(fui::Color::DarkGray), cardart::kRadius);
      target.stroke(slot, black, toybox::kHairline, cardart::kRadius);
    } else {
      cardart::drawCardFace(screen, slot, chosen[i], ch);
    }
  }
}

// YOUR HAND, and the two things it has to say at a glance.
//
// It fans left to right, so every card shows a sliver down its left edge -- the
// same shape Solitaire's waste leaves, which is why the index is rank OVER pip
// here rather than rank beside it.
//
// An ILLEGAL card is drawn dithered. Hearts' rules are almost entirely about
// which card you may not play, and a tap that silently does nothing is the
// worst possible way to teach them. A dithered face says "not this one, not
// now" before the finger moves, and it is the only dimming this panel supports:
// there is no grey type, so a greyed LABEL would draw solid black.
void drawHand(toybox::Screen& screen, const BoardModel& model, Layout& layout) {
  if (model.game == nullptr) return;
  const Hand& hand = model.game->hands[seatIndex(Seat::South)];
  layout.handCount = hand.count;
  if (hand.count == 0) return;

  const int16_t cw = cardart::kCardW;
  const int16_t available = static_cast<int16_t>(kScreenW - kPageMargin * 2);
  // Spread as far as the cards will go and no further: a five-card hand fanned
  // at thirteen-card pitch looks like eight cards fell off the table.
  int16_t step = static_cast<int16_t>(cw + 10);
  if (hand.count > 1) {
    const int16_t maxStep = static_cast<int16_t>((available - cw) / (hand.count - 1));
    if (maxStep < step) step = maxStep;
  }
  const int16_t span = static_cast<int16_t>(cw + step * (hand.count - 1));
  const int16_t x0 = static_cast<int16_t>((kScreenW - span) / 2);

  // ONE APPEARANCE AT A TIME. Whether a card shows its full face is a property
  // of the HAND, not of the card: while the fan overlaps, every card is a
  // sliver and none of them carries a centre pip; once the hand has thinned
  // enough to spread, they all do. Deciding it per card meant the last card was
  // the only full face in thirteen, which reads as a selection.
  const bool fanned = step < cw;

  for (int i = 0; i < hand.count; ++i) {
    const bool picked = model.picked[i];
    // A picked card lifts out of the fan. Six pixels is enough to read as a
    // different row without opening a gap the fan cannot close.
    const int16_t y = static_cast<int16_t>(kHandTop - (picked ? 10 : 0));
    const fui::Rect rect = fui::makeRect(static_cast<int16_t>(x0 + step * i), y, cw, kHandH);
    layout.handCard[i] = rect;

    const cardart::Ink ink = picked           ? cardart::Ink::Picked
                             : model.legal[i] ? cardart::Ink::Normal
                                              : cardart::Ink::Dimmed;
    cardart::drawCardFace(screen, rect, hand.at(i), fanned ? step : kHandH,
                          fanned ? cardart::Fan::Sideways : cardart::Fan::None, ink);
    // The hit region is the sliver a fanned card actually shows, and the whole
    // card once the hand has spread. Derived from the rect that drew it, never
    // recomputed: hit-testing that recalculates geometry is the rule three
    // separate bugs in this project came from breaking.
    const int16_t hitW = (fanned && i + 1 < hand.count) ? step : cw;
    screen.frame().hit(fui::makeRect(rect.x, rect.y, hitW, kHandH), ActionHandCard, i);
  }
}

// The status line and its right-hand note share one row, so neither may spill
// into the other. Both are assembled at runtime -- "WEST TAKES IT, 13 POINTS",
// "PICK THREE CARDS TO PASS ACROSS" -- and label() neither wraps nor clips, so
// each gets its own half and the fitting ladder.
void drawStatus(toybox::Screen& screen, const BoardModel& model) {
  const int16_t width = static_cast<int16_t>(kScreenW - kPageMargin * 2);
  const int16_t half = static_cast<int16_t>(width * 3 / 5);
  if (model.status != nullptr && model.status[0] != '\0') {
    fittedLabel(screen, fui::makeRect(kPageMargin, kStatusTop, half, kStatusH), model.status, toybox::kUiFont,
                fui::TextAlign::Left, false);
  }
  if (model.subStatus != nullptr && model.subStatus[0] != '\0') {
    fittedLabel(screen,
                fui::makeRect(static_cast<int16_t>(kPageMargin + half), kStatusTop, static_cast<int16_t>(width - half),
                              kStatusH),
                model.subStatus, toybox::kSmallFont, fui::TextAlign::Right, false);
  }
}

}  // namespace

void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout) {
  if (model.game == nullptr) return;
  const Game& game = *model.game;
  auto& target = screen.target();
  (void)target;

  fui::HeaderProps header;
  header.title = "HEARTS";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  const int16_t buttonY = 8;
  const int16_t buttonH = static_cast<int16_t>(toybox::headerBandRect(screen).height - buttonY * 2);
  if (model.showConfirm) {
    fui::ButtonProps confirm;
    confirm.label = model.confirmLabel;
    confirm.action = ActionButton;
    confirm.value = ButtonConfirm;
    confirm.enabled = model.confirmEnabled;
    // A DISABLED BUTTON MUST NOT LOOK LIKE A LIVE ONE. knockedOutStyles sets
    // normal, selected and disabled to the same white-on-black plate, so PASS
    // with one card chosen was pixel-identical to PASS with three and did
    // nothing when tapped: a dead control that advertises itself. Outlined on
    // the black band is the same thing Solitaire's UNDO does when there is
    // nothing to undo.
    confirm.styles = model.confirmEnabled ? knockedOutStyles() : outlinedOnBlackStyles();
    confirm.borderEdges = fui::EdgesNone;
    screen.button(confirm, fui::makeRect(kScreenW - 150, buttonY, 134, buttonH));
  }

  // RAIL: the seats are a column down the left, in table order from the top, so
  // the rail reads the way the play goes round.
  const int16_t railW = 224;
  // Four of these plus their gaps have to fit the panel, which the gutter above
  // made 14px shorter. 54 did not, and a rail that overflows is four plaques
  // walking off the bottom of the screen.
  const int16_t plaqueH = 48;
  const int16_t railGap = 10;
  const int16_t railTop =
      static_cast<int16_t>(kTableTop + ((kTableBottom - kTableTop) - (plaqueH * 4 + railGap * 3)) / 2);
  // PLAY ORDER, from you. The rail read North, East, You, West at first, which
  // is the compass going round and is not the order anybody plays in: the eye
  // follows the rail down expecting the next player and gets the one two seats
  // away. Same order as the score band the other variants use, for the same
  // reason.
  static const Seat kOrder[kSeats] = {Seat::South, Seat::West, Seat::North, Seat::East};
  for (int i = 0; i < kSeats; ++i) {
    const fui::Rect box =
        fui::makeRect(kPageMargin, static_cast<int16_t>(railTop + i * (plaqueH + railGap)), railW, plaqueH);
    seatPlaque(screen, box, model.seats[seatIndex(kOrder[i])]);
  }

  const fui::Rect felt = fui::makeRect(static_cast<int16_t>(kPageMargin + railW + 16), kTableTop,
                                       static_cast<int16_t>(kScreenW - kPageMargin * 2 - railW - 16),
                                       static_cast<int16_t>(kTableBottom - kTableTop));
  layout.felt = felt;
  drawTablePanel(screen, felt);
  // Sized so the diamond FITS: the panel is 242 tall, and two 116px cards plus
  // an 8px inset at each end came to 248, so North's card ran six pixels into
  // South's place and the two read as one object. Derived rather than typed --
  // the height the two rows may share, halved -- so it cannot drift again if
  // the band above it moves.
  const int16_t gap = 8;
  const int16_t middle = 14;
  const int16_t ch = static_cast<int16_t>((felt.height - gap * 2 - middle) / 2);
  const int16_t cw = 80;
  // The pass owns the table while it is happening. See drawPassPanel.
  if (game.phase == Phase::Passing) {
    static const char* kDirections[4] = {"LEFT", "RIGHT", "ACROSS", "NOBODY"};
    drawPassPanel(screen, felt, model, kDirections[static_cast<int>(game.passDirection()) & 3]);
    drawStatus(screen, model);
    drawHand(screen, model, layout);
    return;
  }

  // The panel's top corner, which the diamond leaves empty. How far through the
  // hand you are is the one thing about its shape that nothing else on screen
  // says, and thirteen tricks is short enough that the count means something.
  {
    // trickNumber counts tricks SWEPT, so after the thirteenth it is 13 and a
    // bare +1 printed "TRICK 14/13". Clamped rather than special-cased: the
    // last trick stays on screen as the thirteenth while it is being read.
    const int shown = game.trickNumber >= kTricks ? kTricks : game.trickNumber + 1;
    char trick[24];
    std::snprintf(trick, sizeof(trick), "TRICK %d/%d", shown, kTricks);
    label(screen, fui::makeRect(felt.x + 18, static_cast<int16_t>(felt.y + 12), 160, 28), trick, toybox::kButtonCut,
          toybox::kSmallFont, fui::TextAlign::Left, false);
  }

  // A card lands where its player sits, which is how a real trick
  // reads and what makes "West is void in clubs" something you SEE.
  for (int s = 0; s < kSeats; ++s) {
    const Seat seat = static_cast<Seat>(s);
    const fui::Rect slot = trickSlot(felt, seat, cw, ch, gap);
    layout.trickCard[s] = slot;
    const uint8_t card = game.trick.played[s];
    if (card == kNoCard) {
      drawPlace(screen, slot, model.seats[s].initial, model.seats[s].isTurn);
    } else {
      cardart::drawCardFace(screen, slot, card, ch);
    }
  }

  drawStatus(screen, model);
  drawHand(screen, model, layout);
}

// ---------------------------------------------------------------------------
// The menu.
//
// Solitaire's menu is the cautionary tale this one is drawn against: a strong
// title block top-left, a card graphic top-right, and then a third of the panel
// below both of them holding nothing at all. On a screen that keeps its image
// with the power off, dead space is not untidy, it is the thing you are looking
// at while nothing happens.
//
// So this one is a two-column grid that reaches the bottom bar: the state and
// your record on the left, the table you are about to sit down at on the right,
// and the actions across the foot.

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);

  fui::HeaderProps header;
  header.title = "HEARTS";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  const int16_t top = kHeaderBand + 26;
  const int16_t footH = 68;
  const int16_t footY = static_cast<int16_t>(kScreenH - kHandBottomMargin - footH);
  const int16_t colW = 372;

  // Left: where you are, in one loud line and one quiet one.
  label(screen, fui::makeRect(32, top, colW, 56), model.hasSave ? "TABLE WAITING" : "FOUR SEATS", toybox::kDisplayCut,
        toybox::kDisplayFont, fui::TextAlign::Left, false);

  char line[64];
  if (model.hasSave) {
    std::snprintf(line, sizeof(line), "HAND %d, PART PLAYED", model.savedHand);
  } else {
    // Short enough to FIT the column at the UI cut. The first version ran to
    // "SHOOT LOW. AVOID THE QUEEN." and the text layer cut it mid-word, which
    // is what an unwrapped string over a fixed width does here: there is no
    // wrapping in label(), only truncation.
    std::snprintf(line, sizeof(line), "AVOID THE QUEEN");
  }
  fittedLabel(screen, fui::makeRect(32, static_cast<int16_t>(top + 58), colW, 36), line, toybox::kUiFont,
              fui::TextAlign::Left, false);

  // The record, as three facts rather than a paragraph.
  const int16_t recTop = static_cast<int16_t>(top + 116);
  char played[40];
  char won[40];
  char best[40];
  std::snprintf(played, sizeof(played), "%d", model.gamesPlayed);
  std::snprintf(won, sizeof(won), "%d", model.gamesWon);
  static const char* kPlaces[] = {"-", "1ST", "2ND", "3RD", "4TH"};
  std::snprintf(best, sizeof(best), "%s",
                kPlaces[(model.bestPlace >= 0 && model.bestPlace <= 4) ? model.bestPlace : 0]);
  static const char* kCaps[3] = {"GAMES", "WON", "BEST"};
  const char* values[3] = {played, won, best};
  for (int i = 0; i < 3; ++i) {
    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(32 + i * 124), recTop, 112, 92);
    target.stroke(cell, black, toybox::kHairline, 8);
    label(screen, fui::makeRect(cell.x, static_cast<int16_t>(cell.y + 6), cell.width, 52), values[i], toybox::kLargeCut,
          toybox::kDisplayFont, fui::TextAlign::Center, false);
    label(screen, fui::makeRect(cell.x, static_cast<int16_t>(cell.y + 58), cell.width, 28), kCaps[i],
          toybox::kButtonCut, toybox::kSmallFont, fui::TextAlign::Center, false);
  }

  // Right: the table itself, so the shape of the game is visible before it
  // starts. Three seats round a felt and your own hand fanned at the foot of
  // it: nobody has to be told it is a four-player trick game.
  const fui::Rect felt =
      fui::makeRect(444, top, static_cast<int16_t>(kScreenW - 444 - 32), static_cast<int16_t>(footY - top - 20));
  drawTablePanel(screen, felt);

  static const Seat kRing[3] = {Seat::North, Seat::West, Seat::East};
  const int16_t seatW = 96;
  const int16_t seatH = 40;
  const fui::Rect ringSlots[3] = {
      fui::makeRect(static_cast<int16_t>(felt.x + (felt.width - seatW) / 2), static_cast<int16_t>(felt.y + 16), seatW,
                    seatH),
      fui::makeRect(static_cast<int16_t>(felt.x + 18), static_cast<int16_t>(felt.y + felt.height / 2 - seatH / 2 - 8),
                    seatW, seatH),
      fui::makeRect(static_cast<int16_t>(felt.right() - 18 - seatW),
                    static_cast<int16_t>(felt.y + felt.height / 2 - seatH / 2 - 8), seatW, seatH),
  };
  static const char* kRingNames[3] = {"NORTH", "WEST", "EAST"};
  for (int i = 0; i < 3; ++i) {
    target.fill(ringSlots[i], fui::Paint::solid(fui::Color::White), 6);
    target.stroke(ringSlots[i], black, toybox::kHairline, 6);
    label(screen, ringSlots[i], kRingNames[i], toybox::kButtonCut, toybox::kSmallFont, fui::TextAlign::Center, false);
    (void)kRing[i];
  }

  // Your hand, fanned at the bottom of the felt. Card backs at a size that is
  // clearly a hand rather than a deck.
  const int16_t miniW = 48;
  const int16_t miniH = 64;
  const int16_t miniStep = 26;
  const int16_t miniSpan = static_cast<int16_t>(miniW + miniStep * 4);
  const int16_t miniX = static_cast<int16_t>(felt.x + (felt.width - miniSpan) / 2);
  const int16_t miniY = static_cast<int16_t>(felt.bottom() - 18 - miniH);
  for (int i = 0; i < 5; ++i) {
    cardart::drawCardBack(screen, fui::makeRect(static_cast<int16_t>(miniX + i * miniStep), miniY, miniW, miniH),
                          i == 4 ? miniH : miniStep);
  }

  // The foot. PLAY is the only thing anyone came here to do, so it is solid
  // black and the width of the left column; the table's strength sits beside it
  // as a toggle rather than a screen of its own.
  // The door to the rules, knocked out of the menu's header band. There is
  // nothing else in that band and it is the first thing read on the screen,
  // which is where a player who does not know Hearts is looking.
  // "HOW TO PLAY" is eleven capitals at the UI cut and did not fit its button:
  // it shipped as "HOW TO P...", which is a label that names nothing. One word
  // cannot truncate, and RULES is what the screen behind it actually is.
  fui::ButtonProps how;
  how.label = "RULES";
  how.action = ActionButton;
  how.value = ButtonHowTo;
  how.styles = knockedOutStyles();
  how.borderEdges = fui::EdgesNone;
  screen.button(how, fui::makeRect(kScreenW - 152, 8, 136, static_cast<int16_t>(kHeaderBand - 16)));

  fui::ButtonProps play;
  play.label = model.hasSave ? "RESUME" : "PLAY";
  play.action = ActionButton;
  play.value = ButtonConfirm;
  play.borderEdges = fui::EdgesNone;
  screen.button(play, fui::makeRect(32, footY, 236, footH));

  if (model.hasSave) {
    // OUTLINED, NOT KNOCKED OUT. knockedOutStyles is white-on-black type for
    // the header band; used on a white page it is black text on white with no
    // border, which is a label. Both of these were tappable controls that
    // looked like captions next to the one solid button.
    fui::ButtonProps fresh;
    fresh.label = "NEW GAME";
    fresh.action = ActionButton;
    fresh.value = ButtonMenu;
    fresh.styles = toybox::rowStyles();
    fresh.borderEdges = fui::EdgesAll;
    screen.button(fresh, fui::makeRect(280, footY, 200, footH));
  }

  fui::ButtonProps table;
  table.label = model.sharp ? "TABLE: SHARP" : "TABLE: ROOKIE";
  table.action = ActionButton;
  table.value = ButtonHint;
  table.styles = toybox::rowStyles();
  table.borderEdges = fui::EdgesAll;
  screen.button(table, fui::makeRect(static_cast<int16_t>(model.hasSave ? 492 : 280), footY,
                                     static_cast<int16_t>(model.hasSave ? 276 : 260), footH));
}

// ---------------------------------------------------------------------------
// Between hands.
//
// Four rows, each carrying the one thing that is actually tense in Hearts: how
// close you are to a hundred. The bar is not decoration -- "who is about to
// lose" is the whole reason anyone looks at a score sheet, and a column of
// numbers makes you work it out.

void buildScore(toybox::Screen& screen, const ScoreModel& model) {
  if (model.game == nullptr) return;
  const Game& game = *model.game;
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);

  fui::HeaderProps header;
  header.title = model.gameOver ? "GAME OVER" : "HAND OVER";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  const int16_t top = kHeaderBand + 20;
  const int16_t footH = 68;
  const int16_t footY = static_cast<int16_t>(kScreenH - kHandBottomMargin - footH);

  // The moon gets a line of its own, because it inverts the entire hand and a
  // scoreboard that just shows everyone gaining 26 looks like a bug.
  int16_t rowTop = top;
  if (game.lastHand.moon) {
    char moon[64];
    std::snprintf(moon, sizeof(moon), "%s SHOT THE MOON", model.seats[seatIndex(game.lastHand.shooter)].name);
    const fui::Rect banner = fui::makeRect(kPageMargin, top, kScreenW - kPageMargin * 2, 50);
    target.fill(banner, black, 8);
    fittedLabel(screen, banner, moon, toybox::kUiFont, fui::TextAlign::Center, true);
    rowTop = static_cast<int16_t>(top + 62);
  }

  const int16_t rowH = 54;
  const int16_t rowGap = 10;
  static const Seat kOrder[kSeats] = {Seat::South, Seat::West, Seat::North, Seat::East};
  for (int i = 0; i < kSeats; ++i) {
    const int s = seatIndex(kOrder[i]);
    const fui::Rect row = fui::makeRect(kPageMargin, static_cast<int16_t>(rowTop + i * (rowH + rowGap)),
                                        static_cast<int16_t>(kScreenW - kPageMargin * 2), rowH);
    const bool me = model.seats[s].isMe;
    target.stroke(row, black, me ? toybox::kRule : toybox::kHairline, 8);

    label(screen, fui::makeRect(row.x + 16, row.y, 130, rowH), model.seats[s].name, toybox::kUiCut, toybox::kUiFont,
          fui::TextAlign::Left, false);

    char delta[24];
    std::snprintf(delta, sizeof(delta), "+%d", game.lastHand.scored[s]);
    label(screen, fui::makeRect(row.x + 150, row.y, 80, rowH), delta, toybox::kUiCut, toybox::kUiFont,
          fui::TextAlign::Right, false);

    // The race to a hundred, drawn. The bar is the row's own width minus the
    // label and total columns, so it cannot drift when those change.
    const int16_t barX = static_cast<int16_t>(row.x + 250);
    const int16_t barW = static_cast<int16_t>(row.width - 250 - 100);
    const fui::Rect track = fui::makeRect(barX, static_cast<int16_t>(row.y + rowH / 2 - 9), barW, 18);
    target.stroke(track, black, toybox::kHairline, 9);
    int filled = game.total[s] * barW / kTargetScore;
    if (filled > barW) filled = barW;
    if (filled > 4) {
      target.fill(fui::makeRect(barX, track.y, static_cast<int16_t>(filled), 18),
                  fui::Paint::dither(fui::Color::DarkGray), 9);
    }

    char total[24];
    std::snprintf(total, sizeof(total), "%d", game.total[s]);
    label(screen, fui::makeRect(static_cast<int16_t>(row.right() - 90), row.y, 74, rowH), total, toybox::kUiCut,
          toybox::kUiFont, fui::TextAlign::Right, false);
  }

  // THE BAND BETWEEN THE ROWS AND THE FOOT, which was 80px of nothing.
  //
  // Dead space at the bottom of a layout is a real defect on a panel that holds
  // its image with the power off, and this screen had the same hole Solitaire's
  // menu has. What goes in it is the one thing four totals do not say: where
  // you stand and how much room is left.
  const int16_t noteY = static_cast<int16_t>(rowTop + kSeats * (rowH + rowGap) + 14);
  int best = 0;
  int worst = 0;
  for (int s = 1; s < kSeats; ++s) {
    if (game.total[s] < game.total[best]) best = s;
    if (game.total[s] > game.total[worst]) worst = s;
  }
  const int me = seatIndex(Seat::South);
  char note[80];
  if (model.gameOver) {
    std::snprintf(note, sizeof(note), "%s WINS ON %d", model.seats[best].name, game.total[best]);
  } else if (best == me) {
    std::snprintf(note, sizeof(note), "YOU ARE AHEAD BY %d", game.total[worst == me ? best : worst] - game.total[me]);
  } else {
    std::snprintf(note, sizeof(note), "%s IS AHEAD. YOU ARE %d BEHIND", model.seats[best].name,
                  game.total[me] - game.total[best]);
  }
  label(screen, fui::makeRect(kPageMargin, noteY, kScreenW - kPageMargin * 2, 44), note,
        model.gameOver ? toybox::kDisplayCut : toybox::kUiCut, model.gameOver ? toybox::kDisplayFont : toybox::kUiFont,
        fui::TextAlign::Left, false);

  fui::ButtonProps go;
  go.label = model.gameOver ? "PLAY AGAIN" : "NEXT HAND";
  go.action = ActionButton;
  go.value = ButtonConfirm;
  go.borderEdges = fui::EdgesNone;
  screen.button(go, fui::makeRect(kPageMargin, footY, 280, footH));

  // The rule, on the one screen where it decides how you feel about the numbers
  // above it. Hearts is the wrong way round from most games and a player two
  // hands in is still checking.
  char rule[48];
  std::snprintf(rule, sizeof(rule), "LOWEST WINS  %s  FIRST TO %d ENDS IT", model.gameOver ? "-" : "-", kTargetScore);
  label(screen, fui::makeRect(316, footY, static_cast<int16_t>(kScreenW - 316 - kPageMargin), footH), rule,
        toybox::kButtonCut, toybox::kSmallFont, fui::TextAlign::Right, false);
}

// ---------------------------------------------------------------------------
// How to play.
//
// Four pages, because Hearts is four ideas and they do not fit on one screen at
// a size anyone will read. The order is what a person needs in the order they
// need it: what the points ARE, how a trick works, the two rules that catch
// everybody, and then the pass and the moon, which only matter once the rest is
// understood.
//
// The third page carries the one line that is about this SCREEN rather than
// about Hearts -- a dithered card is one the rules will not take -- because
// that is the bridge between knowing the game and being able to play this copy
// of it.

namespace {

struct HowToPage {
  const char* title;
  const char* lines[5];
};

const HowToPage kHowTo[] = {
    {"WHAT IT COSTS",
     {"EVERY HEART YOU TAKE IS ONE POINT.", "THE QUEEN OF SPADES IS THIRTEEN.", "TWENTY-SIX POINTS GO OUT EVERY HAND.",
      "LOWEST SCORE WINS. FIRST TO 100 ENDS IT.", nullptr}},
    {"TAKING A TRICK",
     {"THE TWO OF CLUBS OPENS EVERY HAND.", "FOLLOW THE SUIT THAT WAS LED IF YOU CAN.",
      "HIGHEST CARD OF THAT SUIT TAKES THE TRICK", "AND EVERYTHING IN IT. THEN LEADS THE NEXT.", nullptr}},
    {"THE TWO THAT CATCH PEOPLE",
     {"YOU CANNOT LEAD A HEART UNTIL SOMEBODY", "HAS DISCARDED ONE. THE TABLE SAYS WHICH.",
      "NOTHING THAT COSTS A POINT MAY BE PLAYED", "ON THE FIRST TRICK OF A HAND.",
      "A GREY CARD IS ONE THE RULES WILL REFUSE."}},
    {"PASSING, AND THE MOON",
     {"BEFORE EACH HAND YOU PASS THREE CARDS:", "LEFT, THEN RIGHT, THEN ACROSS, THEN NOBODY.",
      "TAKE ALL TWENTY-SIX AND YOU SCORE NOTHING", "WHILE EVERYONE ELSE TAKES TWENTY-SIX.",
      "THAT IS SHOOTING THE MOON. IT IS RARE."}},
};

constexpr int kHowToCount = static_cast<int>(sizeof(kHowTo) / sizeof(kHowTo[0]));

}  // namespace

int howToPages() { return kHowToCount; }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);
  const int page = (model.page < 0 || model.page >= kHowToCount) ? 0 : model.page;
  const HowToPage& text = kHowTo[page];

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // The block is CENTRED in what the chrome leaves, not hung from the top. Four
  // lines under a heading is about 250px in a 330px space, and pinned to the
  // top it left a white band above the page marks -- which on a panel that
  // holds its image with the power off is the thing you are looking at.
  int lines = 0;
  for (int i = 0; i < 5; ++i) {
    if (text.lines[i] == nullptr) break;
    ++lines;
  }
  const int16_t markY = static_cast<int16_t>(kScreenH - 60);
  const int16_t blockH = static_cast<int16_t>(56 + 18 + lines * 42);
  const int16_t top = static_cast<int16_t>(kTableTop + ((markY - 16 - kTableTop) - blockH) / 2);

  fittedLabel(screen, fui::makeRect(kPageMargin + 16, top, kScreenW - kPageMargin * 2 - 32, 56), text.title,
              toybox::kDisplayFont, fui::TextAlign::Left, false);

  int16_t y = static_cast<int16_t>(top + 74);
  for (int i = 0; i < lines; ++i) {
    fittedLabel(screen, fui::makeRect(kPageMargin + 16, y, kScreenW - kPageMargin * 2 - 32, 38), text.lines[i],
                toybox::kUiFont, fui::TextAlign::Left, false);
    y = static_cast<int16_t>(y + 42);
  }

  // Page marks, the same shape the shelf uses for its pages so the gesture and
  // the meaning are learned once.
  const int16_t markW = 34;
  const int16_t markGap = 10;
  const int16_t markSpan = static_cast<int16_t>(markW * kHowToCount + markGap * (kHowToCount - 1));
  const int16_t markX = static_cast<int16_t>((kScreenW - markSpan) / 2);
  for (int i = 0; i < kHowToCount; ++i) {
    const fui::Rect mark = fui::makeRect(static_cast<int16_t>(markX + i * (markW + markGap)), markY, markW, 34);
    char n[4];
    std::snprintf(n, sizeof(n), "%d", i + 1);
    if (i == page) {
      target.fill(mark, black, 8);
    } else {
      target.stroke(mark, black, toybox::kHairline, 8);
    }
    label(screen, mark, n, toybox::kButtonCut, toybox::kSmallFont, fui::TextAlign::Center, i == page);
  }

  fui::ButtonProps next;
  next.label = page + 1 < kHowToCount ? "NEXT" : "GOT IT";
  next.action = ActionButton;
  next.value = ButtonHowToNext;
  next.borderEdges = fui::EdgesNone;
  screen.button(next, fui::makeRect(static_cast<int16_t>(kScreenW - kPageMargin - 200),
                                    static_cast<int16_t>(kScreenH - 78), 200, 60));
}

}  // namespace heartsui
