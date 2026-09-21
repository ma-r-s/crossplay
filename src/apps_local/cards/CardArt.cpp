#include "CardArt.h"

#include "CardSuits.h"

namespace cardart {
namespace {

namespace c = cards;

const freeink::Icon& suitArt(const c::Suit suit, const int size, const bool outline) {
  const bool big = size >= 32;
  switch (suit) {
    case c::Suit::Spades:
      if (big) return outline ? icon_spadeOutline_46 : icon_spadeSolid_46;
      return outline ? icon_spadeOutline_18 : icon_spadeSolid_18;
    case c::Suit::Hearts:
      if (big) return outline ? icon_heartOutline_46 : icon_heartSolid_46;
      return outline ? icon_heartOutline_18 : icon_heartSolid_18;
    case c::Suit::Diamonds:
      if (big) return outline ? icon_diamondOutline_46 : icon_diamondSolid_46;
      return outline ? icon_diamondOutline_18 : icon_diamondSolid_18;
    case c::Suit::Clubs:
    default:
      if (big) return outline ? icon_clubOutline_46 : icon_clubSolid_46;
      return outline ? icon_clubOutline_18 : icon_clubSolid_18;
  }
}

void drawPip(toybox::Screen& screen, const fui::Rect& box, const uint8_t card) {
  drawSuit(screen, box, c::suitOf(card), c::isRed(card));
}

}  // namespace

void drawSuit(toybox::Screen& screen, const fui::Rect& box, const c::Suit suit, const bool outline) {
  const freeink::Icon& art = suitArt(suit, box.width, outline);
  fui::BitmapRef ref;
  ref.data = art.bits;
  ref.width = art.w;
  ref.height = art.h;
  ref.format = fui::BitmapFormat::Mask1;
  ref.progmem = false;
  // Center, never Stretch: the box is the artwork's own size and Center is the
  // one mode that cannot resample.
  screen.target().bitmap(box, ref, fui::BitmapMode::Center, fui::Paint::solid(fui::Color::Black));
}

// THE INDEX IS ONE LINE, RANK THEN PIP, AND THAT IS THE WHOLE POINT.
//
// It used to be stacked: rank at row 2, pip at row 42. A covered card only
// shows its top strip, and that strip is 30px at its most generous, compressing
// to 16 and to single digits in a deep column. 42 is past 30 in every pile
// shape that can occur, so no overlapped face-up card had EVER shown its suit.
// In a game whose rules turn on suit, that is the one thing a covered card
// needs to say.
//
// The two fans cover a card from different directions, so the index has to go
// somewhere different in each. Down leaves a short wide strip (full width, a
// few px tall); Sideways leaves a tall narrow sliver (full height, ~36px wide).
// Rank-then-pip fits the strip and falls off the sliver; rank-over-pip fits the
// sliver and falls off the strip. There is no single placement that survives
// both -- 36px wide and 16px tall do not overlap in any useful way.
void drawCardFace(toybox::Screen& screen, const fui::Rect& rect, const uint8_t card, const int visible, const Fan fan,
                  const Ink ink) {
  // One inset for both halves of the index, so the rank and the pip cannot
  // drift apart. It is the corner radius: where the curve stops.
  constexpr int16_t kIndexInset = kRadius;
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);

  // A dimmed card is a DITHERED face, not grey type. There is no grey type on
  // this panel: GfxRendererTarget::text() decides ink with
  // `style.color != Color::White`, so a DarkGray label draws solid black and
  // the dimming silently does nothing.
  //
  // LIGHT grey rather than dark, for two reasons that turn out to be the same
  // one. It reads better -- the rank and the pip stay legible through it, so a
  // card you may not play is still a card you can identify -- and it is the
  // largest CHANGING area on the panel: up to eleven dimmed cards across
  // 768x122, re-dithered every time the led suit changes. Ghosting is a residue
  // of change, so halving the ink in the region that changes most is the
  // cheapest reduction available.
  target.fill(rect,
              ink == Ink::Dimmed ? fui::Paint::dither(fui::Color::LightGray) : fui::Paint::solid(fui::Color::White),
              kRadius);
  target.stroke(rect, black, ink == Ink::Picked ? kEdge * 2 : kEdge, kRadius);

  fui::TextStyle rankStyle;
  rankStyle.font = toybox::kUiFont;
  rankStyle.align = fui::TextAlign::Left;
  // THE INDEX SITS kIndexInset FROM THE EDGES IT TOUCHES, rank and pip alike.
  //
  // The rank's box was at rect.y + 2, which inkCentred turns into ink starting
  // three pixels below the card's edge -- one pixel clear of its own 2px
  // border. Mario read it as the rank crowding the top of the card, and
  // measuring it agreed: 3px against the pip's 8 on the opposite corner.
  //
  // Both corners now take the same inset, so they cannot drift apart again,
  // and it is the radius: the index starts where the rounded corner stops
  // curving.
  target.text(toybox::inkCentred(fui::makeRect(rect.x + 9, rect.y + kIndexInset, 40, 28), toybox::kUiCut),
              c::rankLabel(c::rankOf(card)), rankStyle);

  if (fan == Fan::Sideways) {
    // Under the rank, both inside the left sliver, and moved down with it.
    drawPip(screen, fui::makeRect(rect.x + 10, rect.y + kIndexInset + 40, 16, 18), card);
  } else {
    // The opposite corner from the rank, the same inset from both edges it
    // touches.
    constexpr int16_t kPip = 18;
    drawPip(screen,
            fui::makeRect(static_cast<int16_t>(rect.x + rect.width - kIndexInset - kPip),
                          static_cast<int16_t>(rect.y + kIndexInset), kPip, kPip),
            card);
  }

  // The big centre pip only exists on a card you can see all of, and it is
  // placed FROM THE BOTTOM EDGE rather than at a fixed offset from the top.
  //
  // `rect.y + 64` plus 48 of artwork needs 112px of card. The guard said 96, so
  // every 96px trick card drew its pip 16px out through its own bottom -- and
  // the south card's went on through the table panel's border. The guard had
  // already been tightened once for this exact defect at 64x86 and the number
  // was wrong that time too, which is what a magic offset does: it has to be
  // re-guessed for every size anybody uses.
  //
  // Measured from the bottom it cannot escape, and it lands in the identical
  // place on the 122px card Klondike draws: 122 - 48 - 10 = 64.
  if (visible < rect.height) return;
  constexpr int16_t kPipH = 48;
  constexpr int16_t kPipBottomInset = 10;
  if (rect.width < 72 || rect.height < kPipH + kPipBottomInset + 32) return;
  drawPip(screen,
          fui::makeRect(static_cast<int16_t>(rect.x + rect.width / 2 - 20),
                        static_cast<int16_t>(rect.bottom() - kPipBottomInset - kPipH), 46, kPipH),
          card);
}

void drawCardBack(toybox::Screen& screen, const fui::Rect& rect, const int visible, const c::Suit markSuit) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);
  target.fill(rect, fui::Paint::solid(fui::Color::White), kRadius);
  target.stroke(rect, black, kEdge, kRadius);

  const fui::Rect inner = fui::makeRect(rect.x + 7, rect.y + 7, rect.width - 14, rect.height - 14);
  target.fill(inner, fui::Paint::dither(fui::Color::DarkGray), kRadius / 2);
  target.stroke(inner, black, kEdge, kRadius / 2);
  if (visible < rect.height) return;

  // The mark comes in two sizes and NOTHING RESAMPLES, so a card too small for
  // the 46px art gets the 18px art rather than a squashed one. A menu drawing
  // 48x64 cards with a 46px mark on them is a spade with a card behind it.
  const int mark = (rect.width >= 72 && rect.height >= 96) ? 46 : 18;
  // 5 at the full size is Klondike's own number, kept exactly so migrating it
  // onto this file changes no pixel of a game that already shipped.
  const int pad = mark == 46 ? 5 : 4;
  const fui::Rect halo = fui::makeRect(rect.x + (rect.width - mark) / 2 - pad, rect.y + (rect.height - mark) / 2 - pad,
                                       mark + pad * 2, mark + pad * 2);
  target.fill(halo, fui::Paint::solid(fui::Color::White), 6);
  drawSuit(screen, fui::makeRect(halo.x + pad, halo.y + pad, mark, mark), markSuit, false);
}

void drawCardSlot(toybox::Screen& screen, const fui::Rect& rect) {
  auto& target = screen.target();
  const fui::Paint black = fui::Paint::solid(fui::Color::Black);
  const int dash = 10;
  for (int x = rect.x + kRadius; x < rect.right() - kRadius; x += dash * 2) {
    const int run = (x + dash > rect.right() - kRadius) ? rect.right() - kRadius - x : dash;
    target.fill(fui::makeRect(x, rect.y, run, kEdge), black);
    target.fill(fui::makeRect(x, rect.bottom() - kEdge, run, kEdge), black);
  }
  for (int y = rect.y + kRadius; y < rect.bottom() - kRadius; y += dash * 2) {
    const int run = (y + dash > rect.bottom() - kRadius) ? rect.bottom() - kRadius - y : dash;
    target.fill(fui::makeRect(rect.x, y, kEdge, run), black);
    target.fill(fui::makeRect(rect.right() - kEdge, y, kEdge, run), black);
  }
}

}  // namespace cardart
