#include "PlayerScreen.h"

#include "PlayerAvatar.h"

namespace playerui {
namespace {

// The doubled rule under a solid header. Local rather than shared because
// ChessScreens, ConnectionsScreens and LinkScreens each keep their own; a fifth
// copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// Corner brackets, the same shape the chess board and the Connections ornament
// wear, so three unrelated screens read as one device. Drawn through the
// DrawTarget rather than through the Toybox helper because that one takes a
// GfxRenderer and screens stay freestanding.
void drawBrackets(toybox::Screen& screen, const fui::Rect& box, const int arm) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int w = toybox::kFrame;
  for (int cx = 0; cx < 2; ++cx) {
    for (int cy = 0; cy < 2; ++cy) {
      const int x = cx == 0 ? box.x : box.right() - arm;
      const int y = cy == 0 ? box.y : box.bottom() - w;
      screen.target().fill(fui::makeRect(x, y, arm, w), ink);
      const int vx = cx == 0 ? box.x : box.right() - w;
      const int vy = cy == 0 ? box.y : box.bottom() - arm;
      screen.target().fill(fui::makeRect(vx, vy, w, arm), ink);
    }
  }
}

// Slot `index` of three across `band`. Derived by division from the band's own
// edges rather than by multiplying a rounded width, so the three add up to the
// band exactly and the last one does not fall a pixel short. Hit-testing uses
// this same rect, because the button registers the region it draws into.
fui::Rect slotRect(const fui::Rect& band, const int index) {
  const int gap = toybox::kGutter;
  const int16_t left = static_cast<int16_t>(band.x + (index * (band.width + gap)) / 3);
  const int16_t right = static_cast<int16_t>(band.x + ((index + 1) * (band.width + gap)) / 3 - gap);
  return fui::makeRect(left, band.y, static_cast<int16_t>(right - left), band.height);
}

}  // namespace

void buildPlayer(toybox::Screen& screen, const PlayerModel& model) {
  toyboxChrome(screen, "PLAYER");

  // Footer first, so nothing above can grow into it.
  fui::ButtonProps back;
  back.label = "BACK";
  back.action = ActionLeavePlayer;
  screen.button(back, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect body = screen.body();

  // The face, anchored under the rule rather than floating in the middle of the
  // page. What is left below the words is one deliberate zone above the footer,
  // which is how chess leaves room for its capture strips -- slack gathered in
  // one place reads as a page, slack scattered reads as a mistake.
  const fui::Rect face = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kFaceSize) / 2),
                                       static_cast<int16_t>(body.y + toybox::kGutter), kFaceSize, kFaceSize);
  player::drawAvatar(screen.target(), face, model.name, player::AvatarSize::Portrait);

  // Brackets rather than a box. A closed frame around a portrait reads as a
  // picture on a wall; brackets read as a thing being chosen, which is what
  // this is.
  const int16_t pad = toybox::kGutter;
  drawBrackets(screen,
               fui::makeRect(static_cast<int16_t>(face.x - pad), static_cast<int16_t>(face.y - pad),
                             static_cast<int16_t>(face.width + pad * 2), static_cast<int16_t>(face.height + pad * 2)),
               toybox::kGutter * 3);

  // The three words, and they are the name: read left to right, that is what
  // the other device will show. No separate line spelling it out, because two
  // copies of one string is two things that can look different.
  const fui::Rect words = fui::makeRect(body.x, static_cast<int16_t>(face.bottom() + pad + toybox::kGutter * 2),
                                        body.width, toybox::kRowHeight);
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    fui::ButtonProps word;
    word.label = model.words[slot];
    word.action = ActionStepSlot;
    // One action, three values. The handler rolls `event.value`, so a fourth
    // slot would need no new id and no new branch.
    word.value = static_cast<int16_t>(slot);
    screen.button(word, slotRect(words, slot));
  }

  // Said once, quietly, because nothing about three black capsules says they
  // are dice. It earns its line by being the only instruction in the fork.
  fui::TextStyle hint;
  // The dense cut, not the UI one. At UI size it had the same weight as the
  // three controls it was explaining and read as a fourth thing to press.
  hint.font = toybox::kSmallFont;
  hint.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(words.bottom() + toybox::kGutter), body.width, 28),
                       "TAP A WORD TO CHANGE IT", hint);
}

}  // namespace playerui
