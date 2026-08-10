#include "SeaSaltScreens.h"

#include <cstdio>

#include "../link/LinkScreens.h"
#include "SeaSaltArt.h"
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
  screen.header(header);

  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

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
  target.stroke(cell, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(tile.selected ? 5 : 2));

  const bool tall = cell.height >= 110;
  const int16_t markSize = 20;
  const fui::Rect mark = fui::makeRect(cell.x + 7, cell.y + 7, markSize, markSize);
  blitIcon(screen, mark, *kColourMarks[tile.colour]);

  if (tile.groupPoints >= 0) {
    char points[8];
    std::snprintf(points, sizeof(points), "%d", tile.groupPoints);
    target.text(fui::makeRect(cell.x, cell.y + 5, cell.width - 9, 24), points,
                styled(toybox::kUiFont, fui::TextAlign::Right));
  }

  const freeink::Icon& face = tall ? *kIcon48[tile.kind] : *kIcon40[tile.kind];
  const int16_t faceSize = tall ? 48 : 40;
  const int16_t faceTop = static_cast<int16_t>(cell.y + (tall ? 28 : 22));
  blitIcon(screen, fui::makeRect(cell.x + (cell.width - faceSize) / 2, faceTop, faceSize, faceSize), face);

  const int16_t censusTop = static_cast<int16_t>(cell.y + cell.height - 20);
  if (tall) {
    // The small font, as the design had it, centred in the whole band between
    // the face and the census rather than at a guessed offset.
    const int16_t nameTop = static_cast<int16_t>(faceTop + faceSize);
    target.text(fui::makeRect(cell.x, nameTop, cell.width, censusTop - nameTop), kKindNames[tile.kind],
                styled(toybox::kSmallFont, fui::TextAlign::Center));
  }

  char census[12];
  std::snprintf(census, sizeof(census), "%d OF %d", tile.held, tile.supply);
  target.text(fui::makeRect(cell.x, censusTop, cell.width, 18), census,
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
  char depth[8];
  std::snprintf(depth, sizeof(depth), "%d", pile.size);
  target.text(fui::makeRect(cell.x, cell.y + 5, cell.width - 8, 24), depth,
              styled(toybox::kUiFont, fui::TextAlign::Right));
  blitIcon(screen, fui::makeRect(cell.x + (cell.width - 40) / 2, cell.y + 26, 40, 40), *kIcon40[pile.kind]);
  target.text(fui::makeRect(cell.x, cell.y + cell.height - 22, cell.width, 18), kKindNames[pile.kind],
              styled(toybox::kSmallFont, fui::TextAlign::Center));
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
    char value[8];
    std::snprintf(value, sizeof(value), "%d", cells[i].value);
    target.text(fui::makeRect(x + 8, strip.y, 28, strip.height), value, styled(toybox::kUiFont, fui::TextAlign::Left));
    const int16_t half = static_cast<int16_t>(strip.height / 2);
    target.text(fui::makeRect(x + 40, strip.y + 2, cellW - 44, half - 2), cells[i].line1, capStyle);
    target.text(fui::makeRect(x + 40, strip.y + half, cellW - 44, half - 2), cells[i].line2, capStyle);
  }
  // BEST: the count, the mark, the caption.
  const int16_t x = static_cast<int16_t>(strip.x + 3 * cellW);
  target.fill(fui::makeRect(x, strip.y, toybox::kHairline, strip.height), fui::Paint::solid(fui::Color::Black));
  char value[8];
  std::snprintf(value, sizeof(value), "%d", model.bestColourCount);
  target.text(fui::makeRect(x + 8, strip.y, 28, strip.height), value, styled(toybox::kUiFont, fui::TextAlign::Left));
  blitIcon(screen, fui::makeRect(x + 32, strip.y + (strip.height - 20) / 2, 20, 20), *kColourMarks[model.bestColour]);
  target.text(fui::makeRect(x + 58, strip.y + strip.height / 2 - 8, cellW - 62, 16), "BEST", capStyle);
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
  const int16_t tabW = static_cast<int16_t>((strip.width - 2 * toybox::kHairline) / 3);
  for (int i = 0; i < 3; ++i) {
    const int16_t x = static_cast<int16_t>(strip.x + i * (tabW + toybox::kHairline));
    const fui::Rect tab = fui::makeRect(x, strip.y, tabW, strip.height);
    const bool active = model.tab == i;
    target.fill(tab, fui::Paint::solid(active ? fui::Color::Black : fui::Color::White));
    if (!active) target.stroke(tab, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    char label[16];
    std::snprintf(label, sizeof(label), "%s %d", tabs[i].label, tabs[i].count);
    // The small font, as designed, and the whole tab as the text rect: the
    // target centres vertically in the rect it is given, so a hand-computed
    // band is only ever a centring bug waiting to happen.
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
  char score[16];
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
    char n[8];
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
    primary.action = model.primaryEnabled ? ActionPrimary : fui::NO_ACTION;
    if (!model.primaryEnabled) primary.styles = toybox::disabledButtonStyles();
    primary.borderEdges = fui::EdgesNone;
    screen.button(primary, fui::makeRect(pillRow.x, pillRow.y, static_cast<int16_t>(pillRow.width - callW - kCardGap),
                                         pillRow.height));
    char call[20];
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
    char pager[16];
    std::snprintf(pager, sizeof(pager), "%d / %d", model.page + 1, model.pages);
    screen.target().text(fui::makeRect(grid.x, grid.y + grid.height - 16, grid.width, 14), pager,
                         styled(toybox::kSmallFont, fui::TextAlign::Right));
  }
  return grid;
}

// --- the modal choices ------------------------------------------------------

fui::Rect buildKeepChoice(toybox::Screen& screen, const KeepModel& model) {
  toyboxChrome(screen, "SEA SALT");
  const fui::Rect line = screen.takeTop(26, toybox::kGutter);
  screen.target().text(line, "KEEP ONE.", styled(toybox::kUiFont, fui::TextAlign::Center));
  const fui::Rect hintBox = screen.takeBottom(54, toybox::kGutter / 2);
  drawHintBox(screen, hintBox, "THE OTHER GOES FACE UP ON A PILE.");

  const fui::Rect body = screen.body();
  const int16_t cardW = static_cast<int16_t>((body.width - toybox::kGutter) / 2);
  const int16_t cardH = 160;
  // Top-biased: equal slack above and below is unresolved centring.
  const int16_t top = static_cast<int16_t>(body.y + (body.height - cardH) / 3);
  const fui::Rect left = fui::makeRect(body.x, top, cardW, cardH);
  const fui::Rect right = fui::makeRect(static_cast<int16_t>(body.x + cardW + toybox::kGutter), top, cardW, cardH);
  drawCardTile(screen, left, model.left);
  drawCardTile(screen, right, model.right);
  screen.frame().hit(left, ActionKeepLeft, 0);
  screen.frame().hit(right, ActionKeepRight, 0);
  return body;
}

fui::Rect buildPileChoice(toybox::Screen& screen, const PileChoiceModel& model) {
  toyboxChrome(screen, model.digging ? "CRAB" : "SEA SALT");
  const fui::Rect line = screen.takeTop(26, toybox::kGutter);
  char headline[48];
  if (model.digging) {
    std::snprintf(headline, sizeof(headline), "DIG THROUGH WHICH PILE?");
  } else {
    std::snprintf(headline, sizeof(headline), "THE %s GOES ON A PILE.", kKindNames[model.rejected.kind]);
  }
  screen.target().text(line, headline, styled(toybox::kUiFont, fui::TextAlign::Center));

  const fui::Rect body = screen.body();
  int16_t pileTop = body.y;
  if (!model.digging) {
    // The rejected card, small, above the two piles it could land on.
    const int16_t cardW = 108;
    drawCardTile(screen, fui::makeRect(static_cast<int16_t>(body.x + (body.width - cardW) / 2), body.y, cardW, 125),
                 model.rejected);
    pileTop = static_cast<int16_t>(body.y + 150);
  }

  const int16_t pileW = static_cast<int16_t>((body.width - toybox::kGutter) / 2);
  for (int p = 0; p < 2; ++p) {
    const fui::Rect cell =
        fui::makeRect(static_cast<int16_t>(body.x + p * (pileW + toybox::kGutter)), pileTop, pileW, 120);
    drawPileTile(screen, cell, model.piles[p], model.digging ? "EMPTY" : "EMPTY - GOES HERE");
    if (!model.digging || model.piles[p].size > 0) {
      screen.frame().hit(cell, p == 0 ? ActionPileA : ActionPileB, 0);
    }
  }
  return body;
}

fui::Rect buildDig(toybox::Screen& screen, const DigModel& model) {
  toyboxChrome(screen, "CRAB");
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  screen.target().text(line, "TAKE ANY CARD FROM THIS PILE.", styled(toybox::kUiFont, fui::TextAlign::Center));
  if (model.pages > 1) {
    const fui::Rect pager = screen.takeBottom(24);
    char label[24];
    std::snprintf(label, sizeof(label), "SIDE KEYS - %d / %d", model.page + 1, model.pages);
    screen.target().text(pager, label, styled(toybox::kSmallFont, fui::TextAlign::Center));
  }
  const fui::Rect grid = screen.body();
  for (int i = 0; i < model.tileCount; ++i) {
    drawCardTile(screen, cardCellRect(grid, i, model.tileCount), model.tiles[i]);
  }
  return grid;
}

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
  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", model.page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  screen.frame().hit(body, ActionAdvance, 0);

  auto& target = screen.target();
  auto caption = [&](const int16_t y, const char* text) {
    target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + y), body.width, 20), text,
                styled(toybox::kUiFont, fui::TextAlign::Center));
  };
  auto small = [&](const int16_t y, const char* text) {
    target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + y), body.width, 18), text,
                styled(toybox::kSmallFont, fui::TextAlign::Center));
  };
  auto tile = [&](const int16_t x, const int16_t y, const int kind, const int colour, const int held, const int supply,
                  const int pts) {
    CardTile t;
    t.kind = static_cast<uint8_t>(kind);
    t.colour = static_cast<uint8_t>(colour);
    t.groupPoints = static_cast<int8_t>(pts);
    t.held = static_cast<uint8_t>(held);
    t.supply = static_cast<uint8_t>(supply);
    drawCardTile(screen, fui::makeRect(static_cast<int16_t>(body.x + x), static_cast<int16_t>(body.y + y), 104, 125),
                 t);
  };

  switch (model.page) {
    case 0:
      caption(10, "EVERY TURN: TAKE ONE CARD.");
      small(40, "DRAW TWO AND KEEP ONE,");
      small(62, "OR TAKE THE TOP OF A DISCARD PILE.");
      tile(64, 100, 1, 0, 1, 8, 0);
      tile(280, 100, 2, 2, 1, 7, 0);
      small(250, "FIRST TO 40 POINTS WINS THE GAME.");
      break;
    case 1:
      caption(10, "PAIRS HAVE POWERS.");
      tile(10, 44, 1, 0, 2, 8, 1);
      small(180, "TWO BOATS: TAKE ANOTHER TURN.");
      tile(122, 210, 2, 2, 2, 7, 1);
      small(346, "TWO FISH: DRAW A CARD.");
      tile(240, 376, 0, 4, 2, 9, 1);
      small(512, "TWO CRABS: DIG THROUGH A PILE.");
      break;
    case 2:
      caption(10, "SWIMMER AND SHARK HUNT TOGETHER.");
      tile(110, 50, 3, 1, 1, 5, 0);
      tile(230, 50, 4, 3, 1, 5, 0);
      small(200, "PLAY BOTH: STEAL A RANDOM CARD");
      small(222, "FROM THEIR HAND.");
      small(266, "A PAIR IS A POINT WHETHER YOU");
      small(288, "PLAY IT OR KEEP IT IN HAND.");
      break;
    case 3:
      caption(10, "COLLECTORS GROW.");
      tile(10, 50, 5, 0, 2, 6, 2);
      tile(124, 50, 6, 1, 2, 5, 3);
      tile(238, 50, 7, 6, 2, 3, 3);
      tile(352, 50, 8, 9, 2, 2, 5);
      small(200, "SHELLS 0-2-4-6-8-10. TURTLES 0-3-6-9-12.");
      small(222, "GULLS 1-3-5. SAILORS 0 THEN 5.");
      small(266, "ONE-OFF CARDS MULTIPLY THEM:");
      small(288, "LIGHTHOUSE 1 PER BOAT. SHOAL 1 PER FISH.");
      small(310, "NEST 2 PER GULL. CAPTAIN 3 PER SAILOR.");
      break;
    case 4:
      caption(10, "MERMAIDS COUNT COLOURS.");
      tile(184, 50, 9, 5, 1, 4, 0);
      small(200, "EACH MERMAID SCORES YOUR BIGGEST");
      small(222, "COLOUR GROUP. EACH TAKES A NEW COLOUR.");
      small(266, "HOLD ALL FOUR: YOU WIN ON THE SPOT.");
      break;
    case 5:
      caption(10, "ENDING A ROUND, FROM 7 POINTS.");
      small(50, "STOP: EVERYBODY BANKS THEIR CARDS.");
      small(94, "LAST CHANCE: A BET. THEY GET ONE MORE");
      small(116, "TURN, THEN IF YOU STILL HAVE THE MOST,");
      small(138, "YOU BANK CARDS PLUS YOUR COLOUR BONUS");
      small(160, "AND THEY BANK ONLY THEIR BONUS.");
      small(182, "IF THEY PASS YOU, IT IS REVERSED.");
      small(226, "IF THE DECK RUNS OUT, NOBODY SCORES.");
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
