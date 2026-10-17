#include "SolitaireScreens.h"

#include <cstdio>

#include "../cards/CardArt.h"

namespace solitaireui {

using namespace solitaire;

namespace {

// The landscape board, in one place.
//
// Seven columns across 800 pixels is what sets every other number here: the
// card width falls out of it, the card height follows the width at roughly the
// proportion of a real playing card, and what is left over vertically is the
// fan room. A slim header rather than the usual 76 is the one concession, and
// it is worth it -- in portrait the header costs a tenth of the screen, in
// landscape it would cost a sixth of a dimension the tableau needs. The height
// itself is solitaireui::kHeaderBand, in the header, because the Activity has
// to set the theme token to the same number these builders lay out against.
constexpr int kHeader = kHeaderBand;
constexpr int kSideMargin = 16;
constexpr int kPitch = 112;
constexpr int kCardW = 92;
constexpr int kCardH = 122;
// One radius and one stroke for every drawn object in this app. It used to be
// four radii (0, 0, 4, 8) and a 1px card border competing with 4px pip strokes,
// which on e-ink reads as a panel artefact next to deliberate ink.
constexpr int kRadius = 8;
constexpr int kEdge = 2;
// Below the whole chrome -- the band, the gap under it and the rule -- plus a
// gutter. kHeader + 16 counted the band only, which left the top row of cards
// nine pixels under a line the arithmetic could not see.
constexpr int kTopRowY = toybox::chromeBelow(kHeader) + toybox::kGutter;
constexpr int kTableauY = kTopRowY + kCardH + 24;
constexpr int kBottomMargin = 16;

constexpr int kStockColumn = 0;
constexpr int kWasteColumn = 1;
constexpr int kFinishColumn = 2;
constexpr int kFirstFoundationColumn = 3;

constexpr int kWasteFan = 36;

int columnX(const int column) { return kSideMargin + column * kPitch; }

// THE CARD ITSELF NOW LIVES IN cards/CardArt.cpp.
//
// It was written here, and it stayed here until Hearts became the second game
// in this fork to deal from a standard deck. Two copies of a card face is two
// places for the index bug this one already had -- a covered card that never
// showed its suit, found by a tester and by no test -- so the drawing moved to
// the deck and these four forwarders keep every call site in this file exactly
// where it was.
void drawPip(toybox::Screen& screen, const fui::Rect& box, const uint8_t card) {
  cardart::drawSuit(screen, box, suitOf(card), isRed(card));
}

void drawCardFace(toybox::Screen& screen, const fui::Rect& rect, const uint8_t card, const int visible,
                  const bool sideways = false) {
  cardart::drawCardFace(screen, rect, card, visible, sideways ? cardart::Fan::Sideways : cardart::Fan::Down);
}

void drawCardBack(toybox::Screen& screen, const fui::Rect& rect, const int visible) {
  cardart::drawCardBack(screen, rect, visible);
}

void drawSlot(toybox::Screen& screen, const fui::Rect& rect, const char* wants) {
  // `wants` named the rank a foundation was waiting for and was never drawn:
  // the dashed outline says "a place" and a letter promising a suit this game
  // does not assign was a lie. Kept in the signature so the call sites read the
  // same; dropped on the way through.
  (void)wants;
  cardart::drawCardSlot(screen, rect);
}

// A chip knocked out of the black header: white ground, black type. This is
// the "available" weight.
fui::StyleSet knockedOutStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.selected = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

// The "unavailable" weight for the same band. It cannot be a dither -- a light
// dither on black is invisible -- and it cannot be grey type, because
// GfxRendererTarget::text() draws every non-white colour solid black. So dim
// here is a change of shape: outline instead of fill, with the label still
// legible in white. The control keeps its place and still says what it does.
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

// How far apart the cards in a column sit. Compressed only as far as the pile
// forces, and never past the point where a rank is still readable in the strip.
struct Fan {
  int down = 14;
  int up = 30;
};

// The fan has to fit. Always.
//
// The first version compressed only as far as a floor of 14/7 and then gave up,
// so a column holding six face-down cards under a long run ran straight off the
// bottom of the screen. A deep column is exactly when you most need to see what
// is in it, so the last step here distributes whatever room is left evenly
// rather than letting the pile overflow: cards overlap harder, the index in
// each card's top-left corner is the thing that survives, and the top card is
// always whole.
Fan fanFor(const Pile& pile, const int available) {
  Fan fan;
  if (pile.count <= 1) return fan;

  int down = 0;
  for (int c = 0; c + 1 < pile.count; ++c) {
    if (!isFaceUp(pile.cards[c])) down++;
  }
  const int up = (pile.count - 1) - down;

  while (down * fan.down + up * fan.up > available && (fan.up > 16 || fan.down > 8)) {
    if (fan.up > 16) fan.up--;
    if (fan.down > 8) fan.down--;
  }
  if (down * fan.down + up * fan.up <= available) return fan;

  // Still too tall even fully compressed. Share what there is.
  const int each = available / (pile.count - 1);
  fan.down = each < 3 ? 3 : each;
  fan.up = fan.down;
  return fan;
}

}  // namespace

int Layout::cardAt(const Game& game, const int pile, const int ty) const {
  if (pile < 0 || pile >= kPileCount) return -1;
  const Pile& contents = game.pile(pile);
  if (contents.empty()) return -1;
  // Only a tableau fans. Everywhere else just the top card is reachable.
  if (pile < kFirstTableau) return contents.count - 1;

  const int column = pile - kFirstTableau;
  const int local = ty - y[pile];
  if (local < 0) return -1;
  for (int c = contents.count - 1; c >= 0; --c) {
    if (local >= cardOffset[column][c]) return c;
  }
  return 0;
}

void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout) {
  if (model.game == nullptr) return;
  const Game& game = *model.game;
  auto& target = screen.target();
  const fui::Rect band = screen.device().screen();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  layout.cardWidth = kCardW;
  layout.cardHeight = kCardH;
  layout.hasSelection = false;

  // Header. Solid black and drawn once per frame at a size that never changes,
  // which is the cheapest ink on this panel.
  // No right-hand label here: the two standing actions live in this band, and a
  // move counter sharing it just overprinted them. The count is on the menu,
  // which is where anyone who wants it is looking anyway.
  fui::HeaderProps header;
  header.title = "SOLITAIRE";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // The two standing actions live in the header band, knocked out of the black.
  // They are always available and never move, so they cost no layout anywhere
  // the cards need.
  // Inset 8px top and bottom INSIDE the band, so the band is asked for rather
  // than named: an app that spells the chrome's height itself is the shape
  // host-tests/chromeguard exists to stop, and a height is not exempt from it
  // just because it happens to be correct today.
  const int buttonY = 8;
  const int buttonH = toybox::headerBandRect(screen).height - buttonY * 2;
  fui::ButtonProps undo;
  undo.label = "UNDO";
  undo.action = ActionButton;
  undo.value = ButtonUndo;
  undo.enabled = game.canUndo();
  undo.styles = game.canUndo() ? knockedOutStyles() : outlinedOnBlackStyles();
  undo.borderEdges = fui::EdgesNone;
  screen.button(undo, fui::makeRect(band.width - 228, buttonY, 100, buttonH));

  fui::ButtonProps fresh;
  fresh.label = "NEW";
  fresh.action = ActionButton;
  fresh.value = ButtonNew;
  fresh.styles = knockedOutStyles();
  fresh.borderEdges = fui::EdgesNone;
  screen.button(fresh, fui::makeRect(band.width - 116, buttonY, 100, buttonH));

  // Stock. Empty means the next tap recycles the waste, so it wears a ring
  // rather than a plain slot: an empty box says "nothing here", a ring says
  // "go round again".
  const fui::Rect stockRect = fui::makeRect(columnX(kStockColumn), kTopRowY, kCardW, kCardH);
  layout.x[kStockPile] = static_cast<int16_t>(stockRect.x);
  layout.y[kStockPile] = static_cast<int16_t>(stockRect.y);
  if (game.pile(kStockPile).empty()) {
    drawSlot(screen, stockRect, nullptr);
    const int ring = 44;
    target.stroke(fui::makeRect(stockRect.x + (kCardW - ring) / 2, stockRect.y + (kCardH - ring) / 2, ring, ring), ink,
                  toybox::kRule, static_cast<uint8_t>(ring / 2));
  } else {
    drawCardBack(screen, stockRect, kCardH);
  }
  screen.frame().hit(stockRect, ActionPile, kStockPile);

  // Waste. Draw-three shows the last three fanned sideways, which is the only
  // way to know what is underneath the card you are allowed to take.
  const fui::Rect wasteRect = fui::makeRect(columnX(kWasteColumn), kTopRowY, kCardW, kCardH);
  layout.x[kWastePile] = static_cast<int16_t>(wasteRect.x);
  layout.y[kWastePile] = static_cast<int16_t>(wasteRect.y);
  const Pile& waste = game.pile(kWastePile);
  if (waste.empty()) {
    drawSlot(screen, wasteRect, nullptr);
  } else {
    // Fans right, into the empty column, with the newest card on top and
    // rightmost. It used to fan left and ran six pixels into the stock, which
    // read as the two piles touching.
    // 36px of pitch, not 14. At 14 the next card sliced through the middle of
    // the one behind it's rank and the buried cards read as corrupted pixels
    // rather than as cards; 36 clears the whole corner index.
    const int shown = waste.count >= 3 ? 3 : waste.count;
    for (int i = shown - 1; i >= 0; --i) {
      const fui::Rect card = fui::makeRect(wasteRect.x + (shown - 1 - i) * kWasteFan, wasteRect.y, kCardW, kCardH);
      // sideways: the card behind is covered from the right, not from below, so
      // its index has to stack down the sliver rather than run across the top.
      drawCardFace(screen, card, waste.cards[waste.count - 1 - i], i == 0 ? kCardH : kWasteFan, i != 0);
      if (i == 0 && model.selectedPile == kWastePile) {
        layout.selection = card;
        layout.hasSelection = true;
      }
    }
  }
  const int wasteSpan = kCardW + (waste.count >= 3 ? 2 : (waste.count >= 1 ? waste.count - 1 : 0)) * kWasteFan;
  screen.frame().hit(fui::makeRect(wasteRect.x, wasteRect.y, wasteSpan, kCardH), ActionPile, kWastePile);

  for (int f = 0; f < kFoundationPiles; ++f) {
    const int pileIndex = kFirstFoundation + f;
    const fui::Rect rect = fui::makeRect(columnX(kFirstFoundationColumn + f), kTopRowY, kCardW, kCardH);
    layout.x[pileIndex] = static_cast<int16_t>(rect.x);
    layout.y[pileIndex] = static_cast<int16_t>(rect.y);
    const Pile& pile = game.pile(pileIndex);
    if (pile.empty()) {
      drawSlot(screen, rect, "A");
    } else {
      drawCardFace(screen, rect, pile.top(), kCardH);
      if (model.selectedPile == pileIndex) {
        layout.selection = rect;
        layout.hasSelection = true;
      }
    }
    screen.frame().hit(rect, ActionPile, pileIndex);
  }

  // FINISH drops into the gap between the waste and the foundations, and only
  // when the game is already decided. See Game::canAutoFinish for why it is not
  // offered any earlier.
  if (game.canAutoFinish()) {
    fui::ButtonProps finish;
    finish.label = "FINISH";
    finish.action = ActionButton;
    finish.value = ButtonFinish;
    finish.borderEdges = fui::EdgesNone;
    // Clear of the waste fan on the left and the foundations on the right.
    screen.button(finish, fui::makeRect(columnX(kFinishColumn) + 22, kTopRowY + (kCardH - 56) / 2, 82, 56));
  }

  const int fanRoom = band.height - kTableauY - kBottomMargin - kCardH;
  for (int column = 0; column < kTableauPiles; ++column) {
    const int pileIndex = kFirstTableau + column;
    const Pile& pile = game.pile(pileIndex);
    const int x = columnX(column);
    layout.x[pileIndex] = static_cast<int16_t>(x);
    layout.y[pileIndex] = static_cast<int16_t>(kTableauY);

    if (pile.empty()) {
      // An empty column takes a king and only a king, so it says so. The slot
      // was previously indistinguishable from a blank card and gave no hint at
      // all about what it wanted.
      const fui::Rect slot = fui::makeRect(x, kTableauY, kCardW, kCardH);
      drawSlot(screen, slot, "K");
      screen.frame().hit(slot, ActionPile, pileIndex);
      continue;
    }

    const Fan fan = fanFor(pile, fanRoom);
    int offset = 0;
    for (int c = 0; c < pile.count; ++c) {
      layout.cardOffset[column][c] = static_cast<int16_t>(offset);
      const int step = isFaceUp(pile.cards[c]) ? fan.up : fan.down;
      if (c + 1 < pile.count) offset += step;
    }

    for (int c = 0; c < pile.count; ++c) {
      const fui::Rect rect = fui::makeRect(x, kTableauY + layout.cardOffset[column][c], kCardW, kCardH);
      const int visible = c + 1 < pile.count ? layout.cardOffset[column][c + 1] - layout.cardOffset[column][c] : kCardH;
      if (isFaceUp(pile.cards[c])) {
        drawCardFace(screen, rect, pile.cards[c], visible);
      } else {
        drawCardBack(screen, rect, visible);
      }
      if (model.selectedPile == pileIndex && c == model.selectedCard) {
        layout.selection = fui::makeRect(rect.x, rect.y, kCardW,
                                         kTableauY + layout.cardOffset[column][pile.count - 1] + kCardH - rect.y);
        layout.hasSelection = true;
      }
    }

    // One hit per pile, not one per card. Which card was touched comes from
    // Layout::cardAt, reading the same offsets that placed the pixels.
    const int bottom = kTableauY + layout.cardOffset[column][pile.count - 1] + kCardH;
    screen.frame().hit(fui::makeRect(x, kTableauY, kCardW, bottom - kTableauY), ActionPile, pileIndex);
  }

  // The selection last, so it sits over every card it encloses. The rect comes
  // from the builder rather than from the pile's origin, because a fanned pile
  // does not start where its cards end up.
  if (layout.hasSelection) {
    target.stroke(layout.selection, ink, toybox::kFrame + kEdge, kRadius);
  }
}

void buildWin(toybox::Screen& screen, const WinModel& model) {
  auto& target = screen.target();
  const fui::Rect band = screen.device().screen();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  fui::HeaderProps header;
  header.title = "SOLITAIRE";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // The cascade. Every physical solitaire ends with the deck coming off the
  // foundations and bouncing down the table, and it is the one image everyone
  // who has played the game recognises. We cannot animate it, so we draw where
  // it landed: fifty-two cards fanned across the foot of the screen, arcing.
  //
  // This is ornament made of the game's own material, which is the only kind
  // this design language allows -- and unlike a static mascot it is only ever
  // seen once per win, so it cannot become wallpaper.
  const int cascadeCount = 28;
  const int cardW = 58;
  const int cardH = 86;
  const int step = (band.width - cardW - 36) / (cascadeCount - 1);
  for (int i = 0; i < cascadeCount; ++i) {
    // A shallow parabola, lowest in the middle, so the run reads as a bounce
    // rather than as a row.
    const int fromCentre = i - cascadeCount / 2;
    const int lift = (fromCentre * fromCentre * 30) / ((cascadeCount / 2) * (cascadeCount / 2));
    const fui::Rect card = fui::makeRect(18 + i * step, band.height - kBottomMargin - cardH - lift, cardW, cardH);
    target.fill(card, fui::Paint::solid(fui::Color::White), 6);
    target.stroke(card, ink, toybox::kHairline, 6);
    // The pip goes in the strip the next card will not cover. Centring it
    // looked right in the code and came out as half a diamond on screen,
    // because every card but the last is overlapped by the one after it.
    if (i % 3 == 0) {
      drawPip(screen, fui::makeRect(card.x + 3, card.y + 24, 20, 24), makeCard(static_cast<Suit>((i / 3) % kSuits), 0));
    }
  }

  const int top = toybox::chromeBelow(kHeader) + 27;
  fui::TextStyle hero;
  hero.font = toybox::kDisplayFont;
  hero.align = fui::TextAlign::Center;
  target.text(fui::makeRect(0, top, band.width, 64), "ALL FIFTY-TWO HOME", hero);

  // Varied off the move count so the second win does not read exactly like the
  // first. Cheap personality: it is a string table and a modulo.
  static const char* kBrags[4] = {
      "%d MOVES. THE DECK NEVER STOOD A CHANCE.",
      "%d MOVES, AND NOT ONE OF THEM WASTED.",
      "%d MOVES. SOMEBODY HAS DONE THIS BEFORE.",
      "%d MOVES. THE STOCK IS STILL RECOVERING.",
  };
  char line[96];
  std::snprintf(line, sizeof(line), kBrags[model.moves % 4], model.moves);
  fui::TextStyle sub;
  sub.font = toybox::kUiFont;
  sub.align = fui::TextAlign::Center;
  target.text(toybox::inkCentred(fui::makeRect(0, top + 70, band.width, 26), toybox::kUiCut), line, sub);

  std::snprintf(line, sizeof(line), "%d CLEARED, %d IN A ROW", model.wins, model.streak);
  fui::TextStyle stats;
  stats.font = toybox::kTileFont;
  stats.align = fui::TextAlign::Center;
  target.text(fui::makeRect(0, top + 104, band.width, 24), line, stats);

  const int buttonW = 210;
  const int buttonY = top + 144;
  fui::ButtonProps again;
  again.label = "DEAL AGAIN";
  again.action = ActionButton;
  again.value = WinAgain;
  again.borderEdges = fui::EdgesNone;
  screen.button(again,
                fui::makeRect(band.width / 2 - buttonW - toybox::kGutter / 2, buttonY, buttonW, toybox::kPillHeight));

  fui::ButtonProps menu;
  menu.label = "BACK TO THE TABLE";
  menu.action = ActionButton;
  menu.value = WinMenu;
  menu.styles = toybox::rowStyles();
  screen.button(menu, fui::makeRect(band.width / 2 + toybox::kGutter / 2, buttonY, buttonW, toybox::kPillHeight));
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  auto& target = screen.target();
  const fui::Rect band = screen.device().screen();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  fui::HeaderProps header;
  header.title = "SOLITAIRE";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // Two columns, because 480 pixels of height will not take the portrait front
  // door and squeezing it would only make it worse. Left is what you would say
  // out loud about the state of play; right is the record.
  const int left = kSideMargin;
  const int columnWidth = 420;
  const int top = toybox::chromeBelow(kHeader) + 33;

  fui::TextStyle hero;
  hero.font = toybox::kDisplayFont;
  hero.align = fui::TextAlign::Left;
  target.text(toybox::inkCentred(fui::makeRect(left, top, columnWidth, 56), toybox::kDisplayCut),
              model.hasSave ? "DEAL IN PLAY" : "FRESH DECK", hero);

  // Copy written by a person rather than by a form. The state line used to read
  // "5 MOVES - DRAW ONE", which is a status field; this says the same thing in
  // a voice.
  char state[64];
  if (model.hasSave) {
    std::snprintf(state, sizeof(state), "%d MOVES DEEP", model.savedMoves);
  } else {
    std::snprintf(state, sizeof(state), "SHUFFLED AND WAITING");
  }
  fui::TextStyle sub;
  sub.font = toybox::kUiFont;
  sub.align = fui::TextAlign::Left;
  target.text(toybox::inkCentred(fui::makeRect(left, top + 60, columnWidth, 26), toybox::kUiCut), state, sub);

  char stats[80];
  if (model.played == 0) {
    // A zero state is the one screen every player sees and almost nobody
    // designs. It should say something rather than print three noughts.
    std::snprintf(stats, sizeof(stats), "NEVER PLAYED. NO SHAME IN THAT.");
  } else {
    std::snprintf(stats, sizeof(stats), "%d DEALT, %d CLEARED, %d IN A ROW", model.played, model.wins, model.streak);
  }
  fui::TextStyle statsStyle;
  statsStyle.font = toybox::kTileFont;
  statsStyle.align = fui::TextAlign::Left;
  target.text(fui::makeRect(left, top + 100, columnWidth, 24), stats, statsStyle);

  // The ornament, and it is made of solitaire rather than borrowed.
  //
  // It was a 4x4 grid of rounded squares, which is Connections' mark: sixteen
  // cells because a Connections board is sixteen cells. Solitaire has no such
  // shape. What it has is cards, so the record is a fanned hand of sixteen --
  // face up for a deal you cleared, face down for one you walked away from, an
  // empty outline for one you have not played yet. Same three states, told in
  // this game's own material.
  const int handX = left + columnWidth + 20;
  const int handY = top + 10;
  const int miniW = 52;
  const int miniH = 74;
  const int miniStep = 26;
  for (int i = 0; i < 16; ++i) {
    const fui::Rect card = fui::makeRect(handX + (i % 8) * miniStep, handY + (i / 8) * (miniH + 18), miniW, miniH);
    if (model.recent[i] == 2) {
      target.fill(card, fui::Paint::solid(fui::Color::White), 5);
      target.stroke(card, ink, kEdge, 5);
      drawPip(screen, fui::makeRect(card.x + 7, card.y + 10, 18, 20), makeCard(static_cast<Suit>(i % kSuits), 0));
    } else if (model.recent[i] == 1) {
      target.fill(card, fui::Paint::solid(fui::Color::White), 5);
      target.fill(fui::makeRect(card.x + 4, card.y + 4, miniW - 8, miniH - 8), fui::Paint::dither(fui::Color::DarkGray),
                  3);
      target.stroke(card, ink, kEdge, 5);
    } else {
      target.fill(card, fui::Paint::solid(fui::Color::White), 5);
      target.stroke(card, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 5);
    }
  }
  fui::TextStyle caption;
  caption.font = toybox::kTileFont;
  caption.align = fui::TextAlign::Left;
  target.text(fui::makeRect(handX, handY + (miniH + 22) * 2 + 2, 300, 24),
              model.played == 0 ? "SIXTEEN DEALS TO FILL" : "YOUR LAST SIXTEEN", caption);

  const int rowY = band.height - kBottomMargin - toybox::kPillHeight;
  const int usable = band.width - kSideMargin * 2;
  const int gap = toybox::kGutter;
  const int slots = model.hasSave ? 3 : 2;
  const int width = (usable - gap * (slots - 1)) / slots;
  int slot = 0;

  if (model.hasSave) {
    fui::ButtonProps resume;
    resume.label = "PICK IT UP";
    resume.action = ActionButton;
    resume.value = MenuResume;
    resume.borderEdges = fui::EdgesNone;
    screen.button(resume, fui::makeRect(kSideMargin, rowY, width, toybox::kPillHeight));
    slot++;
  }

  fui::ButtonProps deal;
  deal.label = model.hasSave ? "DEAL FRESH" : "DEAL";
  deal.action = ActionButton;
  deal.value = MenuNew;
  deal.styles = model.hasSave ? toybox::rowStyles() : fui::StyleSet{};
  deal.borderEdges = model.hasSave ? fui::EdgesAll : fui::EdgesNone;
  screen.button(deal, fui::makeRect(kSideMargin + slot * (width + gap), rowY, width, toybox::kPillHeight));
  slot++;

  // The draw mode is a setting, not an action, so it says which way it is
  // pointing rather than naming a verb. It changes in place and bites on the
  // next deal, which is what makes it safe to sit beside PICK IT BACK UP.
  char mode[40];
  std::snprintf(mode, sizeof(mode), "TURNING %s", model.drawThree ? "THREE" : "ONE");
  fui::ButtonProps draw;
  draw.label = mode;
  draw.action = ActionButton;
  draw.value = MenuDrawMode;
  draw.styles = toybox::rowStyles();
  screen.button(draw, fui::makeRect(kSideMargin + slot * (width + gap), rowY, width, toybox::kPillHeight));
}

}  // namespace solitaireui
