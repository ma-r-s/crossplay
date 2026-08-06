#include "MurdleScreens.h"

#include <cstdio>
#include <cstring>

#include "MurdleCast.h"
#include "MurdleText.h"

// Three complete arrangements of the two faces, built at once so they can be
// rendered side by side and judged rather than described. Prose about a layout
// is worth almost nothing on this device and a list of options in chat is worth
// less. The winner keeps the switch's contents and the losers are deleted in
// the same commit; a variant macro that survives is a second codepath nobody
// renders. See docs/building-apps.md.
//
//   1  TABS   a two-segment bar under the header, one tap between faces
//   2  RAIL   the grid holds the screen, one clue at a time in a strip below it
//   3  PAGES  no toggle chrome at all, a page indicator and a tap to swap
#ifndef MURDLE_VIEW_VARIANT
#define MURDLE_VIEW_VARIANT 1
#endif

namespace murdleui {

namespace {

using murdle::Cat;
using murdle::Grid;
using murdle::kMaxCats;
using murdle::Mark;
using murdle::Puzzle;
using murdle::Tier;

constexpr int16_t kTabH = 44;
#if MURDLE_VIEW_VARIANT == 2
constexpr int16_t kRailH = 96;
#endif
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

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
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
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  // The page margin. `Screen` starts its content rect at the whole safe area,
  // so this is the caller's job and not the theme's; every screen in this fork
  // takes the same one, plus room under the rule the header does not know it
  // has drawn.
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// One character standing for a whole category, in the grid's gutters where a
// word cannot go. S, W, P, M are all distinct, which is the only reason this
// works and the reason locations are PLACES.
char categoryLetter(const int cat) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return 'S';
    case Cat::Weapon:
      return 'W';
    case Cat::Location:
      return 'P';
    case Cat::Motive:
      return 'M';
  }
  return '?';
}

void putChar(char* out, const char c) {
  out[0] = c;
  out[1] = '\0';
}

// ---------------------------------------------------------------------------
// The grid

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
  const int16_t catLine = screen.target().lineHeight(toybox::kUiFont);
  const int16_t itemLine = screen.target().lineHeight(toybox::kTileFont);
  layout.headerH = static_cast<int16_t>(catLine + itemLine + 6);
  layout.gutter = static_cast<int16_t>(catLine + itemLine + 6);

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

void drawGrid(toybox::Screen& screen, const Puzzle& puzzle, const Grid& marks, const GridLayout& g) {
  auto& target = screen.target();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int items = g.items;

  char one[2];
  const fui::TextStyle letter = styled(toybox::kTileFont, fui::TextAlign::Center);
  const fui::TextStyle capital = styled(toybox::kUiFont, fui::TextAlign::Center);
  const int16_t catLine = target.lineHeight(toybox::kUiFont);
  const int16_t itemLine = target.lineHeight(toybox::kTileFont);

  // Column labels: the category letter over each group, then the item letters,
  // in two bands that do not overlap because both are a measured line tall.
  for (int c = 0; c < g.groups; ++c) {
    const int cat = g.colCat[c];
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, cat, letters);
    putChar(one, categoryLetter(cat));
    target.text(fui::makeRect(g.cellX(c * items), static_cast<int16_t>(g.originY - g.headerH),
                              static_cast<int16_t>(items * g.cell), catLine),
                one, capital);
    for (int i = 0; i < items; ++i) {
      putChar(one, letters[i]);
      target.text(
          fui::makeRect(g.cellX(c * items + i), static_cast<int16_t>(g.originY - itemLine - 2), g.cell, itemLine), one,
          letter);
    }
  }

  // Row labels, the same two bands turned on their side: the category letter in
  // the outer column, the item letters in the inner one.
  const int16_t catCol = static_cast<int16_t>(catLine + 2);
  for (int r = 0; r < g.groups; ++r) {
    const int cat = g.rowCat[r];
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, cat, letters);
    const int16_t groupY = g.cellY(r * items);
    const int16_t groupH = static_cast<int16_t>(items * g.cell);
    putChar(one, categoryLetter(cat));
    target.text(fui::makeRect(static_cast<int16_t>(g.originX - g.gutter),
                              static_cast<int16_t>(groupY + (groupH - catLine) / 2), catCol, catLine),
                one, capital);
    for (int i = 0; i < items; ++i) {
      putChar(one, letters[i]);
      target.text(fui::makeRect(static_cast<int16_t>(g.originX - g.gutter + catCol),
                                static_cast<int16_t>(g.cellY(r * items + i) + (g.cell - itemLine) / 2),
                                static_cast<int16_t>(g.gutter - catCol - 4), itemLine),
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

          const Mark mark = marks.get(g.rowCat[r], ir, g.colCat[c], ic);
          if (mark == Mark::No) {
            // A bar, not a cross. The draw target has fills and strokes and no
            // diagonal, so an X would have to be a bitmap; a bar reads as
            // "ruled out" just as clearly and costs nothing.
            const int16_t barW = static_cast<int16_t>(g.cell * 5 / 9);
            target.fill(fui::makeRect(static_cast<int16_t>(cell.x + (g.cell - barW) / 2),
                                      static_cast<int16_t>(cell.y + g.cell / 2 - 2), barW, 4),
                        ink);
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

// The legend: which letter is which. All of it or none of it.
//
// At four categories of four it does not fit under the grid, and the first
// version drew as much as would go -- which looked exactly like a complete key
// and quietly omitted places and motives. So this measures first and draws
// nothing rather than something misleading; the cast page carries the same
// letters and is one tap away, which is where Murdle itself puts them.
//
// Returns false when it drew nothing.
bool drawLegend(toybox::Screen& screen, const Puzzle& puzzle, const fui::Rect& area) {
  auto& target = screen.target();
  const fui::TextStyle small = styled(toybox::kTileFont, fui::TextAlign::Left);
  const int16_t lh = static_cast<int16_t>(target.lineHeight(toybox::kTileFont) + 2);

  const auto run = [&](const bool draw) {
    int16_t y = area.y;
    for (int cat = 0; cat < puzzle.shape.cats; ++cat) {
      char letters[murdle::kMaxItems + 1];
      murdletext::axisLetters(puzzle, cat, letters);
      char line[192];
      int fill = std::snprintf(line, sizeof(line), "%c ", categoryLetter(cat));
      for (int i = 0; i < puzzle.shape.items && fill < static_cast<int>(sizeof(line)) - 1; ++i) {
        fill += std::snprintf(line + fill, sizeof(line) - static_cast<size_t>(fill), "%s%c=%s", i ? "  " : "",
                              letters[i], murdletext::label(puzzle, cat, i));
      }
      y = static_cast<int16_t>(
          y + paragraph(screen, small, line, fui::makeRect(area.x, y, area.width, static_cast<int16_t>(lh * 8)), draw));
    }
    return y;
  };

  if (run(false) > area.bottom()) return false;
  run(true);
  return true;
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
  const fui::TextStyle name = styled(toybox::kUiFont, fui::TextAlign::Left);
  const fui::TextStyle small = styled(toybox::kTileFont, fui::TextAlign::Left);
  // Measured, never guessed. A hard-coded advance is shorter than the UI cut's
  // line box, so the first version drew every suspect's attributes straight
  // through their name -- and it built, ran and logged nothing.
  const int16_t lh = static_cast<int16_t>(target.lineHeight(toybox::kTileFont) + 2);
  const int16_t nameH = static_cast<int16_t>(target.lineHeight(toybox::kUiFont));

  int16_t y = area.y;
  int block = firstBlock;
  char buf[murdletext::kLineMax];

  for (; block < puzzle.shape.cats; ++block) {
    char letters[murdle::kMaxItems + 1];
    murdletext::axisLetters(puzzle, block, letters);

    // How tall this block wants to be, before committing to any of it. A block
    // half drawn is worse than a block on the next page.
    int16_t want = static_cast<int16_t>(lh + 6);
    if (block == 0) {
      want = static_cast<int16_t>(want + puzzle.shape.items * (nameH + lh + 8));
    } else {
      want = static_cast<int16_t>(want + 16 + puzzle.shape.items * lh);
    }
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
      if (block == 0) {
        char titled[96];
        std::snprintf(titled, sizeof(titled), "%c  %s", letters[i], murdletext::label(puzzle, 0, i));
        if (draw) target.text(fui::makeRect(area.x, y, area.width, nameH), titled, name);
        y = static_cast<int16_t>(y + nameH);
        murdletext::suspectAttributes(puzzle, i, buf, sizeof(buf));
        if (draw) {
          target.text(fui::makeRect(static_cast<int16_t>(area.x + 12), y, static_cast<int16_t>(area.width - 12), lh),
                      buf, small);
        }
        y = static_cast<int16_t>(y + lh + 8);
      } else {
        char line[160];
        const char* mark = murdletext::trait(puzzle, block, i);
        // The letter first, because this page is the grid's key as well as its
        // cast list, and at four categories it is the only key there is room for.
        if (mark[0] != '\0') {
          std::snprintf(line, sizeof(line), "%c  %s  (%s)", letters[i], murdletext::label(puzzle, block, i), mark);
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

    const fui::Rect box = fui::makeRect(static_cast<int16_t>(area.x + 26), y, static_cast<int16_t>(area.width - 26),
                                        static_cast<int16_t>(area.bottom() - y));
    const int16_t height = paragraph(screen, body, line, box, false);
    if (y + height > area.bottom()) break;

    if (draw) {
      char num[8];
      std::snprintf(num, sizeof(num), "%d.", i + 1);
      target.text(fui::makeRect(area.x, y, 24, lh), num, body);
      paragraph(screen, body, line, box, true);
    }

    // Struck through rather than hidden. A clue you have finished with is still
    // a clue you might want to re-read, and hiding it would make the numbering
    // in the list stop matching the numbering in your head.
    if (draw && (struck & (1u << i)) != 0) {
      target.fill(fui::makeRect(area.x, static_cast<int16_t>(y + height / 2 - 1), area.width, 2),
                  fui::Paint::solid(fui::Color::Black));
    }

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
  if (model.puzzle == nullptr || model.marks == nullptr) return report;
  const Puzzle& puzzle = *model.puzzle;

  char right[24];
  std::snprintf(right, sizeof(right), "CASE %d", model.caseNumber);
  chrome(screen, "MURDLE", right);

  fui::ButtonProps accuse;
  accuse.label = model.solved ? "SOLVED" : "ACCUSE";
  accuse.action = model.solved ? fui::NO_ACTION : ActionAccuse;
  accuse.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  accuse.styles = toybox::invertedStyles();
  accuse.radius = 10;
  screen.button(accuse, fui::LayoutAnchor::Bottom);

  fui::Rect body = screen.body();

#if MURDLE_VIEW_VARIANT == 1
  // TABS. Two segments under the header; whichever face is showing owns the
  // whole body. The cost is that the clue you just read is gone the moment you
  // go to mark it.
  for (int i = 0; i < 2; ++i) {
    const bool active = (i == 1) == (model.face == Face::Grid);
    fui::ButtonProps tab;
    tab.label = i == 0 ? "CLUES" : "GRID";
    tab.action = active ? fui::NO_ACTION : ActionFace;
    tab.value = i;
    tab.text = styled(toybox::kUiFont, fui::TextAlign::Center, active ? fui::Color::White : fui::Color::Black);
    tab.styles = active ? toybox::invertedStyles() : toybox::rowStyles();
    tab.radius = 8;
    screen.button(tab, fui::makeRect(static_cast<int16_t>(body.x + i * (body.width / 2)), body.y,
                                     static_cast<int16_t>(body.width / 2 - 4), kTabH));
  }
  body = fui::makeRect(body.x, static_cast<int16_t>(body.y + kTabH + 10), body.width,
                       static_cast<int16_t>(body.height - kTabH - 10));
#elif MURDLE_VIEW_VARIANT == 2
  // RAIL. The grid keeps the screen and one clue sits under it with a stepper,
  // so marking while reading is one gesture instead of three. The clue face is
  // still reachable, it is just no longer where you live.
  if (model.face == Face::Grid) {
    body = fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(body.height - kRailH));
  } else {
    // And a way back. The first render of this variant had the rail's ALL CLUES
    // door and nothing facing the other way, so the clue list was a room with
    // no exit -- which is the sort of thing that is obvious the moment three
    // layouts are next to each other and invisible in any one of them.
    fui::ButtonProps toGrid;
    toGrid.label = "BACK TO THE GRID";
    toGrid.action = ActionFace;
    toGrid.value = 1;
    toGrid.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
    toGrid.styles = toybox::invertedStyles();
    toGrid.radius = 8;
    screen.button(toGrid, fui::makeRect(body.x, body.y, body.width, kTabH));
    body = fui::makeRect(body.x, static_cast<int16_t>(body.y + kTabH + 10), body.width,
                         static_cast<int16_t>(body.height - kTabH - 10));
  }
#else
  // PAGES. No toggle chrome at all: the body itself is the target, and a dot
  // pair says which of the two you are on.
  screen.frame().hit(body, ActionFace, model.face == Face::Grid ? 0 : 1);
#endif

  if (model.face == Face::Grid) {
    layout = layoutGrid(screen, puzzle, body);
    drawGrid(screen, puzzle, *model.marks, layout);
    screen.frame().hit(fui::makeRect(static_cast<int16_t>(layout.originX - layout.gutter),
                                     static_cast<int16_t>(layout.originY - layout.headerH),
                                     static_cast<int16_t>(layout.gutter + layout.groups * layout.items * layout.cell),
                                     static_cast<int16_t>(layout.headerH + layout.groups * layout.items * layout.cell)),
                       ActionGrid, 0);
    const int16_t gridBottom = static_cast<int16_t>(layout.originY + layout.groups * layout.items * layout.cell + 14);
    if (gridBottom < body.bottom()) {
      drawLegend(screen, puzzle,
                 fui::makeRect(body.x, gridBottom, body.width, static_cast<int16_t>(body.bottom() - gridBottom)));
    }
  } else {
    // The clue face is always more than one page, so the pager strip is always
    // there and always comes out of the text area. Reserving it only when
    // pages > 1 would need the count before the measurement that produces it.
    const fui::Rect text = fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(body.height - kPagerH));

    // Walk the whole thing once, measuring, to find how many pages there are
    // and where each starts. The same call that measures is the one that draws,
    // so the count and the split are the same arithmetic and cannot disagree
    // and lose a clue off the end of the last page.
    Stop stops[murdle::kMaxClues + murdle::kMaxCats + 2];
    int pages = 0;
    int used = 0;
    for (int block = 0; block < puzzle.shape.cats && pages < static_cast<int>(sizeof(stops) / sizeof(stops[0]));) {
      stops[pages++] = Stop{true, block};
      drawCastBlocks(screen, puzzle, text, block, used, false);
      block += used;
    }
    for (int clue = 0; clue < puzzle.clueCount && pages < static_cast<int>(sizeof(stops) / sizeof(stops[0]));) {
      stops[pages++] = Stop{false, clue};
      drawCluePage(screen, puzzle, text, clue, 0, used, false);
      clue += used > 0 ? used : 1;
    }
    if (pages == 0) {
      stops[pages++] = Stop{true, 0};
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

#if MURDLE_VIEW_VARIANT == 2
  if (model.face == Face::Grid) {
    const fui::Rect rail =
        fui::makeRect(screen.body().x, static_cast<int16_t>(body.bottom() + 6), screen.body().width, kRailH);
    screen.target().stroke(rail, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);
    char line[murdletext::kLineMax];
    const int shown = model.page < puzzle.clueCount ? model.page : 0;
    murdletext::clueLine(puzzle, shown, line, sizeof(line));
    paragraph(screen, styled(toybox::kTileFont, fui::TextAlign::Left), line, rail.inset(fui::Insets{10, 46, 8, 46}),
              true);
    for (int i = 0; i < 2; ++i) {
      fui::ButtonProps step;
      step.label = i == 0 ? "<" : ">";
      step.action = ActionPage;
      step.value = i == 0 ? -1 : 1;
      step.text = styled(toybox::kUiFont, fui::TextAlign::Center);
      step.styles = toybox::rowStyles();
      step.radius = 8;
      screen.button(step, fui::makeRect(i == 0 ? rail.x : static_cast<int16_t>(rail.right() - 40),
                                        static_cast<int16_t>(rail.y + 24), 40, 44));
    }
    char count[24];
    std::snprintf(count, sizeof(count), "%d/%d", shown + 1, puzzle.clueCount);
    screen.target().text(fui::makeRect(rail.x, static_cast<int16_t>(rail.bottom() - 22), rail.width, 20), count,
                         styled(toybox::kTileFont, fui::TextAlign::Center));
    fui::ButtonProps all;
    all.label = "ALL CLUES";
    all.action = ActionFace;
    all.value = 0;
    all.text = styled(toybox::kTileFont, fui::TextAlign::Center);
    all.styles = toybox::rowStyles();
    all.radius = 8;
    screen.button(all, fui::makeRect(static_cast<int16_t>(rail.x + rail.width / 2 - 60),
                                     static_cast<int16_t>(rail.bottom() - 24), 120, 22));
  }
#endif

  // Paging, on the clue face, in every variant. The clue list runs past one
  // screen at the top tiers and a page that silently drops its tail would be
  // a case that cannot be solved.
  if (model.face == Face::Clues && report.pages > 1) {
    for (int i = 0; i < 2; ++i) {
      const bool live = i == 0 ? report.page > 0 : report.page + 1 < report.pages;
      fui::ButtonProps step;
      step.label = i == 0 ? "<" : ">";
      step.action = live ? ActionPage : fui::NO_ACTION;
      step.value = i == 0 ? -1 : 1;
      step.text = styled(toybox::kUiFont, fui::TextAlign::Center, live ? fui::Color::Black : fui::Color::Black);
      step.styles = live ? toybox::rowStyles() : toybox::disabledStepperStyles();
      step.radius = 8;
      screen.button(step, fui::makeRect(i == 0 ? screen.body().x : static_cast<int16_t>(screen.body().right() - 48),
                                        static_cast<int16_t>(screen.body().bottom() - 46), 48, 40));
    }
    char count[24];
    std::snprintf(count, sizeof(count), "%d / %d", report.page + 1, report.pages);
    screen.target().text(
        fui::makeRect(screen.body().x, static_cast<int16_t>(screen.body().bottom() - 36), screen.body().width, 22),
        count, styled(toybox::kTileFont, fui::TextAlign::Center));
  }

  return report;
}

// ---------------------------------------------------------------------------

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "MURDLE", tierName(model.tier));
  const fui::Rect body = screen.body();
  auto& target = screen.target();

  // The headline is the door, the way Connections' date is. The biggest thing
  // on the screen is also the thing you came here to tap.
  const char* headline = model.hasCase ? (model.caseSolved ? "CASE CLOSED" : "THE CASE") : "A NEW CASE";
  target.text(fui::makeRect(body.x, body.y, body.width, 56), headline,
              styled(toybox::kDisplayFont, fui::TextAlign::Left));
  screen.frame().hit(fui::makeRect(body.x, body.y, body.width, 56), ActionPlay, 0);

  char state[96];
  if (model.hasCase) {
    std::snprintf(state, sizeof(state), "CASE %d, %s", model.caseNumber, tierName(model.tier));
  } else {
    std::snprintf(state, sizeof(state), "%s", tierShape(model.tier));
  }
  target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 58), body.width, 22), state,
              styled(toybox::kTileFont, fui::TextAlign::Left));

  const int16_t ruleY = static_cast<int16_t>(body.y + 92);
  target.fill(fui::makeRect(body.x, ruleY, body.width, toybox::kRule), fui::Paint::solid(fui::Color::Black));

  char record[96];
  std::snprintf(record, sizeof(record), "%d SOLVED   %d WRONG ACCUSATIONS", model.solvedCount, model.wrongCount);
  target.text(fui::makeRect(body.x, static_cast<int16_t>(ruleY + 14), body.width, 22), record,
              styled(toybox::kTileFont, fui::TextAlign::Left));

  // The ornament, and it is the player's own record rather than decoration: a
  // screenshot of it is different on every device. Sixteen cases, two bits
  // each, oldest at the top left.
  target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 150), body.width, 22), "LAST 16 CASES",
              styled(toybox::kTileFont, fui::TextAlign::Center));
  constexpr int16_t kPip = 46;
  constexpr int16_t kPipGap = 8;
  const int16_t rowW = static_cast<int16_t>(4 * kPip + 3 * kPipGap);
  const int16_t pipX = static_cast<int16_t>(body.x + (body.width - rowW) / 2);
  for (int i = 0; i < 16; ++i) {
    const int state2 = static_cast<int>((model.record >> (i * 2)) & 3u);
    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(pipX + (i % 4) * (kPip + kPipGap)),
                                         static_cast<int16_t>(body.y + 180 + (i / 4) * (kPip + kPipGap)), kPip, kPip);
    target.stroke(cell, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 4);
    if (state2 == 1) {
      target.fill(cell.inset(fui::Insets{6, 6, 6, 6}), fui::Paint::solid(fui::Color::Black), 3);
    } else if (state2 == 2) {
      target.fill(cell.inset(fui::Insets{6, 6, 6, 6}), fui::Paint::dither(fui::Color::Black), 3);
    } else if (state2 == 3) {
      target.fill(fui::makeRect(static_cast<int16_t>(cell.x + 10), static_cast<int16_t>(cell.y + kPip / 2 - 2),
                                static_cast<int16_t>(kPip - 20), 4),
                  fui::Paint::solid(fui::Color::Black));
    }
  }

  // Bottom-anchored doors, quietest first: the anchor pops upward, so the row
  // taken first ends up lowest.
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
  fresh.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White);
  fresh.styles = toybox::invertedStyles();
  fresh.radius = 10;
  screen.button(fresh, fui::LayoutAnchor::Bottom);
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
  confirm.action = model.complete() ? ActionConfirm : fui::NO_ACTION;
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
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 56), "DROP THIS CASE?",
                       styled(toybox::kDisplayFont, fui::TextAlign::Left));
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

struct HowToPage {
  const char* title;
  const char* body;
};

// Four beats, in the order you actually do them. No story: this explains a
// mechanic, and a mechanic explained with a character in it takes longer to
// read and is not funnier.
constexpr HowToPage kHowTo[] = {
    {"EVERYONE HAS A PLACE",
     "Each suspect carried one weapon and was in one place. Nobody shares. One of them did it."},
    {"THE GRID IS YOUR PENCIL",
     "Every pair of things meets in one square. Tap it once to rule it out, again to lock it in. Locking one in "
     "crosses off the rest of its row and column for you."},
    {"THE CLUES DO THE REST",
     "A clue never lies, except where the case says a suspect is speaking. Read one, mark what it rules out, and "
     "keep going until the grid is full."},
    {"THEN NAME THEM",
     "The last clue tells you where the body was found, not who left it there. Match that place against your grid, "
     "and accuse."},
};

}  // namespace

int howToPages() { return static_cast<int>(sizeof(kHowTo) / sizeof(kHowTo[0])); }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  char right[16];
  std::snprintf(right, sizeof(right), "%d/%d", model.page + 1, howToPages());
  chrome(screen, "HOW TO SOLVE", right);
  const fui::Rect body = screen.body();
  const int page = model.page < howToPages() ? model.page : 0;

  screen.target().text(fui::makeRect(body.x, body.y, body.width, 48), kHowTo[page].title,
                       styled(toybox::kUiFont, fui::TextAlign::Left));
  paragraph(
      screen, styled(toybox::kTileFont, fui::TextAlign::Left), kHowTo[page].body,
      fui::makeRect(body.x, static_cast<int16_t>(body.y + 56), body.width, static_cast<int16_t>(body.height - 120)),
      true);

  // One tap anywhere carries on, the same gesture the whole way through. Three
  // separate controls for "next" would be three ways to get the same state
  // machine wrong.
  screen.frame().hit(body, ActionHowTo, 0);
}

}  // namespace murdleui
