#include "DungeonScreens.h"

#include <cstdio>

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
constexpr int kArtSize = 36;
// How far a clue's box hangs past its cell, on each side. Negative: the box is
// wider than the cell it belongs to.
constexpr int kLaneOverhang = (kCell - kLane) / 2;

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

// How far to raise a clue's digit inside its box.
//
// MEASURED, NOT GUESSED. GfxRendererTarget centres a run on the font's line
// box rather than on cap height, so a digit in the display cut lands low in
// whatever rect it is given -- 10px of air above and 5 below, at this size.
// Invisible on white; obvious the moment a spent clue fills a chip behind the
// number, where the digit then nearly touches the chip's bottom edge.
//
// The number comes from cropping a render and measuring the ink: 2.5px low
// before, 0.5px after, which is as centred as a whole-pixel lift gets. It is
// specific to this cut in a 56px box -- re-measure if either changes, because
// the error grows with the gap between line height and cap height.
constexpr int kClueLift = 3;

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
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
}

Layout layoutBoard(const fui::Rect& body, const int size, const int cellSize = kCell) {
  Layout layout;
  layout.cell = static_cast<int16_t>(cellSize);
  layout.lane = kLane;
  layout.size = static_cast<int16_t>(size);
  // Lane, then the gap the frame lives in, then the cells.
  const int extent = kLane + kClueGap + size * cellSize;
  const int x = body.x + (body.width - extent) / 2;
  const int y = body.y + (body.height - extent) / 2;
  layout.board = fui::makeRect(static_cast<int16_t>(x + kLane + kClueGap), static_cast<int16_t>(y + kLane + kClueGap),
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

void drawClue(toybox::Screen& screen, const fui::Rect& box, const int value, const int placed) {
  char text[4];
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
    screen.target().fill(box.inset(fui::Insets{4, 4, 4, 4}), fui::Paint::solid(fui::Color::Black));
    style.color = fui::Color::White;
  } else if (placed > value) {
    // LightGray, not DarkGray. The number inside stays black, and 50% dither
    // under black type is close to no contrast at all.
    screen.target().fill(box.inset(fui::Insets{4, 4, 4, 4}), fui::Paint::dither(fui::Color::LightGray));
  }

  // The whole box, not a line-height slice of it: the target centres a run on
  // the font's line box within the rect it is given, and handing it a rect
  // shorter than that line box is what pushed the digits out of the lane and
  // over the board's frame. Lifted by kClueLift on top of that, because the
  // line box is not the ink -- see the note on that constant.
  screen.target().text(fui::makeRect(box.x, static_cast<int16_t>(box.y - kClueLift), box.width, box.height), text,
                       style);
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
    const fui::Rect colBox = fui::makeRect(static_cast<int16_t>(layout.board.x + i * layout.cell + kLaneOverhang),
                                           static_cast<int16_t>(layout.board.y - kClueGap - kLane),
                                           static_cast<int16_t>(kLane), static_cast<int16_t>(kLane));
    const fui::Rect rowBox = fui::makeRect(static_cast<int16_t>(layout.board.x - kClueGap - kLane),
                                           static_cast<int16_t>(layout.board.y + i * layout.cell + kLaneOverhang),
                                           static_cast<int16_t>(kLane), static_cast<int16_t>(kLane));
    drawClue(screen, colBox, puzzle.colClues[i], board.colWalls(i));
    drawClue(screen, rowBox, puzzle.rowClues[i], board.rowWalls(i));
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
    // The two lanes, ringed separately. Ringing lanes and board together was
    // the first attempt and it enclosed nearly the whole screen, which points
    // at everything and therefore at nothing.
    const fui::Insets grow{4, 4, 4, 4};
    screen.target().stroke(
        fui::makeRect(static_cast<int16_t>(layout.board.x + kLaneOverhang),
                      static_cast<int16_t>(layout.board.y - kClueGap - kLane),
                      static_cast<int16_t>(layout.board.width - 2 * kLaneOverhang), static_cast<int16_t>(kLane))
            .inset(grow),
        fui::Paint::solid(fui::Color::Black), toybox::kRule, 10);
    screen.target().stroke(
        fui::makeRect(static_cast<int16_t>(layout.board.x - kClueGap - kLane),
                      static_cast<int16_t>(layout.board.y + kLaneOverhang), static_cast<int16_t>(kLane),
                      static_cast<int16_t>(layout.board.height - 2 * kLaneOverhang))
            .inset(grow),
        fui::Paint::solid(fui::Color::Black), toybox::kRule, 10);
  } else if (spotlight.width > 0) {
    // Drawn after every cell, in its own pass. Drawn inside the cell loop it
    // would be overdrawn by whichever neighbour rendered later, which is the
    // broken-rectangle bug this project has already paid for once.
    const fui::Rect patch = fui::makeRect(static_cast<int16_t>(layout.board.x + spotlight.col * layout.cell),
                                          static_cast<int16_t>(layout.board.y + spotlight.row * layout.cell),
                                          static_cast<int16_t>(spotlight.width * layout.cell),
                                          static_cast<int16_t>(spotlight.height * layout.cell));
    screen.target().stroke(patch.inset(fui::Insets{-5, -5, -5, -5}), fui::Paint::solid(fui::Color::Black),
                           toybox::kRule, 8);
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

  char progress[12];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  // The app's name in the band, the dungeon's name in a strip of its own.
  //
  // The dungeon name belongs on this screen -- it is what identifies it, the
  // way the date identifies a Connections board -- but it cannot go in the
  // band: the band holds one line of a display cut sized for six or seven
  // characters, and "THE DAIS OF THE SUN GOD" came out as "THE DAIS OF TH"
  // with no ellipsis and nothing to say it had been cut.
  chrome(screen, "DUNGEONS", progress);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const fui::Rect nameStrip = screen.takeTop(kNameStrip, toybox::kGutter);
  fui::TextStyle nameText;
  nameText.font = toybox::kUiFont;
  nameText.align = fui::TextAlign::Center;
  screen.target().text(nameStrip, puzzle.name, nameText);

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
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black), 8);
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);
  }

  // A cleared room is solid black, so its occupant has to be paper. Getting
  // this backwards paints the mark in the colour it is standing on and it
  // simply is not there, with nothing to say so.
  const int16_t art = 32;
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(box.x + (box.width - art) / 2),
                                        static_cast<int16_t>(box.y + (box.height - art) / 2), art, art);
  screen.target().bitmap(where, fui::bitmapFromIcon(monsterArt(index)), fui::BitmapMode::Contain,
                         fui::Paint::solid(done ? fui::Color::White : fui::Color::Black));

  if (current) {
    screen.target().stroke(box.inset(fui::Insets{-5, -5, -5, -5}), fui::Paint::solid(fui::Color::Black), toybox::kRule,
                           10);
  }
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
  char progress[12];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  chrome(screen, "DUNGEONS", progress);
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
  screen.target().bitmap(artBox, fui::bitmapFromIcon(monsterArt(model.nextIndex)), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));
  fui::TextStyle nextText;
  nextText.align = fui::TextAlign::Left;
  const int nextWidth = nextBand.width - art - toybox::kGutter;
  nextText.font = fitLabel(screen, model.dungeonName, nextWidth, nextText);
  nextText.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(static_cast<int16_t>(artBox.right() + toybox::kGutter), nextBand.y,
                                     static_cast<int16_t>(nextWidth), nextBand.height),
                       model.dungeonName, nextText);
  screen.target().text(screen.takeTop(24, toybox::kGutter), "TAP A DUNGEON", label);

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
                index, cleared, model.nextIndex == index);
      }
      char tierLabel[3];
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
      screen.target().text(chip, tierLabel, number);
    }
  }

  const int width = (actions.width - toybox::kGutter) / 2;
  const char* labels[2] = {"PLAY", "TUTORIAL"};
  const int values[2] = {ButtonPlay, ButtonGuide};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = i == 0 && model.hasProgress ? "RESUME" : labels[i];
    props.action = ActionButton;
    props.value = static_cast<int16_t>(values[i]);
    props.text = toybox::buttonText(screen.theme());
    props.radius = toybox::kPillRadius;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       static_cast<int16_t>(width), actions.height));
  }
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
     "A DUNGEON IS HIDDEN IN THIS GRID. YOU FIND IT BY PLACING WALLS.",
     {"......", "......", "......", "......", "......", "......"},
     {0, 0, 0, 0, false}},

    {"THE NUMBERS",
     "EACH NUMBER IS HOW MANY WALLS BELONG IN THAT ROW OR COLUMN.",
     {"###...", "#.....", "......", "#.....", "#.....", "......"},
     {0, 0, 0, 0, true}},

    {"YOUR NOTES",
     "TAP FOR A WALL, AGAIN FOR A FLOOR NOTE, AGAIN TO CLEAR IT.",
     {"###ooo", "#ooooo", ".o....", "#.....", "#.....", "......"},
     {0, 0, 0, 0, false}},

    {"TREASURE",
     "A CHEST SITS IN A 3x3 ROOM OF FLOOR WITH ONE WAY IN.",
     {"###ooo", "#ooooo", ".oooo.", "#.....", "#.....", "......"},
     {0, 3, 3, 3, false}},

    {"MONSTERS",
     "EVERY MONSTER SITS IN A DEAD END, AND EVERY DEAD END HOLDS ONE.",
     {"###ooo", "#ooooo", ".oooo.", "#.####", "#....#", "..#..."},
     {2, 0, 1, 1, false}},

    {"ONE DUNGEON",
     "ALL THE FLOOR JOINS UP. TOUCHING AT A CORNER DOES NOT COUNT.",
     {"###ooo", "#ooooo", ".oooo.", "#.####", "#o..##", "..#..."},
     {0, 0, 0, 0, false}},

    {"CORRIDORS",
     "A CORRIDOR IS ONE SQUARE WIDE. NO 2x2 OF FLOOR OUTSIDE A ROOM.",
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

  char counter[8];
  std::snprintf(counter, sizeof(counter), "%d/%d", page + 1, kGuidePages);
  chrome(screen, content.title, counter);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  // The words sit under the picture, because the picture is the rule and the
  // sentence is its caption. Four lines is what the longest of them needs.
  fui::TextStyle body;
  body.font = toybox::kUiFont;
  body.align = fui::TextAlign::Center;
  // Three lines, and a band that holds three. It asked for four and the band
  // held three, so the last line was drawn straight through the buttons -- the
  // component clips to the rect it is given and says nothing about it.
  body.maxLines = 3;
  screen.target().text(screen.takeBottom(124, toybox::kMargin), content.body, body);

  const dungeon::Board board = guideBoard(page);
  // A smaller cell than the real board: this one shares its screen with four
  // lines of type, and the guide is for reading rather than for tapping.
  Layout layout = layoutBoard(screen.body(), board.size(), 42);
  drawBoardSurface(screen, board, layout, content.spotlight);

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
  char progress[12];
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
