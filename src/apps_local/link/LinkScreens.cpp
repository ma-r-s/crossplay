#include "LinkScreens.h"

#include "../player/PlayerAvatar.h"
#include "../ui/ToyboxIcons.h"

namespace linkui {
namespace {

// The rail is the link. It runs down the left of both seats, so the two rows
// read as one object with a connection rather than two unrelated rows.
constexpr int16_t kRailWidth = toybox::kFrame;
constexpr int16_t kRailGap = toybox::kGutter;

// A seat shows a face, and this is the whole reason the avatar was worth
// building. Their name already crossed the radio -- Hello and Join carry it --
// and the face is a pure function of that string, so the person opposite has a
// face for free. Nothing was added to LinkProtocol and there is nothing that
// can arrive stale or wrong.
//
// An empty seat gets one too: parse("") knows no words, which draws the plain
// head everyone starts from. That reads as "somebody will be here", which is
// what LOOKING means, and it needed no special case to say it.
void drawSeatFace(toybox::Screen& screen, const fui::Rect& row, const char* name) {
  const int16_t size = player::avatarPixels(player::AvatarSize::Row);
  player::drawAvatar(screen.target(),
                     fui::makeRect(row.x, static_cast<int16_t>(row.y + (row.height - size) / 2), size, size),
                     name == nullptr ? "" : name, player::AvatarSize::Row);
}

// Same doubled rule the other apps draw. Local rather than shared because
// ChessScreens and ConnectionsScreens each keep their own; a fourth copy is
// cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

const freeink::Icon& nearbyMark() { return icon_nearby_32; }

fui::Rect withOpponentFace(toybox::Screen& screen, const fui::Rect& band, const char* theirName) {
  if (theirName == nullptr || theirName[0] == '\0') return band;
  const int16_t size = player::avatarPixels(player::AvatarSize::Row);
  // A band too narrow to give the face its strip keeps the whole band and shows
  // no face. Unreachable from either game today -- both hand over the full
  // content width -- but the alternative is returning a negative width, and a
  // Rect that cannot exist is worse than a face that is not drawn.
  if (band.width < size + toybox::kGutter * 2) return band;
  // Centred in the band's height rather than pinned to its top: the band is a
  // capsule's height and the face is smaller, and a face resting on the top
  // edge reads as having slipped.
  player::drawAvatar(screen.target(),
                     fui::makeRect(band.x, static_cast<int16_t>(band.y + (band.height - size) / 2), size, size),
                     theirName, player::AvatarSize::Row);
  const int16_t taken = static_cast<int16_t>(size + toybox::kGutter);
  return fui::makeRect(static_cast<int16_t>(band.x + taken), band.y, static_cast<int16_t>(band.width - taken),
                       band.height);
}

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

  // Footer first, so the seats can never grow into it. Each pill reserves a
  // gutter above itself, or the two fuse into one black slab and read as a
  // single control.
  //
  // WHICH pill gets the bottom band is not a style choice. y=732 (the panel
  // height less the margin and one pill) is where this fork puts a screen's
  // primary action, and it is where every link game's board puts the status
  // capsule that becomes PLAY AGAIN at game over -- chess (16,732,448,52),
  // battleship (76,732,388,52), sea salt, jaipur, toy battle. The link screen
  // REPLACES that board, unannounced, in the same pass that ends the game, so
  // whatever lands at 732 is what the thumb already on its way there will hit.
  // LEAVE used to be that control, which turned a rematch into killing the
  // radio. PLAY AGAIN takes the band and BACK sits above it.
  //
  // Above it is measured, not assumed: the 668 band is empty on every board
  // this screen replaces (chess, battleship, connect four, checkers, sea salt,
  // toy battle -- host-tests/ui asserts the ones it can build). The solo
  // Result screens that DO use 668 for PLAY AGAIN are never on the panel in a
  // match: driveLink() takes the pass before gameLoop() runs.
  fui::ButtonProps leave;
  leave.label = "BACK";
  leave.action = ActionLeaveLink;
  const fui::Rect bottomRow = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  if (model.offerPlayAgain) {
    fui::ButtonProps again;
    again.label = "PLAY AGAIN";
    again.action = ActionPlayAgain;
    screen.button(again, bottomRow);
    // Two solid pills stacked read as one black slab with a scratch across it,
    // which is the thing this file's own gap was meant to prevent and did not.
    // The gap separates them; the weight is what says which one is the answer
    // to the question above. BACK stays solid when it is the only control on
    // the screen, because then there is no hierarchy to express.
    leave.styles = toybox::rowStyles();
    screen.button(leave, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
  } else {
    // Alone, BACK keeps the bottom band: a single pill floating one row up
    // over an empty margin reads as a layout that lost something. Nothing is
    // at risk there, because the only screen that reaches this branch is the
    // search (replacing a menu) or an opponent who has already gone.
    screen.button(leave, bottomRow);
  }

  // The headline sits under the rule rather than floating: a block with equal
  // slack above and below reads as unresolved.
  fui::ButtonProps title;
  title.label = model.headline;
  title.action = fui::NO_ACTION;
  title.borderEdges = fui::EdgesNone;
  const fui::Rect headlineRow = screen.takeTop(toybox::kPillHeight, toybox::kGutter * 2);

  // The mark gets its own strip rather than being laid over the label. The
  // headline centres its text across whatever width it is given, so overlapping
  // put "LOOKING FOR A PLAYER" hard against the arcs with no air at all.
  const int16_t markStrip = static_cast<int16_t>(toybox::kIconSize + toybox::kGutter * 4);
  const fui::Rect headline = fui::makeRect(headlineRow.x, headlineRow.y,
                                           static_cast<int16_t>(headlineRow.width - markStrip), headlineRow.height);
  screen.button(title, headline);

  // The mark, inside the headline slab. This screen is the one every game
  // shares, so it is where the symbol is learned; the seats below say who, the
  // rail says whether, and this says what kind of thing is happening at all.
  //
  // Ink, not paper. The mark sits in the strip beside the headline slab, which
  // is page background, so it draws black. It was white while it was laid over
  // the slab, and moving it without rechecking what is behind it made it
  // invisible twice in a row -- once black on black, once white on white.
  // Nothing complains either way.
  const fui::Rect markRect =
      fui::makeRect(static_cast<int16_t>(headlineRow.right() - toybox::kIconSize),
                    static_cast<int16_t>(headlineRow.y + (headlineRow.height - toybox::kIconSize) / 2),
                    toybox::kIconSize, toybox::kIconSize);
  screen.target().bitmap(markRect, fui::bitmapFromIcon(nearbyMark()), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));

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

  const int16_t faceX = static_cast<int16_t>(rail.x + kRailWidth + kRailGap);
  const int16_t faceSize = player::avatarPixels(player::AvatarSize::Row);
  const int16_t textX = static_cast<int16_t>(faceX + faceSize + kRailGap);
  const int16_t textWidth = static_cast<int16_t>(card.right() - toybox::kFrame - textX);
  const int16_t yourY = static_cast<int16_t>(card.y + toybox::kFrame);
  const int16_t theirY = static_cast<int16_t>(card.bottom() - toybox::kFrame - rowHeight);

  fui::SettingRowProps you;
  you.label = model.yourName;
  you.value = seatValue(model.you, model.linked);
  you.action = fui::NO_ACTION;
  you.styles = fui::plainStyles(fui::Paint{});
  drawSeatFace(screen, fui::makeRect(faceX, yourY, faceSize, rowHeight), model.yourFaceName);
  fui::settingRow(screen.frame(), fui::makeRect(textX, yourY, textWidth, rowHeight), you);

  fui::SettingRowProps them;
  // A seat nobody is in yet shows the shape of the absence, not a blank.
  them.label = model.theirName != nullptr && model.theirName[0] != '\0' ? model.theirName : "- - - -";
  them.value = seatValue(model.them, model.linked);
  them.action = fui::NO_ACTION;
  them.styles = fui::plainStyles(fui::Paint{});
  drawSeatFace(screen, fui::makeRect(faceX, theirY, faceSize, rowHeight), model.theirName);
  fui::settingRow(screen.frame(), fui::makeRect(textX, theirY, textWidth, rowHeight), them);

  // What is left between the seats and the footer. Slack until a game puts
  // something in it.
  return screen.body();
}

}  // namespace linkui
