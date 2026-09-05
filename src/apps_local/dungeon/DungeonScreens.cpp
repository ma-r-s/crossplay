#include "DungeonScreens.h"

#include <cstdio>

#include "../ui/ToyboxFormat.h"
#include "DungeonArt.h"

namespace dungeonui {

// The board is a plan drawn on paper: hairline lattice, walls filling their
// cells so runs of rock merge into one mass, floor notes as dots.
//
// Two alternatives were built and rendered beside it and are gone: one where
// unknown ground was dithered rock the player carved light out of, and one
// where cells were separate rounded slabs. The slabs lost on the thing that
// matters most here -- walls that cannot merge do not show the shape of the
// rock, which is what the puzzle is about -- and they lent this one their
// heavy frame on the way out.

namespace {

// The board is a clue lane, a gap, then eight cells: 56 + 13 + 8*48 is 453,
// which is what a 480px panel has left after its page margins. So the lattice
// is sized by the panel rather than by taste, and the 6x6 tutorial simply uses
// fewer of the same cells.
//
// The lane is wider than it looks like it needs to be because the display cut
// is taller than a clue's digit: at 40 the numerals overflowed their box and
// were drawn over the board's own frame. The lane has to hold a line of the
// font it draws with.
constexpr int kCell = 48;
constexpr int kLane = 56;
// The row clues get their own, narrower lane.
//
// kLane is how much room a clue needs VERTICALLY: it has to hold a line of the
// display cut. Using it horizontally too put a 26px digit in the middle of a
// 56px lane, so half the left margin was empty lane -- measured on the panel,
// 30px of air before the first digit against 5px after the board's frame. The
// digit needs its own width plus a chip around it, and that is all.
constexpr int kClueLane = 38;
constexpr int kArtSize = 36;
// A clue's TEXT box is a lane square, because it has to hold a line of the
// display cut. Its CHIP is sized to the cell pitch instead, and that difference
// is the whole point: the box is wider than the cell, so a chip drawn at box
// size overlaps its neighbours by (lane - cell) and the digits run into each
// other. At the board's own 48px cell that was an exact touch and looked
// deliberate; the guide draws at 42 and the same code welded six clue chips
// into one black bar with the numbers melting together.
//
// Derived from the layout's cell rather than from kCell for the same reason:
// this used to be a constant, and the constant was right for exactly one of
// the two boards that use it.
int laneOverhang(const int cell) { return (cell - kLane) / 2; }

// Air between the clue lane and the board's frame.
//
// The frame is stroked OUTSIDE the play area, so it reaches back into whatever
// sits beside it. Once it went from 4px to 9 it reached further than a spent
// clue's chip is inset, and the two overlapped -- the number looked welded to
// the border. Reserving the frame's width in the layout is the fix; insetting
// the chip further is not, because the digit is centred in the box and an
// asymmetric chip would put it off-centre in its own highlight.
//
// Four pixels on top of the frame, so they read as separate rather than as
// touching. The cells give the space up: 48 is still comfortably above the
// theme's 44px minimum touch size.
constexpr int kClueGap = toybox::kBoardFrame + 4;
// The strip above the board carrying the dungeon's name.
constexpr int kNameStrip = 34;

// The bestiary. One kind of monster per dungeon, never more: the original
// varies its creature by dungeon, and a single board carrying four different
// ones reads as clutter rather than as a rule to satisfy.
//
// Which dungeon gets which is a hash of its name, NOT a rotation over its
// index. The rotation was the obvious thing and it was wrong for a reason only
// a render showed: the campaign map lays the dungeons out eight to a row, and
// index % 8 put the same creature down every column. Sixty-four cells of
// perfect vertical stripes read as wallpaper, and they advertise that the
// choice means nothing.
//
// Hashing the name costs nothing, stores nothing, and cannot fall out of step
// with the bank -- and it ties the creature to the dungeon's own identity, so
// a dungeon keeps its monster no matter where it sits in the list.
constexpr const freeink::Icon* kMonsters[] = {
    &icon_monsterSkull_36, &icon_monsterGhost_36, &icon_monsterBug_36, &icon_monsterWorm_36,
    &icon_monsterBird_36,  &icon_monsterFlame_36, &icon_monsterEye_36, &icon_monsterBone_36,
};
constexpr int kMonsterCount = static_cast<int>(sizeof(kMonsters) / sizeof(kMonsters[0]));

uint32_t hashName(const char* text) {
  // FNV-1a. Small, no state, and well enough spread for eight buckets: over the
  // 65 names in the bank the least-used creature still gets four dungeons and
  // no column of the map repeats.
  uint32_t hash = 2166136261u;
  while (*text != '\0') {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619u;
  }
  return hash;
}

const freeink::Icon& monsterArt(const int puzzleIndex) {
  const int index = (puzzleIndex < 0 || puzzleIndex >= dungeon::kPuzzleCount) ? 0 : puzzleIndex;
  return *kMonsters[hashName(dungeon::kPuzzles[index].name) % kMonsterCount];
}

// One chest, everywhere. It is the same object in every dungeon and has no
// reason to change.
const freeink::Icon& chestArt() { return icon_chest_36; }

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, not trailingText, and the theme's
  // default subtitle is black -- which on this solid black band is invisible
  // and indistinguishable from never having been set. Same trap the header
  // title falls into one prop over.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
}

// The full width and height the board actually paints, frame included.
//
// The frame is stroked OUTSIDE the play area, so a layout centred on lane plus
// cells is centred on something 9px narrower than what appears -- which pushed
// the whole board right and left 5px of margin on that side. Whatever centres
// this has to centre what it draws.
int boardDrawnWidth(const int size, const int cellSize) {
  return kClueLane + kClueGap + size * cellSize + toybox::kBoardFrame;
}
int boardDrawnHeight(const int size, const int cellSize) {
  return kLane + kClueGap + size * cellSize + toybox::kBoardFrame;
}

Layout layoutBoard(const fui::Rect& body, const int size, const int cellSize = kCell) {
  Layout layout;
  layout.cell = static_cast<int16_t>(cellSize);
  layout.lane = kLane;
  layout.size = static_cast<int16_t>(size);
  const int x = body.x + (body.width - boardDrawnWidth(size, cellSize)) / 2;
  const int y = body.y + (body.height - boardDrawnHeight(size, cellSize)) / 2;
  layout.board =
      fui::makeRect(static_cast<int16_t>(x + kClueLane + kClueGap), static_cast<int16_t>(y + kLane + kClueGap),
                    static_cast<int16_t>(size * cellSize), static_cast<int16_t>(size * cellSize));
  return layout;
}

fui::Rect cellRect(const Layout& layout, const int row, const int col) {
  return fui::makeRect(static_cast<int16_t>(layout.board.x + col * layout.cell),
                       static_cast<int16_t>(layout.board.y + row * layout.cell), layout.cell, layout.cell);
}

// The artwork standing in a cell. `ink` is the colour it is painted in, which
// is the caller's business: a monster on a white floor is black, and the same
// monster on a view that fills its cell has to be paper. The mark is a 1-bpp
// mask in one colour, so getting this wrong makes it invisible with nothing to
// say so.
void drawArt(toybox::Screen& screen, const fui::Rect& cell, const freeink::Icon& icon, const fui::Color ink) {
  const fui::Rect where =
      fui::makeRect(static_cast<int16_t>(cell.x + (cell.width - kArtSize) / 2),
                    static_cast<int16_t>(cell.y + (cell.height - kArtSize) / 2), kArtSize, kArtSize);
  screen.target().bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain, fui::Paint::solid(ink));
}

// The largest cut of the face that fits `width`, largest first.
//
// A dungeon name runs to 33 characters and the list component's answer to one
// that does not fit is to ellipsise it -- with a glyph the Toybox face does not
// have, because the face is subset to ASCII and U+2026 is not in it. A missing
// glyph draws as nothing, so "THE GRAVEYARD OF THE VERNAL KING" came back as
// "THE GRAVEYARD OF THE VE" with no mark at all to say it had been cut, and the
// only sign anything was wrong was `No glyph for codepoint 8230` in the log.
// Setting the name smaller is not a lie about it; truncating silently is.
fui::FontId fitLabel(toybox::Screen& screen, const char* text, const int width, fui::TextStyle& style) {
  const fui::FontId cuts[2] = {toybox::kUiFont, toybox::kSmallFont};
  for (const fui::FontId cut : cuts) {
    style.font = cut;
    if (screen.target().measureText(cut, text, style).width <= width) return cut;
  }
  return cuts[1];
}

void drawClue(toybox::Screen& screen, const fui::Rect& box, const fui::Rect& chip, const int value, const int placed) {
  char text[toybox::kIntTextChars];
  std::snprintf(text, sizeof(text), "%d", value);
  fui::TextStyle style;
  style.font = toybox::kDisplayFont;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::Black;

  // Three states, and the number stays readable in all of them -- a clue can be
  // the right count and still be in the wrong places, so the player has to be
  // able to recount it.
  //
  //   short    plain number
  //   exact    solid chip, knocked-out number
  //   over     dithered chip, black number
  //
  // Never dimmed type: text() decides ink with `color != White`, so a grey
  // numeral is a black numeral and the code looks like it never ran. A fill
  // behind the number is the only dimming this renderer has.
  //
  // The chips are square and they touch their neighbours, which is deliberate.
  // Rounded ones at this size butted into each other and read as a mistake, and
  // the obvious fix -- shrink them until they clear -- is not available: the
  // numeral's ink measures 43px tall on a 48px row pitch, so a chip small
  // enough to leave a gap is a chip too small to hold its own number. Square
  // and flush reads as a rail of settled clues instead of as crowding, and the
  // fills differ enough that no two neighbours merge.
  if (placed == value) {
    screen.target().fill(chip, fui::Paint::solid(fui::Color::Black));
    style.color = fui::Color::White;
  } else if (placed > value) {
    // LightGray, not DarkGray. The number inside stays black, and 50% dither
    // under black type is close to no contrast at all.
    screen.target().fill(chip, fui::Paint::dither(fui::Color::LightGray));
  }

  // inkCentred rather than the box itself: the display cut's line box is 63
  // and a clue box is 56, so the target's clamp lands the digit low enough to
  // graze the chip's bottom edge once a spent clue fills it.
  screen.target().text(toybox::inkCentred(box, toybox::kDisplayCut), text, style);
}

// Four corner marks around a rect, and nothing in between.
//
// This is how this fork flags a region: Toybox has drawn selection like it
// since Chess. A rounded outline was the first attempt and it was wrong for a
// screen made entirely of square cells and square chips -- the radius was the
// only curve anywhere on the board, so it read as a stray UI element rather
// than as a mark on the map. Brackets also leave what they point at completely
// uncovered, which a ring round a single cell does not.
void cornerBrackets(toybox::Screen& screen, const fui::Rect& box, const int arm = 18,
                    const int weight = toybox::kFrame) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int16_t w = static_cast<int16_t>(weight);
  const int16_t a = static_cast<int16_t>(arm);
  const int16_t right = static_cast<int16_t>(box.right() - a);
  const int16_t bottom = static_cast<int16_t>(box.bottom() - a);
  const int16_t edgeX = static_cast<int16_t>(box.right() - w);
  const int16_t edgeY = static_cast<int16_t>(box.bottom() - w);
  screen.target().fill(fui::makeRect(box.x, box.y, a, w), ink);
  screen.target().fill(fui::makeRect(box.x, box.y, w, a), ink);
  screen.target().fill(fui::makeRect(right, box.y, a, w), ink);
  screen.target().fill(fui::makeRect(edgeX, box.y, w, a), ink);
  screen.target().fill(fui::makeRect(box.x, edgeY, a, w), ink);
  screen.target().fill(fui::makeRect(box.x, bottom, w, a), ink);
  screen.target().fill(fui::makeRect(right, edgeY, a, w), ink);
  screen.target().fill(fui::makeRect(edgeX, bottom, w, a), ink);
}

// Where a guide page wants the eye. Cell coordinates; width 0 means nowhere.
struct Spotlight {
  int8_t row = 0;
  int8_t col = 0;
  int8_t width = 0;
  int8_t height = 0;
  // Ring the clue lanes instead of a patch of board.
  bool lanes = false;
};

// The playing surface: clue lanes, frame, lattice, cells and their occupants.
//
// Shared by the board and by the adventurer's guide, which is the whole point.
// The guide teaches on the same surface the player is about to use, so there is
// no second illustration style to translate; a page of the guide is a board.
void drawBoardSurface(toybox::Screen& screen, const dungeon::Board& board, const Layout& layout,
                      const Spotlight& spotlight) {
  const dungeon::Puzzle& puzzle = board.puzzle();
  const int size = board.size();

  for (int i = 0; i < size; ++i) {
    // A clue's box is a lane square centred on its row or column, not a cell.
    // A cell is shorter than a line of the display cut, so a clue drawn in one
    // had its ink clipped by the board's own edge at the last row -- and the
    // first render showed exactly that, the bottom of the last 5 sliced off.
    const int overhang = laneOverhang(layout.cell);
    const fui::Rect colBox = fui::makeRect(static_cast<int16_t>(layout.board.x + i * layout.cell + overhang),
                                           static_cast<int16_t>(layout.board.y - kClueGap - kLane),
                                           static_cast<int16_t>(kLane), static_cast<int16_t>(kLane));
    const fui::Rect rowBox = fui::makeRect(static_cast<int16_t>(layout.board.x - kClueGap - kLane),
                                           static_cast<int16_t>(layout.board.y + i * layout.cell + overhang),
                                           static_cast<int16_t>(kLane), static_cast<int16_t>(kLane));
    // The chip spans the cell along the lane and stops short of it across, so
    // consecutive chips meet exactly and never overlap, whatever the cell size.
    const fui::Rect colChip =
        fui::makeRect(static_cast<int16_t>(layout.board.x + i * layout.cell), static_cast<int16_t>(colBox.y + 5),
                      layout.cell, static_cast<int16_t>(kLane - 10));
    const fui::Rect rowChip =
        fui::makeRect(static_cast<int16_t>(rowBox.x + 5), static_cast<int16_t>(layout.board.y + i * layout.cell),
                      static_cast<int16_t>(kLane - 10), layout.cell);
    drawClue(screen, colBox, colChip, puzzle.colClues[i], board.colWalls(i));
    drawClue(screen, rowBox, rowChip, puzzle.rowClues[i], board.rowWalls(i));
  }

  // White paper with a hairline lattice, inside a frame far heavier than
  // anything drawn within it, so the playing surface reads as a single object
  // rather than as a grid that happens to have a line round it. kBoardFrame is
  // the weight Toybox reserves for exactly this.
  screen.target().stroke(layout.board.inset(fui::Insets{-toybox::kBoardFrame, -toybox::kBoardFrame,
                                                        -toybox::kBoardFrame, -toybox::kBoardFrame}),
                         fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);
  for (int i = 1; i < size; ++i) {
    const int16_t x = static_cast<int16_t>(layout.board.x + i * layout.cell);
    const int16_t y = static_cast<int16_t>(layout.board.y + i * layout.cell);
    screen.target().fill(fui::makeRect(x, layout.board.y, toybox::kHairline, layout.board.height),
                         fui::Paint::solid(fui::Color::Black));
    screen.target().fill(fui::makeRect(layout.board.x, y, layout.board.width, toybox::kHairline),
                         fui::Paint::solid(fui::Color::Black));
  }

  for (int row = 0; row < size; ++row) {
    for (int col = 0; col < size; ++col) {
      const fui::Rect cell = cellRect(layout, row, col);
      const dungeon::Mark mark = board.mark(row, col);
      const bool monster = board.isMonster(row, col);
      const bool chest = board.isChest(row, col);

      // Walls fill the cell edge to edge, so a run of them merges into one
      // mass. That is the whole readability of the board: the shape of the
      // rock is what you are solving, not the individual squares.
      if (mark == dungeon::Mark::Wall) {
        screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
      } else if (mark == dungeon::Mark::Floor && !monster && !chest) {
        const int16_t dot = 10;
        screen.target().fill(fui::makeRect(static_cast<int16_t>(cell.x + (cell.width - dot) / 2),
                                           static_cast<int16_t>(cell.y + (cell.height - dot) / 2), dot, dot),
                             fui::Paint::solid(fui::Color::Black), dot / 2);
      }

      // A monster or a chest always stands on floor, so it is always drawn on
      // a light cell and always in black. If a view ever fills its cell, this
      // is the line that has to change with it.
      if (monster) drawArt(screen, cell, monsterArt(board.index()), fui::Color::Black);
      if (chest) drawArt(screen, cell, chestArt(), fui::Color::Black);
    }
  }

  if (spotlight.lanes) {
    // The two lanes, bracketed separately. Ringing lanes and board together was
    // the first attempt and it enclosed nearly the whole screen, which points
    // at everything and therefore at nothing.
    const int overhang = laneOverhang(layout.cell);
    cornerBrackets(screen, fui::makeRect(static_cast<int16_t>(layout.board.x + overhang - 4),
                                         static_cast<int16_t>(layout.board.y - kClueGap - kLane - 4),
                                         static_cast<int16_t>(layout.board.width - 2 * overhang + 8),
                                         static_cast<int16_t>(kLane + 8)));
    cornerBrackets(
        screen, fui::makeRect(static_cast<int16_t>(layout.board.x - kClueGap - kClueLane - 4),
                              static_cast<int16_t>(layout.board.y + overhang - 4), static_cast<int16_t>(kClueLane + 8),
                              static_cast<int16_t>(layout.board.height - 2 * overhang + 8)));
  } else if (spotlight.width > 0) {
    // Drawn after every cell, in its own pass. Drawn inside the cell loop it
    // would be overdrawn by whichever neighbour rendered later, which is the
    // broken-rectangle bug this project has already paid for once.
    // INSIDE the region, not around it. Around it was the first attempt and
    // half of every bracket vanished: a region touching the board's edge has
    // the 9px frame behind it, and a black mark on a black frame is not there
    // -- with nothing to say so. Inside, the corners sit on the region's own
    // floor, where they are always visible and still cover nothing that
    // matters, because the furniture is centred in its cell.
    cornerBrackets(screen, fui::makeRect(static_cast<int16_t>(layout.board.x + spotlight.col * layout.cell + 3),
                                         static_cast<int16_t>(layout.board.y + spotlight.row * layout.cell + 3),
                                         static_cast<int16_t>(spotlight.width * layout.cell - 6),
                                         static_cast<int16_t>(spotlight.height * layout.cell - 6)));
  }
}

}  // namespace

bool Layout::cellAt(const int x, const int y, int& row, int& col) const {
  if (cell <= 0) return false;
  if (x < board.x || x >= board.right() || y < board.y || y >= board.bottom()) return false;
  col = (x - board.x) / cell;
  row = (y - board.y) / cell;
  return row >= 0 && row < size && col >= 0 && col < size;
}

void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout) {
  const dungeon::Board& board = *model.board;
  const dungeon::Puzzle& puzzle = board.puzzle();

  char progress[toybox::kSlashCounterChars];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  // The app's name in the band, the dungeon's name in a strip of its own.
  //
  // The dungeon name belongs on this screen -- it is what identifies it, the
  // way the date identifies a Connections board -- but it cannot go in the
  // band: the band holds one line of a display cut sized for six or seven
  // characters, and "THE DAIS OF THE SUN GOD" came out as "THE DAIS OF TH"
  // with no ellipsis and nothing to say it had been cut.
  chrome(screen, "D&DIAGRAMS", progress);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const fui::Rect nameStrip = screen.takeTop(kNameStrip, toybox::kGutter);
  fui::TextStyle nameText;
  nameText.font = toybox::kUiFont;
  nameText.align = fui::TextAlign::Center;
  screen.target().text(toybox::inkCentred(nameStrip, toybox::kUiCut), puzzle.name, nameText);

  const int size = board.size();
  layout = layoutBoard(screen.body(), size);

  // One target for the whole play area. Registered from the rect the cells were
  // laid out in, and resolved back through the same Layout.
  screen.frame().hit(layout.board, ActionBoard, 0);

  drawBoardSurface(screen, board, layout, Spotlight{});

  // The action bar. One button while playing, because there is exactly one
  // thing to do to a board you have got wrong.
  const int width = (actions.width - toybox::kGutter) / 2;
  const char* labels[2] = {"CLEAR", "DUNGEONS"};
  const int values[2] = {ButtonReset, ButtonMenu};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = ActionButton;
    props.value = static_cast<int16_t>(values[i]);
    props.text = toybox::buttonText(screen.theme());
    props.radius = toybox::kPillRadius;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       static_cast<int16_t>(width), actions.height));
  }
}

int PickerLayout::indexAt(const int x, const int y) const {
  if (cell <= 0) return -1;
  const int pitch = cell + gap;
  if (x < grid.x || y < grid.y) return -1;
  const int col = (x - grid.x) / pitch;
  const int row = (y - grid.y) / pitch;
  if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
  // A tap in the gap between two cells belongs to the one before it, which is
  // what makes a grid of small targets usable with a thumb.
  return row * cols + col;
}

namespace {

// A dungeon's cell on the campaign grid: its own monster, standing in a room
// that is filled once the dungeon is cleared.
//
// The monster is what makes this a map rather than a progress bar, and it is
// free: which creature a dungeon has is already a pure function of its name, so
// the grid shows sixty-four different things without a byte of new data. This
// is the fork's rule about decoration -- a screenshot of it is different on
// every device, because it is the player's own record drawn in the game's own
// material.
void mapCell(toybox::Screen& screen, const fui::Rect& box, const int index, const bool done, const bool current) {
  if (done) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
  }

  // A cleared room is solid black, so its occupant has to be paper. Getting
  // this backwards paints the mark in the colour it is standing on and it
  // simply is not there, with nothing to say so.
  const int16_t art = 32;
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(box.x + (box.width - art) / 2),
                                        static_cast<int16_t>(box.y + (box.height - art) / 2), art, art);
  screen.target().bitmap(where, fui::bitmapFromIcon(monsterArt(index)), fui::BitmapMode::Contain,
                         fui::Paint::solid(done ? fui::Color::White : fui::Color::Black));

  // The dungeon in hand gets the same brackets the guide points with, so the
  // two places this app says "look here" say it the same way.
  if (current) cornerBrackets(screen, box.inset(fui::Insets{-5, -5, -5, -5}), 12);
}

}  // namespace

// The front door.
//
// The campaign IS the menu: the grid is the biggest thing on the screen and it
// is tappable, so the separate CHOOSE screen it used to open has gone with it.
// One screen fewer, and the grid stops being filler between a name and a row of
// buttons.
//
// Two alternatives were built complete, rendered beside it and deleted: the
// next dungeon as a solid slab with progress as eight tier bars, and that
// dungeon's own empty board shown as a preview. The preview was the prettiest
// single screen and it lost for showing you what you would see two taps later
// anyway; the slab read best from across a room, and neither of them removed a
// screen.

void buildMenu(toybox::Screen& screen, const MenuModel& model, PickerLayout& layout) {
  layout = PickerLayout{};
  char progress[toybox::kSlashCounterChars];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  chrome(screen, "D&DIAGRAMS", progress);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::TextStyle label;
  label.font = toybox::kSmallFont;
  label.align = fui::TextAlign::Center;

  // MAP. The campaign is the front door: the grid is the biggest thing on the
  // screen, it is tappable, and CHOOSE goes away because what it opened is
  // already here. One screen fewer, and the grid stops being filler between a
  // name and a row of buttons.
  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  // The next dungeon, named at the foot with its own creature beside it, as one
  // line rather than a two-line display cut that breaks names in odd places.
  const fui::Rect nextBand = screen.takeBottom(44, toybox::kMargin);
  const int16_t art = 32;
  const fui::Rect artBox =
      fui::makeRect(nextBand.x, static_cast<int16_t>(nextBand.y + (nextBand.height - art) / 2), art, art);
  screen.target().bitmap(artBox, fui::bitmapFromIcon(monsterArt(model.selectedIndex)), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));
  // The guide, at the size of a secondary action, sharing this line with the
  // name rather than taking half the button bar below it.
  constexpr int16_t kGuideWidth = 150;
  fui::ButtonProps guide;
  guide.label = "TUTORIAL";
  guide.action = ActionButton;
  guide.value = ButtonGuide;
  guide.text = toybox::buttonText(screen.theme());
  guide.radius = toybox::kPillRadius;
  guide.styles = toybox::rowStyles();
  screen.button(guide, fui::makeRect(static_cast<int16_t>(nextBand.right() - kGuideWidth), nextBand.y, kGuideWidth,
                                     nextBand.height));

  fui::TextStyle nextText;
  nextText.align = fui::TextAlign::Left;
  const int nextWidth = nextBand.width - art - kGuideWidth - 2 * toybox::kGutter;
  nextText.font = fitLabel(screen, model.dungeonName, nextWidth, nextText);
  nextText.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(static_cast<int16_t>(artBox.right() + toybox::kGutter), nextBand.y,
                                     static_cast<int16_t>(nextWidth), nextBand.height),
                       model.dungeonName, nextText);
  screen.target().text(screen.takeTop(24, toybox::kGutter), "TAP TO CHOOSE, THEN PLAY", label);

  if (model.progress != nullptr) {
    const fui::Rect body = screen.body();
    constexpr int lane = 30;
    constexpr int gap = 5;
    int cell = (body.width - lane - 7 * gap) / 8;
    if (cell > (body.height - 7 * gap) / 8) cell = (body.height - 7 * gap) / 8;
    const int extent = 8 * cell + 7 * gap;
    const int left = body.x + (body.width - lane - extent) / 2;
    layout.grid =
        fui::makeRect(static_cast<int16_t>(left + lane), static_cast<int16_t>(body.y + (body.height - extent) / 2),
                      static_cast<int16_t>(extent), static_cast<int16_t>(extent));
    layout.cell = static_cast<int16_t>(cell);
    layout.gap = gap;
    layout.cols = 8;
    layout.rows = 8;
    screen.frame().hit(layout.grid, ActionPick, -1);

    for (int tier = 0; tier < 8; ++tier) {
      const int16_t rowY = static_cast<int16_t>(layout.grid.y + tier * (cell + gap));
      int done = 0;
      for (int slot = 0; slot < 8; ++slot) {
        const int index = dungeon::kCampaignFirst + tier * 8 + slot;
        const bool cleared = model.progress->isSolved(index);
        if (cleared) ++done;
        mapCell(screen,
                fui::makeRect(static_cast<int16_t>(layout.grid.x + slot * (cell + gap)), rowY,
                              static_cast<int16_t>(cell), static_cast<int16_t>(cell)),
                index, cleared, model.selectedIndex == index);
      }
      char tierLabel[toybox::kIntTextChars];
      std::snprintf(tierLabel, sizeof(tierLabel), "%d", tier + 1);
      const fui::Rect chip =
          fui::makeRect(static_cast<int16_t>(left), rowY, static_cast<int16_t>(lane - 6), static_cast<int16_t>(cell));
      fui::TextStyle number;
      number.font = toybox::kUiFont;
      number.align = fui::TextAlign::Center;
      if (done == 8) {
        screen.target().fill(chip, fui::Paint::solid(fui::Color::Black));
        number.color = fui::Color::White;
      }
      screen.target().text(toybox::inkCentred(chip, toybox::kUiCut), tierLabel, number);
    }
  }

  // PLAY takes the whole bar. TUTORIAL had half of it, which is a lot of the
  // screen's one loud control for something you read once -- it sits on the
  // name line instead, at the size of a secondary action.
  fui::ButtonProps play;
  play.label = model.hasProgress ? "RESUME" : "PLAY";
  play.action = ActionButton;
  play.value = ButtonPlay;
  play.text = toybox::buttonText(screen.theme());
  play.radius = toybox::kPillRadius;
  screen.button(play, actions);
}

namespace {

// ---------------------------------------------------------------------------
// The adventurer's guide.
//
// One board, revealed a step at a time. Every page shows the SAME dungeon --
// the tutorial, kPuzzles[0] -- a little further along, with the rule that
// justifies the next step underneath and a ring round the part of the board it
// is about. Read it through and you have watched a dungeon get solved.
//
// The first version was five pages of abstract little diagrams with captions,
// and it was worse in a way worth recording: a diagram is a second thing to
// learn before you can learn the game. Teaching on the real surface means the
// page and the board are furnished identically, down to which creature lives
// there, so nothing has to be translated when the tutorial opens.
//
// Each page's board is written out as text and turned into the same masks the
// player's own board uses, so a page cannot drift from the puzzle: a host test
// asserts every page's walls are a subset of the real solution and that the
// last page IS the solution.
//
//   #  wall        o  floor, marked        .  untouched
// ---------------------------------------------------------------------------

struct GuidePage {
  const char* title;
  const char* body;
  const char* rows[8];
  Spotlight spotlight;
};

constexpr GuidePage kGuide[] = {
    {"THE MAP",
     "SOMEWHERE IN THIS GRID IS A DUNGEON. YOU FIND IT BY PLACING WALLS, USING NOTHING BUT LOGIC.",
     {"......", "......", "......", "......", "......", "......"},
     {0, 0, 0, 0, false}},

    {"THE NUMBERS",
     "EACH NUMBER COUNTS THE WALLS IN ITS ROW OR COLUMN. TAP A SQUARE TO PLACE ONE.",
     {"###...", "#.....", "......", "#.....", "#.....", "......"},
     {0, 0, 0, 0, true}},

    {"YOUR NOTES",
     "TAP AGAIN TO MARK FLOOR, ONCE MORE TO CLEAR. THOSE MARKS ARE YOUR NOTES, NOT THE ANSWER.",
     {"###ooo", "#ooooo", ".o....", "#.....", "#.....", "......"},
     {0, 0, 0, 0, false}},

    {"TREASURE",
     "A CHEST SITS IN A 3x3 ROOM OF FLOOR. ONE WAY IN, ONE CHEST, AND NOTHING LIVING IN IT.",
     {"###ooo", "#ooooo", ".oooo.", "#.....", "#.....", "......"},
     {0, 3, 3, 3, false}},

    {"MONSTERS",
     "EVERY MONSTER SITS IN A DEAD END, AND EVERY DEAD END HAS A MONSTER IN IT.",
     {"###ooo", "#ooooo", ".oooo.", "#.####", "#....#", "..#..."},
     {2, 0, 1, 1, false}},

    {"ONE DUNGEON",
     "ALL THE FLOOR JOINS UP. SQUARES THAT ONLY TOUCH AT A CORNER ARE NOT JOINED.",
     {"###ooo", "#ooooo", ".oooo.", "#.####", "#o..##", "..#..."},
     {0, 0, 0, 0, false}},

    {"CORRIDORS",
     "CORRIDORS ARE ONE SQUARE WIDE. NO 2x2 OF OPEN FLOOR OUTSIDE A TREASURE ROOM.",
     {"###ooo", "#ooooo", ".oooo.", "#.####", "#ooo#.", "..#..."},
     {3, 1, 2, 2, false}},

    {"YOUR TURN",
     "THAT IS EVERY RULE. SIXTY-FOUR DUNGEONS ARE WAITING FOR YOU.",
     {"###...", "#.....", "..#...", "#.####", "#...#.", "..#..."},
     {0, 0, 0, 0, false}},
};

constexpr int kGuidePages = static_cast<int>(sizeof(kGuide) / sizeof(kGuide[0]));

// The tutorial board at the state page `page` shows it.
dungeon::Board guideBoard(const int page) {
  const GuidePage& content = kGuide[page];
  uint64_t walls = 0;
  uint64_t floors = 0;
  const int size = dungeon::kPuzzles[0].size;
  for (int r = 0; r < size; ++r) {
    for (int c = 0; c < size; ++c) {
      const char ch = content.rows[r][c];
      const uint64_t bit = uint64_t{1} << (r * 8 + c);
      if (ch == '#') {
        walls |= bit;
      } else if (ch == 'o') {
        floors |= bit;
      }
    }
  }
  dungeon::Board board;
  // restore() strips anything standing on a monster or a chest, so a page can
  // never draw a wall through the furniture even if the text above says so.
  board.restore(0, walls, floors);
  return board;
}

}  // namespace

int guidePageCount() { return kGuidePages; }

bool guidePageWalls(const int page, uint64_t& walls) {
  if (page < 0 || page >= kGuidePages) return false;
  walls = guideBoard(page).wallMask();
  return true;
}

void buildGuide(toybox::Screen& screen, const GuideModel& model) {
  const int page = (model.page < 0 || model.page >= kGuidePages) ? 0 : model.page;
  const GuidePage& content = kGuide[page];

  char counter[toybox::kSlashCounterChars];
  std::snprintf(counter, sizeof(counter), "%d/%d", page + 1, kGuidePages);
  chrome(screen, content.title, counter);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  const dungeon::Board board = guideBoard(page);
  // A smaller cell than the real board: this page shares its screen with three
  // lines of type, and the guide is for reading rather than for tapping.
  constexpr int kGuideCell = 44;
  // Four lines, and a band that holds four.
  //
  // The band and the text have to be sized against each other or the component
  // silently truncates: at this cut a line is about twenty-two characters, so
  // three lines is sixty-six, and a sixty-eight character caption came back as
  // "...AND NOTHIN" with no ellipsis and nothing in the log. Every caption in
  // kGuide is written to fit four.
  constexpr int kCaption = 132;

  // The board and its caption are centred TOGETHER. Pinning the caption to the
  // bottom of the screen and centring the board in whatever was left put a
  // hand's width of white between them, and the page read as two unrelated
  // things rather than as a picture with a line under it.
  const fui::Rect body = screen.body();
  const int extent = boardDrawnHeight(board.size(), kGuideCell);
  const int block = extent + toybox::kMargin + kCaption;
  const int top = body.y + (body.height - block) / 2;

  Layout layout =
      layoutBoard(fui::makeRect(body.x, static_cast<int16_t>(top), body.width, static_cast<int16_t>(extent)),
                  board.size(), kGuideCell);
  drawBoardSurface(screen, board, layout, content.spotlight);

  // The words go under the picture, because the picture is the rule and the
  // sentence is its caption.
  fui::TextStyle caption;
  caption.font = toybox::kUiFont;
  caption.align = fui::TextAlign::Center;
  caption.maxLines = 4;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(top + extent + toybox::kMargin), body.width,
                                     static_cast<int16_t>(kCaption)),
                       content.body, caption);

  const int width = (actions.width - toybox::kGutter) / 2;
  const bool last = page == kGuidePages - 1;
  // The last page opens a real dungeon, never this one. The board on these
  // pages is the tutorial, and the tutorial is a lesson rather than a level:
  // it is solved in front of you by page eight, so there would be nothing left
  // to play.
  const char* labels[2] = {page == 0 ? "BACK" : "PREV", last ? "PLAY" : "NEXT"};
  const int values[2] = {ButtonGuideBack, ButtonGuideNext};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = ActionButton;
    props.value = static_cast<int16_t>(values[i]);
    props.text = toybox::buttonText(screen.theme());
    props.radius = toybox::kPillRadius;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       static_cast<int16_t>(width), actions.height));
  }
}

void buildWin(toybox::Screen& screen, const WinModel& model) {
  char progress[toybox::kSlashCounterChars];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  chrome(screen, "CLEARED", progress);
  screen.insetContent(fui::Insets{toybox::kMargin, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const fui::Rect body = screen.body();

  fui::TextStyle name;
  name.font = toybox::kDisplayFont;
  name.align = fui::TextAlign::Center;
  name.maxLines = 2;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 40), body.width, 90), model.dungeonName,
                       name);

  // The map they have just finished. Nothing else on this screen is worth
  // looking at, and this is the one thing on it they made.
  if (model.cleared != nullptr) {
    const dungeon::Puzzle& p = *model.cleared;
    constexpr int mapCell = 34;
    const int extent = p.size * mapCell;
    const int mapX = body.x + (body.width - extent) / 2;
    const int mapY = body.y + body.height / 2 - extent / 2 + 40;
    screen.target().stroke(
        fui::makeRect(static_cast<int16_t>(mapX - toybox::kFrame), static_cast<int16_t>(mapY - toybox::kFrame),
                      static_cast<int16_t>(extent + 2 * toybox::kFrame),
                      static_cast<int16_t>(extent + 2 * toybox::kFrame)),
        fui::Paint::solid(fui::Color::Black), toybox::kFrame);
    for (int row = 0; row < p.size; ++row) {
      for (int col = 0; col < p.size; ++col) {
        const uint64_t bit = uint64_t{1} << (row * 8 + col);
        const fui::Rect cell = fui::makeRect(static_cast<int16_t>(mapX + col * mapCell),
                                             static_cast<int16_t>(mapY + row * mapCell), mapCell, mapCell);
        if ((p.walls & bit) != 0) {
          screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
        } else if ((p.monsters & bit) != 0 || (p.chests & bit) != 0) {
          const int16_t dot = 12;
          screen.target().fill(fui::makeRect(static_cast<int16_t>(cell.x + (mapCell - dot) / 2),
                                             static_cast<int16_t>(cell.y + (mapCell - dot) / 2), dot, dot),
                               fui::Paint::solid(fui::Color::Black), dot / 2);
        }
      }
    }
  }

  const int width = (actions.width - toybox::kGutter) / 2;
  const char* labels[2] = {"NEXT", "DUNGEONS"};
  const int values[2] = {ButtonNext, ButtonMenu};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = ActionButton;
    props.value = static_cast<int16_t>(values[i]);
    props.text = toybox::buttonText(screen.theme());
    props.radius = toybox::kPillRadius;
    props.state = (i == 0 && !model.moreToPlay) ? fui::StateDisabled : fui::StateNormal;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       static_cast<int16_t>(width), actions.height));
  }
}

}  // namespace dungeonui
