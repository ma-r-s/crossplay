#include "MurdleScreens.h"

#include <cstdio>
#include <cstring>

#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxIcons.h"
#include "MurdleCast.h"
#include "MurdleText.h"

// TWO PAGES, AND A DOOR IN THE HEADER.
//
// Chosen by rendering all three arrangements side by side against the same
// case. Tabs and a clue rail both spent vertical space on the toggle, and at
// four categories that space is exactly what the grid's legend needs -- so the
// arrangement with no toggle chrome at all is the only one where a player can
// read the grid without flipping to look a letter up.
//
// The cost of no chrome is that nothing says the pages exist, which is why the
// door is a labelled control rather than a bare tap zone: the header's right
// side carries the name of the *other* page with a chevron, so it reads as a
// page turn and says where it goes. It sits in the header because the header is
// already there and already black -- the door costs no vertical space, which
// was the whole reason for choosing this arrangement.
//
// The body cannot be the toggle. It has two jobs of its own now: marking cells
// on the grid, and ticking clues off on the case file.

namespace murdleui {

namespace {

using murdle::Cat;
using murdle::kMaxCats;
using murdle::Mark;
using murdle::Marks;
using murdle::Puzzle;
using murdle::Tier;

// The pager strip at the foot of the clue face. Always reserved: there is
// always more than one page, on every tier.
constexpr int16_t kPagerH = 54;

// Where a page of the clue face starts. The cast and the clues are one paged
// stream, because at four categories the cast alone is two screenfuls and
// treating it as a fixed first page is what ran motives off the bottom.
struct Stop {
  bool cast;
  int index;  // a cast category, or a clue
};

ClueLayout gClueLayout;

fui::TextStyle styled(const fui::FontId font, const fui::TextAlign align, const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  // Always named, even when it matches what the component would apply anyway:
  // FONT_SLOT_SMALL is 0, and a style whose font is 0 with every other field at
  // its default reads as *unset*, so Screen puts the theme's style back and a
  // small label silently returns at full size.
  style.align = align;
  style.color = color;
  return style;
}

// `doorAction` makes the header's right side a control. Left at NO_ACTION the
// label is just a label, which is what every screen but the case wants.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel,
            const fui::ActionId doorAction = fui::NO_ACTION, const int16_t doorValue = 0) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, not trailingText, and the theme's
  // small text is black -- on a solid black band that is invisible and
  // indistinguishable from never having been set. Bitten three times in this
  // fork already.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  const fui::Rect band = toybox::headerBandRect(screen);
  // The page margin. `Screen` starts its content rect at the whole safe area,
  // so this is the caller's job and not the theme's; every screen in this fork
  // takes the same one, plus room under the rule the header does not know it
  // has drawn.
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  if (doorAction != fui::NO_ACTION) {
    // The right two fifths of the band, which is a 190x76 target and cannot
    // reach the title. Registered after the header drew, so the hit rect and
    // the label are the same band and cannot drift apart.
    screen.frame().hit(fui::makeRect(static_cast<int16_t>(band.width * 3 / 5), band.y,
                                     static_cast<int16_t>(band.width * 2 / 5), band.height),
                       doorAction, doorValue);
  }
}

// The mark for a whole category, in the grid's gutters where a word cannot go.
//
// An icon rather than a letter, and the reason is the letters themselves. Items
// are labelled with single initials, and a case is drawn so every initial means
// exactly one thing; spending S, W, P and M on the axes took four letters away
// from that and, worse, made S mean both "the suspects axis" and STABLE. An
// icon is not a letter, so it collides with nothing and gives the alphabet back.
constexpr int16_t kMarkSize = 24;

const freeink::Icon& categoryMark(const int cat) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return icon_murdle_suspects_24;
    case Cat::Weapon:
      return icon_murdle_weapons_24;
    case Cat::Location:
      return icon_murdle_places_24;
    case Cat::Motive:
      return icon_murdle_motives_24;
  }
  return icon_murdle_suspects_24;
}

void drawMark(toybox::Screen& screen, const fui::Rect& where, const int cat) {
  screen.target().bitmap(where, fui::bitmapFromIcon(categoryMark(cat)), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));
}

void putChar(char* out, const char c) {
  out[0] = c;
  out[1] = '\0';
}

// ---------------------------------------------------------------------------
// The grid

// "Ruled out", in two weights.
//
// A bar, not a cross: the draw target has fills and strokes and no diagonal, so
// an X would have to be a bitmap, and a bar reads as ruled out just as clearly.
//
// The two weights are the whole of the answer to "which of these marks are
// mine?". The board crosses out a whole row and column the moment a tick goes
// in, and until now it drew those in the player's own hand -- identical bars,
// no legend, nothing to tell a conclusion from an offer. HOW TO SOLVE names
// both. The heavy one is theirs and only they can take it away; the light one
// is the board's working, and it comes and goes with the tick behind it.
void drawCross(toybox::Screen& screen, const fui::Rect& cell, const int16_t size, const bool derived) {
  const int16_t barW = static_cast<int16_t>(size * 5 / 9);
  const int16_t barH = derived ? static_cast<int16_t>(2) : static_cast<int16_t>(4);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(cell.x + (size - barW) / 2),
                                     static_cast<int16_t>(cell.y + size / 2 - barH / 2), barW, barH),
                       fui::Paint::solid(fui::Color::Black));
}

GridLayout layoutGrid(toybox::Screen& screen, const Puzzle& puzzle, const fui::Rect& area) {
  GridLayout layout;
  layout.groups = puzzle.shape.cats - 1;
  layout.items = puzzle.shape.items;

  // The staircase. Suspects are the left column group; every other category
  // appears once across the top and once down the side, so each pair of
  // categories meets in exactly one block and the bottom right is empty.
  layout.colCat[0] = static_cast<uint8_t>(Cat::Suspect);
  for (int i = 1; i < layout.groups; ++i) {
    layout.colCat[i] = static_cast<uint8_t>(puzzle.shape.cats - i);
  }
  for (int i = 0; i < layout.groups; ++i) {
    layout.rowCat[i] = static_cast<uint8_t>(i + 1);
  }

  // Measured, not chosen. The first version picked 42 and 46 out of the air and
  // the category letter's line box hung down into the item letters, so the
  // column head read as one glyph on top of another.
  const int16_t itemLine = screen.target().lineHeight(toybox::kTileFont);
  layout.headerH = static_cast<int16_t>(kMarkSize + itemLine + 6);
  layout.gutter = static_cast<int16_t>(kMarkSize + itemLine + 6);

  const int cells = layout.groups * layout.items;
  const int16_t byWidth = static_cast<int16_t>((area.width - layout.gutter) / cells);
  const int16_t byHeight = static_cast<int16_t>((area.height - layout.headerH) / cells);
  layout.cell = byWidth < byHeight ? byWidth : byHeight;
  if (layout.cell < 18) layout.cell = 18;

  const int16_t gridW = static_cast<int16_t>(layout.gutter + cells * layout.cell);
  layout.originX = static_cast<int16_t>(area.x + (area.width - gridW) / 2 + layout.gutter);
  layout.originY = static_cast<int16_t>(area.y + layout.headerH);
  layout.valid = true;
  return layout;
}

void drawGrid(toybox::Screen& screen, const Puzzle& puzzle, const Marks& marks, const GridLayout& g) {
  auto& target = screen.target();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int items = g.items;

  char one[2];
  const fui::TextStyle letter = styled(toybox::kTileFont, fui::TextAlign::Center);
  const int16_t itemLine = target.lineHeight(toybox::kTileFont);

  // Column labels: the category letter over each group, then the item letters,
  // in two bands that do not overlap because both are a measured line tall.
  for (int c = 0; c < g.groups; ++c) {
    const int cat = g.colCat[c];
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, cat, letters);
    const int16_t groupW = static_cast<int16_t>(items * g.cell);
    drawMark(screen,
             fui::makeRect(static_cast<int16_t>(g.cellX(c * items) + (groupW - kMarkSize) / 2),
                           static_cast<int16_t>(g.originY - g.headerH), kMarkSize, kMarkSize),
             cat);
    for (int i = 0; i < items; ++i) {
      putChar(one, letters[i]);
      target.text(
          fui::makeRect(g.cellX(c * items + i), static_cast<int16_t>(g.originY - itemLine - 2), g.cell, itemLine), one,
          letter);
    }
  }

  // Row labels, the same two bands turned on their side: the category letter in
  // the outer column, the item letters in the inner one.
  const int16_t markCol = static_cast<int16_t>(kMarkSize + 2);
  for (int r = 0; r < g.groups; ++r) {
    const int cat = g.rowCat[r];
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, cat, letters);
    const int16_t groupY = g.cellY(r * items);
    const int16_t groupH = static_cast<int16_t>(items * g.cell);
    drawMark(screen,
             fui::makeRect(static_cast<int16_t>(g.originX - g.gutter),
                           static_cast<int16_t>(groupY + (groupH - kMarkSize) / 2), kMarkSize, kMarkSize),
             cat);
    for (int i = 0; i < items; ++i) {
      putChar(one, letters[i]);
      target.text(fui::makeRect(static_cast<int16_t>(g.originX - g.gutter + markCol),
                                static_cast<int16_t>(g.cellY(r * items + i) + (g.cell - itemLine) / 2),
                                static_cast<int16_t>(g.gutter - markCol - 4), itemLine),
                  one, letter);
    }
  }

  // The cells. Hairlines inside a block, a heavy rule around it: the grouping
  // is what makes a 12x12 grid readable at all.
  for (int r = 0; r < g.groups; ++r) {
    for (int c = 0; c < g.groups; ++c) {
      if (!g.blockLive(r, c)) continue;
      const fui::Rect block = fui::makeRect(g.cellX(c * items), g.cellY(r * items),
                                            static_cast<int16_t>(items * g.cell), static_cast<int16_t>(items * g.cell));
      for (int ir = 0; ir < items; ++ir) {
        for (int ic = 0; ic < items; ++ic) {
          const fui::Rect cell = fui::makeRect(g.cellX(c * items + ic), g.cellY(r * items + ir), g.cell, g.cell);
          target.stroke(cell, ink, toybox::kHairline);

          const Mark mark = marks.shown(g.rowCat[r], ir, g.colCat[c], ic);
          if (mark == Mark::No) {
            drawCross(screen, cell, g.cell, marks.derived(g.rowCat[r], ir, g.colCat[c], ic));
          } else if (mark == Mark::Yes) {
            const int16_t inset = static_cast<int16_t>(g.cell / 4);
            target.fill(
                fui::makeRect(static_cast<int16_t>(cell.x + inset), static_cast<int16_t>(cell.y + inset),
                              static_cast<int16_t>(g.cell - inset * 2), static_cast<int16_t>(g.cell - inset * 2)),
                ink, 3);
          }
        }
      }
      target.stroke(block, ink, toybox::kRule);
    }
  }
}

// ---------------------------------------------------------------------------
// The clue face

// Greedy wrap, drawn line by line. Deliberately not the target's multi-line
// text(): that delegates to the renderer's own wrapper, which ellipsises what
// does not fit into a glyph the ASCII-subset face does not have, so an
// overflowing sentence simply stops with nothing to show for it.
int16_t paragraph(toybox::Screen& screen, const fui::TextStyle& style, const char* text, const fui::Rect& box,
                  const bool draw) {
  auto& target = screen.target();
  const int16_t lh = target.lineHeight(style.font);
  char line[128] = {};
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

    // Measure the assembled line, never the sum of its words: adding widths
    // undercounts whatever the face does between glyphs and the line then
    // overflows the rect it is drawn into.
    if (kept != 0 && target.measureText(style.font, line, style).width > box.width) {
      line[kept] = '\0';
      if (draw) target.text(fui::makeRect(box.x, y, box.width, lh), line, style);
      y = static_cast<int16_t>(y + lh);
      fill = 0;
      for (int i = 0; i < n && fill + 1 < static_cast<int>(sizeof(line)); ++i) line[fill++] = at[i];
      line[fill] = '\0';
    }
    at += n;
  }
  if (fill != 0) {
    if (draw) target.text(fui::makeRect(box.x, y, box.width, lh), line, style);
    y = static_cast<int16_t>(y + lh);
  }
  return static_cast<int16_t>(y - box.y);
}

// The key: which letter is which. One row a category, four columns, and the
// rows spread through whatever height is left under the grid.
//
// Two things were wrong with the first version and only one of them was the
// obvious one. It packed four entries into a run-on line with two spaces
// between them, which read as a wall -- but it also set the four lines two
// pixels apart and then left a hand's width of empty screen underneath. The
// space was already there; the layout simply was not asking for it.
//
// The fix is spacing, not size. The grid is the thing being read and it keeps
// every pixel it had; the key spends the remainder, spreading its rows to fill
// it and aligning the entries into columns so the eye can run down them.
void drawLegend(toybox::Screen& screen, const Puzzle& puzzle, const fui::Rect& area) {
  auto& target = screen.target();
  const fui::TextStyle small = styled(toybox::kTileFont, fui::TextAlign::Left);
  const int16_t lh = target.lineHeight(toybox::kTileFont);
  const int cats = puzzle.shape.cats;
  if (cats <= 0 || area.height <= 0) return;

  // Spread to fill, but stop at a point where more air stops reading as
  // grouping and starts reading as an unrelated list. Three categories leave
  // far more room than four do, and both should look deliberate.
  const int16_t roomy = static_cast<int16_t>(lh * 5 / 2);
  int16_t pitch = static_cast<int16_t>(area.height / cats);
  if (pitch > roomy) pitch = roomy;
  const int16_t rowH = static_cast<int16_t>(pitch > kMarkSize ? pitch : kMarkSize);
  int16_t y = static_cast<int16_t>(area.y + (area.height - rowH * cats) / 2);
  if (y < area.y) y = area.y;

  const int16_t textX = static_cast<int16_t>(area.x + kMarkSize + 10);

  // The column pitch is measured, not divided. Splitting the width evenly meant
  // an entry could be wider than its column and run into the next one, which is
  // exactly what J=JEALOUSY did -- and it could not be caught in host tests,
  // because the fake draw target there returns a flat ten pixels a character
  // and would have called it fine. Asking the real face at draw time is the
  // only version of this check that is worth anything, and it tightens the
  // columns when the names happen to be short.
  int16_t widest = 0;
  for (int cat = 0; cat < cats; ++cat) {
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, cat, letters);
    for (int i = 0; i < puzzle.shape.items; ++i) {
      char entry[32];
      std::snprintf(entry, sizeof(entry), "%c=%s", letters[i], murdletext::label(puzzle, cat, i));
      const int16_t w = target.measureText(toybox::kTileFont, entry, small).width;
      if (w > widest) widest = w;
    }
  }
  const int16_t evenly = static_cast<int16_t>((area.right() - textX) / puzzle.shape.items);
  int16_t colW = static_cast<int16_t>(widest + 12);
  if (colW > evenly) colW = evenly;  // clamped to the body; the 7-char cap keeps it off this branch

  for (int cat = 0; cat < cats; ++cat) {
    // The same mark as the axis, so the icon is learned once and read
    // everywhere.
    drawMark(screen, fui::makeRect(area.x, static_cast<int16_t>(y + (rowH - kMarkSize) / 2), kMarkSize, kMarkSize),
             cat);

    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, cat, letters);
    for (int i = 0; i < puzzle.shape.items; ++i) {
      char entry[32];
      // No spaces around the equals: at four columns an entry has about a
      // hundred pixels, and J=JEALOUSY is already ninety of them.
      std::snprintf(entry, sizeof(entry), "%c=%s", letters[i], murdletext::label(puzzle, cat, i));
      target.text(
          fui::makeRect(static_cast<int16_t>(textX + i * colW), static_cast<int16_t>(y + (rowH - lh) / 2), colW, lh),
          entry, small);
    }
    y = static_cast<int16_t>(y + rowH);
  }
}

// The cast, as blocks that page. Block 0 is the suspects with their attributes;
// blocks 1..cats-1 are the fixtures of each other category. Draws as many as
// fit from `firstBlock` and reports how many that was.
//
// It pages because at four categories it does not fit: the first version
// assumed one screenful and ran places and motives off the bottom, underneath
// the pager and the accuse button, where they were invisible and the case was
// unsolvable.
void drawCastBlocks(toybox::Screen& screen, const Puzzle& puzzle, const fui::Rect& area, const int firstBlock,
                    int& usedBlocks, const bool draw) {
  auto& target = screen.target();
  const fui::TextStyle small = styled(toybox::kTileFont, fui::TextAlign::Left);
  // Measured, never guessed. A hard-coded advance is shorter than the UI cut's
  // line box, so the first version drew every suspect's attributes straight
  // through their name -- and it built, ran and logged nothing.
  //
  // Only the tile face is measured now: every fixture including a suspect is
  // one line in `small`, so the UI-face style and its line height that the
  // two-line cast used to need both went with it (see the loop below).
  const int16_t lh = static_cast<int16_t>(target.lineHeight(toybox::kTileFont) + 2);

  int16_t y = area.y;
  int block = firstBlock;
  char buf[murdletext::kLineMax];

  for (; block < puzzle.shape.cats; ++block) {
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, block, letters);

    // How tall this block wants to be, before committing to any of it. A block
    // half drawn is worse than a block on the next page. Every category costs
    // the same now that a suspect is one line.
    const int16_t want = static_cast<int16_t>(lh + 6 + 16 + puzzle.shape.items * lh);
    if (block > firstBlock && y + want > area.bottom()) break;

    if (block > firstBlock) {
      y = static_cast<int16_t>(y + 6);
      if (draw) {
        target.fill(fui::makeRect(area.x, y, area.width, toybox::kHairline), fui::Paint::solid(fui::Color::Black));
      }
      y = static_cast<int16_t>(y + 10);
    }
    if (draw) {
      target.text(fui::makeRect(area.x, y, area.width, lh),
                  block == 0 ? "THE SUSPECTS" : murdletext::categoryName(block), small);
    }
    y = static_cast<int16_t>(y + lh + 4);

    for (int i = 0; i < puzzle.shape.items; ++i) {
      {
        // "%c  %s  (%s%s)", sized from that format rather than from the
        // sixty-odd characters the current cast happens to produce. `detail`
        // is whatever murdletext wrote into a kLineMax buffer, so it gets a
        // whole one; a line cut here would be a shortened fact on the case
        // file, which is the one page a player reads to solve the thing.
        constexpr int kCastLineChars = 1                                // %c, the axis letter
                                       + murdletext::kLabelMax          // %s, the label
                                       + toybox::literalChars("with ")  // %s, the preposition or ""
                                       + (murdletext::kLineMax - 1)     // %s, the detail
                                       + toybox::literalChars("  ") + toybox::literalChars("  (") +
                                       toybox::literalChars(")")  // the format's own characters
                                       + 1;                       // the terminator
        char line[kCastLineChars];
        // A SUSPECT IS ONE LINE, THE SAME SHAPE AS EVERY OTHER FIXTURE, and
        // that is what makes the whole cast fit one page at every tier. It used
        // to be two -- a name in the UI face, then an indented dossier line --
        // which cost roughly three times the height of a weapon and pushed the
        // cast alone onto two pages at four categories. Mario asked for exactly
        // this: give the suspects the same format as the rest of the things and
        // everything fits on one page a hundred percent of the time.
        //
        // What goes in the parentheses is the case's own detail for that
        // category: the dossier columns the clues actually use for a suspect
        // (see suspectAttributes) and the fixture detail for everything else
        // (see trait). Both are empty when the case never asks, so no line ever
        // carries something no clue will reach for.
        const char* detail = block == 0 ? (murdletext::suspectAttributes(puzzle, i, buf, sizeof(buf)), buf)
                                        : murdletext::trait(puzzle, block, i);
        // The letter first, because this page is the grid's key as well as its
        // cast list, and at four categories it is the only key there is room for.
        if (detail[0] != '\0') {
          std::snprintf(line, sizeof(line), "%c  %s  (%s%s)", letters[i], murdletext::label(puzzle, block, i),
                        block == 0 ? "" : "with ", detail);
        } else {
          std::snprintf(line, sizeof(line), "%c  %s", letters[i], murdletext::label(puzzle, block, i));
        }
        if (draw) {
          target.text(fui::makeRect(static_cast<int16_t>(area.x + 12), y, static_cast<int16_t>(area.width - 12), lh),
                      line, small);
        }
        y = static_cast<int16_t>(y + lh);
      }
    }
  }
  usedBlocks = block - firstBlock;
  if (usedBlocks <= 0) usedBlocks = 1;
}

// Lays the clues out and, if drawing, draws them. Records where each one landed
// so a tap can find it: the list runs to seventeen lines at the top tier and the
// interaction buffer holds twenty-four in total, so registering a rect per clue
// would crowd out the chrome.
void drawCluePage(toybox::Screen& screen, const Puzzle& puzzle, const fui::Rect& area, const int firstClue,
                  const uint32_t struck, int& usedClues, const bool draw) {
  auto& target = screen.target();
  const fui::TextStyle body = styled(toybox::kTileFont, fui::TextAlign::Left);
  const int16_t lh = target.lineHeight(toybox::kTileFont);
  gClueLayout.count = 0;

  int16_t y = area.y;
  int i = firstClue;
  for (; i < puzzle.clueCount; ++i) {
    char line[murdletext::kLineMax];
    murdletext::clueLine(puzzle, i, line, sizeof(line));

    const fui::Rect box = fui::makeRect(static_cast<int16_t>(area.x + 32), y, static_cast<int16_t>(area.width - 32),
                                        static_cast<int16_t>(area.bottom() - y));
    const int16_t height = paragraph(screen, body, line, box, false);
    if (y + height > area.bottom()) break;

    const bool done = (struck & (1u << i)) != 0;
    if (draw) {
      // The number lives in a box, and the box is the record: outlined while
      // the clue is still in play, filled once it has been used. One element
      // doing both jobs, and it survives a wrapped clue -- a line struck
      // through three ragged lines of text does not.
      const fui::Rect tick = fui::makeRect(area.x, y, 24, static_cast<int16_t>(lh + 2));
      char num[toybox::kIntTextChars];
      std::snprintf(num, sizeof(num), "%d", i + 1);
      if (done) {
        target.fill(tick, fui::Paint::solid(fui::Color::Black), 5);
      } else {
        target.stroke(tick, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 5);
      }
      target.text(fui::makeRect(tick.x, static_cast<int16_t>(tick.y + 1), tick.width, lh), num,
                  styled(toybox::kTileFont, fui::TextAlign::Center, done ? fui::Color::White : fui::Color::Black));
      paragraph(screen, body, line, box, true);
    }

    // Ticked off, never hidden. A clue you have finished with is still a clue
    // you may want to re-read, and removing it would make the numbers in the
    // list stop matching the numbers in your head.

    if (draw && gClueLayout.count < murdle::kMaxClues) {
      gClueLayout.top[gClueLayout.count] = y;
      gClueLayout.index[gClueLayout.count] = static_cast<uint8_t>(i);
      ++gClueLayout.count;
    }
    y = static_cast<int16_t>(y + height + 10);
  }
  gClueLayout.top[gClueLayout.count] = y;
  usedClues = i - firstClue;
}

}  // namespace

ClueLayout lastClueLayout() { return gClueLayout; }

bool cellAt(const GridLayout& layout, const int x, const int y, GridCell& out) {
  if (!layout.valid || layout.cell <= 0) return false;
  if (x < layout.originX || y < layout.originY) return false;

  const int col = (x - layout.originX) / layout.cell;
  const int row = (y - layout.originY) / layout.cell;
  const int span = layout.groups * layout.items;
  if (col < 0 || col >= span || row < 0 || row >= span) return false;

  const int groupC = col / layout.items;
  const int groupR = row / layout.items;
  // The empty corner of the staircase. A tap there is not a cell, and treating
  // it as one would mark a pair that has no square.
  if (!layout.blockLive(groupR, groupC)) return false;

  out.catA = layout.rowCat[groupR];
  out.itemA = row % layout.items;
  out.catB = layout.colCat[groupC];
  out.itemB = col % layout.items;
  return true;
}

bool AccuseModel::complete() const {
  if (puzzle == nullptr) return false;
  for (int c = 0; c < puzzle->shape.cats; ++c) {
    if (picks[c] == kNothingPicked) return false;
  }
  return true;
}

const char* tierName(const Tier tier) {
  switch (tier) {
    case Tier::Elementary:
      return "ELEMENTARY";
    case Tier::Nosy:
      return "NOSY";
    case Tier::HardBoiled:
      return "HARD BOILED";
    case Tier::Impossible:
      return "IMPOSSIBLE";
  }
  return "";
}

const char* tierShape(const Tier tier) {
  switch (tier) {
    case Tier::Elementary:
      return "3 OF EACH, PLAIN CLUES";
    case Tier::Nosy:
      return "4 OF EACH, NOTHING SAID PLAINLY";
    case Tier::HardBoiled:
      return "4 OF EACH PLUS MOTIVES";
    case Tier::Impossible:
      return "MOTIVES, AND THE KILLER LIES";
  }
  return "";
}

// ---------------------------------------------------------------------------

CaseReport buildCase(toybox::Screen& screen, const CaseModel& model) {
  CaseReport report;
  GridLayout& layout = report.grid;
  // Hand the page back unchanged unless the clue face actually re-measures it.
  // The activity takes this value as authoritative, so a default of zero here
  // would silently send the reader back to page one every time they looked at
  // the grid.
  report.page = model.page;
  if (model.puzzle == nullptr || model.marks == nullptr) return report;
  const Puzzle& puzzle = *model.puzzle;

  // TWO DOORS, NOT ONE REVOLVING ONE.
  //
  // A single cycling door was one label naming the next face, which is fine at
  // two faces and wrong at three: reaching the far one cost two taps and two
  // full-screen refreshes on a panel that takes about a second each, plus a
  // guess about which way round the cycle went. Both other faces are now one
  // tap away and always in the same place.
  //
  // Icons rather than words because there is no room for two words beside the
  // title, and because a lattice and a folder are told apart at a glance where
  // "GRID" and "INFO" have to be read.
  chrome(screen, "MURDLE", nullptr);
  {
    const fui::Rect band = toybox::headerBandRect(screen);
    const int here = static_cast<int>(model.face);
    // The two faces that are not this one, left to right in face order:
    // CLUES, GRID, INFO. That is an ORDER, not a fixed slot per face -- with
    // two doors and three faces, GRID is the left-hand door on the clues page
    // and the right-hand one on the info page. What stays constant is the
    // sequence, so the doors read like a row of tabs with the current one
    // taken out rather than like two buttons that move around.
    int other[2] = {0, 0};
    int n = 0;
    for (int f = 0; f < kFaceCount; ++f) {
      if (f != here) other[n++] = f;
    }
    const freeink::Icon* marks[kFaceCount] = {&icon_murdle_face_clues_24, &icon_murdle_face_grid_24,
                                              &icon_murdle_face_info_24};
    constexpr int16_t kDoor = 56;  // 56x76 each: a comfortable thumb, twice over
    for (int i = 0; i < 2; ++i) {
      const int16_t x = static_cast<int16_t>(band.width - kDoor * (2 - i));
      const fui::Rect box = fui::makeRect(x, band.y, kDoor, band.height);
      // White on the black band. Inset so the two icons do not touch each other
      // or the screen edge.
      screen.target().bitmap(
          fui::makeRect(static_cast<int16_t>(x + (kDoor - 24) / 2), toybox::bandCenterY(screen, 24), 24, 24),
          fui::bitmapFromIcon(*marks[other[i]]), fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::White));
      // Registered after the header drew, so the hit rect and the icon come
      // from the same arithmetic and cannot drift apart.
      screen.frame().hit(box, ActionFace, static_cast<int16_t>(other[i]));
    }
  }

  fui::ButtonProps accuse;
  accuse.label = model.solved ? "SOLVED" : "ACCUSE";
  accuse.action = model.solved ? fui::NO_ACTION : static_cast<fui::ActionId>(ActionAccuse);
  accuse.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  accuse.styles = toybox::invertedStyles();
  accuse.radius = 10;
  screen.button(accuse, fui::LayoutAnchor::Bottom);

  fui::Rect body = screen.body();

  if (model.face == Face::Grid) {
    // The notice band is reserved on EVERY render of this face, empty or not,
    // for the same reason the pager strip is. Taking it only when there is
    // something to say moved the grid the moment a tap was refused and back
    // when the next tap cleared it, which on this panel is a full refresh of
    // the one surface the player is reading by position. It also re-measured
    // the cell against a shorter area, which every tier survived only because
    // every tier is width-bound today.
    //
    // The band comes out of the grid's own room rather than the key's. The key
    // is what a refusal sends the player to check, so squeezing it to make
    // space for the message would take away the thing the message points at --
    // and the grid is width-bound at every tier, so it loses no cell size to
    // this, only the slack the key was spreading into.
    //
    // It sits BETWEEN the grid and the key, not above the grid: the band is
    // reserved off the BOTTOM of the grid's room rather than the top, so the
    // grid draws at the top of the face and the reserved height falls in the
    // gap above the key. The grid is top-anchored and width-bound, so shrinking
    // its area from the bottom leaves originY -- and every cell -- exactly
    // where it was whether or not a notice is showing, which is the no-jump
    // property the reservation exists for. The key keeps the same top and
    // height it had when the band lived up top, because the band is the same
    // size and now sits directly above the key instead of above the grid.
    //
    // ONE LINE, measured rather than chosen, and the reason the band is cheap
    // enough to keep. blockedLine says murdletext::kBlockedNotice and nothing
    // else, so its width does not depend on the cast: at the real tile cut
    // against this face's 448px body it is one line with room to spare, which
    // host-tests/murdle measures against the font itself rather than trusting
    // the character count. The wording used to name both blocking pairs, which
    // at real fixture names came to 710px and two wrapped lines -- and because
    // the band is reserved on every frame, the board paid a paragraph of its
    // room on every frame to say something on a handful of them.
    constexpr int kNoticeLines = 1;
    const int16_t noticeH = static_cast<int16_t>(screen.target().lineHeight(toybox::kTileFont) * kNoticeLines + 8);
    // The full body bottom, kept before the grid's room is shortened: the key
    // still reaches it, so it keeps the room it had and the band is paid for
    // out of the grid's slack, not the key's.
    const int16_t bodyBottom = body.bottom();
    body = fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(body.height - noticeH));
    layout = layoutGrid(screen, puzzle, body);
    drawGrid(screen, puzzle, *model.marks, layout);
    screen.frame().hit(fui::makeRect(static_cast<int16_t>(layout.originX - layout.gutter),
                                     static_cast<int16_t>(layout.originY - layout.headerH),
                                     static_cast<int16_t>(layout.gutter + layout.groups * layout.items * layout.cell),
                                     static_cast<int16_t>(layout.headerH + layout.groups * layout.items * layout.cell)),
                       ActionGrid, 0);
    const int16_t gridBottom = static_cast<int16_t>(layout.originY + layout.groups * layout.items * layout.cell + 8);
    // The refusal band, in the gap the grid gave up. Top of the band rather
    // than centred in it, so the message starts on the row the band starts on
    // however tall the band is sized.
    if (model.notice != nullptr && model.notice[0] != '\0') {
      paragraph(screen, styled(toybox::kTileFont, fui::TextAlign::Center), model.notice,
                fui::makeRect(body.x, gridBottom, body.width, noticeH), true);
    }
    const int16_t keyTop = static_cast<int16_t>(gridBottom + noticeH);
    drawLegend(screen, puzzle, fui::makeRect(body.x, keyTop, body.width, static_cast<int16_t>(bodyBottom - keyTop)));
  } else {
    // The pager strip always comes out of the text area, even on a face that
    // turns out to be one page. Reserving it only when pages > 1 would need the
    // count before the measurement that produces it -- and the measurement is
    // done against this rect, so the count would be measuring a taller page
    // than the one it then draws into and would lose a clue off the bottom.
    // Since the cast moved to its own face, INFO is one page always and CLUES
    // usually is too, so this is a few pixels of honest waste rather than the
    // permanent cost it was when every case ran to three or four pages.
    const fui::Rect text = fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(body.height - kPagerH));

    // Walk the whole thing once, measuring, to find how many pages there are
    // and where each starts. The same call that measures is the one that draws,
    // so the count and the split are the same arithmetic and cannot disagree
    // and lose a clue off the end of the last page.
    // One face, one kind of thing. The cast used to be paged into the front of
    // this same stream, which is what made a case three or four pages deep and
    // moved the boundary around with the tier.
    const bool onInfo = model.face == Face::Info;
    Stop stops[murdle::kMaxClues + murdle::kMaxCats + 2];
    int pages = 0;
    int used = 0;
    if (onInfo) {
      for (int block = 0; block < puzzle.shape.cats && pages < static_cast<int>(sizeof(stops) / sizeof(stops[0]));) {
        stops[pages++] = Stop{true, block};
        drawCastBlocks(screen, puzzle, text, block, used, false);
        block += used > 0 ? used : 1;
      }
    } else {
      for (int clue = 0; clue < puzzle.clueCount && pages < static_cast<int>(sizeof(stops) / sizeof(stops[0]));) {
        stops[pages++] = Stop{false, clue};
        drawCluePage(screen, puzzle, text, clue, 0, used, false);
        clue += used > 0 ? used : 1;
      }
    }
    if (pages == 0) {
      stops[pages++] = Stop{onInfo, 0};
    }
    report.pages = pages;
    report.page = model.page < pages ? model.page : pages - 1;
    if (report.page < 0) report.page = 0;

    gClueLayout.count = 0;
    const Stop& stop = stops[report.page];
    if (stop.cast) {
      drawCastBlocks(screen, puzzle, text, stop.index, used, true);
    } else {
      drawCluePage(screen, puzzle, text, stop.index, model.struck, used, true);
    }
  }

  // Paging, on the clue face, in every variant. The clue list runs past one
  // screen at the top tiers and a page that silently drops its tail would be
  // a case that cannot be solved.
  if (model.face == Face::Clues && report.pages > 1) {
    for (int i = 0; i < 2; ++i) {
      const bool live = i == 0 ? report.page > 0 : report.page + 1 < report.pages;
      fui::ButtonProps step;
      step.label = i == 0 ? "<" : ">";
      step.action = live ? static_cast<fui::ActionId>(ActionPage) : fui::NO_ACTION;
      step.value = i == 0 ? -1 : 1;
      step.text = styled(toybox::kUiFont, fui::TextAlign::Center, live ? fui::Color::Black : fui::Color::Black);
      step.styles = live ? toybox::rowStyles() : toybox::disabledStepperStyles();
      step.radius = 8;
      screen.button(step, fui::makeRect(i == 0 ? screen.body().x : static_cast<int16_t>(screen.body().right() - 48),
                                        static_cast<int16_t>(screen.body().bottom() - 46), 48, 40));
    }
    int done = 0;
    for (int i = 0; i < puzzle.clueCount; ++i) {
      if (model.struck & (1u << i)) ++done;
    }
    // "%d / %d      %d OF %d DONE"
    constexpr int kCountChars = toybox::kIntChars + toybox::kIntChars + toybox::kIntChars + toybox::kIntChars +
                                toybox::literalChars(" /        OF  DONE") + 1;
    char count[kCountChars];
    std::snprintf(count, sizeof(count), "%d / %d      %d OF %d DONE", report.page + 1, report.pages, done,
                  puzzle.clueCount);
    screen.target().text(
        fui::makeRect(screen.body().x, static_cast<int16_t>(screen.body().bottom() - 36), screen.body().width, 22),
        count, styled(toybox::kTileFont, fui::TextAlign::Center));
  }

  return report;
}

// ---------------------------------------------------------------------------

// THE FRONT DOOR: your own board, small.
//
// Chosen by building four and rendering them side by side against the same
// case. The one it replaces carried a 4x4 block of your last sixteen cases,
// lifted from Connections without checking whether it meant anything here. In
// Connections it is a calendar -- one puzzle a day, so sixteen days is a shape
// you recognise and a streak you can lose. Murdle has no cadence at all: cases
// are endless and on demand, so sixteen boxes in arbitrary order say nothing,
// and on a fresh device they are sixteen *empty* boxes taking half the screen.
// Decoration has to carry the player's own data and that data has to mean
// something; the pips passed the first test and failed the second.
//
// A miniature of the real grid passes both, and it is not really decoration at
// all -- it is the thing itself. It answers "how far in am I" as a texture
// rather than a number, it is different on every device and every case, and it
// could not appear in any other app on this shelf.
//
// The three that lost: the case's cast listed (which is the case-file page
// again), a bordered card of facts (calm, and the only one whose empty state
// looked deliberate -- its numbers survive here, under the board), and one
// enormous case number (fast, confident, and nothing to look at on a panel that
// holds its image for hours).

namespace {

// The doors, bottom-anchored, quietest first: the anchor pops upward, so the
// row taken first ends up lowest.
void menuDoors(toybox::Screen& screen, const MenuModel& model) {
  fui::ButtonProps how;
  how.label = "HOW TO SOLVE";
  how.action = ActionHowTo;
  how.text = styled(toybox::kUiFont, fui::TextAlign::Center);
  how.styles = toybox::rowStyles();
  how.radius = 10;
  screen.button(how, fui::LayoutAnchor::Bottom);

  fui::ButtonProps settings;
  settings.label = "DIFFICULTY";
  settings.action = ActionSettings;
  settings.text = styled(toybox::kUiFont, fui::TextAlign::Center);
  settings.styles = toybox::rowStyles();
  settings.radius = 10;
  screen.button(settings, fui::LayoutAnchor::Bottom);

  fui::ButtonProps fresh;
  fresh.label = model.hasCase ? "NEW CASE" : "OPEN A CASE";
  fresh.action = ActionNewCase;
  // The UI cut, not the display cut, and this is a fix rather than a
  // preference. A button centres its label's *line box*, and the display cut's
  // line box carries descender space that an all-caps label never uses -- so
  // the glyphs sat visibly high in the pill while its two neighbours, set
  // smaller, looked centred. The emphasis was never coming from the size
  // anyway: a black pill with knocked-out white type is already the loudest
  // thing on the screen.
  fresh.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  fresh.styles = toybox::invertedStyles();
  fresh.radius = 10;
  screen.button(fresh, fui::LayoutAnchor::Bottom);
}

// A miniature of the real grid, marks and all. No labels: at this size it is a
// picture of how far in you are, not something to read.
void miniGrid(toybox::Screen& screen, const murdle::Puzzle& puzzle, const murdle::Marks& marks, const fui::Rect& area) {
  auto& target = screen.target();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int groups = puzzle.shape.cats - 1;
  const int items = puzzle.shape.items;
  const int span = groups * items;

  int16_t cell = static_cast<int16_t>((area.width < area.height ? area.width : area.height) / span);
  if (cell < 6) cell = 6;
  const int16_t size = static_cast<int16_t>(cell * span);
  const int16_t x0 = static_cast<int16_t>(area.x + (area.width - size) / 2);
  const int16_t y0 = static_cast<int16_t>(area.y + (area.height - size) / 2);

  uint8_t colCat[murdle::kMaxCats] = {};
  uint8_t rowCat[murdle::kMaxCats] = {};
  colCat[0] = 0;
  for (int i = 1; i < groups; ++i) colCat[i] = static_cast<uint8_t>(puzzle.shape.cats - i);
  for (int i = 0; i < groups; ++i) rowCat[i] = static_cast<uint8_t>(i + 1);

  for (int r = 0; r < groups; ++r) {
    for (int c = 0; c < groups; ++c) {
      if (r + c >= groups) continue;
      const fui::Rect block =
          fui::makeRect(static_cast<int16_t>(x0 + c * items * cell), static_cast<int16_t>(y0 + r * items * cell),
                        static_cast<int16_t>(items * cell), static_cast<int16_t>(items * cell));
      for (int ir = 0; ir < items; ++ir) {
        for (int ic = 0; ic < items; ++ic) {
          const fui::Rect at = fui::makeRect(static_cast<int16_t>(block.x + ic * cell),
                                             static_cast<int16_t>(block.y + ir * cell), cell, cell);
          target.stroke(at, ink, toybox::kHairline);
          const murdle::Mark mark = marks.shown(rowCat[r], ir, colCat[c], ic);
          if (mark == murdle::Mark::No) {
            target.fill(fui::makeRect(static_cast<int16_t>(at.x + cell / 3), static_cast<int16_t>(at.y + cell / 2 - 1),
                                      static_cast<int16_t>(cell / 3), 2),
                        ink);
          } else if (mark == murdle::Mark::Yes) {
            target.fill(fui::makeRect(static_cast<int16_t>(at.x + cell / 4), static_cast<int16_t>(at.y + cell / 4),
                                      static_cast<int16_t>(cell / 2), static_cast<int16_t>(cell / 2)),
                        ink);
          }
        }
      }
      target.stroke(block, ink, toybox::kRule);
    }
  }
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "MURDLE", tierName(model.tier));
  menuDoors(screen, model);
  const fui::Rect body = screen.body();
  auto& target = screen.target();

  // The headline is the way in. The biggest thing on the screen is the thing
  // you came here to tap.
  const char* headline = model.hasCase ? (model.caseSolved ? "CASE CLOSED" : "THE CASE") : "A NEW CASE";
  const int16_t big = target.lineHeight(toybox::kDisplayFont);
  const int16_t lh = target.lineHeight(toybox::kTileFont);
  target.text(fui::makeRect(body.x, body.y, body.width, big), headline,
              styled(toybox::kDisplayFont, fui::TextAlign::Left));
  screen.frame().hit(fui::makeRect(body.x, body.y, body.width, big), ActionPlay, 0);

  char state[96];
  if (!model.hasCase || model.puzzle == nullptr) {
    std::snprintf(state, sizeof(state), "%s", tierShape(model.tier));
  } else if (model.caseSolved) {
    std::snprintf(state, sizeof(state), "CASE %d, %s, CLOSED", model.caseNumber, tierName(model.tier));
  } else {
    std::snprintf(state, sizeof(state), "CASE %d, %s", model.caseNumber, tierName(model.tier));
  }
  target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + big + 2), body.width, lh), state,
              styled(toybox::kTileFont, fui::TextAlign::Left));
  int16_t y = static_cast<int16_t>(body.y + big + lh + 14);

  if (!model.hasCase || model.puzzle == nullptr || model.marks == nullptr) {
    // Nothing to show but the fact that there is nothing. Said once, plainly,
    // rather than drawn as an empty ornament.
    target.text(fui::makeRect(body.x, y, body.width, lh), "NO CASE OPEN.",
                styled(toybox::kTileFont, fui::TextAlign::Left));
    return;
  }

  // Two lines of facts under the board, kept from the card design that lost:
  // the board says how far in you are, these say what "in" consists of.
  char facts[2][64];
  std::snprintf(facts[0], sizeof(facts[0]), "%d OF %d CLUES USED", model.cluesTicked, model.puzzle->clueCount);
  std::snprintf(facts[1], sizeof(facts[1]), "%d OF %d SQUARES SETTLED", model.marks->decided(), model.marks->cells());
  const int16_t factsH = static_cast<int16_t>(lh * 2 + 10);

  // The board takes everything between the state line and the facts, which is
  // what closes the gap the four candidates all had between their content and
  // their doors.
  const fui::Rect board = fui::makeRect(body.x, y, body.width, static_cast<int16_t>(body.bottom() - y - factsH));
  miniGrid(screen, *model.puzzle, *model.marks, board);
  screen.frame().hit(board, ActionPlay, 0);

  y = static_cast<int16_t>(board.bottom() + 6);
  for (int i = 0; i < 2; ++i) {
    target.text(fui::makeRect(body.x, y, body.width, lh), facts[i], styled(toybox::kTileFont, fui::TextAlign::Center));
    y = static_cast<int16_t>(y + lh);
  }
}

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "DIFFICULTY", "");
  const fui::Rect body = screen.body();
  auto& target = screen.target();

  // All four at once, rather than a stepper you pump. The stepper version left
  // two thirds of the screen empty, and on a panel that holds its image for
  // hours dead space at the bottom of a layout is a real defect rather than
  // merely untidy. It was also worse at the job: you could not see what you
  // were choosing between.
  const int16_t rowH = 76;
  for (int i = 0; i < murdle::kTierCount; ++i) {
    const Tier value = static_cast<Tier>(i);
    const bool current = value == model.tier;
    const fui::Rect row = fui::makeRect(body.x, static_cast<int16_t>(body.y + i * (rowH + 10)), body.width, rowH);

    fui::ButtonProps pick;
    pick.label = "";
    pick.action = ActionTier;
    pick.value = static_cast<int16_t>(i);
    pick.styles = current ? toybox::invertedStyles() : toybox::rowStyles();
    pick.radius = 10;
    screen.button(pick, row);

    // The label is drawn over the button rather than through it, because the
    // tier needs two lines: its name, and the shape it actually means. A button
    // draws one.
    const fui::Color ink = current ? fui::Color::White : fui::Color::Black;
    const int16_t nameH = target.lineHeight(toybox::kUiFont);
    const int16_t shapeH = target.lineHeight(toybox::kTileFont);
    const int16_t top = static_cast<int16_t>(row.y + (rowH - nameH - shapeH) / 2);
    target.text(fui::makeRect(static_cast<int16_t>(row.x + toybox::kGutter), top,
                              static_cast<int16_t>(row.width - toybox::kGutter * 2), nameH),
                tierName(value), styled(toybox::kUiFont, fui::TextAlign::Left, ink));
    target.text(fui::makeRect(static_cast<int16_t>(row.x + toybox::kGutter), static_cast<int16_t>(top + nameH),
                              static_cast<int16_t>(row.width - toybox::kGutter * 2), shapeH),
                tierShape(value), styled(toybox::kTileFont, fui::TextAlign::Left, ink));
  }

  // The one thing worth saying, because it is the question anybody with a case
  // open would otherwise have to find out the hard way.
  const char* note = model.caseOpen
                         ? "The case you have open keeps the size it was made at. This applies to the next one."
                         : "Applies to the next case.";
  paragraph(screen, styled(toybox::kTileFont, fui::TextAlign::Left), note,
            fui::makeRect(body.x, static_cast<int16_t>(body.y + murdle::kTierCount * (rowH + 10) + 16), body.width, 80),
            true);
}

void buildAccuse(toybox::Screen& screen, const AccuseModel& model) {
  chrome(screen, "ACCUSE", "");
  if (model.puzzle == nullptr) return;
  const Puzzle& puzzle = *model.puzzle;
  const fui::Rect body = screen.body();
  auto& target = screen.target();

  // Two per row, not four. Four across gives each name 107px, and THE
  // APOTHECARY and DAME ROOKWOOD do not fit in that -- they were ellipsised,
  // on the one screen in the game where you have to be certain which person
  // you are naming. Two across gives 220px, which the longest name in the
  // table clears, and it fills a screen that was otherwise two thirds empty.
  const int16_t rowH = 40;
  const int16_t gap = 8;
  const int16_t cellW = static_cast<int16_t>((body.width - gap) / 2);
  const int16_t labelH = static_cast<int16_t>(target.lineHeight(toybox::kTileFont) + 4);
  int16_t y = body.y;

  for (int cat = 0; cat < puzzle.shape.cats; ++cat) {
    target.text(fui::makeRect(body.x, y, body.width, labelH), murdletext::categoryName(cat),
                styled(toybox::kTileFont, fui::TextAlign::Left));
    y = static_cast<int16_t>(y + labelH);
    for (int i = 0; i < puzzle.shape.items; ++i) {
      const bool picked = model.picks[cat] == i;
      fui::ButtonProps pick;
      pick.label = murdletext::label(puzzle, cat, i);
      pick.action = ActionPick;
      pick.value = static_cast<int16_t>(cat * 8 + i);
      pick.text = styled(toybox::kTileFont, fui::TextAlign::Center, picked ? fui::Color::White : fui::Color::Black);
      pick.styles = picked ? toybox::invertedStyles() : toybox::rowStyles();
      pick.radius = 8;
      screen.button(pick, fui::makeRect(static_cast<int16_t>(body.x + (i % 2) * (cellW + gap)),
                                        static_cast<int16_t>(y + (i / 2) * (rowH + gap)), cellW, rowH));
    }
    const int rows = (puzzle.shape.items + 1) / 2;
    y = static_cast<int16_t>(y + rows * (rowH + gap) + 10);
  }

  fui::ButtonProps confirm;
  confirm.label = "THAT IS MY ACCUSATION";
  confirm.action = model.complete() ? static_cast<fui::ActionId>(ActionConfirm) : fui::NO_ACTION;
  confirm.text =
      styled(toybox::kUiFont, fui::TextAlign::Center, model.complete() ? fui::Color::White : fui::Color::Black);
  confirm.styles = model.complete() ? toybox::invertedStyles() : toybox::disabledStepperStyles();
  confirm.radius = 10;
  screen.button(confirm, fui::LayoutAnchor::Bottom);
}

void buildVerdict(toybox::Screen& screen, const VerdictModel& model) {
  chrome(screen, "MURDLE", "");
  if (model.puzzle == nullptr) return;
  const fui::Rect body = screen.body();
  auto& target = screen.target();

  target.text(fui::makeRect(body.x, body.y, body.width, 60), model.right ? "CASE CLOSED" : "THEY WALK FREE",
              styled(toybox::kDisplayFont, fui::TextAlign::Center));

  char line[murdletext::kLineMax];
  uint8_t truth[kMaxCats] = {};
  for (int c = 0; c < model.puzzle->shape.cats; ++c) {
    truth[c] = model.puzzle->assign[c][model.puzzle->murderRow];
  }
  murdletext::accusationLine(*model.puzzle, model.right ? truth : model.picks, line, sizeof(line));
  // The note goes wherever the sentence ended, not at a fixed offset. A three
  // line accusation ran straight through it, which is the same mistake as the
  // cast page and the settings rows and is the third time it has been made.
  int16_t y = static_cast<int16_t>(body.y + 80);
  y = static_cast<int16_t>(y + paragraph(screen, styled(toybox::kUiFont, fui::TextAlign::Left), line,
                                         fui::makeRect(body.x, y, body.width, static_cast<int16_t>(body.height - 120)),
                                         true));

  char note[96];
  if (model.right) {
    std::snprintf(note, sizeof(note),
                  model.wrongAccusations == 0 ? "SOLVED, FIRST TIME OF ASKING." : "SOLVED, AFTER %d WRONG ACCUSATIONS.",
                  model.wrongAccusations);
  } else {
    std::snprintf(note, sizeof(note), "THE EVIDENCE DOES NOT SUPPORT IT.");
  }
  const int16_t noteH = target.lineHeight(toybox::kTileFont);
  target.text(fui::makeRect(body.x, static_cast<int16_t>(y + 14), body.width, noteH), note,
              styled(toybox::kTileFont, fui::TextAlign::Left));

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  done.text = styled(toybox::kUiFont, fui::TextAlign::Center);
  done.styles = toybox::rowStyles();
  done.radius = 10;
  screen.button(done, fui::LayoutAnchor::Bottom);

  fui::ButtonProps next;
  next.label = model.right ? "ANOTHER CASE" : "KEEP LOOKING";
  next.action = model.right ? ActionNewCase : ActionKeepLooking;
  next.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White);
  next.styles = toybox::invertedStyles();
  next.radius = 10;
  screen.button(next, fui::LayoutAnchor::Bottom);
}

void buildConfirmNew(toybox::Screen& screen) {
  chrome(screen, "MURDLE", "");
  const fui::Rect body = screen.body();
  screen.target().text(toybox::inkCentred(fui::makeRect(body.x, body.y, body.width, 56), toybox::kDisplayCut),
                       "DROP THIS CASE?", styled(toybox::kDisplayFont, fui::TextAlign::Left));
  paragraph(screen, styled(toybox::kTileFont, fui::TextAlign::Left),
            "The marks you have made will go with it. There is only ever one case open at a time.",
            fui::makeRect(body.x, static_cast<int16_t>(body.y + 70), body.width, 100), true);

  fui::ButtonProps keep;
  keep.label = "KEEP IT";
  keep.action = ActionCancel;
  keep.text = styled(toybox::kUiFont, fui::TextAlign::Center);
  keep.styles = toybox::rowStyles();
  keep.radius = 10;
  screen.button(keep, fui::LayoutAnchor::Bottom);

  fui::ButtonProps drop;
  drop.label = "START A NEW ONE";
  drop.action = ActionConfirmNew;
  drop.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  drop.styles = toybox::invertedStyles();
  drop.radius = 10;
  screen.button(drop, fui::LayoutAnchor::Bottom);
}

// ---------------------------------------------------------------------------

namespace {

struct Beat {
  const char* title;
  const char* body;
};

// Four beats, in the order you actually do them, on one page.
//
// It was four pages and each was a heading and two lines on an 800px screen --
// nine tenths empty, three taps to read four sentences. On a panel that holds
// its image for hours dead space is a defect rather than untidiness, and a
// tutorial you can see all of at once is also a better tutorial: the four steps
// are one loop and splitting them hid that.
//
// No story. This explains a mechanic, and a mechanic explained through a
// character takes longer to read and is not funnier.
constexpr Beat kBeats[] = {
    {"EVERYONE HAS A PLACE",
     "Each suspect carried one weapon and was in one place, and no two of them share. One of them did it."},
    {"THE GRID IS YOUR PENCIL",
     "Every pair of things meets in one square. Tap once to rule it out, again to lock it in. Locking one rules out "
     "the rest of its row and column in a lighter mark. The grid only ever takes back marks it made itself; yours "
     "stay until you clear them."},
    {"THE CLUES DO THE REST",
     "Read one, mark what it rules out, tap its number to tick it off. Clues never lie, unless the case says a "
     "suspect is speaking."},
    // "What was beside it", not "where it was". The last clue names either the
    // room or the murder weapon, and has done since the crime scene stopped
    // always being a place -- but this line went on saying "match that place"
    // for three rounds, so a player following the tutorial was told to look for
    // a room and handed a clue with no room in it. A play-tester found it by
    // reading the rules against the case rather than by solving anything.
    {"THEN NAME THEM",
     "The last clue describes what was beside the body, not who left it there. Find that thing on your grid."},
};

constexpr int kBeatCount = static_cast<int>(sizeof(kBeats) / sizeof(kBeats[0]));

}  // namespace

int howToPages() { return 1; }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  (void)model;
  chrome(screen, "HOW TO SOLVE", "");
  const fui::Rect body = screen.body();
  auto& target = screen.target();
  const fui::TextStyle heading = styled(toybox::kUiFont, fui::TextAlign::Left);
  const fui::TextStyle small = styled(toybox::kTileFont, fui::TextAlign::Left);
  const int16_t headH = target.lineHeight(toybox::kUiFont);
  const int16_t lineH = target.lineHeight(toybox::kTileFont);

  int16_t y = body.y;
  for (int i = 0; i < kBeatCount; ++i) {
    target.text(fui::makeRect(body.x, y, body.width, headH), kBeats[i].title, heading);
    y = static_cast<int16_t>(y + headH + 2);
    y = static_cast<int16_t>(
        y + paragraph(screen, small, kBeats[i].body,
                      fui::makeRect(body.x, y, body.width, static_cast<int16_t>(body.bottom() - y)), true));

    // The four states of a square, drawn with the same marks the grid uses
    // rather than described. Sits under the beat that introduces them.
    //
    // Two by two rather than four across, because "IT WAS THEM" measures wider
    // than a quarter of this screen and would have stopped mid-word with
    // nothing to show for it -- this face has no ellipsis glyph.
    if (i == 1) {
      y = static_cast<int16_t>(y + 10);
      constexpr int16_t kDemo = 40;
      // The two crosses share the bottom row: telling them apart is the whole
      // reason this went from three swatches to four, and a comparison reads
      // where the things being compared are side by side.
      const char* names[4] = {"NOT SURE", "IT WAS THEM", "YOU RULED OUT", "GRID RULED OUT"};
      const int16_t colW = static_cast<int16_t>(body.width / 2);
      const int16_t rowH = static_cast<int16_t>(kDemo + 10);
      for (int c = 0; c < 4; ++c) {
        const int16_t x = static_cast<int16_t>(body.x + (c % 2) * colW);
        const int16_t top = static_cast<int16_t>(y + (c / 2) * rowH);
        const fui::Rect cell = fui::makeRect(x, top, kDemo, kDemo);
        target.stroke(cell, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
        if (c == 2 || c == 3) {
          drawCross(screen, cell, kDemo, c == 3);
        } else if (c == 1) {
          const int16_t inset = static_cast<int16_t>(kDemo / 4);
          target.fill(fui::makeRect(static_cast<int16_t>(x + inset), static_cast<int16_t>(top + inset),
                                    static_cast<int16_t>(kDemo - inset * 2), static_cast<int16_t>(kDemo - inset * 2)),
                      fui::Paint::solid(fui::Color::Black), 3);
        }
        target.text(fui::makeRect(static_cast<int16_t>(x + kDemo + 8), static_cast<int16_t>(top + (kDemo - lineH) / 2),
                                  static_cast<int16_t>(colW - kDemo - 12), lineH),
                    names[c], small);
      }
      y = static_cast<int16_t>(y + rowH * 2 + 6);
    }
    y = static_cast<int16_t>(y + 16);
  }

  // One tap anywhere leaves, which is the only thing there is to do here now
  // that it is a single page.
  screen.frame().hit(body, ActionHowTo, 0);
}

}  // namespace murdleui
