#include "GoScreens.h"

#include <cstdio>
#include <cstdlib>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxIcons.h"

namespace goui {

namespace {

// The grid pitch, and with it every other number on the board. Nine lines at 49
// gives a 44px stone with 5px of air, which is a fingertip; nineteen lines
// would give 23px, which is why this game is nine by nine and says so.
constexpr int16_t kPitch = 49;
constexpr int16_t kFrame = toybox::kBoardFrame;
// Room outside the outermost line, so an edge stone has air round it rather
// than sitting against the frame. It has to exceed the stone RADIUS: at half a
// pitch it was two pixels and the top row visibly touched the border.
constexpr int16_t kPad = 28;

constexpr int16_t kGridSpan = static_cast<int16_t>(kPitch * (go::kSize - 1));
constexpr int16_t kBoardSide = static_cast<int16_t>(kGridSpan + kPad * 2);

// A seat band's height, variant 2 only.
constexpr int16_t kSeatBand = 58;

int16_t boardLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kBoardSide) / 2); }

int16_t boardTop() {
  // The opponent's seat sits above the board, so the board starts below it.
  return static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter + kSeatBand + toybox::kGutter + kFrame);
}

int16_t firstLineX(const fui::DeviceContext& device) { return static_cast<int16_t>(boardLeft(device) + kPad); }
int16_t firstLineY() { return static_cast<int16_t>(boardTop() + kPad); }

// A stone, drawn the way the design language says a light shape has to be: the
// silhouette knocked out in paper first, then stroked, or the grid line under a
// white stone shows through it and the two colours stop being two colours.
void stone(toybox::Screen& screen, const int16_t cx, const int16_t cy, const int16_t radius, const uint8_t colour) {
  toybox::disc(screen, cx, cy, radius, fui::Color::Black);
  toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 3),
               colour == go::kBlack ? fui::Color::Black : fui::Color::White);
}

// The mark on the stone just played. Go without it is a memory test: on a board
// of identical discs there is no other way to see what moved.
void lastMoveMark(toybox::Screen& screen, const int16_t cx, const int16_t cy, const uint8_t colour) {
  const fui::Color ink = colour == go::kBlack ? fui::Color::White : fui::Color::Black;
  toybox::disc(screen, cx, cy, 8, ink);
  toybox::disc(screen, cx, cy, 5, colour == go::kBlack ? fui::Color::Black : fui::Color::White);
}

// The board's own border, where the variant has one. Drawn flush against the
// board box so the surface reads as one object -- at three pixels it came out
// lighter than the selection marks inside it, which is the weight order the
// metrics header exists to prevent.
void drawFrame(toybox::Screen& screen, const fui::DeviceContext& device) {
  if (kFrame == 0) return;
  const fui::Rect frame =
      fui::makeRect(static_cast<int16_t>(boardLeft(device) - kFrame), static_cast<int16_t>(boardTop() - kFrame),
                    static_cast<int16_t>(kBoardSide + kFrame * 2), static_cast<int16_t>(kBoardSide + kFrame * 2));
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), kFrame);
}

void drawGrid(toybox::Screen& screen, const fui::DeviceContext& device) {
  const int16_t x0 = firstLineX(device);
  const int16_t y0 = firstLineY();
  const int16_t x1 = static_cast<int16_t>(x0 + kGridSpan);
  const int16_t y1 = static_cast<int16_t>(y0 + kGridSpan);

  for (int i = 0; i < go::kSize; ++i) {
    // The outermost lines are heavier, because on a real board they ARE the
    // edge. On variant 3 they are the only edge there is.
    const bool outer = i == 0 || i == go::kSize - 1;
    const int16_t weight = outer ? toybox::kRule : toybox::kHairline;
    const int16_t x = static_cast<int16_t>(x0 + i * kPitch);
    const int16_t y = static_cast<int16_t>(y0 + i * kPitch);
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(x - weight / 2), y0, weight, static_cast<int16_t>(kGridSpan + 1)),
        fui::Paint::solid(fui::Color::Black));
    screen.target().fill(
        fui::makeRect(x0, static_cast<int16_t>(y - weight / 2), static_cast<int16_t>(kGridSpan + 1), weight),
        fui::Paint::solid(fui::Color::Black));
  }
  (void)x1;
  (void)y1;

  // Star points. Five of them on a nine by nine, at the 3-3s and the middle,
  // and they are not decoration: they are how a player reads where they are on
  // a board with no coordinates.
  constexpr int kStars[5][2] = {{2, 2}, {2, 6}, {6, 2}, {6, 6}, {4, 4}};
  for (const auto& star : kStars) {
    toybox::disc(screen, static_cast<int16_t>(x0 + star[1] * kPitch), static_cast<int16_t>(y0 + star[0] * kPitch), 5,
                 fui::Color::Black);
  }
}

void drawStones(toybox::Screen& screen, const fui::DeviceContext& device, const go::Game& game) {
  const int16_t radius = stoneRadius();
  for (int point = 0; point < go::kPoints; ++point) {
    if (!go::isStone(game.point[point])) continue;
    int16_t cx = 0;
    int16_t cy = 0;
    stoneCentre(device, point, cx, cy);
    stone(screen, cx, cy, radius, game.point[point]);
  }
  if (game.lastMove < go::kPoints && go::isStone(game.point[game.lastMove])) {
    int16_t cx = 0;
    int16_t cy = 0;
    stoneCentre(device, game.lastMove, cx, cy);
    lastMoveMark(screen, cx, cy, game.point[game.lastMove]);
  }
}

// The stone that is aimed at but not yet played: dithered, so it is plainly not
// on the board yet, with the fork's corner brackets round it saying that a
// second tap is what puts it there.
void drawAim(toybox::Screen& screen, const fui::DeviceContext& device, const int point, const uint8_t colour) {
  if (point < 0 || point >= go::kPoints) return;
  int16_t cx = 0;
  int16_t cy = 0;
  stoneCentre(device, point, cx, cy);
  const int16_t radius = stoneRadius();
  toybox::disc(screen, cx, cy, radius, fui::Color::Black);
  toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 3),
               fui::Paint::dither(colour == go::kBlack ? fui::Color::DarkGray : fui::Color::LightGray));
  const fui::Rect box =
      fui::makeRect(static_cast<int16_t>(cx - kPitch / 2), static_cast<int16_t>(cy - kPitch / 2), kPitch, kPitch);
  toybox::bracket(screen, box, 12, 4);
}

// A miniature of a finished position, for the front door's ornament.
void miniBoard(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t pitch,
               const uint8_t* points) {
  const int16_t span = static_cast<int16_t>(pitch * (go::kSize - 1));
  for (int i = 0; i < go::kSize; ++i) {
    const int16_t x = static_cast<int16_t>(left + i * pitch);
    const int16_t y = static_cast<int16_t>(top + i * pitch);
    screen.target().fill(fui::makeRect(x, top, toybox::kHairline, static_cast<int16_t>(span + 1)),
                         fui::Paint::solid(fui::Color::Black));
    screen.target().fill(fui::makeRect(left, y, static_cast<int16_t>(span + 1), toybox::kHairline),
                         fui::Paint::solid(fui::Color::Black));
  }
  const int16_t radius = static_cast<int16_t>(pitch / 2);
  for (int point = 0; point < go::kPoints; ++point) {
    if (!go::isStone(points[point])) continue;
    const int16_t cx = static_cast<int16_t>(left + go::colOf(point) * pitch);
    const int16_t cy = static_cast<int16_t>(top + go::rowOf(point) * pitch);
    toybox::disc(screen, cx, cy, radius, fui::Color::Black);
    toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 2),
                 points[point] == go::kBlack ? fui::Color::Black : fui::Color::White);
  }
}

const char* statusWords(const BoardModel& model) {
  if (model.thinking) return "THINKING";
  if (model.nothingLeft) return model.yourTurn ? "NOTHING LEFT: PASS" : "THEIR MOVE";
  if (model.caution == go::Caution::FillsOwnEye) return "THAT FILLS YOUR OWN EYE";
  if (model.caution == go::Caution::SelfAtari) return "THAT STONE WOULD BE IN ATARI";
  if (model.theyPassed && model.yourTurn) return "THEY PASSED";
  if (model.sharedDevice) return model.game.toMove == go::kBlack ? "BLACK TO PLAY" : "WHITE TO PLAY";
  return model.yourTurn ? "YOUR MOVE" : "THEIR MOVE";
}

void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme's default is black on
  // the black band -- invisible, and indistinguishable from never having been
  // set. Jaipur paid for this discovery.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

int16_t stoneRadius() { return static_cast<int16_t>(kPitch / 2 - 2); }

void stoneCentre(const fui::DeviceContext& device, const int point, int16_t& cx, int16_t& cy) {
  cx = static_cast<int16_t>(firstLineX(device) + go::colOf(point) * kPitch);
  cy = static_cast<int16_t>(firstLineY() + go::rowOf(point) * kPitch);
}

bool pointAt(const fui::DeviceContext& device, const int x, const int y, int& point) {
  // The whole box belongs to the nearest intersection, padding included, so an
  // edge point is as easy to hit as a middle one. Computing a small target
  // round each line instead leaves dead gutters between the points, which on a
  // touch board reads as the game ignoring taps.
  const int dx = x - boardLeft(device);
  const int dy = y - boardTop();
  if (dx < 0 || dy < 0 || dx >= kBoardSide || dy >= kBoardSide) return false;
  int col = (dx - kPad + kPitch / 2) / kPitch;
  int row = (dy - kPad + kPitch / 2) / kPitch;
  if (dx < kPad) col = 0;
  if (dy < kPad) row = 0;
  if (col < 0) col = 0;
  if (row < 0) row = 0;
  if (col > go::kSize - 1) col = go::kSize - 1;
  if (row > go::kSize - 1) row = go::kSize - 1;
  point = go::pointAt(row, col);
  return true;
}

void formatResult(char* out, const int capacity, const int blackHalves, const int whiteHalves) {
  const int difference = blackHalves - whiteHalves;
  const char winner = difference > 0 ? 'B' : 'W';
  const int margin = difference > 0 ? difference : -difference;
  std::snprintf(out, static_cast<size_t>(capacity), "%c+%d.%d", winner, margin / 2, (margin % 2) * 5);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "GO");

  char record[48];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.wins + model.losses, model.wins);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(line, model.wins + model.losses > 0 ? record : "NO GAMES YET", small);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  // Three doors. Everything configurable moved behind the third one: a front
  // door carrying six rows, three of them settings, is a settings screen with a
  // PLAY button on it.
  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = model.inProgress ? "RESUME GAME" : "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::Settings)].label = "SETTINGS";
  rows[static_cast<int>(MenuRow::Settings)].actionValue = static_cast<int16_t>(MenuRow::Settings);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::Play), 0, icon_go_play_32,
                         selected == static_cast<int>(MenuRow::Play));
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         selected == static_cast<int>(MenuRow::PlayNearby));
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::Settings), 0, icon_go_settings_32,
                         selected == static_cast<int>(MenuRow::Settings));

  if (!model.hasHistory || model.lastPoints == nullptr) return;

  // The last game's final position. Ornament made of the app's own material
  // carrying the app's own data: a screenshot of it is different on every
  // device, which is the whole test. It grew when the front door lost three
  // rows, because the space a layout gains has to go somewhere deliberate.
  constexpr int16_t kMini = 30;
  const int16_t span = static_cast<int16_t>(kMini * (go::kSize - 1));
  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  const int16_t room = static_cast<int16_t>(listBand.y - areaTop);
  const int16_t blockH = static_cast<int16_t>(span + 24 + 12 + 24);
  const int16_t top = static_cast<int16_t>(areaTop + (room > blockH ? (room - blockH) / 2 : 12));
  const fui::DeviceContext device = screen.device();
  miniBoard(screen, static_cast<int16_t>((device.width - span) / 2), static_cast<int16_t>(top + 12), kMini,
            model.lastPoints);

  char caption[48];
  std::snprintf(caption, sizeof(caption), "LAST GAME: %s BY %d.%d", model.lastWon ? "WON" : "LOST",
                model.lastMarginHalves / 2, (model.lastMarginHalves % 2) * 5);
  fui::TextStyle cap;
  cap.font = toybox::kTileFont;
  cap.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(content.x, static_cast<int16_t>(top + 12 + span + 20), content.width, 24), caption,
                       cap);
}

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  toyboxChrome(screen, "SETTINGS");

  fui::ListItem rows[static_cast<int>(SettingsRow::Count)] = {};
  rows[static_cast<int>(SettingsRow::Opponent)].label = "OPPONENT";
  rows[static_cast<int>(SettingsRow::Opponent)].value =
      model.opponent == go::Opponent::Computer ? "COMPUTER" : "2 PLAYERS";
  rows[static_cast<int>(SettingsRow::Opponent)].actionValue = static_cast<int16_t>(SettingsRow::Opponent);
  // LEADING icons here, where the front door's are trailing, and the reason is
  // structural rather than taste: a row carrying a value has no room on the
  // right, and an icon drawn there anyway lands ON the value. The first version
  // did exactly that and squeezed the third row's LABEL off the screen
  // entirely.
  //
  // The opponent's mark carries the VALUE rather than restating the label: it is
  // a machine or it is two people. The level's cannot -- see icons.txt for the
  // three graded ones that had to go -- so the row leans on its value, which is
  // the loudest thing on it anyway.
  rows[static_cast<int>(SettingsRow::Opponent)].icon =
      fui::bitmapFromIcon(model.opponent == go::Opponent::Computer ? icon_go_computer_32 : icon_go_humans_32);

  rows[static_cast<int>(SettingsRow::Level)].label = "LEVEL";
  // Dimmed rather than gone when two people share the device: a control that
  // vanishes takes its space with it and the list jumps under the finger.
  rows[static_cast<int>(SettingsRow::Level)].value =
      model.opponent == go::Opponent::Computer ? go::levelName(model.level) : "--";
  rows[static_cast<int>(SettingsRow::Level)].enabled = model.opponent == go::Opponent::Computer;
  rows[static_cast<int>(SettingsRow::Level)].actionValue = static_cast<int16_t>(SettingsRow::Level);
  rows[static_cast<int>(SettingsRow::Level)].icon = fui::bitmapFromIcon(icon_go_level_32);

  const bool colourIsYours = model.opponent == go::Opponent::Computer && model.handicap == 0;
  rows[static_cast<int>(SettingsRow::PlayAs)].label = "YOU PLAY";
  rows[static_cast<int>(SettingsRow::PlayAs)].value = model.opponent != go::Opponent::Computer ? "--"
                                                      : model.handicap > 0                     ? "BLACK +2"
                                                      : model.playAs == go::kBlack             ? "BLACK"
                                                                                               : "WHITE";
  rows[static_cast<int>(SettingsRow::PlayAs)].enabled = colourIsYours;
  rows[static_cast<int>(SettingsRow::PlayAs)].actionValue = static_cast<int16_t>(SettingsRow::PlayAs);
  rows[static_cast<int>(SettingsRow::PlayAs)].icon = fui::bitmapFromIcon(icon_go_colour_32);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(SettingsRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionSettingsRow;
  const int count = static_cast<int>(SettingsRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand = fui::makeRect(content.x, content.y, content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Top);

  // What the level means, said once, below the rows rather than inside them: a
  // subtitle on a value row is set at the title cut and about twenty characters
  // is all there is, which is not enough to say anything true.
  if (model.opponent != go::Opponent::Computer) return;
  const char* explain = model.handicap > 0 ? "EASY SPOTS YOU TWO STONES AND MISSES THINGS ON THE FAR SIDE OF THE BOARD."
                        : model.level == go::Level::Medium
                            ? "AN EVEN GAME. IT SEES EVERYTHING AND DOES NOT ALWAYS PLAY ITS BEST MOVE."
                            : "AN EVEN GAME, AND IT THINKS FOR AS LONG AS IT IS ALLOWED.";
  fui::TextStyle body;
  body.font = toybox::kTileFont;
  body.align = fui::TextAlign::Left;
  body.maxLines = 3;
  screen.target().text(
      fui::makeRect(content.x, static_cast<int16_t>(listBand.bottom() + toybox::kGutter * 2), content.width, 90),
      explain, body);
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "GO";
  char moves[16];
  std::snprintf(moves, sizeof(moves), "%u", static_cast<unsigned>(model.game.moveNumber));
  header.rightLabel = moves;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();

  // Whose turn it is is the loudest thing on the screen, said by a whole band
  // rather than by a caption at the bottom: the seat to move is inverted, the
  // other is outlined. Each band carries its side's colour and captures, so it
  // is information as well as state.
  //
  // Chosen by Mario on 2026-09-12 over two alternatives that were rendered
  // beside it: the fork's standard framed board with capture shelves, and a
  // bare goban with no frame and one thin bar. This one won on the seat bands,
  // which is also what fills the space a square board leaves in portrait.
  const fui::Rect bottom = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  fui::ButtonProps pass;
  pass.label = model.nothingLeft ? "PASS -- NOTHING LEFT" : "PASS";
  pass.action = ActionPass;
  pass.enabled = model.yourTurn;
  pass.borderEdges = fui::EdgesNone;
  screen.button(pass, bottom);

  // Three things on one band: the stone, who it is, and what they hold. They get
  // three bands of their own rather than one centre line, because a label that
  // shares a bar with anything needs bounds of its own -- the shelf's player bar
  // ran a long name straight through the face at one end and the chevron at the
  // other for exactly this reason.
  const auto seatBand = [&](const int16_t top, const uint8_t colour, const char* who, const int captured) {
    const fui::Rect box = fui::makeRect(boardLeft(device), top, kBoardSide, kSeatBand);
    const bool toMove = model.game.toMove == colour;
    if (toMove) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule);
    }
    const int16_t cy = static_cast<int16_t>(box.y + box.height / 2);
    // On the inverted band the stone is drawn the other way round, because ink
    // inverts: a black stone on black paper is nothing at all. Whenever a drawn
    // element moves, re-check its background.
    toybox::disc(screen, static_cast<int16_t>(box.x + 34), cy, 18, toMove ? fui::Color::White : fui::Color::Black);
    toybox::disc(screen, static_cast<int16_t>(box.x + 34), cy, 15,
                 colour == go::kBlack ? (toMove ? fui::Color::White : fui::Color::Black)
                                      : (toMove ? fui::Color::Black : fui::Color::White));

    constexpr int16_t kNameLeft = 66;
    constexpr int16_t kTailWidth = 214;
    fui::TextStyle name;
    name.font = toybox::kUiFont;
    name.align = fui::TextAlign::Left;
    name.color = toMove ? fui::Color::White : fui::Color::Black;
    screen.target().text(
        toybox::inkCentred(fui::makeRect(static_cast<int16_t>(box.x + kNameLeft), box.y,
                                         static_cast<int16_t>(box.width - kNameLeft - kTailWidth), box.height),
                           toybox::kUiCut),
        who, name);

    // The colour is said in WORDS here rather than left to the glyph. Inverted,
    // a black stone is a white disc and a white stone is a white disc with a
    // black middle, and at a glance across a room those are the same thing.
    char tail[32];
    std::snprintf(tail, sizeof(tail), "%s  %d TAKEN", colour == go::kBlack ? "BLACK" : "WHITE", captured);
    fui::TextStyle count = name;
    count.font = toybox::kTileFont;
    count.align = fui::TextAlign::Right;
    screen.target().text(toybox::inkCentred(fui::makeRect(static_cast<int16_t>(box.right() - kTailWidth), box.y,
                                                          static_cast<int16_t>(kTailWidth - 16), box.height),
                                            toybox::kTileCut),
                         tail, count);
  };

  // Two people sharing one device have no "you", so the band names the seat
  // instead. The colour is said on the right either way.
  const bool youAreBlack = model.seat == go::kBlack;
  const char* yourSeat = model.sharedDevice ? "THIS SIDE" : "YOU";
  const char* theirSeat =
      model.sharedDevice ? "THAT SIDE" : (model.opponentName != nullptr ? model.opponentName : "THEM");
  seatBand(static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter), youAreBlack ? go::kWhite : go::kBlack,
           theirSeat, model.game.capturedBy[youAreBlack ? go::kWhite : go::kBlack]);

  drawFrame(screen, device);
  drawGrid(screen, device);
  drawStones(screen, device, model.game);
  if (model.aimed != go::kNothingAimed) drawAim(screen, device, model.aimed, model.seat);

  seatBand(static_cast<int16_t>(boardTop() + kBoardSide + kFrame + toybox::kGutter),
           youAreBlack ? go::kBlack : go::kWhite, yourSeat,
           model.game.capturedBy[youAreBlack ? go::kBlack : go::kWhite]);

  // The caution still needs somewhere to speak, and the seat bands are not it.
  if (model.caution != go::Caution::None || model.theyPassed || model.thinking) {
    fui::TextStyle note;
    note.font = toybox::kTileFont;
    note.align = fui::TextAlign::Center;
    screen.target().text(
        toybox::inkCentred(fui::makeRect(boardLeft(device), static_cast<int16_t>(bottom.y - 30), kBoardSide, 26),
                           toybox::kTileCut),
        statusWords(model), note);
  }
}

void buildCount(toybox::Screen& screen, const CountModel& model) {
  char result[24];
  formatResult(result, sizeof(result), model.blackHalves, model.whiteHalves);
  toyboxChrome(screen, "COUNTING", result);
  screen.insetContent(fui::Insets{0, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();

  const fui::Rect bottom = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  fui::ButtonProps accept;
  const bool canAct = model.yourTurn && !model.youAccepted;
  accept.label = model.youAccepted ? "WAITING" : (model.yourTurn ? "ACCEPT" : "THEIR TURN");
  accept.action = canAct ? static_cast<fui::ActionId>(ActionAccept) : fui::NO_ACTION;
  accept.enabled = canAct;
  accept.borderEdges = fui::EdgesNone;
  screen.button(accept, fui::makeRect(bottom.x, bottom.y, static_cast<int16_t>(bottom.width - 152), bottom.height));

  fui::ButtonProps resume;
  resume.label = "PLAY ON";
  resume.action = model.yourTurn ? static_cast<fui::ActionId>(ActionResume) : fui::NO_ACTION;
  resume.enabled = model.yourTurn;
  resume.borderEdges = fui::EdgesNone;
  screen.button(resume, fui::makeRect(static_cast<int16_t>(bottom.right() - 140), bottom.y, 140, bottom.height));

  drawFrame(screen, device);
  drawGrid(screen, device);

  // Dead stones are drawn as ghosts and the territory they concede is marked
  // like any other. Tapping a group flips it, which is the whole negotiation:
  // the machine's opinion is a starting point, not a verdict.
  const int16_t radius = stoneRadius();
  for (int point = 0; point < go::kPoints; ++point) {
    int16_t cx = 0;
    int16_t cy = 0;
    stoneCentre(device, point, cx, cy);
    const uint8_t here = model.game.point[point];
    if (go::isStone(here)) {
      if (go::marked(model.game.dead, point)) {
        toybox::disc(screen, cx, cy, radius, fui::Paint::dither(fui::Color::LightGray));
        toybox::disc(
            screen, cx, cy, static_cast<int16_t>(radius - 3),
            here == go::kBlack ? fui::Paint::dither(fui::Color::DarkGray) : fui::Paint::solid(fui::Color::White));
      } else {
        stone(screen, cx, cy, radius, here);
      }
      continue;
    }
    const uint8_t owner = model.owner[point];
    if (owner == go::kEmpty) continue;
    // A small square, not a stone: a point that is somebody's is not a point
    // somebody has played on, and drawing it as a stone would make a counted
    // board unreadable.
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy - 9), 18, 18);
    if (owner == go::kBlack) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().fill(box, fui::Paint::solid(fui::Color::White));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule);
    }
  }

  // Territory marks over dead stones too, so a dead group visibly becomes the
  // other side's ground rather than merely fading.
  for (int point = 0; point < go::kPoints; ++point) {
    if (!go::marked(model.game.dead, point)) continue;
    const uint8_t owner = model.owner[point];
    if (owner == go::kEmpty) continue;
    int16_t cx = 0;
    int16_t cy = 0;
    stoneCentre(device, point, cx, cy);
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(cx - 7), static_cast<int16_t>(cy - 7), 14, 14);
    if (owner == go::kBlack) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().fill(box, fui::Paint::solid(fui::Color::White));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    }
  }

  const int16_t bandTop = static_cast<int16_t>(boardTop() + kBoardSide + toybox::kGutter);
  char blackLine[32];
  char whiteLine[32];
  std::snprintf(blackLine, sizeof(blackLine), "BLACK  %d", model.blackHalves / 2);
  std::snprintf(whiteLine, sizeof(whiteLine), "WHITE  %d.%d", model.whiteHalves / 2, (model.whiteHalves % 2) * 5);
  fui::TextStyle line;
  line.font = toybox::kUiFont;
  line.align = fui::TextAlign::Left;
  screen.target().text(toybox::inkCentred(fui::makeRect(boardLeft(device), bandTop, 220, 34), toybox::kUiCut),
                       blackLine, line);
  screen.target().text(
      toybox::inkCentred(fui::makeRect(boardLeft(device), static_cast<int16_t>(bandTop + 36), 220, 34), toybox::kUiCut),
      whiteLine, line);

  fui::TextStyle hint;
  hint.font = toybox::kTileFont;
  hint.align = fui::TextAlign::Right;
  screen.target().text(toybox::inkCentred(fui::makeRect(static_cast<int16_t>(boardLeft(device) + 220), bandTop,
                                                        static_cast<int16_t>(kBoardSide - 220), 70),
                                          toybox::kTileCut),
                       model.yourTurn ? "TAP A DEAD GROUP" : "THEY ARE MARKING", hint);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  char result[24];
  formatResult(result, sizeof(result), model.blackHalves, model.whiteHalves);

  const bool blackWon = model.blackHalves > model.whiteHalves;
  const bool youWon = (blackWon ? go::kBlack : go::kWhite) == model.seat;
  const char* headline =
      model.sharedDevice ? (blackWon ? "BLACK WINS" : "WHITE WINS") : (youWon ? "YOU WIN" : "THEY WIN");

  toyboxChrome(screen, headline, result);
  screen.insetContent(fui::Insets{0, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();
  const fui::Rect bottom = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  again.borderEdges = fui::EdgesNone;
  screen.button(again, fui::makeRect(bottom.x, bottom.y, static_cast<int16_t>(bottom.width - 152), bottom.height));

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  done.borderEdges = fui::EdgesNone;
  screen.button(done, fui::makeRect(static_cast<int16_t>(bottom.right() - 140), bottom.y, 140, bottom.height));

  drawFrame(screen, device);
  drawGrid(screen, device);
  const int16_t radius = stoneRadius();
  for (int point = 0; point < go::kPoints; ++point) {
    int16_t cx = 0;
    int16_t cy = 0;
    stoneCentre(device, point, cx, cy);
    const uint8_t here = model.game.point[point];
    if (go::isStone(here) && !go::marked(model.game.dead, point)) {
      stone(screen, cx, cy, radius, here);
      continue;
    }
    const uint8_t owner = model.owner[point];
    if (owner == go::kEmpty) continue;
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy - 9), 18, 18);
    if (owner == go::kBlack) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().fill(box, fui::Paint::solid(fui::Color::White));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule);
    }
  }

  const int16_t bandTop = static_cast<int16_t>(boardTop() + kBoardSide + toybox::kGutter);
  char blackLine[64];
  char whiteLine[64];
  if (model.game.handicap > 0) {
    std::snprintf(blackLine, sizeof(blackLine), "BLACK  %d  (%u STONES)", model.blackHalves / 2,
                  static_cast<unsigned>(model.game.handicap));
  } else {
    std::snprintf(blackLine, sizeof(blackLine), "BLACK  %d", model.blackHalves / 2);
  }
  // The komi comes from the GAME, not from a constant: the level ladder changes
  // it, so a number written into the sentence would be wrong at two levels out
  // of three and there would be nothing on screen to notice it with.
  std::snprintf(whiteLine, sizeof(whiteLine), "WHITE  %d.%d  (KOMI %d.%d)", model.whiteHalves / 2,
                (model.whiteHalves % 2) * 5, model.game.komiHalves / 2, (model.game.komiHalves % 2) * 5);
  fui::TextStyle line;
  line.font = toybox::kUiFont;
  line.align = fui::TextAlign::Left;
  screen.target().text(toybox::inkCentred(fui::makeRect(boardLeft(device), bandTop, kBoardSide, 34), toybox::kUiCut),
                       blackLine, line);
  screen.target().text(
      toybox::inkCentred(fui::makeRect(boardLeft(device), static_cast<int16_t>(bandTop + 36), kBoardSide, 34),
                         toybox::kUiCut),
      whiteLine, line);
}

}  // namespace goui
