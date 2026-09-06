#include "XkcdScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"

namespace xkcdui {
namespace {

// The reader's bar. Sized to hold the comic number, a title, the rail and the
// ALT control on one line, and no taller: every pixel here is a pixel of comic
// that has to be panned instead of seen.
constexpr int16_t kBarHeight = 44;

// The map at the right-hand end of the bar, and the OK mark beside it that
// says the Confirm button has something to do. Sized so a long title still
// gets most of the bar: the old rail took a fifth of the panel and cut
// "Paleontology" to "Paleon".
constexpr int16_t kMapWidth = 40;
constexpr int16_t kMapHeight = 26;
constexpr int16_t kMapMinSide = 6;
constexpr int16_t kOkWidth = 56;

// The top of any body: below the header band and the rule Toybox draws under
// it. Shared by every screen here so they line up with each other AND with the
// shelf the reader just came from -- which, until card 358, they did not.
//
// Two faults, both closed here. The row is measured from the whole chrome (band
// + gap + rule), which is what toybox::kBodyTop derives from chromeBelow();
// kHeaderBand is the BAND's height, which is what the theme token wants and not
// what a body top wants. And every site below used to add safe.y to it as well,
// which put the whole app ten pixels under the rest of the fork: the chrome is
// pinned at panel row 0 by absoluteChrome, so it already covers the rows the
// glass hides. toybox::kBodyTop carries the argument in full.
constexpr int16_t kBodyTop = toybox::kBodyTop;

// The menu's bands, laid out once. Both the builder and the Activity need the
// mosaic's rect, and two functions that compute it separately are the defect
// this fork has hit more than any other -- the first version put the mosaic
// 118px below a body top that no longer existed, and it drew straight through
// the record line.
struct MenuBands {
  fui::Rect headline;
  fui::Rect title;
  fui::Rect rule;
  fui::Rect record;
  fui::Rect mosaic;
  fui::Rect doors;
};

// Four of them, and in portrait they are full-width rows rather than a strip
// of four narrow buttons: at 480 wide, four across leaves 103px a button and
// "GO TO NUMBER" has nowhere to go. docs/design-language.md calls for
// bottom-anchored rows anyway.
constexpr int kDoorCount = 4;
constexpr int16_t kDoorGap = 8;

MenuBands menuBands(const fui::DeviceContext& device) {
  // Sides and bottom from the safe rect, so the bands clear the columns the
  // glass hides. The TOP is absolute: kBodyTop is measured from panel row 0
  // and the header band already paints over the covered rows, so adding safe.y
  // here only pushed the menu below every other app's body. See
  // toybox::kBodyTop.
  const fui::Rect safe = device.safeRect();
  const int16_t left = static_cast<int16_t>(safe.x + toybox::kMargin);
  const int16_t width = static_cast<int16_t>(safe.width - toybox::kMargin * 2);

  MenuBands b;
  int16_t y = kBodyTop;
  b.headline = fui::makeRect(left, y, width, 44);
  y = static_cast<int16_t>(y + 44 + 2);
  b.title = fui::makeRect(left, y, width, 28);
  y = static_cast<int16_t>(y + 28 + toybox::kGutter);
  b.rule = fui::makeRect(left, y, width, toybox::kRule);
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter);
  b.record = fui::makeRect(left, y, width, 24);
  y = static_cast<int16_t>(y + 24 + toybox::kGutter * 2);

  const int16_t doorsHeight = static_cast<int16_t>(kDoorCount * toybox::kPillHeight + (kDoorCount - 1) * kDoorGap);
  b.doors =
      fui::makeRect(left, static_cast<int16_t>(safe.bottom() - toybox::kMargin - doorsHeight), width, doorsHeight);
  b.mosaic = fui::makeRect(left, y, width, static_cast<int16_t>(b.doors.y - toybox::kGutter - y));
  return b;
}

// Header band, rule, and the page margin. Every screen except the reader
// opens with this.
//
// `rightLabel` is drawn in paper, not ink. The band is solid black and
// Screen::header() resolves the subtitle style from the theme, whose colour is
// Black -- so a label left at the default is painted black on black and simply
// is not there, indistinguishable from never having been set. That is how the
// Hacker News page indicator went missing through two renders.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
  }
  toybox::headerBand(screen, header);

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A style the components will treat as owned. A style whose font is
// FONT_SLOT_SMALL and whose every other field is default reads as *unset*, so
// the theme quietly puts its own back and the label returns at full size --
// which looks exactly like the assignment never happened. Naming one more
// field (the alignment the component was going to apply anyway) is what makes
// it stick. See docs/building-apps.md.
fui::TextStyle owned(fui::TextStyle style, fui::TextAlign align) {
  style.align = align;
  return style;
}

// A button that cannot act right now. `enabled = false` alone is not enough:
// the theme's button StyleSet sets `styles.disabled = styles.normal`, so a
// disabled control draws identically to a live one and the only way to find
// out is to tap it. Toybox ships the dithered treatment; it has to be asked
// for. docs/design-language.md: a control that cannot act dims, and the dither
// goes in the fill because there is no grey text on this device.
void addButton(toybox::Screen& screen, fui::ButtonProps props, const fui::Rect& where) {
  if (!props.enabled) props.styles = toybox::disabledButtonStyles();
  screen.button(props, where);
}

// Truncate to fit, with an **ASCII** ellipsis.
//
// The components truncate for you, but they do it with U+2026, and the Toybox
// faces are subset to U+0020-007E. A glyph the font lacks draws as nothing at
// all, so a cut title ends in a stray mark: the reader bar showed
// "#1732  Earth Tempe '" against the real archive. Three dots are in the
// subset, so the cut is done here instead and the component is never given
// anything long enough to trim.
//
// Worth knowing beyond this app: *every* component truncation in the fork has
// this defect, because the ellipsis is not in the font. Adding U+2026 to
// tools_local/toybox/gen_toybox_fonts.sh would fix the class, at the cost of
// regenerating shared font headers.

// The theme's title style is built for the header band, which is solid black,
// so its colour is White. Drawn on paper it is white on white: invisible, and
// indistinguishable from the code never having run. That is the black-on-black
// header defect this fork has already hit twice, in reverse. Anything using a
// chrome style away from the band has to say what colour it is.
fui::TextStyle onPaper(fui::TextStyle style, fui::TextAlign align) {
  style.align = align;
  style.color = fui::Color::Black;
  return style;
}

}  // namespace

// --- The front door ------------------------------------------------------

fui::Rect menuMosaicBand(const fui::DeviceContext& device) { return menuBands(device).mosaic; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "XKCD");

  const MenuBands bands = menuBands(screen.device());

  // The headline is the newest comic on the card, and it is also the hit
  // target for the most common action -- so the commonest tap is on the
  // largest thing on the screen and needs no button at all.
  char headline[64];
  if (model.hasArchive && model.latestNum > 0) {
    snprintf(headline, sizeof(headline), "#%u", static_cast<unsigned>(model.latestNum));
  } else {
    snprintf(headline, sizeof(headline), "NO ARCHIVE");
  }
  screen.target().text(toybox::inkCentred(bands.headline, toybox::kDisplayCut), headline,
                       onPaper(screen.theme().titleText, fui::TextAlign::Left));

  // Short enough to FIT. "The archive, one download away" overflowed this band
  // and truncated to "The archive, one download aw" -- with no ellipsis, because
  // the truncation glyph U+2026 has no bitmap in this face, so the sentence just
  // stopped. Every comic title that lands here is bounded by the same band; only
  // the empty state's own copy is ours to keep short.
  screen.target().text(bands.title, model.hasArchive ? model.latestTitle : "One download away",
                       onPaper(screen.theme().bodyText, fui::TextAlign::Left));

  if (model.hasArchive) {
    // One target spanning both lines, derived from the two rects that drew
    // them rather than recomputed -- the headline and its title are one door.
    screen.frame().hit(fui::makeRect(bands.headline.x, bands.headline.y, bands.headline.width,
                                     static_cast<int16_t>(bands.title.bottom() - bands.headline.y)),
                       ActionOpenLatest);
  }

  screen.target().fill(bands.rule, fui::Paint::solid(fui::Color::Black));

  char record[96];
  if (!model.hasArchive) {
    // Same band, same overflow: this read "GET THE COMICS needs only" on the
    // panel, and the missing word was the one the sentence was about.
    snprintf(record, sizeof(record), "GET THE COMICS needs WiFi");
  } else if (model.waiting > 0) {
    snprintf(record, sizeof(record), "%d COMICS   %d READ   %d WAITING", model.comicCount, model.readCount,
             model.waiting);
  } else {
    snprintf(record, sizeof(record), "%d COMICS   %d READ", model.comicCount, model.readCount);
  }
  // The reading cut for an all-caps count line: safe for inkCentred, which the
  // mixed-case title above it is not.
  screen.target().text(model.hasArchive ? toybox::inkCentred(bands.record, toybox::kReadingCut) : bands.record, record,
                       onPaper(screen.theme().smallText, fui::TextAlign::Left));

  // The mosaic band is left for the Activity, which draws it from the index.

  // The lesser doors, bottom-anchored: that is where a thumb rests, and it
  // keeps them from competing with the headline.
  struct Door {
    const char* label;
    fui::ActionId action;
    bool enabled;
  };
  const Door doors[kDoorCount] = {
      {"BROWSE", ActionBrowse, model.hasArchive},
      {"GO TO NUMBER", ActionGoToNumber, model.hasArchive},
      {"RANDOM", ActionRandom, model.hasArchive},
      // With no archive this same door is the whole first run, so it says
      // what it will actually do there: fetch the pack, not update one.
      {model.hasArchive ? "UPDATE" : "GET THE COMICS", ActionUpdate, true},
  };

  for (int i = 0; i < kDoorCount; ++i) {
    fui::ButtonProps props;
    props.label = doors[i].label;
    props.action = doors[i].action;
    // A control that cannot act dims rather than disappears: a button that
    // vanishes takes its space with it, the layout jumps, and you cannot tell
    // whether the action is unavailable or whether you misremembered the
    // screen. UPDATE stays live with no archive, because it is the way out.
    props.enabled = doors[i].enabled;
    addButton(screen, props,
              fui::makeRect(bands.doors.x, static_cast<int16_t>(bands.doors.y + i * (toybox::kPillHeight + kDoorGap)),
                            bands.doors.width, toybox::kPillHeight));
  }
}

// --- Going to a number ---------------------------------------------------

void buildNumber(toybox::Screen& screen, const NumberModel& model) {
  chrome(screen, "GO TO NUMBER");

  const fui::Rect safe = screen.frame().safeRect();
  const int16_t left = static_cast<int16_t>(safe.x + toybox::kMargin);
  const int16_t width = static_cast<int16_t>(safe.width - toybox::kMargin * 2);
  // Absolute, like every other app's: see toybox::kBodyTop.
  const int16_t bodyTop = kBodyTop;

  // What has been typed, big, with the range under it so the bounds are a
  // thing you can read rather than a thing you discover by being refused.
  const bool empty = model.typed == nullptr || model.typed[0] == '\0';
  char shown[16];
  snprintf(shown, sizeof(shown), "#%s", empty ? "" : model.typed);
  screen.target().text(fui::makeRect(left, bodyTop, width, 56), shown,
                       onPaper(screen.theme().titleText, fui::TextAlign::Center));

  // The pack's real span, not 1..max. A pack can be built from any slice of
  // the archive, so "1 to 460" over a pack that starts at 300 invites typing a
  // number that is not there and then dims GO with no explanation.
  char range[48];
  snprintf(range, sizeof(range), "%u to %u", static_cast<unsigned>(model.firstNum),
           static_cast<unsigned>(model.maxNum));
  screen.target().text(fui::makeRect(left, static_cast<int16_t>(bodyTop + 58), width, 24), range,
                       onPaper(screen.theme().smallText, fui::TextAlign::Center));

  // Ten digits, three to a row, with back and go on the last. Sized from the
  // panel rather than fixed, so the pad fills the width it is given.
  constexpr int16_t kPadGap = 10;
  const int16_t keyW = static_cast<int16_t>((width - kPadGap * 2) / 3);
  const int16_t keyH = 64;
  const int16_t padTop = static_cast<int16_t>(bodyTop + 100);

  const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "GO"};
  for (int i = 0; i < 12; ++i) {
    const int row = i / 3;
    const int col = i % 3;
    const fui::Rect where = fui::makeRect(static_cast<int16_t>(left + col * (keyW + kPadGap)),
                                          static_cast<int16_t>(padTop + row * (keyH + kPadGap)), keyW, keyH);
    fui::ButtonProps props;
    props.label = keys[i];
    if (i == 9) {
      props.action = ActionBackspace;
      props.enabled = !empty;
    } else if (i == 11) {
      props.action = ActionGo;
      // Dimmed rather than absent when the number is not on the card, so the
      // control still says what it would do.
      props.enabled = model.valid;
    } else {
      props.action = ActionDigit;
      // The digit rides on the action's value, so twelve keys cost one action
      // and not twelve. `0` is the eleventh key and its own digit.
      props.value = static_cast<int16_t>(i == 10 ? 0 : i + 1);
    }
    addButton(screen, props, where);
  }
}

// --- Browsing ------------------------------------------------------------

fui::Rect listBand(const fui::DeviceContext& device) {
  const fui::Rect safe = device.safeRect();
  // Absolute, like every other app's: see toybox::kBodyTop.
  const int16_t top = kBodyTop;
  const int16_t bottom = static_cast<int16_t>(safe.bottom() - toybox::kMargin - toybox::kPillHeight - toybox::kGutter);
  return fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), top,
                       static_cast<int16_t>(safe.width - toybox::kMargin * 2), static_cast<int16_t>(bottom - top));
}

void buildList(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, model.title, model.rightLabel);

  const fui::Rect band = listBand(screen.device());
  const fui::Rect panel = screen.device().screen();

  fui::ListProps props;
  props.items = model.items;
  props.count = static_cast<uint16_t>(model.count < 0 ? 0 : model.count);
  props.selectedIndex = static_cast<int16_t>(model.selected);
  props.action = ActionOpenComic;
  // A list row's title band is one line tall the moment it has a subtitle, so
  // a wrapping label would be drawn straight through the subtitle underneath
  // it. The date goes in the value slot, which sits beside the label.
  props.labelText = owned(screen.theme().bodyText, fui::TextAlign::Left);
  props.valueText = owned(screen.theme().smallText, fui::TextAlign::Right);

  // The content rect already carries the page margin; listInset is applied on
  // top of it, so leaving it non-zero indents every row twice. Margins are the
  // band expressed as absolute screen-frame insets, so the two cannot drift.
  screen.setContentMarginAbsolute(fui::Insets{band.y, static_cast<int16_t>(panel.width - band.right()),
                                              static_cast<int16_t>(panel.height - band.bottom()), band.x});
  screen.list(props, band.height);

  // Paging, bottom-anchored beside each other. Older is on the left because
  // the list runs newest-first, so left is back the way you came.
  const fui::Rect safe = screen.frame().safeRect();
  const int16_t pageWidth = 150;
  const int16_t pageY = static_cast<int16_t>(safe.bottom() - toybox::kMargin - toybox::kPillHeight);

  fui::ButtonProps older;
  older.label = "OLDER";
  older.action = ActionPageOlder;
  older.enabled = model.canPageOlder;
  addButton(screen, older,
            fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin), pageY, pageWidth, toybox::kPillHeight));

  fui::ButtonProps newer;
  newer.label = "NEWER";
  newer.action = ActionPageNewer;
  newer.enabled = model.canPageNewer;
  addButton(screen, newer,
            fui::makeRect(static_cast<int16_t>(safe.right() - toybox::kMargin - pageWidth), pageY, pageWidth,
                          toybox::kPillHeight));
}

// --- The reader ----------------------------------------------------------

fui::Rect readerViewport(const fui::DeviceContext& device) {
  // From the safe rect, not the panel: the bezel covers the top rows and edge
  // columns, and a comic's own title often lives in its first pixels. The bar
  // keeps the true bottom edge. The fui adapter fills device.safeArea; a
  // context without it (what the host tests construct) gets the old
  // full-bleed viewport.
  const fui::Rect safe = device.safeRect();
  const fui::Rect panel = device.screen();
  return fui::makeRect(safe.x, safe.y, safe.width, static_cast<int16_t>(panel.height - kBarHeight - safe.y));
}

fui::Rect readerPanUpHalf(const fui::DeviceContext& device) {
  const fui::Rect view = readerViewport(device);
  return fui::makeRect(view.x, view.y, view.width, static_cast<int16_t>(view.height / 2));
}

fui::Rect readerPanDownHalf(const fui::DeviceContext& device) {
  const fui::Rect view = readerViewport(device);
  const int16_t half = static_cast<int16_t>(view.height / 2);
  return fui::makeRect(view.x, static_cast<int16_t>(view.y + half), view.width,
                       static_cast<int16_t>(view.height - half));
}

// The map's box: the right-hand end of the bar, inset so it does not touch the
// panel edge. Shared with the Activity so the two cannot disagree about where
// it is.
fui::Rect readerMapRect(const fui::DeviceContext& device) {
  const fui::Rect panel = device.screen();
  const int16_t h = kMapHeight;
  const int16_t w = kMapWidth;
  const int16_t y = static_cast<int16_t>(panel.height - kBarHeight + (kBarHeight - h) / 2);
  const int16_t x = static_cast<int16_t>(panel.width - toybox::kGutter * 2 - w);
  return fui::makeRect(x, y, w, h);
}

namespace {

// The comic's own shape, with the part you can see filled in.
//
// Ornament made of the app's own material carrying the app's own data, the
// same rule as the menu's mosaic: the material here is the wild variation in
// comic shape that made this app hard to build at all, and the data is where
// you are in one.
void drawMap(toybox::Screen& screen, const fui::Rect& box, const ReaderModel& model) {
  // Fit the comic's aspect into the box. #1732 is 912x18400, which at its true
  // aspect is under a pixel wide with a sub-pixel "you are here" inside it, so
  // the outline is floored at kMapMinSide. What that turns into for a very
  // tall comic is a vertical bar with a block in it -- which is the affordance
  // a comic that long wants anyway, so the degradation runs the right way.
  int w = box.width;
  int h = box.height;
  if (model.imageW > 0 && model.imageH > 0) {
    if (static_cast<int64_t>(model.imageW) * box.height > static_cast<int64_t>(model.imageH) * box.width) {
      h = static_cast<int>(static_cast<int64_t>(box.width) * model.imageH / model.imageW);
    } else {
      w = static_cast<int>(static_cast<int64_t>(box.height) * model.imageW / model.imageH);
    }
  }
  if (w < kMapMinSide) w = kMapMinSide;
  if (h < kMapMinSide) h = kMapMinSide;
  if (w > box.width) w = box.width;
  if (h > box.height) h = box.height;

  const int16_t ox = static_cast<int16_t>(box.x + box.width - w);
  const int16_t oy = static_cast<int16_t>(box.y + (box.height - h) / 2);
  const fui::Rect outline = fui::makeRect(ox, oy, static_cast<int16_t>(w), static_cast<int16_t>(h));

  // Outline first, then the visible part knocked in solid. On the black bar
  // that reads as "this much exists, and this much of it is on your screen".
  screen.target().stroke(outline, fui::Paint::solid(fui::Color::White), 1);

  if (model.imageW <= 0 || model.imageH <= 0) return;
  auto span = [](int start, int extent, int total, int into) {
    if (total <= 0) return 0;
    int v = static_cast<int>(static_cast<int64_t>(extent) * into / total);
    (void)start;
    return v;
  };
  int vw = span(model.viewX, model.viewW, model.imageW, w);
  int vh = span(model.viewY, model.viewH, model.imageH, h);
  int vx = span(0, model.viewX, model.imageW, w);
  int vy = span(0, model.viewY, model.imageH, h);
  // A view that rounds to nothing still has to be visible: the block is the
  // only part of this that moves.
  if (vw < 2) vw = 2;
  if (vh < 2) vh = 2;
  if (vx + vw > w) vx = w - vw;
  if (vy + vh > h) vy = h - vh;
  if (vx < 0) vx = 0;
  if (vy < 0) vy = 0;

  screen.target().fill(fui::makeRect(static_cast<int16_t>(ox + vx), static_cast<int16_t>(oy + vy),
                                     static_cast<int16_t>(vw), static_cast<int16_t>(vh)),
                       fui::Paint::solid(fui::Color::White));
}

}  // namespace

void buildReaderBar(toybox::Screen& screen, const ReaderModel& model) {
  const fui::Rect panel = screen.device().screen();
  const fui::Rect bar = fui::makeRect(0, static_cast<int16_t>(panel.height - kBarHeight), panel.width, kBarHeight);

  // A solid black bar that never repaints while you pan costs nothing on
  // e-ink and cannot ghost, which is the one rule in docs/design-language.md.
  screen.target().fill(bar, fui::Paint::solid(fui::Color::Black));

  char left[80];
  snprintf(left, sizeof(left), "#%u  %s", static_cast<unsigned>(model.num), model.title);

  fui::TextStyle label = owned(screen.theme().smallText, fui::TextAlign::Left);
  label.color = fui::Color::White;

  // The label gets what the map and the OK mark do not need, bounded by what
  // it must not touch. Handing a long title the whole bar was the defect the
  // shelf's player row had: the component centres across the whole rect, so at
  // the widest value the text runs straight through its neighbours.
  const bool showMap = model.imageW > 0 && model.imageH > 0 &&
                       (model.viewW < model.imageW || model.viewH < model.imageH || model.hasOverview);
  const fui::Rect map = readerMapRect(screen.device());
  const int16_t okWidth = model.hasOverview ? kOkWidth : 0;
  const int16_t reserved = showMap ? static_cast<int16_t>(map.width + okWidth + toybox::kGutter) : 0;
  const int16_t labelWidth = static_cast<int16_t>(panel.width - toybox::kGutter * 3 - reserved);
  // The full bar height, not a guessed 22px band inside it: the target centres
  // text on the font's *line box*, which is taller than the ink, so a short
  // rect pushes the baseline past the bottom of the panel. That drew the title
  // half off the screen and filled the log with out-of-range pixel writes.
  //
  // Fitted by the ladder rather than by an ellipsis. This used to give back one
  // character at a time until the title plus "..." fitted, at the one cut it was
  // handed -- so 936 of the 3279 titles on a full pack reached the bar short,
  // measured, and none of them had been offered a smaller cut first. The bar
  // binds a second one it can step down to; both line boxes clear its 44px, so
  // stepping down costs the bar no height. host-tests/fittedtitle walks every
  // title in the pack and publishes what is left.
  const std::string fitted = toybox::fittedTitle(screen.target(), left, labelWidth, label);
  screen.target().text(fui::makeRect(toybox::kGutter, bar.y, labelWidth, kBarHeight), fitted.c_str(), label);

  if (showMap) drawMap(screen, map, model);

  // The OK mark. Only when there is a closer view, because a control that does
  // nothing is worse than no control: it is the reason the disabled-button
  // finding in this fork mattered at all.
  if (model.hasOverview) {
    fui::TextStyle ok = owned(screen.theme().smallText, fui::TextAlign::Right);
    ok.color = fui::Color::White;
    // It said "OK" while the toggle was on the Confirm button. The X4 Pro has
    // no Confirm button, so naming one was doubly wrong: it pointed at a key
    // that cannot fire, and it read as a key rather than as something to tap.
    // These say what tapping does instead.
    //
    // Two words rather than "+" and "-": **'+' draws as nothing in this face.**
    // The subset claims U+0020-007E but the plus is not in the cut, so it comes
    // out zero-width with no box, no fallback and no log line -- the same
    // defect as the missing ellipsis.
    const int16_t okX = static_cast<int16_t>(map.x - kOkWidth);
    screen.target().text(fui::makeRect(okX, bar.y, kOkWidth, kBarHeight), model.showingWhole ? "READ" : "ALL", ok);
  }

  // **The whole bar is the alt text, except the map, which toggles the
  // overview.** Two rects that do not overlap, so which one wins is not a
  // question about hit-test ordering.
  //
  // The toggle was on the Confirm button, which reads better and costs no
  // comic pixels -- on a device that has that button. **The X4 Pro does not.**
  // Its BoardConfig leaves back/confirm/left/right as unassigned pins, so
  // `InputManager::begin` never configures them and they can never fire; the
  // two side keys and power are the whole set. The simulator synthesises the
  // missing ones, so a button-driven control looks perfectly fine there
  // forever, which is exactly how this shipped. See docs/buttons.md:
  // **pointing is touch, stepping is the two side keys, nothing else is a
  // button.**
  const int16_t toggleX = model.hasOverview ? static_cast<int16_t>(map.x - kOkWidth - toybox::kGutter) : panel.width;
  if (model.hasOverview) {
    screen.frame().hit(fui::makeRect(toggleX, bar.y, static_cast<int16_t>(panel.width - toggleX), kBarHeight),
                       ActionToggleOverview);
  }
  if (model.hasAlt && toggleX > 0) {
    screen.frame().hit(fui::makeRect(0, bar.y, toggleX, kBarHeight), ActionShowAlt);
  }
}

// --- The alt text --------------------------------------------------------

void buildAlt(toybox::Screen& screen, const AltModel& model) {
  char title[64];
  snprintf(title, sizeof(title), "#%u", static_cast<unsigned>(model.num));
  chrome(screen, title);

  fui::TextStyle head = owned(screen.theme().bodyText, fui::TextAlign::Left);
  screen.target().text(screen.takeTop(30, toybox::kGutter), model.title, head);

  fui::TextAreaProps text;
  text.text = model.alt;
  text.style = owned(screen.theme().bodyText, fui::TextAlign::Left);
  text.showCaret = false;
  screen.textArea(text, static_cast<int16_t>(screen.body().height - toybox::kPillHeight - toybox::kGutter));

  const fui::Rect safe = screen.frame().safeRect();
  fui::ButtonProps back;
  back.label = "BACK TO THE COMIC";
  back.action = ActionDismiss;
  screen.button(back, fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin),
                                    static_cast<int16_t>(safe.bottom() - toybox::kMargin - toybox::kPillHeight),
                                    static_cast<int16_t>(safe.width - toybox::kMargin * 2), toybox::kPillHeight));
}

// --- Notices -------------------------------------------------------------

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, model.title);

  // onPaper, not owned: titleText is styled for the black header band and is
  // therefore White. Every notice headline drew white-on-paper -- invisible --
  // until the first-run archive offer made someone look for one.
  fui::TextStyle head = onPaper(screen.theme().titleText, fui::TextAlign::Left);
  screen.target().text(screen.takeTop(44, toybox::kGutter), model.headline, head);

  fui::TextAreaProps detail;
  detail.text = model.detail;
  detail.style = owned(screen.theme().bodyText, fui::TextAlign::Left);
  detail.showCaret = false;
  screen.textArea(detail, static_cast<int16_t>(screen.body().height - toybox::kPillHeight - toybox::kGutter));

  if (model.actionLabel != nullptr) {
    const fui::Rect safe = screen.frame().safeRect();
    fui::ButtonProps act;
    act.label = model.actionLabel;
    act.action = model.action;
    screen.button(act, fui::makeRect(static_cast<int16_t>(safe.x + toybox::kMargin),
                                     static_cast<int16_t>(safe.bottom() - toybox::kMargin - toybox::kPillHeight),
                                     static_cast<int16_t>(safe.width - toybox::kMargin * 2), toybox::kPillHeight));
  }
}

}  // namespace xkcdui
