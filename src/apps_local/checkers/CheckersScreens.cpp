#include "CheckersScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>
#include <cstdlib>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxIcons.h"

namespace checkui {

namespace {

namespace ck = checkers;

// The header band with the offset rule under it, as jaipur and the dungeon
// wear it. A local copy rather than a shared helper, for the reason
// LinkScreens gives: a copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme's default is black on
  // the black band. Jaipur paid for this discovery; see its toyboxChrome.
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

// The board at an arbitrary cell size, drawn from the saved final position,
// for the menu's ornament. The live board's squareRect is tied to the live
// board's geometry.
void miniBoard(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell,
               const uint8_t* cells) {
  for (int rank = 0; rank < ck::kSize; ++rank) {
    for (int file = 0; file < ck::kSize; ++file) {
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(left + file * cell), static_cast<int16_t>(top + rank * cell), cell, cell);
      if (ck::playable(file, rank)) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
      const uint8_t piece = cells[ck::indexOf(file, rank)];
      if ((piece & ck::kPiece) == 0) continue;
      const bool filled = (piece & ck::kDark) != 0;
      const bool king = (piece & ck::kKing) != 0;
      const int16_t cx = static_cast<int16_t>(box.x + cell / 2);
      const int16_t cy = static_cast<int16_t>(box.y + cell / 2);
      const int16_t radius = static_cast<int16_t>(cell * 23 / 56);
      toybox::disc(screen, cx, cy, radius, fui::Color::Black);
      toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 2), filled ? fui::Color::Black : fui::Color::White);
      if (king) {
        toybox::disc(screen, cx, cy, static_cast<int16_t>(radius / 2), filled ? fui::Color::White : fui::Color::Black);
        toybox::disc(screen, cx, cy, static_cast<int16_t>(radius / 2 - 3),
                     filled ? fui::Color::Black : fui::Color::White);
      }
    }
  }
  const int16_t side = static_cast<int16_t>(cell * ck::kSize);
  screen.target().stroke(
      fui::makeRect(static_cast<int16_t>(left - toybox::kRule), static_cast<int16_t>(top - toybox::kRule),
                    static_cast<int16_t>(side + toybox::kRule * 2), static_cast<int16_t>(side + toybox::kRule * 2)),
      fui::Paint::solid(fui::Color::Black), toybox::kRule);
}

// 56px, so eight squares are exactly the 448px between the margins.
constexpr int16_t kSquare = 56;
constexpr int16_t kBoardSide = kSquare * ck::kSize;

int16_t boardTop() { return static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2); }

int16_t boardLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kBoardSide) / 2); }

void drawDisc(toybox::Screen& screen, const fui::Rect& where, const bool filled, const bool king) {
  const int16_t cx = static_cast<int16_t>(where.x + where.width / 2);
  const int16_t cy = static_cast<int16_t>(where.y + where.height / 2);
  constexpr int16_t kRadius = 23;
  const fui::Color body = filled ? fui::Color::Black : fui::Color::White;
  const fui::Color mark = filled ? fui::Color::White : fui::Color::Black;

  toybox::disc(screen, cx, cy, kRadius, fui::Color::Black);
  toybox::disc(screen, cx, cy, static_cast<int16_t>(kRadius - 3), body);
  if (!king) return;
  // The stacked second piece, in the opposite ink so it reads on both colours.
  toybox::disc(screen, cx, cy, 12, mark);
  toybox::disc(screen, cx, cy, 9, body);
}

}  // namespace

fui::Rect squareRect(const fui::DeviceContext& device, const int file, const int rank, const uint8_t seat) {
  // Drawn from the playing side's end, so your own men are always nearest you.
  const int drawFile = seat == ck::kLight ? file : ck::kSize - 1 - file;
  const int drawRank = seat == ck::kLight ? rank : ck::kSize - 1 - rank;
  return fui::makeRect(static_cast<int16_t>(boardLeft(device) + drawFile * kSquare),
                       static_cast<int16_t>(boardTop() + drawRank * kSquare), kSquare, kSquare);
}

bool squareAt(const fui::DeviceContext& device, const int x, const int y, const uint8_t seat, int& file, int& rank) {
  const int dx = x - boardLeft(device);
  const int dy = y - boardTop();
  if (dx < 0 || dy < 0 || dx >= kBoardSide || dy >= kBoardSide) return false;
  const int drawFile = dx / kSquare;
  const int drawRank = dy / kSquare;
  file = seat == ck::kLight ? drawFile : ck::kSize - 1 - drawFile;
  rank = seat == ck::kLight ? drawRank : ck::kSize - 1 - drawRank;
  return true;
}

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "CHECKERS");

  // The front door in the documented band order: record, rule, the last
  // game's final position as the ornament, doors anchored bottom.
  char record[48];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.wins + model.losses + model.draws, model.wins);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(line, record, small);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  // Row 0 reads selected by default, jaipur's own trick: the most likely tap
  // is also the loudest thing below the rule.
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
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         selected == static_cast<int>(MenuRow::PlayNearby));

  if (!model.hasHistory || model.lastCells == nullptr) return;

  // The last game, held where the eye rests: the final position with kings
  // marked, and one line saying how it ended. Ornament made of the app's own
  // material and the app's own data, the only kind this fork allows.
  constexpr int16_t kMini = 34;
  const int16_t side = static_cast<int16_t>(kMini * ck::kSize);
  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  const int16_t room = static_cast<int16_t>(listBand.y - areaTop);
  const int16_t blockH = static_cast<int16_t>(side + 12 + 24);
  const int16_t top = static_cast<int16_t>(areaTop + (room > blockH ? (room - blockH) / 2 : 12));
  const fui::DeviceContext device = screen.device();
  miniBoard(screen, static_cast<int16_t>((device.width - side) / 2), top, kMini, model.lastCells);

  // The verb comes from the recorded outcome, never from the material: a win
  // by blocking can leave the counts level, or even against the winner.
  char caption[40];
  const int margin = model.lastYourPieces - model.lastTheirPieces;
  if (model.lastOutcome == 2) {
    std::snprintf(caption, sizeof(caption), "LAST GAME: A DRAW");
  } else if (model.lastOutcome == 0) {
    if (margin > 0) {
      std::snprintf(caption, sizeof(caption), "LAST GAME: WON BY %d", margin);
    } else {
      std::snprintf(caption, sizeof(caption), "LAST GAME: WON");
    }
  } else if (margin < 0) {
    std::snprintf(caption, sizeof(caption), "LAST GAME: LOST BY %d", -margin);
  } else {
    std::snprintf(caption, sizeof(caption), "LAST GAME: LOST");
  }
  fui::TextStyle cap;
  cap.font = toybox::kTileFont;
  cap.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(content.x, static_cast<int16_t>(top + side + 12), content.width, 24), caption,
                       cap);
}

namespace {

// A 3x3 window of the live board, drawn with the live board's own marks -- a
// dot is a landing, a bracket is a piece about to be taken.
void lessonBoard(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell, const int page) {
  // Which parity is "dark" follows the pieces: the step page's column sits on
  // odd squares, the jump chain and the king's diagonals on even ones, and a
  // checkers diagram whose pieces stand on light squares teaches wrongly.
  const int dark = page == 0 ? 1 : 0;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(left + col * cell), static_cast<int16_t>(top + row * cell), cell, cell);
      if (((col + row) & 1) == dark) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    }
  }
  const auto at = [&](const int col, const int row) {
    return fui::makeRect(static_cast<int16_t>(left + col * cell), static_cast<int16_t>(top + row * cell), cell, cell);
  };
  const auto piece = [&](const int col, const int row, const bool filled, const bool king) {
    const fui::Rect box = at(col, row);
    const int16_t cx = static_cast<int16_t>(box.x + cell / 2);
    const int16_t cy = static_cast<int16_t>(box.y + cell / 2);
    const int16_t radius = static_cast<int16_t>(cell * 23 / 56);
    toybox::disc(screen, cx, cy, radius, fui::Color::Black);
    toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 3), filled ? fui::Color::Black : fui::Color::White);
    if (king) {
      toybox::disc(screen, cx, cy, static_cast<int16_t>(radius / 2), filled ? fui::Color::White : fui::Color::Black);
      toybox::disc(screen, cx, cy, static_cast<int16_t>(radius / 2 - 3),
                   filled ? fui::Color::Black : fui::Color::White);
    }
  };
  const auto dot = [&](const int col, const int row) {
    const fui::Rect box = at(col, row);
    toybox::disc(screen, static_cast<int16_t>(box.x + cell / 2), static_cast<int16_t>(box.y + cell / 2),
                 static_cast<int16_t>(cell * 9 / 56 + 3), fui::Color::Black);
  };

  if (page == 0) {
    // A man, its two forward diagonals, and the crown waiting on the far row.
    piece(1, 2, false, false);
    dot(0, 1);
    dot(2, 1);
    piece(1, 0, false, true);
  } else if (page == 1) {
    // The compulsory jump: your man, theirs bracketed, the landing beyond.
    piece(0, 2, false, false);
    piece(1, 1, true, false);
    toybox::bracket(screen, at(1, 1), static_cast<int16_t>(cell / 3), 4);
    dot(2, 0);
  } else {
    // A king and all four of its diagonals.
    piece(1, 1, false, true);
    dot(0, 0);
    dot(2, 0);
    dot(0, 2);
    dot(2, 2);
  }
}

// The tutorial shape jaipur ships: a titled page, the whole page as the
// button, dots at the foot, the caption centred in what the diagram leaves.
void doBuildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  screen.frame().hit(body, ActionHowToNext, 0);

  constexpr int16_t kDot = 14;
  constexpr int16_t kDotGap = 10;
  const int16_t dotRow = static_cast<int16_t>(pages * kDot + (pages - 1) * kDotGap);
  const int16_t dotX = static_cast<int16_t>(body.x + (body.width - dotRow) / 2);
  const int16_t dotY = static_cast<int16_t>(body.bottom() - kDot);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect dotAt = fui::makeRect(static_cast<int16_t>(dotX + i * (kDot + kDotGap)), dotY, kDot, kDot);
    if (i == page) {
      screen.target().fill(dotAt, fui::Paint::solid(fui::Color::Black), 7);
    } else {
      screen.target().stroke(dotAt, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 7);
    }
  }
  fui::TextStyle tapLine;
  tapLine.font = toybox::kTileFont;
  tapLine.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(dotY - 34), body.width, 22),
                       page + 1 == pages ? "TAP TO FINISH" : "TAP TO CONTINUE", tapLine);

  static const char* const kTitles[] = {"MEN AND CROWNS", "TAKING", "KINGS"};
  fui::TextStyle title;
  title.font = toybox::kDisplayFont;
  title.align = fui::TextAlign::Center;
  // inkCentred: the display cut's line box is 63 and this band is 46, so the
  // target's clamp would drop the title 9px and run its feet past the band.
  screen.target().text(toybox::inkCentred(fui::makeRect(body.x, body.y, body.width, 46), toybox::kDisplayCut),
                       kTitles[page], title);

  static const char* const kLines[] = {
      "MEN STEP DIAGONALLY FORWARD. REACH THE FAR ROW AND A MAN IS CROWNED.",
      "IF YOU CAN TAKE, YOU MUST. A CHAIN OF JUMPS IS ONE MOVE.",
      "A CROWNED PIECE MOVES BOTH WAYS. NO MOVES LEFT MEANS YOU HAVE LOST.",
  };
  fui::TextStyle cap;
  cap.font = toybox::kUiFont;
  cap.align = fui::TextAlign::Center;
  // Four lines, not three: the first page's caption needs them at this width,
  // and three was exactly the CROWNE bug this candidate exists to bury.
  cap.maxLines = 4;
  const int16_t capTop = static_cast<int16_t>(body.bottom() - 56 - 204);
  screen.target().text(fui::makeRect(body.x, capTop, body.width, 200), kLines[page], cap);

  // The lesson at nearly double the shipping size, centred in its band.
  constexpr int16_t kCell = 88;
  const int16_t bandTop = static_cast<int16_t>(body.y + 92);
  const int16_t side = kCell * 3;
  const int16_t top = static_cast<int16_t>(bandTop + (capTop - bandTop - side) / 2);
  const fui::DeviceContext device = screen.device();
  lessonBoard(screen, static_cast<int16_t>((device.width - side) / 2), top, kCell, page);
}

}  // namespace

void buildHowTo(toybox::Screen& screen, const HowToModel& model) { doBuildHowTo(screen, model); }

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "CHECKERS";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();

  // The status capsule, taken first so the board can never grow into it. Every
  // other game in the fork ends on one of these.
  fui::ButtonProps status;
  // "YOU MUST TAKE" is the caption for the corner marks below: the marks show
  // WHICH pieces, the capsule says why there are so few of them.
  status.label = model.yourTurn ? (model.mustTake ? "YOU MUST TAKE" : "YOUR MOVE") : "THEIR MOVE";
  status.action = fui::NO_ACTION;
  status.borderEdges = fui::EdgesNone;
  const fui::Rect capsule = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  screen.button(
      status, model.opponentName != nullptr ? linkui::withOpponentFace(screen, capsule, model.opponentName) : capsule);

  // Nine pixels, and the same nine the rect is grown by, so the border sits
  // flush against the squares and the surface reads as one object. It was
  // stroked at three, leaving six pixels of white between it and the board and
  // making it LIGHTER than the selection frame drawn inside it -- the weight
  // order the metrics header exists to prevent.
  const fui::Rect frame = fui::makeRect(static_cast<int16_t>(boardLeft(device) - toybox::kBoardFrame),
                                        static_cast<int16_t>(boardTop() - toybox::kBoardFrame),
                                        static_cast<int16_t>(kBoardSide + toybox::kBoardFrame * 2),
                                        static_cast<int16_t>(kBoardSide + toybox::kBoardFrame * 2));
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);

  for (int rank = 0; rank < ck::kSize; ++rank) {
    for (int file = 0; file < ck::kSize; ++file) {
      const fui::Rect box = squareRect(device, file, rank, model.seat);
      // Only dark squares are played on, and they are the dithered ones, so the
      // board reads as a board before a single piece is drawn.
      if (ck::playable(file, rank)) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));

      const int index = ck::indexOf(file, rank);
      if (ck::occupied(model.game, index)) {
        drawDisc(screen, box, ck::isDark(model.game, index), ck::isKing(model.game, index));
      }
    }
  }

  // What each side still HAS, in the band between the board and the capsule.
  //
  // Remaining rather than captured, which is not the obvious way round. Two
  // strips of losses are both EMPTY at the start, so the band is blank exactly
  // when the game begins, and reserving the slots with placeholder marks put
  // twenty-four dashes on screen carrying nothing. Remaining pieces are always
  // twelve-ish and always shrinking, so the strip is full of information from
  // the first frame and the question a checkers player actually asks -- am I up
  // or down -- is answered by which row is longer. No counting twenty-four discs
  // on the board, and no number to read.
  //
  // Theirs sits nearest the board, yours nearest you, matching the ends the two
  // sides are drawn from. Filled means whose, the same as on the board.
  {
    // Pinned up under the board rather than floating in the band's middle,
    // each row led by its name. THEM and YOU turn the dots into data with a
    // caption rather than decoration, and kill the ordering ambiguity two
    // unlabelled rows always had.
    constexpr int16_t kSmall = 11;
    constexpr int16_t kPitch = 28;
    constexpr int16_t kLabelWidth = 72;
    const int16_t bandTop = static_cast<int16_t>(boardTop() + kBoardSide + toybox::kBoardFrame);
    const int16_t rowPitch = static_cast<int16_t>(kSmall * 2 + toybox::kGutter);
    const bool yoursAreFilled = model.seat == ck::kDarkSeat;
    const int16_t stripTop = static_cast<int16_t>(bandTop + toybox::kGutter + 4);
    for (int row = 0; row < 2; ++row) {
      const int held = row == 0 ? model.theirPieces : model.yourPieces;
      const bool filled = row == 0 ? !yoursAreFilled : yoursAreFilled;
      const int16_t cy = static_cast<int16_t>(stripTop + row * rowPitch + kSmall);
      fui::TextStyle label;
      label.font = toybox::kTileFont;
      label.align = fui::TextAlign::Left;
      screen.target().text(fui::makeRect(boardLeft(device), static_cast<int16_t>(cy - 11), kLabelWidth, 22),
                           row == 0 ? "THEM" : "YOU", label);
      for (int i = 0; i < held; ++i) {
        const int16_t cx = static_cast<int16_t>(boardLeft(device) + kLabelWidth + kSmall + i * kPitch);
        toybox::disc(screen, cx, cy, kSmall, fui::Color::Black);
        toybox::disc(screen, cx, cy, static_cast<int16_t>(kSmall - 2), filled ? fui::Color::Black : fui::Color::White);
      }
    }
  }

  // With nothing in hand, mark every piece that HAS a move. Under a compulsory
  // capture that set collapses from seven squares to one, and the board shows
  // the rule rather than the player finding it by tapping men that will not
  // lift.
  if (model.picked == ck::kNothingPicked && model.yourTurn) {
    for (int square = 0; square < ck::kCells; ++square) {
      if ((model.movable & (static_cast<uint64_t>(1) << square)) == 0) continue;
      toybox::bracket(screen, squareRect(device, square % ck::kSize, square / ck::kSize, model.seat), 14, 3);
    }
  }

  // The piece in hand, framed OUTSIDE its square. An inset frame eats the piece
  // and makes its square look smaller than its neighbours, which is written
  // down in the design language and was ignored here.
  if (model.picked != ck::kNothingPicked) {
    const fui::Rect box = squareRect(device, model.picked % ck::kSize, model.picked / ck::kSize, model.seat);
    screen.target().stroke(
        fui::makeRect(static_cast<int16_t>(box.x - toybox::kFrame), static_cast<int16_t>(box.y - toybox::kFrame),
                      static_cast<int16_t>(box.width + toybox::kFrame * 2),
                      static_cast<int16_t>(box.height + toybox::kFrame * 2)),
        fui::Paint::solid(fui::Color::Black), toybox::kFrame);
  }

  // Where it may land, and what that landing takes. Destinations come from the
  // rules' own move list, so a marked square is always a playable one.
  for (int i = 0; i < model.destinationCount; ++i) {
    const int square = model.destinations[i];
    const fui::Rect box = squareRect(device, square % ck::kSize, square / ck::kSize, model.seat);
    // Round, not square. On a board where every cell is square and every piece
    // is round, "a disc goes here" is a round event -- and a square pip would
    // read as the king mark at a glance.
    // Radius nine, not seven. Chess marks a destination at 13px of 53; a circle
    // of the same DIAMETER covers 78% of the square pip it replaced, so it read
    // noticeably smaller than its sibling until the radius grew to match by
    // area.
    toybox::disc(screen, static_cast<int16_t>(box.x + box.width / 2), static_cast<int16_t>(box.y + box.height / 2), 9,
                 fui::Color::Black);
    for (int taken = 0; taken < ck::kCells; ++taken) {
      if ((model.takenMasks[i] & (static_cast<uint64_t>(1) << taken)) == 0) continue;
      toybox::bracket(screen, squareRect(device, taken % ck::kSize, taken / ck::kSize, model.seat), 18, 4);
    }
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const bool won = (model.seat == ck::kLight && model.outcome == ck::Outcome::LightWins) ||
                   (model.seat == ck::kDarkSeat && model.outcome == ck::Outcome::DarkWins);

  fui::HeaderProps header;
  header.title = model.outcome == ck::Outcome::Draw ? "A DRAW" : (won ? "YOU WIN" : "THEY WIN");
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char line[48];
  std::snprintf(line, sizeof(line), "%d - %d", model.yourPieces, model.theirPieces);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 50), area.width, 70), line, big);

  if (model.outcome == ck::Outcome::Draw) {
    fui::TextStyle body;
    body.font = toybox::kBodyFont;
    body.align = fui::TextAlign::Center;
    body.maxLines = 2;
    screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 130), area.width, 60),
                         "FORTY MOVES EACH WITH NOTHING TAKEN.", body);
  }
}

}  // namespace checkui
