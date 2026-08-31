#include "WavelengthScreens.h"

namespace wavelengthui {
namespace {

using wavelength::kSlots;

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  // Named even when it is the default: FONT_SLOT_SMALL is 0, and a style whose
  // font is 0 with every other field default reads as unset, so the component
  // silently substitutes the theme's size.
  style.font = font;
  style.align = align;
  style.color = colour;
  return style;
}

void caps(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
          const fui::TextAlign align, const toybox::CutMetrics& cut,
          const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cut), text, textStyle(font, align, colour));
}

void fill(toybox::Screen& screen, const fui::Rect& box, const fui::Color colour = fui::Color::Black) {
  screen.target().fill(box, fui::Paint::solid(colour));
}

void dither(toybox::Screen& screen, const fui::Rect& box, const fui::Color shade) {
  screen.target().fill(box, fui::Paint::dither(shade));
}

// An outline drawn as four fills. The renderer has no stroke, and an inset
// frame would eat the cells it surrounds.
void outline(toybox::Screen& screen, const fui::Rect& box, const int16_t weight) {
  fill(screen, fui::makeRect(box.x, box.y, box.width, weight));
  fill(screen, fui::makeRect(box.x, static_cast<int16_t>(box.y + box.height - weight), box.width, weight));
  fill(screen, fui::makeRect(box.x, box.y, weight, box.height));
  fill(screen, fui::makeRect(static_cast<int16_t>(box.x + box.width - weight), box.y, weight, box.height));
}

// Everything derived from the panel rather than from 480 and 800, so the
// Sticky's different geometry lays itself out instead of drawing off the edge.
struct Geometry {
  fui::Rect topWord;
  fui::Rect bottomWord;
  fui::Rect board;
  fui::Rect right;
  fui::Rect lock;
  int16_t slot;
};

Geometry layout(toybox::Screen& screen) {
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;
  const int16_t m = toybox::kMargin;
  const int16_t wordBox = toybox::kDisplayCut.lineHeight;

  Geometry g{};
  g.lock = fui::makeRect(m, static_cast<int16_t>(h - m - 62), static_cast<int16_t>(w - 2 * m), 62);
  g.topWord = fui::makeRect(m, m, static_cast<int16_t>(w - 2 * m), wordBox);
  g.bottomWord = fui::makeRect(m, static_cast<int16_t>(g.lock.y - 10 - wordBox), static_cast<int16_t>(w - 2 * m),
                               wordBox);

  const int16_t boardTop = static_cast<int16_t>(g.topWord.y + g.topWord.height + 18);
  const int16_t boardBottom = static_cast<int16_t>(g.bottomWord.y - 10);
  g.slot = static_cast<int16_t>((boardBottom - boardTop) / kSlots);

  // The strip takes a bit under a third of the width. Its numbers hang to the
  // left of it and must clear the content margin: at x=40 the widest of them
  // rendered from x=-2, off the panel and under the glass.
  const int16_t boardW = static_cast<int16_t>(w * 5 / 16);
  const int16_t boardX = static_cast<int16_t>(m + 48);
  g.board = fui::makeRect(boardX, boardTop, boardW, static_cast<int16_t>(g.slot * kSlots));

  const int16_t rightX = static_cast<int16_t>(boardX + boardW + 26);
  g.right = fui::makeRect(rightX, boardTop, static_cast<int16_t>(w - m - rightX), g.board.height);
  return g;
}

// Slot 1 sits at the bottom of the strip, slot kSlots at the top.
int16_t slotTop(const Geometry& g, const int slot) {
  return static_cast<int16_t>(g.board.y + (kSlots - slot) * g.slot);
}

void slotNumber(toybox::Screen& screen, const Geometry& g, const int slot, const int16_t rightEdge) {
  char buf[4];
  buf[0] = slot >= 10 ? static_cast<char>('0' + slot / 10) : ' ';
  buf[1] = static_cast<char>('0' + slot % 10);
  buf[2] = '\0';
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(rightEdge - 40), slotTop(g, slot), 40, g.slot);
  caps(screen, box, buf, toybox::kSmallFont, fui::TextAlign::Right, toybox::kTileCut);
}

void drawBoard(toybox::Screen& screen, const Geometry& g, const int guess) {
  const fui::Rect& b = g.board;

#if WAVELENGTH_VARIANT == 1
  // THE COLUMN. A contained ladder; the guess is a filled cell with its number
  // knocked out. The marker cell overhangs its slot so the number clears: a
  // slot is 27px and the button cut's ink is 18, which leaves nothing.
  outline(screen, b, toybox::kFrame);
  for (int i = 1; i < kSlots; ++i)
    fill(screen, fui::makeRect(static_cast<int16_t>(b.x + toybox::kFrame), slotTop(g, i),
                               static_cast<int16_t>(b.width - 2 * toybox::kFrame), toybox::kHairline));
  for (int k = 5; k <= kSlots; k += 5) slotNumber(screen, g, k, static_cast<int16_t>(b.x - 10));

  const int16_t mh = static_cast<int16_t>(g.slot + 10);
  const int16_t my = static_cast<int16_t>(slotTop(g, guess) + g.slot / 2 - mh / 2);
  const fui::Rect cell = fui::makeRect(static_cast<int16_t>(b.x + toybox::kFrame), my,
                                       static_cast<int16_t>(b.width - 2 * toybox::kFrame), mh);
  fill(screen, cell);
  char buf[4];
  buf[0] = guess >= 10 ? static_cast<char>('0' + guess / 10) : ' ';
  buf[1] = static_cast<char>('0' + guess % 10);
  buf[2] = '\0';
  caps(screen, cell, buf, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut, fui::Color::White);

#elif WAVELENGTH_VARIANT == 2
  // THE LADDER. No container at all, so nothing large repaints: the cheapest of
  // the three in ink, which is the fork's one rule about black. Every fifth
  // rung runs full width and carries its number.
  for (int j = 1; j <= kSlots; ++j) {
    const bool major = (j % 5) == 0;
    const int16_t y = static_cast<int16_t>(slotTop(g, j) + g.slot / 2 - toybox::kRule / 2);
    const int16_t inset = major ? 0 : static_cast<int16_t>(b.width / 3);
    fill(screen, fui::makeRect(static_cast<int16_t>(b.x + inset), y, static_cast<int16_t>(b.width - inset),
                               toybox::kRule));
    if (major) slotNumber(screen, g, j, static_cast<int16_t>(b.x - 20));
  }
  const int16_t my = static_cast<int16_t>(slotTop(g, guess) + g.slot / 2 - 5);
  fill(screen, fui::makeRect(static_cast<int16_t>(b.x - 14), my, static_cast<int16_t>(b.width + 28), 10));

#else
  // THE TROUGH. One outlined trough with a dithered fill rising to a solid
  // reading edge, so the guess reads as a quantity rather than as a point. It
  // inherits INSIDER's bar, the one board shape this fork has already put in
  // front of people, and it is the most expensive of the three: a large
  // dithered area repaints on every step.
  outline(screen, b, toybox::kFrame);
  const int16_t top = slotTop(g, guess);
  dither(screen,
         fui::makeRect(static_cast<int16_t>(b.x + toybox::kFrame), top,
                       static_cast<int16_t>(b.width - 2 * toybox::kFrame),
                       static_cast<int16_t>(b.y + b.height - top - toybox::kFrame)),
         fui::Color::DarkGray);
  fill(screen, fui::makeRect(static_cast<int16_t>(b.x + toybox::kFrame), top,
                             static_cast<int16_t>(b.width - 2 * toybox::kFrame), 6));
  for (int m = 5; m <= kSlots; m += 5) {
    fill(screen, fui::makeRect(static_cast<int16_t>(b.x - 16), static_cast<int16_t>(slotTop(g, m) + g.slot / 2 - 1),
                               9, toybox::kRule));
    slotNumber(screen, g, m, static_cast<int16_t>(b.x - 22));
  }
#endif
}

}  // namespace

void renderDial(toybox::Screen& screen, const DialModel& model) {
  toybox::absoluteChrome(screen);
  const Geometry g = layout(screen);

  // The ends frame the board rather than sitting in a header band: they are the
  // one thing a loud table has to be able to re-read, and a header would cost
  // the strip a fifth of its height. Deck words are drawn at the display cut,
  // never larger. Measured against the shipped 240-pair deck, 119 of the 478
  // end words are wider than the panel at the large cut, and it reads as
  // perfectly fine with any short example word.
  caps(screen, g.topWord, model.topWord, toybox::kBodyFont, fui::TextAlign::Left, toybox::kDisplayCut);
  fill(screen, fui::makeRect(g.topWord.x, static_cast<int16_t>(g.topWord.y + g.topWord.height + 4), g.topWord.width,
                             toybox::kRule));
  caps(screen, g.bottomWord, model.bottomWord, toybox::kBodyFont, fui::TextAlign::Left, toybox::kDisplayCut);

  drawBoard(screen, g, model.guess);

  // The number once, large enough to be read across a table. The tick numbers
  // beside the strip are about 4mm on the panel and are for the person holding
  // it; this is the one the table argues in.
  char buf[4];
  buf[0] = model.guess >= 10 ? static_cast<char>('0' + model.guess / 10) : ' ';
  buf[1] = static_cast<char>('0' + model.guess % 10);
  buf[2] = '\0';
  const fui::Rect numeral = fui::makeRect(g.right.x, g.right.y, g.right.width, toybox::kHugeCut.lineHeight);
  caps(screen, numeral, buf, toybox::kDisplayFont, fui::TextAlign::Center, toybox::kHugeCut);

  const fui::Rect label = fui::makeRect(g.right.x, static_cast<int16_t>(numeral.y + numeral.height + 6), g.right.width,
                                        toybox::kButtonCut.lineHeight);
  caps(screen, label, "YOUR GUESS", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  fill(screen, fui::makeRect(static_cast<int16_t>(g.right.x + 20), static_cast<int16_t>(label.y + label.height + 14),
                             static_cast<int16_t>(g.right.width - 40), toybox::kRule));

  const int16_t hintY = static_cast<int16_t>(label.y + label.height + 30);
  caps(screen, fui::makeRect(g.right.x, hintY, g.right.width, toybox::kTileCut.lineHeight), "TAP ABOVE",
       toybox::kSmallFont, fui::TextAlign::Center, toybox::kTileCut);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + toybox::kTileCut.lineHeight), g.right.width, toybox::kTileCut.lineHeight),
       "OR BELOW", toybox::kSmallFont, fui::TextAlign::Center, toybox::kTileCut);

  // Two hit regions, not twenty. Geometry resolves which half was tapped, the
  // same shape as murdleui::cellAt: a slot per interaction would spend twenty of
  // the screen's twenty-four and leave the rest drawing but dead.
  const int16_t markerY = slotTop(g, model.guess);
  screen.frame().hit(fui::makeRect(g.board.x, g.board.y, g.board.width, static_cast<int16_t>(markerY - g.board.y)),
                     ActionStepTowardTop);
  screen.frame().hit(fui::makeRect(g.board.x, static_cast<int16_t>(markerY + g.slot), g.board.width,
                                   static_cast<int16_t>(g.board.y + g.board.height - markerY - g.slot)),
                     ActionStepTowardBottom);

  fui::ButtonProps lock;
  lock.label = "HOLD TO LOCK";
  lock.text = toybox::buttonText(screen.theme());
  lock.action = ActionLock;
  screen.button(lock, g.lock);
}

}  // namespace wavelengthui
