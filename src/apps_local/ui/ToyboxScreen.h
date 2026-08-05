#pragma once

// Toybox screens, the way the SDK intends them to be written.
//
// A screen is a free function taking a `toybox::Screen&` and a plain model
// struct. It touches no renderer, no Activity and no storage, which buys two
// things:
//
//   * `freeink::ui::Screen` substitutes theme tokens into every component it
//     builds (title style, side padding, row height, row gap, selection style).
//     Calling the components directly opts out of that, and the first two bugs
//     in this fork's adoption of FreeInkUI were exactly the tokens `Screen`
//     would have supplied: a header that defaulted to 6px of padding, and a
//     title drawn black on a black band.
//   * screens become host-testable. FreeInkUI is freestanding C++17, so a test
//     builds one against a fake draw target and asserts on what it drew and on
//     what it registered as tappable. See host-tests/ui/.
//
// That second point is why this header includes only the SDK and the tokens.
// Anything renderer-shaped belongs in ToyboxTheme.h, which screens must not
// include; host-tests/ui/run.sh compiles the builders with nothing else on the
// include path, so a stray dependency fails the build rather than quietly
// costing the tests.
//
// Activities keep what genuinely needs hardware: reading state, filling in the
// model, and drawing their own play surface into the body rect.

#include <FreeInkApp.h>
#include <FreeInkUI.h>
#include <FreeInkUIIcon.h>
#include <Icon.h>

#include "ToyboxTokens.h"

namespace toybox {

// One size for every screen in this fork, sized for the largest: the
// Connections board's sixteen tiles plus its three action buttons, with room to
// grow. This was 16 and it was not enough -- the board silently dropped all
// three buttons, and the only reason that took minutes rather than an afternoon
// is that the buffer records overflow and toybox::reportOverflow logs it. Raise
// this, do not trim a screen to fit it.
constexpr size_t kMaxInteractions = 24;

using Interactions = freeink::ui::InteractionBuffer<kMaxInteractions>;
using Frame = freeink::ui::Frame<kMaxInteractions>;
using Screen = freeink::ui::Screen<kMaxInteractions>;

// Every icon this fork draws, at the one size ToyboxIcons.h generates.
constexpr int16_t kIconSize = 32;

// An icon at the right edge of row `index` in a list band.
//
// The FreeInkUI list component only draws icons on the left, which indents
// every label and leaves the text starting at a different x from the header
// above it. Right-aligned, the icons share one axis and the labels stay flush.
//
// Rows are laid out by index, which is exact for a band that does not scroll --
// which is every list in this fork that carries icons. A scrolling one would
// need the list's own row rects.
//
// `selected` picks the ink: the selected row is filled black, so its icon has
// to be paper.
inline void iconAtRowRight(Screen& screen, const freeink::ui::Rect& band, const int index, const freeink::Icon& icon,
                           const bool selected) {
  namespace fui = freeink::ui;
  const int16_t rowHeight = screen.theme().rowHeight;
  const int16_t rowGap = screen.theme().listRowGap;
  const int16_t rowY = static_cast<int16_t>(band.y + index * (rowHeight + rowGap));
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(band.x + band.width - kIconSize - kGutter * 2),
                                        static_cast<int16_t>(rowY + (rowHeight - kIconSize) / 2), kIconSize, kIconSize);
  screen.target().bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain,
                         fui::Paint::solid(selected ? fui::Color::White : fui::Color::Black));
}

}  // namespace toybox
