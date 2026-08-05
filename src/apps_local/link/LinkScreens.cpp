#include "LinkScreens.h"

namespace linkui {
namespace {

// The rail is the link. It runs down the left of both seats, so the two rows
// read as one object with a connection rather than two unrelated rows.
constexpr int16_t kRailWidth = toybox::kFrame;
constexpr int16_t kRailGap = toybox::kGutter;

// Same doubled rule the other apps draw. Local rather than shared because
// ChessScreens and ConnectionsScreens each keep their own; a fourth copy is
// cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

const char* seatValue(const SeatState state, const bool linked) {
  switch (state) {
    case SeatState::Looking:
      // Before a match there is nobody to wait for, so the vacant seat says what
      // is happening rather than naming an absence.
      return linked ? "WAITING" : "LOOKING";
    case SeatState::Deciding:
      return "DECIDING";
    case SeatState::Ready:
      return "READY";
    case SeatState::Left:
      return "LEFT";
    case SeatState::Lost:
      return "LOST";
  }
  return "";
}

fui::Rect buildLink(toybox::Screen& screen, const LinkModel& model) {
  toyboxChrome(screen, model.gameTitle);

  // Footer first, so the seats can never grow into it. BACK sits at the bottom
  // margin and reserves a gutter above itself, or the two pills fuse into one
  // black slab and read as a single control.
  fui::ButtonProps leave;
  leave.label = "BACK";
  leave.action = ActionLeaveLink;
  const fui::Rect leaveRect = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  if (model.offerPlayAgain) {
    fui::ButtonProps again;
    again.label = "PLAY AGAIN";
    again.action = ActionPlayAgain;
    screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
    // Two solid pills stacked read as one black slab with a scratch across it,
    // which is the thing this file's own gap was meant to prevent and did not.
    // The gap separates them; the weight is what says which one is the answer
    // to the question above. BACK stays solid when it is the only control on
    // the screen, because then there is no hierarchy to express.
    leave.styles = toybox::rowStyles();
  }
  screen.button(leave, leaveRect);

  // The headline sits under the rule rather than floating: a block with equal
  // slack above and below reads as unresolved.
  fui::ButtonProps title;
  title.label = model.headline;
  title.action = fui::NO_ACTION;
  title.borderEdges = fui::EdgesNone;
  screen.button(title, screen.takeTop(toybox::kPillHeight, toybox::kGutter * 2));

  // The seats are one object, not two rows with a gulf between them. Compact,
  // framed, and anchored under the headline: the slack that is left becomes a
  // single deliberate zone above the footer, the way the board leaves room for
  // the captured strips rather than scattering emptiness through the layout.
  const int16_t rowHeight = toybox::kRowHeight;
  // Compact and anchored under the headline, not stretched to fill. Stretching
  // it turned the card into a framed void, which is worse than the slack below:
  // the slack reads as a page with a footer, the void read as a mistake. The
  // zone under the card is where a game will draw the position it just finished
  // -- the one thing worth looking at while deciding whether to play again.
  const int16_t cardHeight = static_cast<int16_t>(rowHeight * 2 + toybox::kFrame * 2);
  const fui::Rect card = screen.takeTop(cardHeight);
  screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), toybox::kHairline);

  // The rail is the link itself: dithered while there is nobody on the other
  // end, solid once there is. It joins the two seats, so it is the one thing on
  // the screen that changes and therefore the one thing worth watching.
  const fui::Rect rail =
      fui::makeRect(static_cast<int16_t>(card.x + toybox::kFrame), static_cast<int16_t>(card.y + toybox::kFrame),
                    kRailWidth, static_cast<int16_t>(cardHeight - toybox::kFrame * 2));
  screen.target().fill(rail,
                       model.linked ? fui::Paint::solid(fui::Color::Black) : fui::Paint::solid(fui::Color::DarkGray));

  const int16_t textX = static_cast<int16_t>(rail.x + kRailWidth + kRailGap);
  const int16_t textWidth = static_cast<int16_t>(card.right() - toybox::kFrame - textX);

  fui::SettingRowProps you;
  you.label = model.yourName;
  you.value = seatValue(model.you, model.linked);
  you.action = fui::NO_ACTION;
  you.styles = fui::plainStyles(fui::Paint{});
  fui::settingRow(screen.frame(),
                  fui::makeRect(textX, static_cast<int16_t>(card.y + toybox::kFrame), textWidth, rowHeight), you);

  fui::SettingRowProps them;
  // A seat nobody is in yet shows the shape of the absence, not a blank.
  them.label = model.theirName != nullptr && model.theirName[0] != '\0' ? model.theirName : "- - - -";
  them.value = seatValue(model.them, model.linked);
  them.action = fui::NO_ACTION;
  them.styles = fui::plainStyles(fui::Paint{});
  fui::settingRow(
      screen.frame(),
      fui::makeRect(textX, static_cast<int16_t>(card.bottom() - toybox::kFrame - rowHeight), textWidth, rowHeight),
      them);

  // What is left between the seats and the footer. Slack until a game puts
  // something in it.
  return screen.body();
}

}  // namespace linkui
