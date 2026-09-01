#include "WavelengthScreens.h"

#include <cstdio>

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

// A SLOT IS NOT A CUT, and passing a mismatched pair is silent. The slot
// decides how big the ink actually is; the CutMetrics only decide where it is
// centred. This app binds the button cut to SMALL, the display cut to BODY and
// the huge cut to TITLE, so the metrics are derived here rather than passed:
// handing kUiCut alongside the body slot centred 38px of ink as though it were
// 25 and ran one line straight through the next.
const toybox::CutMetrics& cutFor(const fui::FontId slot) {
  if (slot == toybox::kDisplayFont) return toybox::kHugeCut;
  if (slot == toybox::kBodyFont) return toybox::kDisplayCut;
  return toybox::kButtonCut;
}

void caps(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
          const fui::TextAlign align, const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cutFor(font)), text, textStyle(font, align, colour));
}

// An end word at the largest cut it fits in, stepping down rather than being
// truncated. The retail deck runs from HOT to UNDERRATED LETTER OF THE ALPHABET,
// and 54 of its 252 pairs are too wide for the display cut, so this is the
// difference between a legible deck and a fifth of it silently ending mid-word.
// The design language's rule: walk the cuts down, never break a word.
void endWord(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align) {
  fui::TextStyle big = textStyle(toybox::kBodyFont, align);
  const int16_t measured = screen.target().measureText(toybox::kBodyFont, text, big).width;
  const fui::FontId slot = measured <= box.width ? toybox::kBodyFont : toybox::kSmallFont;
  caps(screen, box, text, slot, align);
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

Geometry layout(toybox::Screen& screen, const int16_t boardTopOverride = 0) {
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;
  const int16_t m = toybox::kMargin;
  const int16_t wordBox = toybox::kDisplayCut.lineHeight;

  Geometry g{};
  g.lock = fui::makeRect(m, static_cast<int16_t>(h - m - 62), static_cast<int16_t>(w - 2 * m), 62);
  g.topWord = fui::makeRect(m, m, static_cast<int16_t>(w - 2 * m), wordBox);
  g.bottomWord =
      fui::makeRect(m, static_cast<int16_t>(g.lock.y - 10 - wordBox), static_cast<int16_t>(w - 2 * m), wordBox);

  const int16_t boardTop =
      boardTopOverride ? boardTopOverride : static_cast<int16_t>(g.topWord.y + g.topWord.height + 18);
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
  caps(screen, box, buf, toybox::kSmallFont, fui::TextAlign::Right);
}

// The scoring band, five slots wide and clipped at the ends. Bracketed rather
// than filled: the same corner marks the chess board wears, so the band can be
// seen without covering the cells inside it. No number goes in the target cell;
// a slot gives 13 to 19px of usable height and the smallest cut's ink is 13, so
// it would sit on its own border. The number is large in the column beside it.
void drawBand(toybox::Screen& screen, const Geometry& g, const int target) {
  const int lo = target - wavelength::kBandRadius < 1 ? 1 : target - wavelength::kBandRadius;
  const int hi = target + wavelength::kBandRadius > kSlots ? kSlots : target + wavelength::kBandRadius;
  const int16_t top = static_cast<int16_t>(slotTop(g, hi) - 5);
  const int16_t height = static_cast<int16_t>((hi - lo + 1) * g.slot + 10);
  toybox::bracket(
      screen,
      fui::makeRect(static_cast<int16_t>(g.board.x - 18), top, static_cast<int16_t>(g.board.width + 36), height), 14,
      toybox::kFrame);
  fill(screen, fui::makeRect(static_cast<int16_t>(g.board.x + 6), static_cast<int16_t>(slotTop(g, target) + 2),
                             static_cast<int16_t>(g.board.width - 12), static_cast<int16_t>(g.slot - 4)));
}

// The guess as a hollow heavy frame, so it can never be confused with the solid
// target cell sitting inside the same band.
void drawGuessFrame(toybox::Screen& screen, const Geometry& g, const int guess) {
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(g.board.x - 10), static_cast<int16_t>(slotTop(g, guess) - 3),
                                      static_cast<int16_t>(g.board.width + 20), static_cast<int16_t>(g.slot + 6));
  // A paper gap is knocked out around the frame FIRST. When the guess lands next
  // to the target, the hollow frame and the solid slab touch, and at this size
  // two adjacent marks merge into one blob: you cannot see there are two markers
  // at all, which is the single thing this screen exists to show. Three pixels
  // of paper guarantee they read as separate objects.
  screen.target().fill(
      fui::makeRect(box.x, static_cast<int16_t>(box.y - 3), box.width, static_cast<int16_t>(box.height + 6)),
      fui::Paint::solid(fui::Color::White));
  outline(screen, box, toybox::kFrame);
}

// The scale. Twenty rungs with no container: every fifth runs the full width
// and carries its number, the rest are inset. Nothing large repaints, which is
// the fork's one rule about black, and it is the only arrangement of the three
// with no filled area to ghost.
//
// Split from the marker so a screen with no guess yet simply does not ask for
// one. Passing a sentinel slot instead drew a phantom marker at the foot of the
// strip, because the marker used to be drawn unconditionally.
void drawScale(toybox::Screen& screen, const Geometry& g) {
  const fui::Rect& b = g.board;
  for (int j = 1; j <= kSlots; ++j) {
    const bool major = (j % 5) == 0;
    const int16_t y = static_cast<int16_t>(slotTop(g, j) + g.slot / 2 - toybox::kRule / 2);
    const int16_t inset = major ? 0 : static_cast<int16_t>(b.width / 3);
    fill(screen,
         fui::makeRect(static_cast<int16_t>(b.x + inset), y, static_cast<int16_t>(b.width - inset), toybox::kRule));
    if (major) slotNumber(screen, g, j, static_cast<int16_t>(b.x - 20));
  }
}
// The guess: a fader bar overhanging both edges of the strip, so it cannot be
// mistaken for a rung and can be seen from across a table where the 4mm tick
// numerals cannot.
void drawMarker(toybox::Screen& screen, const Geometry& g, const int guess) {
  if (guess < 1 || guess > kSlots) return;
  const int16_t y = static_cast<int16_t>(slotTop(g, guess) + g.slot / 2 - 5);
  fill(screen, fui::makeRect(static_cast<int16_t>(g.board.x - 14), y, static_cast<int16_t>(g.board.width + 28), 10));
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
  endWord(screen, g.topWord, model.spectrum.top, fui::TextAlign::Left);
  fill(screen, fui::makeRect(g.topWord.x, static_cast<int16_t>(g.topWord.y + g.topWord.height + 4), g.topWord.width,
                             toybox::kRule));
  endWord(screen, g.bottomWord, model.spectrum.bottom, fui::TextAlign::Left);

  drawScale(screen, g);
  drawMarker(screen, g, model.guess);

  // The number once, large enough to be read across a table. The tick numbers
  // beside the strip are about 4mm on the panel and are for the person holding
  // it; this is the one the table argues in.
  char buf[4];
  buf[0] = model.guess >= 10 ? static_cast<char>('0' + model.guess / 10) : ' ';
  buf[1] = static_cast<char>('0' + model.guess % 10);
  buf[2] = '\0';
  const fui::Rect numeral = fui::makeRect(g.right.x, g.right.y, g.right.width, toybox::kHugeCut.lineHeight);
  caps(screen, numeral, buf, toybox::kDisplayFont, fui::TextAlign::Center);

  const fui::Rect label = fui::makeRect(g.right.x, static_cast<int16_t>(numeral.y + numeral.height + 6), g.right.width,
                                        toybox::kButtonCut.lineHeight);
  caps(screen, label, "YOUR GUESS", toybox::kSmallFont, fui::TextAlign::Center);
  fill(screen, fui::makeRect(static_cast<int16_t>(g.right.x + 20), static_cast<int16_t>(label.y + label.height + 14),
                             static_cast<int16_t>(g.right.width - 40), toybox::kRule));

  const int16_t hintY = static_cast<int16_t>(label.y + label.height + 30);
  caps(screen, fui::makeRect(g.right.x, hintY, g.right.width, toybox::kButtonCut.lineHeight), "TAP ABOVE",
       toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + toybox::kButtonCut.lineHeight), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "OR BELOW", toybox::kSmallFont, fui::TextAlign::Center);

  // Two hit regions, not twenty. Geometry resolves which half was tapped, the
  // same shape as murdleui::cellAt: a slot per interaction would spend twenty of
  // the screen's twenty-four and leave the rest drawing but dead.
  const int16_t markerY = slotTop(g, model.guess);
  const int16_t zoneX = toybox::kMargin;
  const int16_t zoneW = static_cast<int16_t>(screen.device().screen().width - 2 * toybox::kMargin);
  screen.frame().hit(fui::makeRect(zoneX, g.board.y, zoneW, static_cast<int16_t>(markerY - g.board.y)),
                     ActionStepTowardTop);
  screen.frame().hit(fui::makeRect(zoneX, static_cast<int16_t>(markerY + g.slot), zoneW,
                                   static_cast<int16_t>(g.board.y + g.board.height - markerY - g.slot)),
                     ActionStepTowardBottom);

  // Deliberately NOT given an action. A tap must not commit: the activity
  // watches for a sustained hold on this exact rect instead, so the label is
  // true and a stray touch on a device lying flat cannot end the round.
  fui::ButtonProps lock;
  lock.label = "HOLD TO LOCK";
  lock.text = toybox::buttonText(screen.theme());
  screen.button(lock, lockBarRect(screen.device().screen().width, screen.device().screen().height));
}

namespace {

// A full-width action at the foot of the panel, where a thumb rests.
fui::Rect footer(toybox::Screen& screen, const int16_t height, const int16_t fromBottom) {
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(h - fromBottom - height),
                       static_cast<int16_t>(w - 2 * toybox::kMargin), height);
}

void action(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId id) {
  fui::ButtonProps props;
  props.label = label;
  props.text = toybox::buttonText(screen.theme());
  props.action = id;
  screen.button(props, box);
}

// The two ends stacked with a dotted spine between them, which is the shape of
// the strip without being the strip: this screen must not show a position.
void endsStacked(toybox::Screen& screen, const Spectrum& spectrum, const int16_t top, const int16_t bottom) {
  const int16_t w = screen.device().screen().width;
  const fui::Rect band = fui::makeRect(toybox::kMargin, top, static_cast<int16_t>(w - 2 * toybox::kMargin),
                                       toybox::kDisplayCut.lineHeight);
  endWord(screen, band, spectrum.top, fui::TextAlign::Center);
  const int16_t spineTop = static_cast<int16_t>(top + toybox::kDisplayCut.lineHeight + 16);
  const int16_t spineBottom = static_cast<int16_t>(bottom - 16);
  for (int16_t y = spineTop; y + 10 < spineBottom; y = static_cast<int16_t>(y + 22))
    fill(screen, fui::makeRect(static_cast<int16_t>(w / 2 - 4), y, 8, 10));
  endWord(screen,
          fui::makeRect(toybox::kMargin, bottom, static_cast<int16_t>(w - 2 * toybox::kMargin),
                        toybox::kDisplayCut.lineHeight),
          spectrum.bottom, fui::TextAlign::Center);
}

}  // namespace

void renderPassLeft(toybox::Screen& screen, const PassModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  // Centred on the whole panel, not on a body below a header: this screen has
  // one message and anything else reads as having slipped.
  caps(screen, fui::makeRect(toybox::kMargin, 200, inner, toybox::kHugeCut.lineHeight), "PASS", toybox::kDisplayFont,
       fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 320, inner, toybox::kHugeCut.lineHeight), "LEFT", toybox::kDisplayFont,
       fui::TextAlign::Center);
  fill(screen, fui::makeRect(90, 470, static_cast<int16_t>(w - 180), toybox::kRule));
  caps(screen, fui::makeRect(toybox::kMargin, 490, inner, toybox::kDisplayCut.lineHeight), "EVERYONE ELSE,",
       toybox::kBodyFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 530, inner, toybox::kDisplayCut.lineHeight), "LOOK AWAY.",
       toybox::kBodyFont, fui::TextAlign::Center);

  char line[40];
  if (model.practice) {
    snprintf(line, sizeof(line), "PRACTICE ROUND");
  } else {
    snprintf(line, sizeof(line), "ROUND %d   %d POINTS", model.roundNumber, model.total);
  }
  caps(screen, fui::makeRect(toybox::kMargin, 620, inner, toybox::kButtonCut.lineHeight), line, toybox::kSmallFont,
       fui::TextAlign::Center);

  action(screen, footer(screen, 62, toybox::kMargin), "I HAVE IT", ActionReady);
}

void renderPick(toybox::Screen& screen, const PickModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const int16_t pad = 22;

  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), "PICK ONE", toybox::kBodyFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 84, inner, toybox::kButtonCut.lineHeight), "THE TARGET IS NOT DRAWN YET.",
       toybox::kSmallFont, fui::TextAlign::Left);

  const int16_t cardH = 250;
  for (int i = 0; i < (model.onlyOne ? 1 : 2); ++i) {
    const Spectrum& s = i == 0 ? model.first : model.second;
    const fui::Rect card = fui::makeRect(toybox::kMargin, static_cast<int16_t>(120 + i * (cardH + 24)), inner, cardH);
    outline(screen, card, toybox::kFrame);
    endWord(screen,
            fui::makeRect(static_cast<int16_t>(card.x + pad), static_cast<int16_t>(card.y + pad),
                          static_cast<int16_t>(card.width - 2 * pad), toybox::kDisplayCut.lineHeight),
            s.top, fui::TextAlign::Left);
    for (int d = 0; d < 7; ++d)
      fill(screen, fui::makeRect(static_cast<int16_t>(card.x + pad + d * 26),
                                 static_cast<int16_t>(card.y + card.height / 2 - 2), 15, 4));
    endWord(screen,
            fui::makeRect(static_cast<int16_t>(card.x + pad),
                          static_cast<int16_t>(card.y + card.height - pad - toybox::kDisplayCut.lineHeight),
                          static_cast<int16_t>(card.width - 2 * pad), toybox::kDisplayCut.lineHeight),
            s.bottom, fui::TextAlign::Left);
    screen.frame().hit(card, i == 0 ? ActionPickFirst : ActionPickSecond);
  }

  const char* hint = model.onlyOne ? "ONE LEFT IN THE DECK" : "TAP ONE TO CHOOSE IT";
  caps(screen, fui::makeRect(toybox::kMargin, 700, inner, toybox::kButtonCut.lineHeight), hint, toybox::kSmallFont,
       fui::TextAlign::Center);
}

fui::Rect lockBarRect(const int16_t screenW, const int16_t screenH) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(screenH - toybox::kMargin - 62),
                       static_cast<int16_t>(screenW - 2 * toybox::kMargin), 62);
}

fui::Rect peekPadRect(const int16_t screenW, const int16_t screenH) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(screenH - 84 - 58),
                       static_cast<int16_t>(screenW - 2 * toybox::kMargin), 58);
}

void renderPeek(toybox::Screen& screen, const PeekModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  // The peek carries two stacked buttons where every other board screen carries
  // one, so it computes its own shorter strip. Both poles get the FULL panel
  // width: a deck word can be 33 characters, which fits 448px at the small cut
  // and does not come close to fitting the 224px side column.
  const fui::Rect padRect = peekPadRect(w, h);
  const int16_t poleH = toybox::kDisplayCut.lineHeight;
  const int16_t bottomPoleY = static_cast<int16_t>(padRect.y - 8 - poleH);

  Geometry g = layout(screen, 96);
  g.slot = static_cast<int16_t>((bottomPoleY - 10 - g.board.y) / kSlots);
  g.board.height = static_cast<int16_t>(g.slot * kSlots);
  g.right = fui::makeRect(g.right.x, g.board.y, g.right.width, g.board.height);

  endWord(screen, fui::makeRect(toybox::kMargin, 14, inner, poleH), model.spectrum.top, fui::TextAlign::Left);
  fill(screen, fui::makeRect(toybox::kMargin, 82, inner, toybox::kRule));
  endWord(screen, fui::makeRect(toybox::kMargin, bottomPoleY, inner, poleH), model.spectrum.bottom,
          fui::TextAlign::Left);

  drawScale(screen, g);
  if (model.revealed) {
    drawBand(screen, g, model.target);
    char buf[4];
    buf[0] = model.target >= 10 ? static_cast<char>('0' + model.target / 10) : ' ';
    buf[1] = static_cast<char>('0' + model.target % 10);
    buf[2] = '\0';
    caps(screen, fui::makeRect(g.right.x, g.right.y, g.right.width, toybox::kHugeCut.lineHeight), buf,
         toybox::kDisplayFont, fui::TextAlign::Center);
  }

  const int16_t textTop = static_cast<int16_t>(g.right.y + toybox::kHugeCut.lineHeight + 10);
  static const char* kLines[] = {"SAY ONE THING", "THAT SITS", "EXACTLY THERE", "", "ONLY ONE.", "DO NOT ADD TO IT."};
  for (int i = 0; i < 6; ++i) {
    if (!kLines[i][0]) continue;
    caps(screen,
         fui::makeRect(g.right.x, static_cast<int16_t>(textTop + i * toybox::kButtonCut.lineHeight), g.right.width,
                       toybox::kButtonCut.lineHeight),
         kLines[i], toybox::kSmallFont, fui::TextAlign::Center);
  }

  fui::ButtonProps hold;
  hold.label = model.revealed ? "HOLDING" : "HOLD TO SEE";
  hold.text = toybox::buttonText(screen.theme());
  hold.action = ActionPeekPad;
  screen.button(hold, padRect);
  action(screen, footer(screen, 58, toybox::kMargin), "I HAVE MY CLUE", ActionClueGiven);
}

void renderClue(toybox::Screen& screen, const ClueModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), "SAY IT OUT LOUD",
       toybox::kBodyFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 84, inner, toybox::kButtonCut.lineHeight), "TARGET HIDDEN",
       toybox::kSmallFont, fui::TextAlign::Center);
  fill(screen, fui::makeRect(60, 108, static_cast<int16_t>(w - 120), toybox::kRule));

  endsStacked(screen, model.spectrum, 150, 470);

  fill(screen, fui::makeRect(60, 580, static_cast<int16_t>(w - 120), toybox::kRule));
  caps(screen, fui::makeRect(toybox::kMargin, 598, inner, toybox::kButtonCut.lineHeight),
       "ONE CLUE. AS LONG AS YOU LIKE.", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 634, inner, toybox::kButtonCut.lineHeight),
       "REPEAT IT. DO NOT ADD TO IT.", toybox::kSmallFont, fui::TextAlign::Center);

  action(screen, footer(screen, 62, toybox::kMargin), "PUT IT ON THE TABLE", ActionClueGiven);
}

void renderCall(toybox::Screen& screen, const CallModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  char locked[24];
  snprintf(locked, sizeof(locked), "YOU LOCKED %d", model.guess);
  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), locked, toybox::kBodyFont,
       fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 84, inner, toybox::kButtonCut.lineHeight), "IS THE TARGET NEARER",
       toybox::kSmallFont, fui::TextAlign::Center);

  // The buttons carry this round's own end words and sit in the strip's own
  // order. Not higher and lower: those are the most seat-dependent words
  // available, and the device is lying flat between people.
  action(screen, fui::makeRect(toybox::kMargin, 140, inner, 200), model.spectrum.top, ActionCallTop);
  // The locked number sits BETWEEN the two ends, in the strip's own order, so
  // the screen is a picture of the question rather than two buttons and a
  // floating caption pointing at nothing.
  char lockedNum[8];
  snprintf(lockedNum, sizeof(lockedNum), "%d", model.guess);
  caps(screen, fui::makeRect(toybox::kMargin, 360, inner, toybox::kDisplayCut.lineHeight), lockedNum, toybox::kBodyFont,
       fui::TextAlign::Center);
  action(screen, fui::makeRect(toybox::kMargin, 450, inner, 200), model.spectrum.bottom, ActionCallBottom);

  caps(screen, fui::makeRect(toybox::kMargin, 668, inner, toybox::kButtonCut.lineHeight),
       "WHICH SIDE IS THE TARGET ON?", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 704, inner, toybox::kButtonCut.lineHeight),
       "RIGHT ANSWER IS WORTH ONE POINT.", toybox::kSmallFont, fui::TextAlign::Center);
}

void renderReveal(toybox::Screen& screen, const RevealModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const Geometry g = layout(screen, 150);

  // The verdict is the one place the design language says to spend solid black:
  // it repaints once, on a full refresh, as the payoff. Set in the display cut
  // rather than the large one because TELEPATHIC measures 456px there against
  // 448px of content width, so the best outcome in the game would overflow its
  // own capsule while every lesser one fitted.
  const char* verdict = model.practice      ? "PRACTICE"
                        : model.points >= 5 ? "TELEPATHIC"
                        : model.points >= 3 ? "CLOSE"
                        : model.points >= 1 ? "WARM"
                                            : "MISS";
  const fui::Rect capsule = fui::makeRect(toybox::kMargin, 16, inner, 76);
  fill(screen, capsule);
  caps(screen, capsule, verdict, toybox::kBodyFont, fui::TextAlign::Center, fui::Color::White);

  caps(screen, fui::makeRect(g.topWord.x, 102, g.topWord.width, toybox::kButtonCut.lineHeight), model.spectrum.top,
       toybox::kSmallFont, fui::TextAlign::Left);
  drawScale(screen, g);
  drawBand(screen, g, model.target);
  drawGuessFrame(screen, g, model.guess);
  caps(screen, g.bottomWord, model.spectrum.bottom, toybox::kSmallFont, fui::TextAlign::Left);

  char line[24];
  if (model.practice) {
    snprintf(line, sizeof(line), "NOT SCORED");
  } else {
    snprintf(line, sizeof(line), "+%d", model.points);
  }
  caps(screen, fui::makeRect(g.right.x, g.right.y, g.right.width, toybox::kHugeCut.lineHeight), line,
       model.practice ? toybox::kSmallFont : toybox::kDisplayFont, fui::TextAlign::Center);

  const int16_t rowTop = static_cast<int16_t>(g.right.y + toybox::kHugeCut.lineHeight + 10);
  const int16_t rowH = toybox::kButtonCut.lineHeight;
  char guessRow[20];
  char targetRow[20];
  snprintf(guessRow, sizeof(guessRow), "GUESS  %d", model.guess);
  snprintf(targetRow, sizeof(targetRow), "TARGET %d", model.target);
  caps(screen, fui::makeRect(g.right.x, rowTop, g.right.width, rowH), guessRow, toybox::kSmallFont,
       fui::TextAlign::Center);
  caps(screen, fui::makeRect(g.right.x, static_cast<int16_t>(rowTop + rowH + 6), g.right.width, rowH), targetRow,
       toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(g.right.x, static_cast<int16_t>(rowTop + 2 * (rowH + 6)), g.right.width, rowH),
       model.callWasRight ? "CALL RIGHT" : "CALL WRONG", toybox::kSmallFont, fui::TextAlign::Center);

  // Two lines, not one. "ROUND 5   15 POINTS" measures 228px in a 224px column
  // and the S was being clipped off POINTS -- four pixels, and it read as a typo
  // rather than as a layout fault.
  char roundLine[20];
  char pointsLine[20];
  snprintf(roundLine, sizeof(roundLine), "ROUND %d", model.roundNumber);
  snprintf(pointsLine, sizeof(pointsLine), "%d POINTS", model.total);
  const int16_t totalsTop = static_cast<int16_t>(rowTop + 3 * (rowH + 6) + 10);
  caps(screen, fui::makeRect(g.right.x, totalsTop, g.right.width, rowH), roundLine, toybox::kSmallFont,
       fui::TextAlign::Center);
  caps(screen, fui::makeRect(g.right.x, static_cast<int16_t>(totalsTop + rowH), g.right.width, rowH), pointsLine,
       toybox::kSmallFont, fui::TextAlign::Center);

  action(screen, footer(screen, 62, toybox::kMargin), "NEXT ROUND", ActionNextRound);
}

namespace {

// The ornament: how far off this table has been, every round it has ever
// played, as a row of marks per bucket.
//
// It is made of the game's own material (marks on a scale) and it carries the
// group's own data, so a screenshot of it is different on every device. That is
// the test the design language sets for anything decorative: a fixed pattern
// would be wallpaper by the third day. It is also the one thing on the front
// door that rewards coming back, because it is the only place you can see
// whether the table is actually getting better.
void ornament(toybox::Screen& screen, const fui::Rect& box, const wavelength::Record& record) {
  static const char* kLabels[wavelength::kBucketCount] = {"0", "1", "2", "3-5", "X"};
  toybox::bracket(screen, box, 14, toybox::kFrame);

  const int16_t rowH = static_cast<int16_t>((box.height - 20) / wavelength::kBucketCount);
  const int16_t markW = 13;
  const int16_t gap = 6;
  const int16_t left = static_cast<int16_t>(box.x + 70);
  const int16_t room = static_cast<int16_t>(box.x + box.width - 14 - left);
  const int maxMarks = room / (markW + gap);
  const uint16_t peak = record.peak();

  for (int i = 0; i < wavelength::kBucketCount; ++i) {
    const int16_t y = static_cast<int16_t>(box.y + 10 + i * rowH);
    caps(screen, fui::makeRect(static_cast<int16_t>(box.x + 12), y, 52, rowH), kLabels[i], toybox::kSmallFont,
         fui::TextAlign::Left);
    // Scaled to the tallest bucket rather than to a guessed maximum, so the row
    // fills the space it has however many rounds have been played.
    int marks = 0;
    if (peak > 0) {
      marks = (record.buckets[i] * maxMarks + peak - 1) / peak;
      if (record.buckets[i] > 0 && marks == 0) marks = 1;
    }
    for (int m = 0; m < marks; ++m) {
      const fui::Rect mark = fui::makeRect(static_cast<int16_t>(left + m * (markW + gap)),
                                           static_cast<int16_t>(y + rowH / 2 - markW / 2), markW, markW);
      // The miss bucket is dithered rather than solid: it is the one row you
      // want to be short, and solid ink would make it the loudest thing here.
      if (i == wavelength::kBucketCount - 1)
        dither(screen, mark, fui::Color::DarkGray);
      else
        fill(screen, mark);
    }
  }
}

}  // namespace

// The rules, reachable at last. A cold play-tester could not learn the scoring
// from the device at all: they reverse-engineered it from four reveals, and the
// practice round -- the one round whose job is to teach -- replaces the verdict
// word with PRACTICE, so you never see CLOSE or TELEPATHIC during the round
// designed to explain them. This screen says the numbers out loud.
void renderHowTo(toybox::Screen& screen) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), "HOW TO PLAY",
       toybox::kBodyFont, fui::TextAlign::Left);
  fill(screen, fui::makeRect(toybox::kMargin, 88, inner, toybox::kRule));

  static const char* kLines[] = {
      "ONE OF YOU SEES A HIDDEN",
      "NUMBER ON THE STRIP.",
      "",
      "THEY SAY ONE CLUE THAT",
      "SITS EXACTLY THERE.",
      "",
      "EVERYONE ELSE ARGUES,",
      "MOVES THE MARK, LOCKS IT,",
      "THEN CALLS WHICH SIDE",
      "THE TARGET IS ON.",
      "",
      "YOU ARE GUESSING WHERE",
      "THEY PUT IT, NOT WHERE",
      "YOU WOULD.",
  };
  const int16_t lineH = toybox::kButtonCut.lineHeight;
  for (int i = 0; i < static_cast<int>(sizeof(kLines) / sizeof(kLines[0])); ++i) {
    if (!kLines[i][0]) continue;
    caps(screen, fui::makeRect(toybox::kMargin, static_cast<int16_t>(104 + i * lineH), inner, lineH), kLines[i],
         toybox::kSmallFont, fui::TextAlign::Left);
  }

  const int16_t tableTop = 552;
  fill(screen, fui::makeRect(toybox::kMargin, static_cast<int16_t>(tableTop - 14), inner, toybox::kRule));
  static const char* kScore[][2] = {
      {"EXACT", "5"},
      {"ONE OFF", "3"},
      {"TWO OFF", "1"},
      {"RIGHT SIDE", "+1"},
  };
  for (int i = 0; i < 4; ++i) {
    const int16_t y = static_cast<int16_t>(tableTop + i * lineH);
    caps(screen, fui::makeRect(toybox::kMargin, y, inner, lineH), kScore[i][0], toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, y, inner, lineH), kScore[i][1], toybox::kSmallFont,
         fui::TextAlign::Right);
  }
  caps(screen, fui::makeRect(toybox::kMargin, static_cast<int16_t>(tableTop + 4 * lineH + 10), inner, lineH),
       "ROUND ONE IS PRACTICE.", toybox::kSmallFont, fui::TextAlign::Left);

  action(screen, lockBarRect(w, screen.device().screen().height), "BACK", ActionBackToMenu);
}

void renderMenu(toybox::Screen& screen, const MenuModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const wavelength::Record blank;
  const wavelength::Record& rec = model.record ? *model.record : blank;

  // The headline is also the hit target, so the commonest tap is on the largest
  // thing on the screen and needs no button of its own.
  const fui::Rect headline = fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight);
  // The display cut, not the huge one: WAVELENGTH is ten characters and the
  // huge cut truncated it to WAVELE. The headline is still the loudest thing
  // here because nothing else on the screen is near its size.
  caps(screen, headline, model.sessionInProgress ? "CARRY ON" : "WAVELENGTH", toybox::kBodyFont, fui::TextAlign::Left);
  screen.frame().hit(fui::makeRect(headline.x, headline.y, headline.width, static_cast<int16_t>(headline.height + 40)),
                     ActionStartRound);

  char state[40];
  if (model.sessionInProgress) {
    snprintf(state, sizeof(state), "ROUND %d, %d POINTS SO FAR", model.sessionRound, model.sessionTotal);
  } else {
    snprintf(state, sizeof(state), "%s", rec.rounds ? "READY WHEN YOU ARE" : "NOBODY HAS PLAYED YET");
  }
  caps(screen, fui::makeRect(toybox::kMargin, 150, inner, toybox::kButtonCut.lineHeight), state, toybox::kSmallFont,
       fui::TextAlign::Left);

  fill(screen, fui::makeRect(toybox::kMargin, 190, inner, toybox::kRule));

  char left[24];
  char right[24];
  snprintf(left, sizeof(left), "%d ROUNDS", rec.rounds);
  snprintf(right, sizeof(right), "AVG SCORE %d.%d", rec.averageTenths() / 10, rec.averageTenths() % 10);
  caps(screen, fui::makeRect(toybox::kMargin, 200, inner, toybox::kButtonCut.lineHeight), left, toybox::kSmallFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 200, inner, toybox::kButtonCut.lineHeight), right, toybox::kSmallFont,
       fui::TextAlign::Right);

  caps(screen, fui::makeRect(toybox::kMargin, 246, inner, toybox::kButtonCut.lineHeight), "HOW FAR OFF YOU HAVE BEEN",
       toybox::kSmallFont, fui::TextAlign::Left);
  ornament(screen, fui::makeRect(toybox::kMargin, 284, inner, 210), rec);

  action(screen, fui::makeRect(toybox::kMargin, 530, inner, 66), model.sessionInProgress ? "NEXT ROUND" : "START",
         ActionStartRound);
  action(screen, fui::makeRect(toybox::kMargin, 612, inner, 54), "HOW TO PLAY", ActionHowTo);
  // A control that cannot act dims rather than disappearing, so the layout does
  // not jump and you can still see what it would have done.
  fui::ButtonProps end;
  end.label = "END SESSION";
  end.text = toybox::buttonText(screen.theme());
  end.action = model.sessionInProgress ? ActionEndSession : fui::NO_ACTION;
  if (!model.sessionInProgress) end.styles = toybox::disabledStepperStyles();
  screen.button(end, fui::makeRect(toybox::kMargin, 674, inner, 54));
}

void renderSummary(toybox::Screen& screen, const SummaryModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const wavelength::Record blank;
  const wavelength::Record& rec = model.record ? *model.record : blank;

  char total[12];
  snprintf(total, sizeof(total), "%d", model.total);
  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kHugeCut.lineHeight), total, toybox::kDisplayFont,
       fui::TextAlign::Left);

  // "POINTS IN 11 ROUNDS" measures 498px of 448 at the display cut and was cut
  // mid-word. Split, which also gives the number the hierarchy it deserves.
  char line[24];
  snprintf(line, sizeof(line), "IN %d ROUNDS", model.rounds);
  caps(screen, fui::makeRect(toybox::kMargin, 150, inner, toybox::kDisplayCut.lineHeight), "POINTS", toybox::kBodyFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 214, inner, toybox::kButtonCut.lineHeight), line, toybox::kSmallFont,
       fui::TextAlign::Left);

  fill(screen, fui::makeRect(toybox::kMargin, 258, inner, toybox::kRule));

  // The average sits next to what a table that is genuinely communicating gets.
  // A number with nothing beside it means nothing: nobody can tell whether 19
  // points is good.
  char avg[16];
  snprintf(avg, sizeof(avg), "%d.%d", model.averageTenths / 10, model.averageTenths % 10);
  caps(screen, fui::makeRect(toybox::kMargin, 240, inner, toybox::kDisplayCut.lineHeight), "THIS SESSION",
       toybox::kSmallFont, fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 240, inner, toybox::kDisplayCut.lineHeight), avg, toybox::kBodyFont,
       fui::TextAlign::Right);

  char good[16];
  snprintf(good, sizeof(good), "%d.%d", wavelength::kGoodTableTenths / 10, wavelength::kGoodTableTenths % 10);
  caps(screen, fui::makeRect(toybox::kMargin, 306, inner, toybox::kButtonCut.lineHeight), "A GOOD TABLE",
       toybox::kSmallFont, fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 306, inner, toybox::kButtonCut.lineHeight), good, toybox::kSmallFont,
       fui::TextAlign::Right);

  caps(screen, fui::makeRect(toybox::kMargin, 356, inner, toybox::kButtonCut.lineHeight), "HOW FAR OFF YOU HAVE BEEN",
       toybox::kSmallFont, fui::TextAlign::Left);
  ornament(screen, fui::makeRect(toybox::kMargin, 394, inner, 210), rec);

  action(screen, fui::makeRect(toybox::kMargin, 630, inner, 62), "KEEP PLAYING", ActionKeepPlaying);
  action(screen, fui::makeRect(toybox::kMargin, 710, inner, 54), "NEW SESSION", ActionNewSession);
}

}  // namespace wavelengthui
