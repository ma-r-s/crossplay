#include "CheckersScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxIcons.h"

namespace checkui {

namespace {

namespace ck = checkers;

// 56px, so eight squares are exactly the 448px between the margins.
constexpr int16_t kSquare = 56;
constexpr int16_t kBoardSide = kSquare * ck::kSize;

int16_t boardTop() { return static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2); }

int16_t boardLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kBoardSide) / 2); }

// A filled disc of radius `r`, by rows. Used in pairs to make rings: a disc in
// ink with a smaller one in paper on top is a closed ring, which the first
// version did not manage.
//
// Circle test per row rather than a table of half-widths. The table gave a
// flat-topped lozenge, and worse, jumped by five between entries.
void disc(toybox::Screen& screen, const int16_t cx, const int16_t cy, const int16_t r, const fui::Color colour) {
  const fui::Paint paint = fui::Paint::solid(colour);
  for (int16_t dy = static_cast<int16_t>(-r); dy <= r; ++dy) {
    int16_t half = 0;
    while ((half + 1) * (half + 1) + dy * dy <= r * r) ++half;
    if (half <= 0) continue;
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(cx - half), static_cast<int16_t>(cy + dy), static_cast<int16_t>(half * 2), 1),
        paint);
  }
}

// Four corner marks around a square: flag it without covering what stands on
// it. Toybox's cornerMarks takes a GfxRenderer, which a freestanding screen
// builder does not have, so this is the same shape drawn from fills.
void bracket(toybox::Screen& screen, const fui::Rect& box, const int16_t arm, const int16_t weight) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int16_t right = static_cast<int16_t>(box.x + box.width - arm);
  const int16_t bottom = static_cast<int16_t>(box.y + box.height - weight);
  const int16_t far = static_cast<int16_t>(box.x + box.width - weight);
  const int16_t low = static_cast<int16_t>(box.y + box.height - arm);
  screen.target().fill(fui::makeRect(box.x, box.y, arm, weight), ink);
  screen.target().fill(fui::makeRect(right, box.y, arm, weight), ink);
  screen.target().fill(fui::makeRect(box.x, bottom, arm, weight), ink);
  screen.target().fill(fui::makeRect(right, bottom, arm, weight), ink);
  screen.target().fill(fui::makeRect(box.x, box.y, weight, arm), ink);
  screen.target().fill(fui::makeRect(far, box.y, weight, arm), ink);
  screen.target().fill(fui::makeRect(box.x, low, weight, arm), ink);
  screen.target().fill(fui::makeRect(far, low, weight, arm), ink);
}

// A piece. Pieces are round because everything else on this board is square,
// and at 56px that difference does more work than any detail inside them would.
//
// Light pieces are outlined, dark are solid: the same convention chess uses
// here, where filled means WHOSE and never whose turn.
//
// The rim used to be vertical bars at each band's ends. Nothing closed the top
// or the bottom, and consecutive bars did not overlap where the half-width
// table jumped, so a light piece rendered as two parenthesis arcs and four
// floating dots with the dither showing straight through. Nested discs close by
// construction.
//
// The king mark is a RING, not the square the first version drew. Twenty lines
// up this file argues that pieces are round because everything else is square;
// a square crown contradicted that, and on a light piece it was the only closed
// shape present, so it read as the piece.
void drawDisc(toybox::Screen& screen, const fui::Rect& where, const bool filled, const bool king) {
  const int16_t cx = static_cast<int16_t>(where.x + where.width / 2);
  const int16_t cy = static_cast<int16_t>(where.y + where.height / 2);
  constexpr int16_t kRadius = 23;
  const fui::Color body = filled ? fui::Color::Black : fui::Color::White;
  const fui::Color mark = filled ? fui::Color::White : fui::Color::Black;

  disc(screen, cx, cy, kRadius, fui::Color::Black);
  disc(screen, cx, cy, static_cast<int16_t>(kRadius - 3), body);
  if (!king) return;
  // The stacked second piece, in the opposite ink so it reads on both colours.
  disc(screen, cx, cy, 12, mark);
  disc(screen, cx, cy, 9, body);
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
  fui::HeaderProps header;
  header.title = "CHECKERS";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  const fui::Rect band = screen.body();
  screen.list(list);

  toybox::iconAtRowRight(screen, band, static_cast<int>(MenuRow::PlayNearby), linkui::nearbyMark(),
                         model.selected == static_cast<int>(MenuRow::PlayNearby));
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char progress[8];
  std::snprintf(progress, sizeof(progress), "%d/%d", page + 1, howToPages());
  fui::TextStyle counter;
  counter.font = toybox::kSmallFont;
  counter.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), progress, counter);

  static const char* const kLines[] = {
      "MEN STEP DIAGONALLY FORWARD. REACH THE FAR ROW AND A MAN IS CROWNED.",
      "IF YOU CAN TAKE, YOU MUST. A CHAIN OF JUMPS IS ONE MOVE.",
      "A CROWNED PIECE MOVES BOTH WAYS. NO MOVES LEFT MEANS YOU HAVE LOST.",
  };
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 3;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 24), area.width, 130), kLines[page], body);

  // Three squares of the real board, at the real size, showing the page.
  const fui::DeviceContext device = screen.device();
  const int16_t top = static_cast<int16_t>(area.y + 190);
  const int16_t left = static_cast<int16_t>((device.width - kSquare * 3) / 2);
  for (int col = 0; col < 3; ++col) {
    for (int row = 0; row < 2; ++row) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + col * kSquare),
                                          static_cast<int16_t>(top + row * kSquare), kSquare, kSquare);
      if (((col + row) & 1) == 1) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
      if (page == 0 && col == 1 && row == 1) drawDisc(screen, box, false, false);
      if (page == 1 && col == 0 && row == 1) drawDisc(screen, box, false, false);
      if (page == 1 && col == 1 && row == 0) drawDisc(screen, box, true, false);
      if (page == 2 && col == 1 && row == 1) drawDisc(screen, box, false, true);
    }
  }
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "CHECKERS";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
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
    constexpr int16_t kPitch = 32;
    constexpr int16_t kSmall = 11;
    const int16_t bandTop = static_cast<int16_t>(boardTop() + kBoardSide + toybox::kBoardFrame);
    const int16_t bandHeight = static_cast<int16_t>(capsule.y - toybox::kGutter - bandTop);
    const int16_t rowPitch = static_cast<int16_t>(kSmall * 2 + toybox::kGutter);
    // Centred in the band rather than pinned under the board: the slack is
    // spread around the strips, not left in one lump at the bottom.
    const int16_t stripTop = static_cast<int16_t>(bandTop + (bandHeight - rowPitch * 2) / 2);
    const int16_t stripLeft = static_cast<int16_t>(boardLeft(device) + kSmall);
    const bool yoursAreFilled = model.seat == ck::kDarkSeat;

    for (int row = 0; row < 2; ++row) {
      const int held = row == 0 ? model.theirPieces : model.yourPieces;
      const bool filled = row == 0 ? !yoursAreFilled : yoursAreFilled;
      const int16_t cy = static_cast<int16_t>(stripTop + row * rowPitch + kSmall);
      for (int i = 0; i < held; ++i) {
        const int16_t cx = static_cast<int16_t>(stripLeft + i * kPitch);
        disc(screen, cx, cy, kSmall, fui::Color::Black);
        disc(screen, cx, cy, static_cast<int16_t>(kSmall - 2), filled ? fui::Color::Black : fui::Color::White);
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
      bracket(screen, squareRect(device, square % ck::kSize, square / ck::kSize, model.seat), 14, 3);
    }
  }

  // The piece in hand, framed OUTSIDE its square. An inset frame eats the piece
  // and makes its square look smaller than its neighbours, which is written
  // down in the design language and was ignored here.
  if (model.picked != ck::kNothingPicked) {
    const fui::Rect box = squareRect(device, model.picked % ck::kSize, model.picked / ck::kSize, model.seat);
    screen.target().stroke(fui::makeRect(static_cast<int16_t>(box.x - toybox::kFrame),
                                         static_cast<int16_t>(box.y - toybox::kFrame),
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
    disc(screen, static_cast<int16_t>(box.x + box.width / 2), static_cast<int16_t>(box.y + box.height / 2), 9,
         fui::Color::Black);
    for (int taken = 0; taken < ck::kCells; ++taken) {
      if ((model.takenMasks[i] & (static_cast<uint64_t>(1) << taken)) == 0) continue;
      bracket(screen, squareRect(device, taken % ck::kSize, taken / ck::kSize, model.seat), 18, 4);
    }
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const bool won = (model.seat == ck::kLight && model.outcome == ck::Outcome::LightWins) ||
                   (model.seat == ck::kDarkSeat && model.outcome == ck::Outcome::DarkWins);

  fui::HeaderProps header;
  header.title = model.outcome == ck::Outcome::Draw ? "A DRAW" : (won ? "YOU WIN" : "THEY WIN");
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
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
