#include "SeaSaltScreens.h"

#include <cstdio>
#include <initializer_list>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxFormat.h"
#include "SeaSaltArt.h"
#include "SeaSaltCore.h"
#include "SeaSaltMarks.h"

namespace seasaltui {

namespace {

// The header band and its offset rule, as every Toybox screen wears them. A
// local copy rather than a shared helper, for the reason LinkScreens gives.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::TextStyle styled(const fui::FontId font, const fui::TextAlign align) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  return style;
}

// --- the vocabulary ---------------------------------------------------------

// Indexed by the core's Kind. TURTLE and GULL are renames, not mistakes: the
// icon is the animal or the card gets the icon's name.
constexpr const char* kKindNames[14] = {
    "CRAB", "BOAT",   "FISH",    "SWIMMER",    "SHARK", "SHELL", "TURTLE",
    "GULL", "SAILOR", "MERMAID", "LIGHTHOUSE", "SHOAL", "NEST",  "CAPTAIN",
};

const freeink::Icon* const kIcon48[14] = {
    &icon_ss_crab_48,       &icon_ss_boat_48,    &icon_ss_fish_48,    &icon_ss_swimmer_48, &icon_ss_shark_48,
    &icon_ss_shell_48,      &icon_ss_octopus_48, &icon_ss_penguin_48, &icon_ss_sailor_48,  &icon_ss_mermaid_48,
    &icon_ss_lighthouse_48, &icon_ss_shoal_48,   &icon_ss_nest_48,    &icon_ss_captain_48,
};
const freeink::Icon* const kIcon24[14] = {
    &icon_ss_crab_24,       &icon_ss_boat_24,    &icon_ss_fish_24,    &icon_ss_swimmer_24, &icon_ss_shark_24,
    &icon_ss_shell_24,      &icon_ss_octopus_24, &icon_ss_penguin_24, &icon_ss_sailor_24,  &icon_ss_mermaid_24,
    &icon_ss_lighthouse_24, &icon_ss_shoal_24,   &icon_ss_nest_24,    &icon_ss_captain_24,
};
const freeink::Icon* const kIcon40[14] = {
    &icon_ss_crab_40,       &icon_ss_boat_40,    &icon_ss_fish_40,    &icon_ss_swimmer_40, &icon_ss_shark_40,
    &icon_ss_shell_40,      &icon_ss_octopus_40, &icon_ss_penguin_40, &icon_ss_sailor_40,  &icon_ss_mermaid_40,
    &icon_ss_lighthouse_40, &icon_ss_shoal_40,   &icon_ss_nest_40,    &icon_ss_captain_40,
};

// Indexed by the core's Colour, which is ordered by how many cards carry it.
// Filled/hollow pairs for the four big pairs, then the five loose glyphs, so
// the two nine-card colours read as siblings and the one-card colour reads as
// the oddity it is.
const freeink::Icon* const kColourMarks[11] = {
    &icon_ss_m_circle_24,      // dark blue, 9
    &icon_ss_m_circle_o_24,    // light blue, 9
    &icon_ss_m_square_24,      // black, 8
    &icon_ss_m_square_o_24,    // yellow, 8
    &icon_ss_m_triangle_24,    // light green, 6
    &icon_ss_m_triangle_o_24,  // white, 4 -- the mermaids
    &icon_ss_m_diamond_24,     // purple, 4
    &icon_ss_m_diamond_o_24,   // light grey, 4
    &icon_ss_m_plus_24,        // light orange, 3
    &icon_ss_m_x_24,           // light pink, 2
    &icon_ss_m_asterisk_24,    // orange, 1
};

void blitIcon(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& icon) {
  screen.target().bitmap(box, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));
}

// --- the card tile ----------------------------------------------------------
//
// One function draws every card face in the app -- hand, piles, keep choice,
// dig, tutorial -- so a card cannot look different in two places. The approved
// design: colour mark top-left, group points top-right, the face centred, the
// name under it when the cell is tall enough, the census always.

void drawCardTile(toybox::Screen& screen, const fui::Rect& cell, const CardTile& tile) {
  auto& target = screen.target();

  // Knock out, then frame. Selection thickens the card's own border, which
  // cannot collide with a neighbour by construction.
  target.fill(cell, fui::Paint::solid(fui::Color::White));
  target.stroke(cell, fui::Paint::solid(fui::Color::Black), 2);
  if (tile.selected) {
    // A halo OUTSIDE the card, in the grid gap. Thickening the card's own
    // border inward kept eating whatever sat nearest the edge; out here it
    // can touch nothing, because the cell's content is the same either way.
    target.stroke(fui::makeRect(static_cast<int16_t>(cell.x - 3), static_cast<int16_t>(cell.y - 3),
                                static_cast<int16_t>(cell.width + 6), static_cast<int16_t>(cell.height + 6)),
                  fui::Paint::solid(fui::Color::Black), 2);
  }

  // The card's vertical budget, laid out from the bottom so the bands cannot
  // close up on each other. The grid does NOT always give 125: a three-row
  // hand gets 121, and the old fixed offsets -- tuned for 125 -- pushed the
  // name into the supply mark at any smaller height. Every band below is
  // derived from cell.height, so they stay apart at any size.
  constexpr int16_t kBottomMargin = 6;  // clear of the 2px frame
  constexpr int16_t kLineBand = 15;     // one small-font line, ink ~13
  constexpr int16_t kLineGap = 4;
  constexpr int16_t kCornerBand = 28;  // the colour mark and the points

  const int16_t markSize = 20;
  blitIcon(screen, fui::makeRect(cell.x + 7, cell.y + 7, markSize, markSize), *kColourMarks[tile.colour]);

  if (tile.groupPoints >= 0) {
    char points[toybox::kIntTextChars];
    std::snprintf(points, sizeof(points), "%d", tile.groupPoints);
    target.text(toybox::inkCentred(fui::makeRect(cell.x, cell.y + 5, cell.width - 9, 24), toybox::kUiCut), points,
                styled(toybox::kUiFont, fui::TextAlign::Right));
  }

  const int16_t censusTop = static_cast<int16_t>(cell.y + cell.height - kBottomMargin - kLineBand);
  // The name only earns a line when one fits above the census with the icon
  // still able to be itself.
  const int16_t nameTop = static_cast<int16_t>(censusTop - kLineGap - kLineBand);
  const int16_t iconRoom = static_cast<int16_t>(nameTop - (cell.y + kCornerBand));
  const bool named = iconRoom >= 40;

  const int16_t faceTop = named ? static_cast<int16_t>(cell.y + kCornerBand) : static_cast<int16_t>(cell.y + 20);
  const int16_t faceRoom = named ? iconRoom : static_cast<int16_t>(censusTop - faceTop);
  const int16_t faceSize = faceRoom >= 48 ? 48 : 40;
  blitIcon(screen,
           fui::makeRect(static_cast<int16_t>(cell.x + (cell.width - faceSize) / 2),
                         static_cast<int16_t>(faceTop + (faceRoom - faceSize) / 2), faceSize, faceSize),
           faceSize == 48 ? *kIcon48[tile.kind] : *kIcon40[tile.kind]);

  if (named) {
    target.text(toybox::inkCentred(fui::makeRect(cell.x, nameTop, cell.width, kLineBand), toybox::kTileCut),
                kKindNames[tile.kind], styled(toybox::kSmallFont, fui::TextAlign::Center));
  }

  // "X%d"
  constexpr int kCensusChars = toybox::kIntChars + toybox::literalChars("X") + 1;
  char census[kCensusChars];
  std::snprintf(census, sizeof(census), "X%d", tile.supply);
  target.text(toybox::inkCentred(fui::makeRect(cell.x, censusTop, cell.width, kLineBand), toybox::kTileCut), census,
              styled(toybox::kSmallFont, fui::TextAlign::Center));
}

// A pile as a card back: the top card's mark and face, the depth in the
// corner. An empty pile is a hollow frame.
void drawPileTile(toybox::Screen& screen, const fui::Rect& cell, const PileTile& pile, const char* caption) {
  auto& target = screen.target();
  target.fill(cell, fui::Paint::solid(fui::Color::White));
  target.stroke(cell, fui::Paint::solid(fui::Color::Black), 2);
  if (pile.size == 0) {
    target.text(cell, caption, styled(toybox::kSmallFont, fui::TextAlign::Center));
    return;
  }
  blitIcon(screen, fui::makeRect(cell.x + 6, cell.y + 6, 20, 20), *kColourMarks[pile.colour]);
  char depth[toybox::kIntTextChars];
  std::snprintf(depth, sizeof(depth), "%d", pile.size);
  target.text(toybox::inkCentred(fui::makeRect(cell.x, cell.y + 5, cell.width - 8, 24), toybox::kUiCut), depth,
              styled(toybox::kUiFont, fui::TextAlign::Right));
  blitIcon(screen, fui::makeRect(cell.x + (cell.width - 40) / 2, cell.y + 26, 40, 40), *kIcon40[pile.kind]);
  char pileLine[20];
  std::snprintf(pileLine, sizeof(pileLine), "%s X%d", kKindNames[pile.kind], seasalt::kKindSupply[pile.kind]);
  target.text(toybox::inkCentred(fui::makeRect(cell.x, cell.y + cell.height - 23, cell.width, 16), toybox::kTileCut),
              pileLine, styled(toybox::kSmallFont, fui::TextAlign::Center));
}

// The dashed hint box, fixed height so the layout never jumps as hints change.
void drawHintBox(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  auto& target = screen.target();
  // Dashes: 6 on, 4 off, drawn as fills. The SDK has no dashed stroke.
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int16_t x = box.x; x < box.x + box.width; x += 10) {
    const int16_t w = static_cast<int16_t>(x + 6 <= box.x + box.width ? 6 : box.x + box.width - x);
    target.fill(fui::makeRect(x, box.y, w, 2), ink);
    target.fill(fui::makeRect(x, box.y + box.height - 2, w, 2), ink);
  }
  for (int16_t y = box.y; y < box.y + box.height; y += 10) {
    const int16_t h = static_cast<int16_t>(y + 6 <= box.y + box.height ? 6 : box.y + box.height - y);
    target.fill(fui::makeRect(box.x, y, 2, h), ink);
    target.fill(fui::makeRect(box.x + box.width - 2, y, 2, h), ink);
  }

  // Up to two centred lines, split on the sentence break when there is one.
  const char* dot = nullptr;
  for (const char* at = text; *at; ++at) {
    if (*at == '.' && at[1] == ' ') {
      dot = at;
      break;
    }
  }
  const fui::TextStyle style = styled(toybox::kSmallFont, fui::TextAlign::Center);
  if (dot != nullptr) {
    char first[80];
    const int n = static_cast<int>(dot - text) + 1;
    std::snprintf(first, sizeof(first), "%.*s", n < 79 ? n : 79, text);
    const int16_t half = static_cast<int16_t>(box.height / 2);
    screen.target().text(fui::makeRect(box.x, box.y + 3, box.width, half - 3), first, style);
    screen.target().text(fui::makeRect(box.x, box.y + half, box.width, half - 3), dot + 2, style);
  } else {
    screen.target().text(box, text, style);
  }
}

}  // namespace

// --- vocabulary exports -----------------------------------------------------

const char* kindName(const int kind) { return kKindNames[kind]; }

// One rule per face, spoken at selection. Sentences split at the first ". "
// into the hint box's two lines; keep every fragment under ~46 characters.
constexpr const char* kKindHints[14] = {
    "PAIRS WITH ANOTHER CRAB. THE PAIR DIGS ANY CARD OUT OF A PILE.",
    "PAIRS WITH ANOTHER BOAT. THE PAIR BUYS ANOTHER TURN.",
    "PAIRS WITH ANOTHER FISH. THE PAIR DRAWS A CARD.",
    "PAIRS WITH A SHARK. THE PAIR STEALS FROM THEIR HAND.",
    "PAIRS WITH A SWIMMER. THE PAIR STEALS FROM THEIR HAND.",
    "WORTH 0-2-4-6-8-10 AS YOU COLLECT 1 TO 6.",
    "WORTH 0-3-6-9-12 AS YOU COLLECT 1 TO 5.",
    "WORTH 1-3-5 AS YOU COLLECT 1 TO 3.",
    "TWO SAILORS ARE 5. THE CAPTAIN PAYS 3 EACH.",
    "SCORES YOUR BIGGEST COLOUR. HOLD ALL FOUR AND YOU WIN.",
    "1 POINT PER BOAT YOU HOLD.",
    "1 POINT PER FISH YOU HOLD.",
    "2 POINTS PER GULL YOU HOLD.",
    "3 POINTS PER SAILOR YOU HOLD.",
};
constexpr const char* kPairHints[5] = {
    "TWO CRABS. PLAY THEM TO DIG THROUGH A PILE.",   "TWO BOATS. PLAYING THEM BUYS ANOTHER TURN.",
    "TWO FISH. PLAY THEM TO DRAW A CARD.",           "SWIMMER AND SHARK. PLAY THEM TO STEAL A CARD.",
    "SWIMMER AND SHARK. PLAY THEM TO STEAL A CARD.",
};
const char* kindHint(const int kind) { return kKindHints[kind]; }
const char* pairHint(const int duoKind) { return kPairHints[duoKind]; }
const freeink::Icon& colourMark(const int colour) { return *kColourMarks[colour]; }
const freeink::Icon& kindIcon48(const int kind) { return *kIcon48[kind]; }
const freeink::Icon& kindIcon24(const int kind) { return *kIcon24[kind]; }
const freeink::Icon& kindIcon40(const int kind) { return *kIcon40[kind]; }

// --- the front door ---------------------------------------------------------

int startRows(const StartModel& model) { return static_cast<int>(StartRow::Count) - (model.hasSavedGame ? 0 : 1); }

StartRow startRowAt(const StartModel& model, const int visibleIndex) {
  return static_cast<StartRow>(visibleIndex + (model.hasSavedGame ? 0 : 1));
}

const char* startRowLabel(const StartRow row) {
  switch (row) {
    case StartRow::Continue:
      return "CONTINUE";
    case StartRow::NewGame:
      return "NEW GAME";
    case StartRow::PlayNearby:
      return "PLAY NEARBY";
    case StartRow::HowToPlay:
      return "HOW TO PLAY";
    case StartRow::Count:
      break;
  }
  return "";
}

fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model) {
  toyboxChrome(screen, "SEA SALT");

  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.played, model.won);
  const fui::Rect line = screen.takeTop(26);
  screen.target().text(line, record, styled(toybox::kTileFont, fui::TextAlign::Left));
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

  for (int i = 0; i < count; ++i) {
    if (startRowAt(model, i) != StartRow::PlayNearby) continue;
    toybox::iconAtRowRight(screen, listBand, i, 0, linkui::nearbyMark(), i == model.selected);
  }

  return screen.body();
}

// --- the board --------------------------------------------------------------

namespace {

constexpr int kCardGap = 7;

// The facts strip: four cells of number-plus-caption, hairline-framed.
void drawFacts(toybox::Screen& screen, const fui::Rect& strip, const BoardModel& model) {
  auto& target = screen.target();
  target.stroke(strip, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
  const int16_t cellW = static_cast<int16_t>(strip.width / 4);
  // Number beside a two-line caption: the words never fit one line at this
  // width, and an ellipsised caption is a caption that says nothing.
  const struct {
    int value;
    const char* line1;
    const char* line2;
  } cells[3] = {
      {model.theyHold, "THEY", "HOLD"},
      {model.theirTableCount, "THEIR", "TABLE"},
      {model.yourTableCount, "YOUR", "TABLE"},
  };
  const fui::TextStyle capStyle = styled(toybox::kSmallFont, fui::TextAlign::Left);
  for (int i = 0; i < 3; ++i) {
    const int16_t x = static_cast<int16_t>(strip.x + i * cellW);
    if (i > 0)
      target.fill(fui::makeRect(x, strip.y, toybox::kHairline, strip.height), fui::Paint::solid(fui::Color::Black));
    char value[toybox::kIntTextChars];
    std::snprintf(value, sizeof(value), "%d", cells[i].value);
    target.text(fui::makeRect(x + 8, strip.y, 28, strip.height), value, styled(toybox::kUiFont, fui::TextAlign::Left));
    const int16_t half = static_cast<int16_t>(strip.height / 2);
    target.text(fui::makeRect(x + 40, strip.y + 2, cellW - 44, half - 2), cells[i].line1, capStyle);
    target.text(fui::makeRect(x + 40, strip.y + half, cellW - 44, half - 2), cells[i].line2, capStyle);
  }
  // BEST: the count, the mark, the caption.
  const int16_t x = static_cast<int16_t>(strip.x + 3 * cellW);
  target.fill(fui::makeRect(x, strip.y, toybox::kHairline, strip.height), fui::Paint::solid(fui::Color::Black));
  char value[toybox::kIntTextChars];
  std::snprintf(value, sizeof(value), "%d", model.bestColourCount);
  target.text(fui::makeRect(x + 8, strip.y, 28, strip.height), value, styled(toybox::kUiFont, fui::TextAlign::Left));
  blitIcon(screen, fui::makeRect(x + 32, strip.y + (strip.height - 20) / 2, 20, 20), *kColourMarks[model.bestColour]);
  target.text(
      toybox::inkCentred(fui::makeRect(x + 58, strip.y + strip.height / 2 - 8, cellW - 62, 16), toybox::kTileCut),
      "BEST", capStyle);
}

// The three-tab strip. The active tab is the inverted one -- inversion means
// "selected" in this fork and nothing else.
void drawTabs(toybox::Screen& screen, const fui::Rect& strip, const BoardModel& model) {
  auto& target = screen.target();
  const struct {
    const char* label;
    int count;
    fui::ActionId action;
  } tabs[3] = {
      {"HAND", model.handCount, ActionTabHand},
      {"YOURS", model.yoursCount, ActionTabYours},
      {"THEIRS", model.theirsCount, ActionTabTheirs},
  };
  // One segmented bar, not three boxes: a single frame, hairline dividers,
  // the active segment inverted.
  target.stroke(strip, fui::Paint::solid(fui::Color::Black), 2);
  const int16_t tabW = static_cast<int16_t>(strip.width / 3);
  for (int i = 0; i < 3; ++i) {
    const int16_t x = static_cast<int16_t>(strip.x + i * tabW);
    const fui::Rect tab =
        fui::makeRect(x, strip.y, i == 2 ? static_cast<int16_t>(strip.width - 2 * tabW) : tabW, strip.height);
    const bool active = model.tab == i;
    if (active) {
      target.fill(tab, fui::Paint::solid(fui::Color::Black));
    } else if (i > 0) {
      // Full height: an inset divider reads as a broken line when the fill
      // changes beside it.
      target.fill(fui::makeRect(x, strip.y, toybox::kHairline, strip.height), fui::Paint::solid(fui::Color::Black));
    }
    char label[16];
    if (i == 0 && model.handTabLabel != nullptr) {
      std::snprintf(label, sizeof(label), "%s", model.handTabLabel);
    } else {
      std::snprintf(label, sizeof(label), "%s %d", tabs[i].label, tabs[i].count);
    }
    fui::TextStyle style = styled(toybox::kSmallFont, fui::TextAlign::Center);
    style.color = active ? fui::Color::White : fui::Color::Black;
    target.text(tab, label, style);
    screen.frame().hit(tab, tabs[i].action, 0);
  }
}

}  // namespace

fui::Rect cardCellRect(const fui::Rect& grid, const int index, const int count) {
  const int rows = (count + 3) / 4;
  const int16_t cellW = static_cast<int16_t>((grid.width - 3 * kCardGap) / 4);
  // Floored with 4px held back, so row rounding cannot push the last row out.
  int16_t cellH = static_cast<int16_t>((grid.height - 4 - (rows - 1) * kCardGap) / rows);
  if (cellH > 125) cellH = 125;
  const int col = index % 4;
  const int row = index / 4;
  return fui::makeRect(static_cast<int16_t>(grid.x + col * (cellW + kCardGap)),
                       static_cast<int16_t>(grid.y + row * (cellH + kCardGap)), cellW, cellH);
}

int cardIndexAt(const fui::Rect& grid, const int count, const int16_t x, const int16_t y) {
  for (int i = 0; i < count; ++i) {
    const fui::Rect cell = cardCellRect(grid, i, count);
    if (x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height) return i;
  }
  return -1;
}

fui::Rect buildBoard(toybox::Screen& screen, const BoardModel& model) {
  // "%d - %d"
  constexpr int kScoreChars = toybox::kIntChars + toybox::kIntChars + toybox::literalChars(" - ") + 1;
  char score[kScoreChars];
  std::snprintf(score, sizeof(score), "%d - %d", model.yourTotal, model.theirTotal);
  toyboxChrome(screen, "SEA SALT", score);

  // Top to bottom: facts, piles, tabs. Bottom up: pills, hint. What is left is
  // the card grid.
  const fui::Rect facts = screen.takeTop(44, toybox::kGutter / 2);
  drawFacts(screen, facts, model);

  const fui::Rect pilesRow = screen.takeTop(96, toybox::kGutter / 2);
  {
    auto& target = screen.target();
    const int16_t w = static_cast<int16_t>((pilesRow.width - 2 * kCardGap) / 3);
    // The deck: dithered, because it repaints every turn.
    const fui::Rect deck = fui::makeRect(pilesRow.x, pilesRow.y, w, pilesRow.height);
    target.fill(deck, fui::Paint::dither(fui::Color::LightGray));
    target.stroke(deck, fui::Paint::solid(fui::Color::Black), 2);
    const int16_t mid = static_cast<int16_t>(deck.y + deck.height / 2);
    target.text(fui::makeRect(deck.x, deck.y + 8, deck.width, mid - deck.y - 8), "DECK",
                styled(toybox::kSmallFont, fui::TextAlign::Center));
    char n[toybox::kIntTextChars];
    std::snprintf(n, sizeof(n), "%d", model.deckCount);
    target.text(fui::makeRect(deck.x, mid, deck.width, deck.height / 2 - 8), n,
                styled(toybox::kUiFont, fui::TextAlign::Center));
    screen.frame().hit(deck, ActionDeck, 0);

    for (int p = 0; p < 2; ++p) {
      const fui::Rect cell =
          fui::makeRect(static_cast<int16_t>(pilesRow.x + (p + 1) * (w + kCardGap)), pilesRow.y, w, pilesRow.height);
      drawPileTile(screen, cell, model.piles[p], "EMPTY");
      screen.frame().hit(cell, p == 0 ? ActionPileA : ActionPileB, 0);
    }
  }

  const fui::Rect tabs = screen.takeTop(40, toybox::kGutter / 2);
  drawTabs(screen, tabs, model);

  // The pills. CALL IT sits beside the primary like the mockup: wide primary,
  // narrow call.
  const fui::Rect pillRow = screen.takeBottom(toybox::kPillHeight);
  screen.takeBottom(8);  // the gap the welded hint box was missing
  {
    // The call pill never disappears: a control that cannot act dims. Below
    // seven points it is the running score, which you want anyway.
    const int16_t callW = 150;
    fui::ButtonProps primary;
    primary.label = model.primaryLabel;
    // The cast is not decoration: ActionPrimary comes from this file's
    // anonymous enum and NO_ACTION is a plain fui::ActionId, and GCC rejects
    // that mix under -Werror while clang says nothing. It built here and
    // broke every CI run on xteink. Battleship and Chess spell it this way.
    primary.action = model.primaryEnabled ? static_cast<fui::ActionId>(ActionPrimary) : fui::NO_ACTION;
    if (!model.primaryEnabled) primary.styles = toybox::disabledButtonStyles();
    primary.borderEdges = fui::EdgesNone;
    screen.button(primary, fui::makeRect(pillRow.x, pillRow.y, static_cast<int16_t>(pillRow.width - callW - kCardGap),
                                         pillRow.height));
    // "%d - CALL IT"
    constexpr int kCallChars = toybox::kIntChars + toybox::literalChars(" - CALL IT") + 1;
    char call[kCallChars];
    fui::ButtonProps callPill;
    if (model.canCall) {
      std::snprintf(call, sizeof(call), "%d - CALL IT", model.callPoints);
      callPill.action = ActionCall;
    } else {
      std::snprintf(call, sizeof(call), "%d PTS", model.callPoints);
      callPill.action = fui::NO_ACTION;
      callPill.styles = toybox::disabledButtonStyles();
    }
    callPill.label = call;
    callPill.borderEdges = fui::EdgesNone;
    screen.button(callPill, fui::makeRect(static_cast<int16_t>(pillRow.x + pillRow.width - callW), pillRow.y, callW,
                                          pillRow.height));
  }
  const fui::Rect hint = screen.takeBottom(54, toybox::kGutter / 2);
  drawHintBox(screen, hint, model.hint);

  // The grid, and the tiles on this page.
  const fui::Rect grid = screen.body();
  for (int i = 0; i < model.tileCount; ++i) {
    drawCardTile(screen, cardCellRect(grid, i, model.tileCount), model.tiles[i]);
  }
  if (model.pages > 1) {
    // "%d / %d"
    constexpr int kPagerChars = 2 * toybox::kIntChars + toybox::literalChars(" / ") + 1;
    char pager[kPagerChars];
    std::snprintf(pager, sizeof(pager), "%d / %d", model.page + 1, model.pages);
    screen.target().text(
        toybox::inkCentred(fui::makeRect(grid.x, grid.y + grid.height - 16, grid.width, 14), toybox::kTileCut), pager,
        styled(toybox::kSmallFont, fui::TextAlign::Right));
  }
  return grid;
}

// --- the modal choices ------------------------------------------------------

fui::Rect buildCallChoice(toybox::Screen& screen, const CallModel& model) {
  char score[16];
  std::snprintf(score, sizeof(score), "%d PTS", model.yourPoints);
  toyboxChrome(screen, "CALL IT", score);

  const fui::Rect body = screen.body();
  const int16_t half = static_cast<int16_t>((body.height - toybox::kGutter) / 2);
  const fui::Rect stop = fui::makeRect(body.x, body.y, body.width, half);
  const fui::Rect bet = fui::makeRect(body.x, static_cast<int16_t>(body.y + half + toybox::kGutter), body.width, half);

  auto choice = [&](const fui::Rect& box, const char* word, const char* l1, const char* l2,
                    const fui::ActionId action) {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
    screen.target().text(fui::makeRect(box.x, box.y + 16, box.width, 48), word,
                         styled(toybox::kDisplayFont, fui::TextAlign::Center));
    screen.target().text(fui::makeRect(box.x + 12, box.y + 76, box.width - 24, 22), l1,
                         styled(toybox::kSmallFont, fui::TextAlign::Center));
    screen.target().text(fui::makeRect(box.x + 12, box.y + 100, box.width - 24, 22), l2,
                         styled(toybox::kSmallFont, fui::TextAlign::Center));
    screen.frame().hit(box, action, 0);
  };
  choice(stop, "STOP", "EVERYBODY BANKS THEIR CARDS.", "SAFE, AND IT PAYS NO COLOUR BONUS.", ActionStop);
  choice(bet, "LAST CHANCE", "BET YOU HAVE THE MOST. WIN: CARDS PLUS BONUS.", "LOSE: YOU BANK ONLY YOUR BONUS.",
         ActionLastChance);
  return body;
}

fui::Rect buildRoundOver(toybox::Screen& screen, const RoundModel& model) {
  char title[24];
  std::snprintf(title, sizeof(title), "ROUND %d", model.round);
  toyboxChrome(screen, model.matchOver ? "SEA SALT" : title);

  auto& target = screen.target();
  const fui::Rect headline = screen.takeTop(34, toybox::kGutter / 2);
  const char* what;
  if (model.mermaidWin) {
    what = model.youWonMatch ? "FOUR MERMAIDS. YOU WIN." : "FOUR MERMAIDS. THEY WIN.";
  } else if (model.deckOut) {
    what = "THE DECK RAN OUT. NOBODY SCORES.";
  } else if (!model.wasLastChance) {
    what = model.youCalled ? "YOU CALLED STOP." : "THEY CALLED STOP.";
  } else if (model.youCalled) {
    what = model.betWon ? "YOUR BET CAME OFF." : "YOUR BET FAILED.";
  } else {
    what = model.betWon ? "THEIR BET CAME OFF." : "THEIR BET FAILED.";
  }
  target.text(headline, what, styled(toybox::kUiFont, fui::TextAlign::Center));

  if (!model.deckOut && !model.mermaidWin) {
    // Two rows of arithmetic: cards + bonus = banked, yours then theirs. A
    // dash where a side banked nothing of that column.
    const fui::Rect yours = screen.takeTop(30);
    const fui::Rect theirs = screen.takeTop(30, toybox::kGutter / 2);
    auto row = [&](const fui::Rect& where, const char* who, const int cards, const int bonus, const int banked) {
      char text[64];
      std::snprintf(text, sizeof(text), "%s  CARDS %d  BONUS %d  BANKED %d", who, cards, bonus, banked);
      target.text(where, text, styled(toybox::kSmallFont, fui::TextAlign::Left));
    };
    row(yours, "YOU ", model.yourCards, model.yourBonus, model.yourBanked);
    row(theirs, "THEM", model.theirCards, model.theirBonus, model.theirBanked);
  }

  const fui::Rect totals = screen.takeTop(40, toybox::kGutter / 2);
  char running[40];
  std::snprintf(running, sizeof(running), "%d - %d", model.yourTotal, model.theirTotal);
  target.text(totals, running, styled(toybox::kDisplayFont, fui::TextAlign::Center));

  fui::ButtonProps go;
  if (model.waitingOnThem) {
    go.label = "THEY DEAL";
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

// --- the tutorial -----------------------------------------------------------

int tutorialPages() { return 6; }

void buildTutorial(toybox::Screen& screen, const TutorialModel& model) {
  const int pages = tutorialPages();
  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", model.page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  screen.frame().hit(body, ActionAdvance, 0);

  auto& target = screen.target();
  // One rhythm for every page: a caption band, tile rows centred as a group,
  // small lines on a 26px beat with a 12px breath between thoughts. Positions
  // are computed, never eyeballed -- the eyeballed version shipped three
  // pages of crowding.
  auto caption = [&](const int16_t y, const char* text) {
    target.text(
        toybox::inkCentred(fui::makeRect(body.x, static_cast<int16_t>(body.y + y), body.width, 26), toybox::kUiCut),
        text, styled(toybox::kUiFont, fui::TextAlign::Center));
  };
  auto small = [&](const int16_t y, const char* text) {
    target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + y), body.width, 22), text,
                styled(toybox::kSmallFont, fui::TextAlign::Center));
  };
  struct TileSpec {
    uint8_t kind, colour, supply;
    int8_t pts;
  };
  // A centred row of grid-shaped cards. Four across fits exactly with an 8px
  // gap; fewer get 16.
  auto tileRow = [&](const int16_t y, std::initializer_list<TileSpec> specs) {
    const int n = static_cast<int>(specs.size());
    const int16_t gap = n >= 4 ? 8 : 16;
    const int16_t rowW = static_cast<int16_t>(n * 106 + (n - 1) * gap);
    int16_t x = static_cast<int16_t>(body.x + (body.width - rowW) / 2);
    for (const TileSpec& spec : specs) {
      CardTile t;
      t.kind = spec.kind;
      t.colour = spec.colour;
      t.supply = spec.supply;
      t.groupPoints = spec.pts;
      drawCardTile(screen, fui::makeRect(x, static_cast<int16_t>(body.y + y), 106, 125), t);
      x = static_cast<int16_t>(x + 106 + gap);
    }
  };

  switch (model.page) {
    case 0:
      caption(16, "TAKE ONE CARD A TURN.");
      small(84, "DRAW TWO FROM THE DECK AND KEEP ONE,");
      small(110, "OR TAKE THE TOP OF A DISCARD PILE.");
      tileRow(176, {{1, 0, 8, 0}, {2, 2, 7, 0}});
      caption(440, "FIRST TO 40 POINTS WINS.");
      break;
    case 1:
      caption(16, "PAIRS HAVE POWERS.");
      tileRow(74, {{1, 0, 8, 1}, {2, 2, 7, 1}, {0, 4, 9, 1}});
      small(254, "TWO BOATS: TAKE ANOTHER TURN.");
      small(280, "TWO FISH: DRAW A CARD.");
      small(306, "TWO CRABS: DIG THROUGH A PILE.");
      small(396, "PLAY A PAIR FACE UP TO FIRE ITS POWER.");
      small(422, "ITS POINT COUNTS PLAYED OR NOT.");
      break;
    case 2:
      caption(16, "SWIMMER AND SHARK.");
      tileRow(74, {{3, 1, 5, 0}, {4, 3, 5, 0}});
      small(254, "PLAY BOTH TOGETHER TO STEAL");
      small(280, "A RANDOM CARD FROM THEIR HAND.");
      small(370, "LIKE EVERY DUO, THE PAIR IS A POINT");
      small(396, "WHETHER PLAYED OR KEPT IN HAND.");
      break;
    case 3:
      caption(16, "COLLECTORS GROW.");
      tileRow(74, {{5, 0, 6, 2}, {6, 1, 5, 3}, {7, 6, 3, 3}, {8, 9, 2, 5}});
      small(254, "SHELLS PAY 0-2-4-6-8-10 AS THEY ADD UP.");
      small(280, "TURTLES 0-3-6-9-12. GULLS 1-3-5.");
      small(306, "TWO SAILORS PAY 5.");
      small(396, "THE ONE-OFF CARDS MULTIPLY THEM:");
      small(422, "LIGHTHOUSE 1 PER BOAT. SHOAL 1 PER FISH.");
      small(448, "NEST 2 PER GULL. CAPTAIN 3 PER SAILOR.");
      break;
    case 4:
      caption(16, "MERMAIDS COUNT COLOURS.");
      tileRow(74, {{9, 5, 4, 0}});
      small(254, "EACH MERMAID SCORES YOUR BIGGEST");
      small(280, "COLOUR GROUP, EACH A DIFFERENT ONE.");
      small(370, "HOLD ALL FOUR AND YOU WIN ON THE SPOT.");
      break;
    case 5:
      caption(16, "ENDING A ROUND.");
      small(84, "FROM 7 POINTS, ON YOUR TURN, CALL IT.");
      small(164, "STOP: EVERYBODY BANKS THEIR CARDS.");
      small(244, "LAST CHANCE: A BET THAT YOU HOLD THE MOST.");
      small(270, "THEY GET ONE MORE TURN. IF YOU STILL LEAD,");
      small(296, "YOU BANK CARDS PLUS YOUR COLOUR BONUS");
      small(322, "AND THEY BANK ONLY THEIR BONUS.");
      small(348, "IF THEY PASS YOU, IT IS REVERSED.");
      small(430, "IF THE DECK RUNS OUT, NOBODY SCORES.");
      break;
  }

  // Page dots.
  const int16_t dotY = static_cast<int16_t>(body.y + body.height - 18);
  const int16_t dotsW = static_cast<int16_t>(pages * 16);
  int16_t x = static_cast<int16_t>(body.x + (body.width - dotsW) / 2);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect dot = fui::makeRect(x, dotY, 8, 8);
    if (i == model.page) {
      target.fill(dot, fui::Paint::solid(fui::Color::Black));
    } else {
      target.stroke(dot, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    }
    x = static_cast<int16_t>(x + 16);
  }
}

}  // namespace seasaltui
