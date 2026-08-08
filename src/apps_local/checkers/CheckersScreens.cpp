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

// A disc, from stacked bars. Pieces are round because everything else on this
// board is square, and at 56px that difference does more work than any detail
// inside them would.
void drawDisc(toybox::Screen& screen, const fui::Rect& where, const bool filled, const bool king) {
  static const int8_t kHalf[] = {9, 14, 17, 19, 20, 20, 19, 17, 14, 9};
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int16_t cx = static_cast<int16_t>(where.x + where.width / 2);
  const int16_t cy = static_cast<int16_t>(where.y + where.height / 2);

  for (int i = 0; i < 10; ++i) {
    const int16_t half = kHalf[i];
    const fui::Rect band = fui::makeRect(static_cast<int16_t>(cx - half), static_cast<int16_t>(cy - 20 + i * 4),
                                         static_cast<int16_t>(half * 2), 4);
    // Light pieces are outlined, dark are solid. That is the same convention
    // chess uses here -- filled means whose, never whose turn.
    screen.target().fill(band, filled ? ink : fui::Paint::solid(fui::Color::White));
  }
  // The rim, so an outlined piece is a piece rather than a hole.
  for (int i = 0; i < 10; ++i) {
    const int16_t half = kHalf[i];
    screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - half), static_cast<int16_t>(cy - 20 + i * 4), 3, 4),
                         ink);
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(cx + half - 3), static_cast<int16_t>(cy - 20 + i * 4), 3, 4), ink);
  }
  if (!king) return;
  // A king carries a second, smaller ring: crowned pieces are stacked in the
  // physical game, and a ring reads as that stack without needing a crown.
  const fui::Rect inner = fui::makeRect(static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy - 9), 18, 18);
  screen.target().stroke(inner, fui::Paint::solid(filled ? fui::Color::White : fui::Color::Black), 3);
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
  status.label = model.yourTurn ? "YOUR MOVE" : "THEIR MOVE";
  status.action = fui::NO_ACTION;
  status.borderEdges = fui::EdgesNone;
  const fui::Rect capsule = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  screen.button(
      status, model.opponentName != nullptr ? linkui::withOpponentFace(screen, capsule, model.opponentName) : capsule);

  const fui::Rect corner = squareRect(device, 0, 0, model.seat);
  const fui::Rect frame = fui::makeRect(static_cast<int16_t>(boardLeft(device) - toybox::kBoardFrame),
                                        static_cast<int16_t>(boardTop() - toybox::kBoardFrame),
                                        static_cast<int16_t>(kBoardSide + toybox::kBoardFrame * 2),
                                        static_cast<int16_t>(kBoardSide + toybox::kBoardFrame * 2));
  (void)corner;
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), 3);

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

  // The piece in hand, and where it may go. Destinations come from the rules'
  // own move list, so a marked square is always a playable one.
  if (model.picked != ck::kNothingPicked) {
    const fui::Rect box = squareRect(device, model.picked % ck::kSize, model.picked / ck::kSize, model.seat);
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 5);
  }
  for (int i = 0; i < model.destinationCount; ++i) {
    const int square = model.destinations[i];
    const fui::Rect box = squareRect(device, square % ck::kSize, square / ck::kSize, model.seat);
    const fui::Rect pip = fui::makeRect(static_cast<int16_t>(box.x + box.width / 2 - 7),
                                        static_cast<int16_t>(box.y + box.height / 2 - 7), 14, 14);
    screen.target().fill(pip, fui::Paint::solid(fui::Color::Black));
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
