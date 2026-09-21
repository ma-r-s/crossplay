#pragma once

// Drawing a playing card. Shared by every game that deals from a standard deck.
//
// Lifted out of SolitaireScreens.cpp when Hearts became the second game to
// need a card face. The geometry is unchanged, because it was arrived at by
// looking at a panel rather than by reasoning -- see the note on the index in
// CardArt.cpp, which cost a shipped bug to learn.
//
// Freestanding in the same sense as the screen builders: it takes a
// toybox::Screen and draws, and it touches no renderer, no Activity and no
// storage, so host-tests/ui/ can run it against a fake target.

#include "../ui/ToyboxScreen.h"
#include "Cards.h"

namespace cardart {

namespace fui = freeink::ui;

// The deck's proportions. Every game that draws a full card uses these, so two
// card games on one device cannot look like two different decks.
constexpr int16_t kCardW = 92;
constexpr int16_t kCardH = 122;
constexpr int16_t kRadius = 8;
constexpr int16_t kEdge = 2;

// How a card is covered by the one in front of it, which decides where its
// index has to go. A fanned card only shows a strip, and the strip is a
// different shape in each direction.
enum class Fan : uint8_t {
  None = 0,  // the whole card is visible
  Down,      // covered from below: a short wide strip across the top
  Sideways,  // covered from the right: a tall narrow sliver down the left
};

// How a card reads. Dimmed is a dithered face rather than a white one, which is
// the only way to say "not now" on a panel with no grey: GfxRendererTarget
// draws every non-white colour as solid black, so colouring type grey silently
// does nothing at all.
enum class Ink : uint8_t {
  Normal = 0,
  Dimmed,
  Picked,  // lifted: a heavier frame, for a card the player has chosen
};

// The suit mark on its own, at whatever size the box is. `box` must be the
// artwork's own size (18 or 46): nothing here resamples.
void drawSuit(toybox::Screen& screen, const fui::Rect& box, cards::Suit suit, bool outline);

// A face-up card.
//
// `visible` is how much of the card's height is not covered by the card in
// front of it. A fanned card draws its whole body -- the one on top paints over
// it -- but only what fits in the visible strip is worth reading, so the centre
// pip appears only on a card you can see all of.
void drawCardFace(toybox::Screen& screen, const fui::Rect& rect, uint8_t card, int visible, Fan fan = Fan::None,
                  Ink ink = Ink::Normal);

// A face-down card. `visible` works as above: a card peeking out of a fan gets
// the frame and the lattice, and only a whole one gets the mark, because a mark
// sliced in half is worse than no mark.
// `mark` is the suit stamped on the back. It defaulted to spades and was
// hardcoded, so a game called Hearts drew a fan of spades on its own menu.
void drawCardBack(toybox::Screen& screen, const fui::Rect& rect, int visible, cards::Suit mark = cards::Suit::Spades);

// An empty place. A dashed outline rather than a hairline rectangle, which is
// pixel-identical to a blank card and reads as one.
void drawCardSlot(toybox::Screen& screen, const fui::Rect& rect);

}  // namespace cardart
