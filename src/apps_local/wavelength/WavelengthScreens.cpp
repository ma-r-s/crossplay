#include "WavelengthScreens.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

fui::FontId slotForAll(toybox::Screen& screen, const int16_t width, const char* const* words, const int count) {
  const fui::TextStyle big = textStyle(toybox::kBodyFont, fui::TextAlign::Center);
  for (int i = 0; i < count; ++i) {
    if (words[i] == nullptr) continue;
    if (screen.target().measureText(toybox::kBodyFont, words[i], big).width > width) return toybox::kSmallFont;
  }
  return toybox::kBodyFont;
}

fui::FontId pairSlot(toybox::Screen& screen, const int16_t width, const char* a, const char* b) {
  const char* both[2] = {a, b};
  return slotForAll(screen, width, both, 2);
}

void fill(toybox::Screen& screen, const fui::Rect& box, const fui::Color colour = fui::Color::Black) {
  screen.target().fill(box, fui::Paint::solid(colour));
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
  // Named rather than inline, and named uniquely: check_widths.py resolves a
  // rect by name, so a second `box` of another width would make both of them
  // unmeasurable.
  const fui::Rect slotBox = fui::makeRect(static_cast<int16_t>(rightEdge - 40), slotTop(g, slot), 40, g.slot);
  caps(screen, slotBox, buf, toybox::kSmallFont, fui::TextAlign::Right);
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

int dialSlotAt(const int16_t screenW, const int16_t screenH, const int16_t x, const int16_t y) {
  const int16_t boardW = static_cast<int16_t>(screenW * 5 / 16);
  const int16_t rightEdge = static_cast<int16_t>(toybox::kMargin + 48 + boardW + 13);
  const int16_t leftEdge = static_cast<int16_t>(toybox::kMargin + 24);
  if (x < leftEdge || x >= rightEdge) return 0;
  const int16_t wordBox = toybox::kDisplayCut.lineHeight;
  const int16_t lockY = static_cast<int16_t>(screenH - toybox::kMargin - 62);
  const int16_t bottomWordY = static_cast<int16_t>(lockY - 10 - wordBox);
  const int16_t boardTop = static_cast<int16_t>(toybox::kMargin + wordBox + 18);
  const int16_t slot = static_cast<int16_t>((bottomWordY - 10 - boardTop) / kSlots);
  if (slot <= 0) return 0;
  const int16_t boardBottom = static_cast<int16_t>(boardTop + slot * kSlots);
  if (y < boardTop - slot || y >= boardBottom + slot) return 0;
  if (y < boardTop) return kSlots;
  if (y >= boardBottom) return 1;
  const int row = (y - boardTop) / slot;
  const int result = kSlots - row;
  return result < 1 ? 1 : (result > kSlots ? kSlots : result);
}

void renderDial(toybox::Screen& screen, const DialModel& model) {
  toybox::absoluteChrome(screen);
  const Geometry g = layout(screen);

  // The ends frame the board rather than sitting in a header band: they are the
  // one thing a loud table has to be able to re-read, and a header would cost
  // the strip a fifth of its height. Deck words are drawn at the display cut,
  // never larger. Measured against the shipped 240-pair deck, 119 of the 478
  // end words are wider than the panel at the large cut, and it reads as
  // perfectly fine with any short example word.
  const fui::FontId endSlot = pairSlot(screen, g.topWord.width, model.spectrum.top, model.spectrum.bottom);
  caps(screen, g.topWord, model.spectrum.top, endSlot, fui::TextAlign::Left);
  fill(screen, fui::makeRect(g.topWord.x, static_cast<int16_t>(g.topWord.y + g.topWord.height + 4), g.topWord.width,
                             toybox::kRule));
  caps(screen, g.bottomWord, model.spectrum.bottom, endSlot, fui::TextAlign::Left);

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
  caps(screen, label, "ONE GUESS", toybox::kSmallFont, fui::TextAlign::Center);
  const fui::Rect label2 =
      fui::makeRect(label.x, static_cast<int16_t>(label.y + label.height), label.width, toybox::kButtonCut.lineHeight);
  caps(screen, label2, "FOR THE TABLE", toybox::kSmallFont, fui::TextAlign::Center);
  const int16_t labelBottom = static_cast<int16_t>(label2.y + label2.height);
  fill(screen, fui::makeRect(static_cast<int16_t>(g.right.x + 20), static_cast<int16_t>(labelBottom + 14),
                             static_cast<int16_t>(g.right.width - 40), toybox::kRule));

  const int16_t hintY = static_cast<int16_t>(labelBottom + 30);
  caps(screen, fui::makeRect(g.right.x, hintY, g.right.width, toybox::kButtonCut.lineHeight), "TAP THE SCALE",
       toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + toybox::kButtonCut.lineHeight), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "TO MOVE THE BAR", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + 2 * toybox::kButtonCut.lineHeight + 14), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "SIDE BUTTONS", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + 3 * toybox::kButtonCut.lineHeight + 14), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "MOVE IT ONE", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + 5 * toybox::kButtonCut.lineHeight + 24), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "GUESSERS", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + 6 * toybox::kButtonCut.lineHeight + 24), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "ONLY", toybox::kSmallFont, fui::TextAlign::Center);
  // The only screen that says what the hardware key does, on the only screen
  // the pause can be reached from. Nothing in the app named Back at all, so the
  // scoring table behind it might as well not have existed.
  caps(screen,
       fui::makeRect(g.right.x, static_cast<int16_t>(hintY + 8 * toybox::kButtonCut.lineHeight + 24), g.right.width,
                     toybox::kButtonCut.lineHeight),
       "BACK PAUSES", toybox::kSmallFont, fui::TextAlign::Center);

  // An ordinary button. It carries the action, so the frame routes it on the
  // RELEASE like every other control here, and the label says what one press
  // does rather than asking for a duration nothing states.
  //
  // Measured, because the fui button truncates a label it cannot fit and this
  // gate does not read ButtonProps: LOCK IT IN is 112px of toybox_14 in a 160px
  // box. tools_local/wavelength/check_widths.py sees caps() draws only.
  fui::ButtonProps lock;
  lock.label = "LOCK IT IN";
  lock.text = toybox::buttonText(screen.theme());
  lock.action = ActionLock;
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

void endButton(toybox::Screen& screen, const fui::Rect& box, const char* word, const fui::ActionId id,
               const fui::FontId slot) {
  fui::ButtonProps props;
  props.label = "";
  props.text = toybox::buttonText(screen.theme());
  props.action = id;
  screen.button(props, box);

  const int16_t inner = static_cast<int16_t>(box.width - 24);
  const int16_t x = static_cast<int16_t>(box.x + 12);
  const fui::TextStyle small = textStyle(toybox::kSmallFont, fui::TextAlign::Center);
  if (screen.target().measureText(toybox::kSmallFont, word, small).width <= inner) {
    const int16_t y = static_cast<int16_t>(box.y + (box.height - toybox::kDisplayCut.lineHeight) / 2);
    caps(screen, fui::makeRect(x, y, inner, toybox::kDisplayCut.lineHeight), word, slot, fui::TextAlign::Center,
         fui::Color::White);
    return;
  }

  // Split at the space nearest the middle, so the two halves are of a length
  // rather than one word orphaned under seven.
  const int len = static_cast<int>(strlen(word));
  int best = -1;
  for (int i = 0; i < len; ++i) {
    if (word[i] != ' ') continue;
    if (best < 0 || abs(i - len / 2) < abs(best - len / 2)) best = i;
  }
  if (best < 0) {
    // One unbreakable token. Nothing to do but let endWord take the smallest
    // cut it has; a hyphen inside a spectrum word would read as part of it.
    const int16_t y = static_cast<int16_t>(box.y + (box.height - toybox::kDisplayCut.lineHeight) / 2);
    caps(screen, fui::makeRect(x, y, inner, toybox::kDisplayCut.lineHeight), word, toybox::kSmallFont,
         fui::TextAlign::Center, fui::Color::White);
    return;
  }
  char head[64];
  char tail[64];
  snprintf(head, sizeof(head), "%.*s", best, word);
  snprintf(tail, sizeof(tail), "%s", word + best + 1);
  const int16_t lineH = toybox::kButtonCut.lineHeight;
  const int16_t top = static_cast<int16_t>(box.y + (box.height - 2 * lineH) / 2);
  caps(screen, fui::makeRect(x, top, inner, lineH), head, toybox::kSmallFont, fui::TextAlign::Center,
       fui::Color::White);
  caps(screen, fui::makeRect(x, static_cast<int16_t>(top + lineH), inner, lineH), tail, toybox::kSmallFont,
       fui::TextAlign::Center, fui::Color::White);
}

void action(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId id) {
  fui::ButtonProps props;
  props.label = label;
  props.text = toybox::buttonText(screen.theme());
  props.action = id;
  screen.button(props, box);
}

// A control that ends something, drawn as an outline rather than a solid bar.
//
// There is no red on a 1-bit panel, so the only thing "destructive" can look
// like here is DIFFERENT from the buttons your thumb lands on by default. Every
// other button in this app is solid black; this one is the page's colour with a
// heavy frame round it, which is the same difference in KIND the peek's dimmed
// footer uses. It reads as a door rather than as the next step.
//
// Both controls that destroy something use it, and there are exactly two:
// ending the session and abandoning a round. ABANDON THIS ROUND was a solid
// black bar identical in weight to I HAVE THE DEVICE and NEXT ROUND on the
// screens either side of it, sitting exactly where the thumb rests.
void endingAction(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId id) {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.normal.border = fui::Paint::solid(fui::Color::Black);
  styles.normal.borderWidth = toybox::kFrame;
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  styles.disabled = styles.normal;

  fui::ButtonProps props;
  props.label = label;
  fui::TextStyle text = toybox::buttonText(screen.theme());
  text.color = fui::Color::Black;
  props.text = text;
  props.styles = styles;
  props.action = id;
  screen.button(props, box);
}

// The two ends stacked with a dotted spine between them, which is the shape of
// the strip without being the strip: this screen must not show a position.
void endsStacked(toybox::Screen& screen, const Spectrum& spectrum, const int16_t top, const int16_t bottom) {
  const int16_t w = screen.device().screen().width;
  const fui::Rect band = fui::makeRect(toybox::kMargin, top, static_cast<int16_t>(w - 2 * toybox::kMargin),
                                       toybox::kDisplayCut.lineHeight);
  const fui::FontId slot = pairSlot(screen, band.width, spectrum.top, spectrum.bottom);
  caps(screen, band, spectrum.top, slot, fui::TextAlign::Center);
  const int16_t spineTop = static_cast<int16_t>(top + toybox::kDisplayCut.lineHeight + 16);
  const int16_t spineBottom = static_cast<int16_t>(bottom - 16);
  for (int16_t y = spineTop; y + 10 < spineBottom; y = static_cast<int16_t>(y + 22))
    fill(screen, fui::makeRect(static_cast<int16_t>(w / 2 - 4), y, 8, 10));
  caps(screen,
       fui::makeRect(toybox::kMargin, bottom, static_cast<int16_t>(w - 2 * toybox::kMargin),
                     toybox::kDisplayCut.lineHeight),
       spectrum.bottom, slot, fui::TextAlign::Center);
}

}  // namespace

void renderPause(toybox::Screen& screen, const PauseModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), "PAUSED", toybox::kBodyFont,
       fui::TextAlign::Left);

  char line[40];
  if (model.practice) {
    snprintf(line, sizeof(line), "PRACTICE ROUND");
  } else {
    snprintf(line, sizeof(line), "ROUND %d   %d POINT%s", model.roundNumber, model.total, model.total == 1 ? "" : "S");
  }
  caps(screen, fui::makeRect(toybox::kMargin, 88, inner, toybox::kButtonCut.lineHeight), line, toybox::kSmallFont,
       fui::TextAlign::Left);
  fill(screen, fui::makeRect(toybox::kMargin, 126, inner, toybox::kRule));

  // The scoring table lives here as well as on the practice reveal, because
  // "how many points is one off again?" is asked mid-round and used to cost the
  // round to answer.
  // A table with a heading and one shape of number in it. Unheaded, five rows
  // of words and figures beside a RESUME button were read as a scoreboard; and
  // RIGHT SIDE was a fourth name for the end call, which the two screens that
  // ask for it call something else again.
  static const char* kScore[][2] = {{"EXACT", "5"}, {"ONE OFF", "3"}, {"TWO OFF", "1"}, {"FURTHER OFF", "0"}};
  const int16_t lineH = toybox::kButtonCut.lineHeight;
  caps(screen, fui::makeRect(toybox::kMargin, 150, inner, lineH), "POINTS FOR THE ROUND", toybox::kSmallFont,
       fui::TextAlign::Left);
  for (size_t i = 0; i < sizeof(kScore) / sizeof(kScore[0]); ++i) {
    const int16_t y = static_cast<int16_t>(179 + i * lineH);
    caps(screen, fui::makeRect(toybox::kMargin, y, inner, lineH), kScore[i][0], toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, y, inner, lineH), kScore[i][1], toybox::kSmallFont,
         fui::TextAlign::Right);
  }

  action(screen, fui::makeRect(toybox::kMargin, 340, inner, 72), "RESUME THE ROUND", ActionResume);

  fill(screen, fui::makeRect(toybox::kMargin, 440, inner, toybox::kRule));
  caps(screen, fui::makeRect(toybox::kMargin, 458, inner, lineH), "THIS ROUND IS SCRAPPED. THE NEXT",
       toybox::kSmallFont, fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 487, inner, lineH), "PLAYER GETS A NEW NUMBER, AND", toybox::kSmallFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 516, inner, lineH), "THE PASS SCREEN SAYS SO.", toybox::kSmallFont,
       fui::TextAlign::Left);
  if (model.abandoned > 0) {
    // A COUNT, not an instruction: "1 ABANDON THIS SESSION" sat directly above a
    // button reading ABANDON THIS ROUND and read as a command to press it.
    char abandonLine[40];
    snprintf(abandonLine, sizeof(abandonLine), "%d ROUND%s ABANDONED SO FAR", model.abandoned,
             model.abandoned == 1 ? "" : "S");
    caps(screen, fui::makeRect(toybox::kMargin, 556, inner, lineH), abandonLine, toybox::kSmallFont,
         fui::TextAlign::Left);
  }

  endingAction(screen, footer(screen, 62, toybox::kMargin), "ABANDON THIS ROUND", ActionAbandon);
}

void renderPassLeft(toybox::Screen& screen, const PassModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  // Centred on the whole panel, not on a body below a header: this screen has
  // one message and anything else reads as having slipped.
  // The object being passed is NAMED, and so is what taking it makes you. This
  // is the first instruction anyone in the game receives, and PASS LEFT alone
  // said neither -- before the app had mentioned that there are two roles at
  // all. The pole word stays huge; the sentence around it does not need to be.
  caps(screen, fui::makeRect(toybox::kMargin, 170, inner, toybox::kDisplayCut.lineHeight), "PASS THE DEVICE",
       toybox::kBodyFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 245, inner, toybox::kHugeCut.lineHeight), "LEFT", toybox::kDisplayFont,
       fui::TextAlign::Center);
  fill(screen, fui::makeRect(90, 400, static_cast<int16_t>(w - 180), toybox::kRule));
  caps(screen, fui::makeRect(toybox::kMargin, 416, inner, toybox::kButtonCut.lineHeight),
       "WHOEVER TAKES IT GIVES THE CLUE.", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 470, inner, toybox::kDisplayCut.lineHeight), "EVERYONE ELSE,",
       toybox::kBodyFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 533, inner, toybox::kDisplayCut.lineHeight), "LOOK AWAY.",
       toybox::kBodyFont, fui::TextAlign::Center);

  if (model.abandoned) {
    // One shape for one fact, first time and every time after: the old screen
    // said LAST ROUND WAS ABANDONED once and then started counting, so the
    // second abandon looked like a different event from the first.
    char note[44];
    snprintf(note, sizeof(note), "%d ROUND%s ABANDONED SO FAR", model.abandonedCount,
             model.abandonedCount == 1 ? "" : "S");
    caps(screen, fui::makeRect(toybox::kMargin, 120, inner, toybox::kButtonCut.lineHeight), note, toybox::kSmallFont,
         fui::TextAlign::Center);
  }

  char line[40];
  if (model.practice) {
    snprintf(line, sizeof(line), "PRACTICE ROUND");
  } else {
    snprintf(line, sizeof(line), "ROUND %d   %d POINT%s", model.roundNumber, model.total, model.total == 1 ? "" : "S");
  }
  // In the BODY slot, not the small one. This was the smallest type on the
  // screen and a tester played an entire careful round without registering it.
  caps(screen, fui::makeRect(toybox::kMargin, 625, inner, toybox::kDisplayCut.lineHeight), line,
       model.practice ? toybox::kBodyFont : toybox::kSmallFont, fui::TextAlign::Center);
  if (model.practice) {
    caps(screen, fui::makeRect(toybox::kMargin, 690, inner, toybox::kButtonCut.lineHeight), "THIS ONE DOES NOT COUNT",
         toybox::kSmallFont, fui::TextAlign::Center);
  }

  // I HAVE IT was read as "I have a clue", so the person who had just READ one
  // pressed it and the device never moved.
  action(screen, footer(screen, 62, toybox::kMargin), "I HAVE THE DEVICE", ActionReady);
}

void renderPick(toybox::Screen& screen, const PickModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const int16_t pad = 22;

  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), "PICK ONE", toybox::kBodyFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 84, inner, toybox::kButtonCut.lineHeight), "THE NUMBER IS NOT DRAWN YET.",
       toybox::kSmallFont, fui::TextAlign::Left);

  const int16_t cardH = 250;
  for (int i = 0; i < (model.onlyOne ? 1 : 2); ++i) {
    const Spectrum& s = i == 0 ? model.first : model.second;
    const fui::Rect card = fui::makeRect(toybox::kMargin, static_cast<int16_t>(120 + i * (cardH + 24)), inner, cardH);
    outline(screen, card, toybox::kFrame);
    // Full card width, not the inset: at 22px a side a 33-character deck word
    // does not fit even at the small cut, and MOVIE THAT GODZILLA WOULD IMPROVE
    // was being chopped mid-word.
    const int16_t cardInner = static_cast<int16_t>(card.width - 2 * (toybox::kFrame + 4));
    const int16_t cardX = static_cast<int16_t>(card.x + toybox::kFrame + 4);
    // Both ends at ONE size: sized separately, the longer pole dropped a whole
    // cut and the card read as a heading with a subheading under it.
    const char* all[4] = {model.first.top, model.first.bottom, model.second.top, model.second.bottom};
    const fui::FontId cardSlot = slotForAll(screen, cardInner, all, model.onlyOne ? 2 : 4);
    caps(screen, fui::makeRect(cardX, static_cast<int16_t>(card.y + pad), cardInner, toybox::kDisplayCut.lineHeight),
         s.top, cardSlot, fui::TextAlign::Left);
    for (int d = 0; d < 7; ++d)
      fill(screen, fui::makeRect(static_cast<int16_t>(card.x + pad + d * 26),
                                 static_cast<int16_t>(card.y + card.height / 2 - 2), 15, 4));
    caps(screen,
         fui::makeRect(cardX, static_cast<int16_t>(card.y + card.height - pad - toybox::kDisplayCut.lineHeight),
                       cardInner, toybox::kDisplayCut.lineHeight),
         s.bottom, cardSlot, fui::TextAlign::Left);
    screen.frame().hit(card, i == 0 ? ActionPickFirst : ActionPickSecond);
  }

  const char* hint = model.onlyOne ? "ONE LEFT IN THE DECK" : "TAP ONE TO CHOOSE IT";
  caps(screen, fui::makeRect(toybox::kMargin, 700, inner, toybox::kButtonCut.lineHeight), hint, toybox::kSmallFont,
       fui::TextAlign::Center);
}

fui::Rect lockBarRect(const int16_t screenW, const int16_t screenH) {
  // The NUMBER COLUMN's x-range, by the same arithmetic layout() uses for
  // g.right. It is derived rather than copied because the two must agree: the
  // guard this rect provides IS that it sits in the column beside the strip
  // rather than under it, and a second set of literals is how that quietly
  // stops being true.
  const int16_t boardW = static_cast<int16_t>(screenW * 5 / 16);
  const int16_t rightX = static_cast<int16_t>(toybox::kMargin + 48 + boardW + 26);
  const int16_t rightW = static_cast<int16_t>(screenW - toybox::kMargin - rightX);

  // WHY THIS IS NOT A FULL-WIDTH BAR ANY MORE. It used to span the panel with a
  // 64px inset either side and only a HOLD stopped it committing. The hold is
  // gone -- a duration nothing on the panel states is a guessing game, not a
  // safeguard -- so the guard is where the button is instead:
  //
  //   * it starts at x=240 while dialSlotAt() stops answering at x=226, so
  //     every pixel below the strip's own column is dead paper;
  //   * the strip's live region ends at y=663 and this starts at y=722, so a
  //     finger sliding off the bottom of the board lands on nothing;
  //   * the bottom-LEFT corner clearance goes from 64px to 224px and the
  //     bottom-right stays at 64px, which is what testWavelengthTheFourThatWereDropped
  //     already required of it for the thumb that holds a portrait slab.
  //
  // Being narrower than the app's other footer bars is deliberate: those mean
  // "move on" and this one ends the round.
  return fui::makeRect(rightX, static_cast<int16_t>(screenH - toybox::kMargin - 62), static_cast<int16_t>(rightW - 64),
                       62);
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

  const fui::FontId poleSlot = pairSlot(screen, inner, model.spectrum.top, model.spectrum.bottom);
  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, poleH), model.spectrum.top, poleSlot, fui::TextAlign::Left);
  fill(screen, fui::makeRect(toybox::kMargin, 82, inner, toybox::kRule));
  caps(screen, fui::makeRect(toybox::kMargin, bottomPoleY, inner, poleH), model.spectrum.bottom, poleSlot,
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

  // The number is LABELLED, because its twin on the guess screen is and this
  // one was not: a bare numeral beside a strip is read as a score by anyone who
  // has not been told what it is. The row is reserved whether or not a thumb is
  // down, so the lines below do not jump when the band appears.
  const fui::Rect numberLabel = fui::makeRect(g.right.x, static_cast<int16_t>(g.right.y + toybox::kHugeCut.lineHeight),
                                              g.right.width, toybox::kButtonCut.lineHeight);
  if (model.revealed) caps(screen, numberLabel, "THE NUMBER", toybox::kSmallFont, fui::TextAlign::Center);

  const int16_t textTop = static_cast<int16_t>(numberLabel.y + numberLabel.height + 28);
  // Named for its column, not `kLines`: HOW TO PLAY has a table of its own by
  // that name, drawn into twice this width, and check_widths.py resolves a
  // table by name across the whole file. Two tables sharing one name measured
  // this 224px column's strings against 448 and reported clean.
  static const char* kAdvice[] = {
      "SAY ONE THING", "THAT SITS",        "EXACTLY THERE", "", "TWO EITHER SIDE", "STILL SCORES", "",
      "ONLY ONE.",     "DO NOT ADD TO IT."};
  for (int i = 0; i < 9; ++i) {
    if (!kAdvice[i][0]) continue;
    caps(screen,
         fui::makeRect(g.right.x, static_cast<int16_t>(textTop + i * toybox::kButtonCut.lineHeight), g.right.width,
                       toybox::kButtonCut.lineHeight),
         kAdvice[i], toybox::kSmallFont, fui::TextAlign::Center);
  }

  fui::ButtonProps hold;
  // A tap here does nothing by design, and drawn the same either way that reads
  // as a dead button rather than as the wrong gesture.
  hold.label = model.revealed ? "KEEP HOLDING" : (model.nudgeHold ? "HOLD IT DOWN TO SEE" : "HOLD TO SEE THE NUMBER");
  hold.text = toybox::buttonText(screen.theme());
  hold.action = ActionPeekPad;
  screen.button(hold, padRect);

  // Until the target has actually been seen this cannot act, so it must not
  // look like it can. The same dimming the front door uses for END SESSION: a
  // control that cannot act dims rather than disappearing.
  const fui::Rect doneBox = footer(screen, 58, toybox::kMargin);
  if (model.everRevealed) {
    action(screen, doneBox, "I HAVE MY CLUE", ActionClueGiven);
  } else {
    caps(
        screen,
        fui::makeRect(doneBox.x, static_cast<int16_t>(doneBox.y + (doneBox.height - toybox::kButtonCut.lineHeight) / 2),
                      doneBox.width, toybox::kButtonCut.lineHeight),
        "HOLD THE BAR ABOVE TO SEE IT", toybox::kSmallFont, fui::TextAlign::Center);
  }
}

void renderClue(toybox::Screen& screen, const ClueModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);

  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kDisplayCut.lineHeight), "SAY IT OUT LOUD",
       toybox::kBodyFont, fui::TextAlign::Center);
  // The all-clear, said as one: TARGET HIDDEN read as a warning that something
  // was wrong, on the one screen whose job is to tell the table it may look.
  caps(screen, fui::makeRect(toybox::kMargin, 78, inner, toybox::kButtonCut.lineHeight), "THE NUMBER IS HIDDEN NOW.",
       toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 107, inner, toybox::kButtonCut.lineHeight), "EVERYONE CAN LOOK.",
       toybox::kSmallFont, fui::TextAlign::Center);
  fill(screen, fui::makeRect(60, 140, static_cast<int16_t>(w - 120), toybox::kRule));

  endsStacked(screen, model.spectrum, 160, 470);

  fill(screen, fui::makeRect(60, 580, static_cast<int16_t>(w - 120), toybox::kRule));
  caps(screen, fui::makeRect(toybox::kMargin, 598, inner, toybox::kButtonCut.lineHeight), "ONE CLUE. TAKE YOUR TIME.",
       toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 630, inner, toybox::kButtonCut.lineHeight),
       "REPEAT IT. DO NOT ADD TO IT.", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 664, inner, toybox::kButtonCut.lineHeight), "THEN HANDS OFF THE DEVICE.",
       toybox::kSmallFont, fui::TextAlign::Center);

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
  // The one screen that said TARGET, on a device where every other screen
  // calls it THE NUMBER, and the one screen that asks for the side call
  // without ever using the words the rules and the scoring table both use.
  caps(screen, fui::makeRect(toybox::kMargin, 84, inner, toybox::kButtonCut.lineHeight),
       "SIDE CALL: IS THE NUMBER NEARER", toybox::kSmallFont, fui::TextAlign::Center);

  // The buttons carry this round's own end words and sit in the strip's own
  // order. Not higher and lower: those are the most seat-dependent words
  // available, and the device is lying flat between people.
  // Nothing sits above slot 20 or below slot 1, so at the ends one of the two
  // answers is not a choice: it cannot be right and cannot be argued for. Offer
  // the one that exists rather than a coin flip with a wrong side.
  if (model.guess <= 1 || model.guess >= wavelength::kSlots) {
    const bool atTop = model.guess >= wavelength::kSlots;
    char only[40];
    snprintf(only, sizeof(only), "NOTHING IS %s %d.", atTop ? "ABOVE" : "BELOW", model.guess);
    caps(screen, fui::makeRect(toybox::kMargin, 140, inner, toybox::kButtonCut.lineHeight), only, toybox::kSmallFont,
         fui::TextAlign::Center);
    endButton(screen, fui::makeRect(toybox::kMargin, 200, inner, 260),
              atTop ? model.spectrum.bottom : model.spectrum.top, atTop ? ActionCallBottom : ActionCallTop,
              toybox::kBodyFont);
    caps(screen, fui::makeRect(toybox::kMargin, 500, inner, toybox::kButtonCut.lineHeight), "THE ONLY WAY LEFT.",
         toybox::kSmallFont, fui::TextAlign::Center);
    caps(screen, fui::makeRect(toybox::kMargin, 704, inner, toybox::kButtonCut.lineHeight),
         "PRACTICE ROUND. NOTHING SCORES.", toybox::kSmallFont, fui::TextAlign::Center);
    return;
  }

  const fui::FontId btnSlot =
      pairSlot(screen, static_cast<int16_t>(inner - 24), model.spectrum.top, model.spectrum.bottom);
  endButton(screen, fui::makeRect(toybox::kMargin, 140, inner, 200), model.spectrum.top, ActionCallTop, btnSlot);
  // The locked number sits BETWEEN the two ends, in the strip's own order, so
  // the screen is a picture of the question rather than two buttons and a
  // floating caption pointing at nothing.
  char lockedNum[8];
  snprintf(lockedNum, sizeof(lockedNum), "%d", model.guess);
  caps(screen, fui::makeRect(toybox::kMargin, 360, inner, toybox::kDisplayCut.lineHeight), lockedNum, toybox::kBodyFont,
       fui::TextAlign::Center);
  endButton(screen, fui::makeRect(toybox::kMargin, 450, inner, 200), model.spectrum.bottom, ActionCallBottom, btnSlot);

  // THE GIVER was one of the four names for this role that the wording pass
  // killed everywhere except here. CLUE-GIVER costs 60px more, which is why
  // the sentence is the negative half: it is the half that prevents the fault.
  caps(screen, fui::makeRect(toybox::kMargin, 668, inner, toybox::kButtonCut.lineHeight),
       "THE CLUE-GIVER STAYS OUT OF THIS.", toybox::kSmallFont, fui::TextAlign::Center);
  caps(screen, fui::makeRect(toybox::kMargin, 704, inner, toybox::kButtonCut.lineHeight),
       "PRACTICE ROUND. NOTHING SCORES.", toybox::kSmallFont, fui::TextAlign::Center);
}

void renderReveal(toybox::Screen& screen, const RevealModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const Geometry g = layout(screen, 150);

  // The verdict is the one place the design language says to spend solid black:
  // it repaints once, on a full refresh, as the payoff. It is set in the
  // display cut rather than the large one because the capsule has to hold the
  // longest of these words across the full content width, and the set it
  // replaced had one (TELEPATHIC) that did not fit even here.
  //
  // The words are the SCORING TABLE'S OWN, because the old set was a second
  // vocabulary that contradicted the first: WARM sat below CLOSE with no COLD
  // anywhere, so a table that read the two together concluded that two off beat
  // one off. And PRACTICE was not a verdict shape at all on the one round whose
  // job is to teach what the verdicts are.
  //
  // The banner says HOW FAR OFF THE GUESS WAS, at every distance. It used to
  // collapse everything past two into MISS, which is a ROUND verdict, and the
  // round it sat on had often scored: a seven-off guess with a right side call
  // printed MISS beside +1, and there is no reading of those two together that
  // agrees. A distance beside +1 is a fact and an arithmetic instead, in the
  // same shape as the ONE OFF and TWO OFF the scoring table already uses.
  static const char* kDistance[kSlots] = {
      "EXACT",        "ONE OFF",     "TWO OFF",     "THREE OFF",     "FOUR OFF",     "FIVE OFF",    "SIX OFF",
      "SEVEN OFF",    "EIGHT OFF",   "NINE OFF",    "TEN OFF",       "ELEVEN OFF",   "TWELVE OFF",  "THIRTEEN OFF",
      "FOURTEEN OFF", "FIFTEEN OFF", "SIXTEEN OFF", "SEVENTEEN OFF", "EIGHTEEN OFF", "NINETEEN OFF"};
  const int miss = model.guess > model.target ? model.guess - model.target : model.target - model.guess;
  const char* verdict = model.practice ? "NO SCORE" : kDistance[miss < kSlots ? miss : kSlots - 1];
  const fui::Rect capsule = fui::makeRect(toybox::kMargin, 16, inner, 76);
  fill(screen, capsule);
  caps(screen, capsule, verdict, toybox::kBodyFont, fui::TextAlign::Center, fui::Color::White);

  caps(screen, fui::makeRect(g.topWord.x, 102, g.topWord.width, toybox::kButtonCut.lineHeight), model.spectrum.top,
       toybox::kSmallFont, fui::TextAlign::Left);
  drawScale(screen, g);
  drawBand(screen, g, model.target);
  drawGuessFrame(screen, g, model.guess);
  caps(screen, g.bottomWord, model.spectrum.bottom, toybox::kSmallFont, fui::TextAlign::Left);

  // Every round shows the figure it scored, the practice round included: it
  // scores +0, which is a number in the same place and the same size as every
  // other round's. NOT SCORED, set at a quarter of that size because the phrase
  // would not fit, read as the screen having failed rather than as a round that
  // counts for nothing.
  // Named for its column, not for its line: check_widths.py resolves a buffer's
  // box by name across the whole file, and a second `line` drawn into a
  // different width leaves BOTH of them unmeasured.
  char scored[48];
  snprintf(scored, sizeof(scored), "+%d", model.points);
  caps(screen, fui::makeRect(g.right.x, g.right.y, g.right.width, toybox::kHugeCut.lineHeight), scored,
       toybox::kDisplayFont, fui::TextAlign::Center);
  // And the figure is labelled. MISS printed beside +1 is a contradiction until
  // you know the +1 is the side call's and the verdict is the guess's.
  const fui::Rect caption = fui::makeRect(g.right.x, static_cast<int16_t>(g.right.y + toybox::kHugeCut.lineHeight),
                                          g.right.width, toybox::kButtonCut.lineHeight);
  caps(screen, caption, model.practice ? "PRACTICE ROUND" : "THIS ROUND", toybox::kSmallFont, fui::TextAlign::Center);

  const int16_t rowTop = static_cast<int16_t>(caption.y + caption.height + 6);
  const int16_t rowH = toybox::kButtonCut.lineHeight;
  char guessRow[20];
  char targetRow[20];
  snprintf(guessRow, sizeof(guessRow), "GUESS  %d", model.guess);
  snprintf(targetRow, sizeof(targetRow), "NUMBER %d", model.target);
  const int16_t swatchX = static_cast<int16_t>(g.right.x + 6);
  const int16_t swatchW = 26;
  outline(screen, fui::makeRect(swatchX, static_cast<int16_t>(rowTop + rowH / 2 - 6), swatchW, 12), 3);
  caps(screen,
       fui::makeRect(static_cast<int16_t>(swatchX + swatchW + 8), rowTop,
                     static_cast<int16_t>(g.right.width - swatchW - 14), rowH),
       guessRow, toybox::kSmallFont, fui::TextAlign::Left);
  fill(screen, fui::makeRect(swatchX, static_cast<int16_t>(rowTop + rowH + 6 + rowH / 2 - 6), swatchW, 12));
  caps(screen,
       fui::makeRect(static_cast<int16_t>(swatchX + swatchW + 8), static_cast<int16_t>(rowTop + rowH + 6),
                     static_cast<int16_t>(g.right.width - swatchW - 14), rowH),
       targetRow, toybox::kSmallFont, fui::TextAlign::Left);
  // The side call, in the name the scoring table gives it, with the point it
  // did or did not add underneath. CALL RIGHT was a third name for the same
  // event and showed no arithmetic at all.
  const int16_t callTop = static_cast<int16_t>(rowTop + 2 * (rowH + 6));
  if (model.showCall) {
    caps(screen, fui::makeRect(g.right.x, callTop, g.right.width, rowH), "SIDE CALL", toybox::kSmallFont,
         fui::TextAlign::Center);
    // The figure is the point this round actually paid, so the practice round --
    // which pays nothing, however the call went -- shows no figure rather than a
    // +1 the total above it does not contain.
    const char* callLine = miss == 0            ? "NOT NEEDED"
                           : model.practice     ? (model.callWasRight ? "RIGHT" : "WRONG")
                           : model.callWasRight ? "RIGHT  +1"
                                                : "WRONG  +0";
    caps(screen, fui::makeRect(g.right.x, static_cast<int16_t>(callTop + rowH), g.right.width, rowH), callLine,
         toybox::kSmallFont, fui::TextAlign::Center);
  }

  // Two lines, not one. "ROUND 5   15 POINTS" measures 228px in a 224px column
  // and the S was being clipped off POINTS -- four pixels, and it read as a typo
  // rather than as a layout fault.
  char roundLine[20];
  char pointsLine[20];
  snprintf(roundLine, sizeof(roundLine), "ROUND %d", model.roundNumber);
  snprintf(pointsLine, sizeof(pointsLine), "%d POINT%s", model.total, model.total == 1 ? "" : "S");
  const int16_t totalsTop = static_cast<int16_t>(callTop + 2 * rowH + 10);
  if (model.practice) {
    // The round designed to teach the scoring says what it is, in the same
    // words and the same order as HOW TO PLAY and the pause screen. The heading
    // takes two lines because this column is 224px wide and the phrase is 261.
    static const char* kScore[][2] = {{"EXACT", "5"}, {"ONE OFF", "3"}, {"TWO OFF", "1"}, {"FURTHER OFF", "0"}};
    caps(screen, fui::makeRect(g.right.x, totalsTop, g.right.width, rowH), "POINTS FOR", toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(g.right.x, static_cast<int16_t>(totalsTop + rowH), g.right.width, rowH), "THE ROUND",
         toybox::kSmallFont, fui::TextAlign::Left);
    for (size_t i = 0; i < sizeof(kScore) / sizeof(kScore[0]); ++i) {
      const int16_t y = static_cast<int16_t>(totalsTop + (i + 2) * rowH);
      caps(screen, fui::makeRect(g.right.x, y, g.right.width, rowH), kScore[i][0], toybox::kSmallFont,
           fui::TextAlign::Left);
      caps(screen, fui::makeRect(g.right.x, y, g.right.width, rowH), kScore[i][1], toybox::kSmallFont,
           fui::TextAlign::Right);
    }
  } else {
    caps(screen, fui::makeRect(g.right.x, totalsTop, g.right.width, rowH), roundLine, toybox::kSmallFont,
         fui::TextAlign::Center);
    caps(screen, fui::makeRect(g.right.x, static_cast<int16_t>(totalsTop + rowH), g.right.width, rowH), pointsLine,
         toybox::kSmallFont, fui::TextAlign::Center);
  }

  action(screen, fui::makeRect(toybox::kMargin, 640, inner, 62), "NEXT ROUND", ActionNextRound);
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
  static const char* kLabels[wavelength::kBucketCount] = {"0", "1", "2", "3-5", "6+"};
  toybox::bracket(screen, box, 14, toybox::kFrame);

  const int16_t rowH = static_cast<int16_t>((box.height - 20) / wavelength::kBucketCount);
  const int16_t markW = 13;
  const int16_t gap = 6;
  const int16_t left = static_cast<int16_t>(box.x + 70);
  const int16_t room = static_cast<int16_t>(box.x + box.width - 66 - left);
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
      if (peak <= maxMarks) {
        marks = record.buckets[i];
      } else {
        marks = (record.buckets[i] * maxMarks + peak - 1) / peak;
        if (record.buckets[i] > 0 && marks == 0) marks = 1;
      }
    }
    for (int m = 0; m < marks; ++m) {
      const fui::Rect mark = fui::makeRect(static_cast<int16_t>(left + m * (markW + gap)),
                                           static_cast<int16_t>(y + rowH / 2 - markW / 2), markW, markW);
      // ONE MATERIAL FOR EVERY ROW. The 6+ row used to be dithered, on the
      // argument that it is the row you want to be short and solid ink would
      // make it the loudest thing here. What it actually produced was a chart
      // with two fills and no legend: a cold tester guessed at the rule twice
      // and was wrong twice, and the faintest marks on the page were the row
      // that says most about how the table is doing.
      fill(screen, mark);
    }
    char count[8];
    snprintf(count, sizeof(count), "%u", static_cast<unsigned>(record.buckets[i]));
    caps(screen, fui::makeRect(static_cast<int16_t>(box.x + box.width - 60), y, 46, rowH), count, toybox::kSmallFont,
         fui::TextAlign::Right);
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

  // The two roles are NAMED here and named the same everywhere else, the range
  // of the number is stated, and the three facts a table cannot start without
  // are on the screen: how many play, that the score is one score, and that the
  // device moves. None of that was written down anywhere in the app.
  static const char* kLines[] = {
      "THE CLUE-GIVER SEES A HIDDEN",
      "NUMBER FROM 1 TO 20, AND SAYS",
      "ONE CLUE THAT SITS THERE.",
      "",
      "THE GUESSERS MOVE THE GUESS,",
      "AND LOCK IT IN.",
      "",
      "3 OR MORE PLAYERS. ONE SCORE",
      "FOR THE WHOLE TABLE. THE DEVICE",
      "PASSES LEFT EACH ROUND.",
      "ROUND ONE IS PRACTICE.",
      "",
      "GUESS WHERE THEIR WORDS SIT, NOT",
      "WHAT THEY REALLY THINK.",
  };
  const int16_t lineH = toybox::kButtonCut.lineHeight;
  for (int i = 0; i < static_cast<int>(sizeof(kLines) / sizeof(kLines[0])); ++i) {
    if (!kLines[i][0]) continue;
    caps(screen, fui::makeRect(toybox::kMargin, static_cast<int16_t>(104 + i * lineH), inner, lineH), kLines[i],
         toybox::kSmallFont, fui::TextAlign::Left);
  }

  const int16_t tableTop = 529;
  fill(screen, fui::makeRect(toybox::kMargin, static_cast<int16_t>(tableTop - 14), inner, toybox::kRule));
  // Headed, and one shape of figure per column: five rows of bare words and
  // numbers under a rule are a list of something, and nobody could tell what.
  static const char* kScore[][2] = {
      {"EXACT", "5"},
      {"ONE OFF", "3"},
      {"TWO OFF", "1"},
      {"FURTHER OFF", "0"},
  };
  caps(screen, fui::makeRect(toybox::kMargin, tableTop, inner, lineH), "POINTS FOR THE ROUND", toybox::kSmallFont,
       fui::TextAlign::Left);
  for (size_t i = 0; i < sizeof(kScore) / sizeof(kScore[0]); ++i) {
    const int16_t y = static_cast<int16_t>(tableTop + (i + 1) * lineH);
    caps(screen, fui::makeRect(toybox::kMargin, y, inner, lineH), kScore[i][0], toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, y, inner, lineH), kScore[i][1], toybox::kSmallFont,
         fui::TextAlign::Right);
  }

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
  //
  // And it is the GAME'S NAME whatever the session is doing. CARRY ON took the
  // title slot the moment a round had been played, so the app you had opened
  // stopped naming itself and the state was said twice: once where the name
  // belongs and again in the row below.
  caps(screen, headline, "WAVELENGTH", toybox::kBodyFont, fui::TextAlign::Left);
  screen.frame().hit(fui::makeRect(headline.x, headline.y, headline.width, static_cast<int16_t>(headline.height + 40)),
                     ActionStartRound);

  // The session said ONCE, in the summary's own words and counted the summary's
  // own way. ROUND 7, 8 POINTS here against 8 POINTS IN 5 ROUNDS one tap away
  // described the same evening with two different numbers, because this one
  // counted the round about to start and that one excluded the practice round.
  // The round about to start is on the button now, where it is an instruction
  // rather than a total.
  char state[48];
  if (model.sessionInProgress) {
    snprintf(state, sizeof(state), "%d POINT%s IN %d ROUND%s", model.sessionTotal, model.sessionTotal == 1 ? "" : "S",
             model.sessionScored, model.sessionScored == 1 ? "" : "S");
    caps(screen, fui::makeRect(toybox::kMargin, 150, inner, toybox::kButtonCut.lineHeight), "THIS SESSION",
         toybox::kSmallFont, fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 150, inner, toybox::kButtonCut.lineHeight), state, toybox::kSmallFont,
         fui::TextAlign::Right);
  } else {
    caps(screen, fui::makeRect(toybox::kMargin, 150, inner, toybox::kButtonCut.lineHeight),
         "NO SESSION RUNNING. TAP START.", toybox::kSmallFont, fui::TextAlign::Left);
  }

  fill(screen, fui::makeRect(toybox::kMargin, 190, inner, toybox::kRule));

  // Two labelled rows rather than one line carrying three numbers and an
  // abbreviation. ALL TIME  5 ROUNDS  1.6 PTS/ROUND was one string in a
  // 24-character buffer, so the D of ROUND was cut off by the buffer rather
  // than by the panel -- and even whole, nothing said which number was which.
  char rounds[12];
  char perRound[12];
  snprintf(rounds, sizeof(rounds), "%d", rec.rounds);
  snprintf(perRound, sizeof(perRound), "%d.%d", rec.averageTenths() / 10, rec.averageTenths() % 10);
  caps(screen, fui::makeRect(toybox::kMargin, 200, inner, toybox::kButtonCut.lineHeight), "ROUNDS ALL TIME",
       toybox::kSmallFont, fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 200, inner, toybox::kButtonCut.lineHeight), rounds, toybox::kSmallFont,
       fui::TextAlign::Right);
  caps(screen, fui::makeRect(toybox::kMargin, 229, inner, toybox::kButtonCut.lineHeight), "POINTS PER ROUND",
       toybox::kSmallFont, fui::TextAlign::Left);
  // An average over no rounds is not 0.0, it is nothing, and 0.0 is the one
  // reading that says the table is bad at this. Same rule the score sheet
  // follows one tap away.
  caps(screen, fui::makeRect(toybox::kMargin, 229, inner, toybox::kButtonCut.lineHeight),
       rec.rounds > 0 ? perRound : "--", toybox::kSmallFont, fui::TextAlign::Right);

  // The chart has two columns and used to name neither: rows of marks, a
  // number beside them, and no unit on either. Distances down the left, the
  // rounds that landed there down the right.
  caps(screen, fui::makeRect(toybox::kMargin, 262, inner, toybox::kButtonCut.lineHeight), "HOW FAR OFF, ALL TIME",
       toybox::kSmallFont, fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 262, inner, toybox::kButtonCut.lineHeight), "ROUNDS", toybox::kSmallFont,
       fui::TextAlign::Right);
  ornament(screen, fui::makeRect(toybox::kMargin, 296, inner, 210), rec);

  char play[24];
  snprintf(play, sizeof(play), "PLAY ROUND %d", model.sessionRound);
  action(screen, fui::makeRect(toybox::kMargin, 530, inner, 66), model.sessionInProgress ? play : "START",
         ActionStartRound);
  action(screen, fui::makeRect(toybox::kMargin, 612, inner, 54), "HOW TO PLAY", ActionHowTo);
  // A control that cannot act dims rather than disappearing, so the layout does
  // not jump and you can still see what it would have done.
  //
  // FINISH AND SEE THE SCORE did not finish anything. It opens the score
  // sheet, whose own first button walks straight back into the next round with
  // the session intact -- so the word FINISH described neither the button nor
  // the screen it opens, and the only control that actually ends a session was
  // the one labelled START OVER. Say what it does: it shows the score, and the
  // evening carries on.
  fui::ButtonProps end;
  end.label = "SEE THE SCORE SO FAR";
  end.text = toybox::buttonText(screen.theme());
  end.action = model.sessionInProgress ? static_cast<fui::ActionId>(ActionEndSession) : fui::NO_ACTION;
  if (!model.sessionInProgress) end.styles = toybox::disabledStepperStyles();
  screen.button(end, fui::makeRect(toybox::kMargin, 674, inner, 54));
}

void renderSummary(toybox::Screen& screen, const SummaryModel& model) {
  toybox::absoluteChrome(screen);
  const int16_t w = screen.device().screen().width;
  const int16_t inner = static_cast<int16_t>(w - 2 * toybox::kMargin);
  const int16_t lineH = toybox::kButtonCut.lineHeight;
  const wavelength::Record blank;
  const wavelength::Record& rec = model.record ? *model.record : blank;

  char total[12];
  snprintf(total, sizeof(total), "%d", model.total);
  caps(screen, fui::makeRect(toybox::kMargin, 14, inner, toybox::kHugeCut.lineHeight), total, toybox::kDisplayFont,
       fui::TextAlign::Left);

  // "POINTS IN 11 ROUNDS" measures 498px of 448 at the display cut and was cut
  // mid-word. Split, which also gives the number the hierarchy it deserves.
  char line[48];
  snprintf(line, sizeof(line), "IN %d SCORED ROUND%s", model.rounds, model.rounds == 1 ? "" : "S");
  caps(screen, fui::makeRect(toybox::kMargin, 150, inner, toybox::kDisplayCut.lineHeight),
       model.total == 1 ? "POINT" : "POINTS", toybox::kBodyFont, fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 214, inner, lineH), model.rounds > 0 ? line : "NO SCORED ROUNDS YET",
       toybox::kSmallFont, fui::TextAlign::Left);

  if (model.abandoned > 0) {
    char ab[40];
    snprintf(ab, sizeof(ab), "%d ABANDONED", model.abandoned);
    caps(screen, fui::makeRect(toybox::kMargin, 214, inner, lineH), ab, toybox::kSmallFont, fui::TextAlign::Right);
  }

  fill(screen, fui::makeRect(toybox::kMargin, 252, inner, toybox::kRule));

  // The average sits next to what a table that is genuinely communicating
  // gets. A number with nothing beside it means nothing: nobody can tell
  // whether 19 points is good.
  //
  // And the pair is HEADED, because 2.5 with no unit is not a benchmark, it is
  // a loose number: A GOOD TABLE 2.5 gave no clue whether that was points,
  // slots or rounds, and nothing anywhere else on the device said either.
  if (model.rounds > 0) {
    char avg[16];
    char good[16];
    snprintf(avg, sizeof(avg), "%d.%d", model.averageTenths / 10, model.averageTenths % 10);
    snprintf(good, sizeof(good), "%d.%d", wavelength::kGoodTableTenths / 10, wavelength::kGoodTableTenths % 10);
    caps(screen, fui::makeRect(toybox::kMargin, 262, inner, lineH), "POINTS PER ROUND", toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 296, inner, toybox::kDisplayCut.lineHeight), "THIS SESSION",
         toybox::kSmallFont, fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 296, inner, toybox::kDisplayCut.lineHeight), avg, toybox::kBodyFont,
         fui::TextAlign::Right);
    caps(screen, fui::makeRect(toybox::kMargin, 364, inner, lineH), "A GOOD TABLE", toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 364, inner, lineH), good, toybox::kSmallFont, fui::TextAlign::Right);
  } else {
    // A table that has played only the practice round was being scored 0.0
    // against a benchmark of 2.5 -- an average over no rounds, presented as a
    // verdict on the one round the game itself refuses to count.
    caps(screen, fui::makeRect(toybox::kMargin, 262, inner, lineH), "ONLY THE PRACTICE ROUND SO FAR,",
         toybox::kSmallFont, fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 296, inner, lineH), "AND IT DOES NOT SCORE.", toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 340, inner, lineH), "PLAY ONE THAT DOES AND THE", toybox::kSmallFont,
         fui::TextAlign::Left);
    caps(screen, fui::makeRect(toybox::kMargin, 369, inner, lineH), "AVERAGE APPEARS HERE.", toybox::kSmallFont,
         fui::TextAlign::Left);
  }

  // ALL TIME, said on the chart itself. Sitting under a THIS SESSION heading it
  // was read as this session's, and a player counted the tally marks to catch
  // it: 0 POINTS IN 0 ROUNDS above thirteen marks. Both columns are named for
  // the same reason: distance down the left, rounds down the right.
  caps(screen, fui::makeRect(toybox::kMargin, 404, inner, lineH), "HOW FAR OFF, ALL TIME", toybox::kSmallFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 404, inner, lineH), "ROUNDS", toybox::kSmallFont, fui::TextAlign::Right);
  // 168 is a FLOOR, not a preference. ornament() gives each bucket
  // (height - 20) / kBucketCount, which at 168 is 29 -- exactly the button
  // cut's line height, so the row labels have zero slack. A sixth bucket or a
  // larger cut collides here silently; give it more height, not less.
  ornament(screen, fui::makeRect(toybox::kMargin, 434, inner, 168), rec);

  // This screen is a look at the score, not the end of anything, so its first
  // button says where it actually goes -- and says it in the front door's own
  // words, because BACK TO THE GAME described a return and this is the next
  // round starting.
  char play[24];
  snprintf(play, sizeof(play), "PLAY ROUND %d", model.nextRound);
  action(screen, fui::makeRect(toybox::kMargin, 612, inner, 58), play, ActionKeepPlaying);

  // The only control in the app that ends a session, and it used to be called
  // START OVER -- a name for beginning something, on the button that ends it,
  // with the cost in a parenthesis nobody reads. It says what it does now, the
  // two lines above it say what that costs and what it does not, and it is
  // drawn as an outline rather than as a third solid bar the thumb finds by
  // reflex. Same shape the pause screen uses for ABANDON THIS ROUND.
  caps(screen, fui::makeRect(toybox::kMargin, 676, inner, lineH), "ENDING CLEARS THIS SCORE.", toybox::kSmallFont,
       fui::TextAlign::Left);
  caps(screen, fui::makeRect(toybox::kMargin, 705, inner, lineH), "THE ALL-TIME RECORD IS KEPT.", toybox::kSmallFont,
       fui::TextAlign::Left);
  endingAction(screen, fui::makeRect(toybox::kMargin, 736, inner, 54), "END THE SESSION", ActionNewSession);
}

}  // namespace wavelengthui
