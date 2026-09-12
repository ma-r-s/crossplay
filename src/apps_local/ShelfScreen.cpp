#include "ShelfScreen.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "player/PlayerAvatar.h"
#include "ui/ToyboxFormat.h"
#include "ui/ToyboxIcons.h"

namespace shelfui {

const char* const kDoneChip = "DONE";

// The chooser's caption, in the band the player bar leaves. The mode is
// legible from the boxes alone once you are looking at them; this is for the
// half second before that, and it is the one place the screen can say that a
// TAP is what changes a row, on a device where a tap usually opens things.
//
// Nineteen characters because the band is 448px at the UI cut and the fuller
// sentence -- TAP A ROW TO SHOW OR HIDE IT -- came back from the simulator as
// "TAP A ROW TO SHOW OR HI...". The renderer ellipsizes and says nothing, and
// the host test that asserted the string was DRAWN passed the whole time: the
// target records what the builder handed it. host-tests/ui measures it now.
const char* const kChoosingCaption = "TAP TO SHOW OR HIDE";

// A folder with nothing on its list. The sentence is what the whole BAND does,
// not what the chip in the corner does: the chip is 400px away at the top of an
// 800px panel, and a caption pointing at a control the reader has not found yet
// is a worse dead end than no caption. So the empty body is itself the way
// back, and the words describe tapping where the eye already is.
const char* const kEmptyHeadline = "NOTHING HERE";
const char* const kEmptyHint = "TAP TO CHOOSE WHAT THIS FOLDER SHOWS";

// The box on a chooser row. A filled slab with the tick knocked out of it, or a
// hairline outline: the page marks below mean exactly that by the same pair, so
// this is the screen's own language rather than a new one. Drawn rather than
// blitted because a 1-bpp mask cannot be both a fill and a knockout, and
// because a hand-drawn TICK is what goes wrong (solitaire's pips, minesweeper's
// flag, three attempts each) -- so the tick is Lucide's and the box is two
// primitives.
constexpr int16_t kBoxSize = toybox::kIconSize;
constexpr int16_t kTickSize = 24;
constexpr uint8_t kBoxRadius = 6;
constexpr int16_t kBoxEdge = 2;

// The footer holds the device's name and is the way into changing it, so the
// list has to stop above it. Shared with the builder, or the paging maths would
// think it has a row's more room than it does and put a row on a page that
// cannot draw it.
//
// A row's height rather than a pill's: it carries a 48px face now, and a pill
// would leave two pixels of air above and below it.
int footerHeight(const bool hasDeviceName) { return hasDeviceName ? toybox::kRowHeight + toybox::kGutter : 0; }

// Shorter than a row on purpose. It carries no text and no face, only marks, and
// a full row of chrome under the player bar would put 148px of furniture along
// the bottom of a screen whose whole job is the list above it.
constexpr int kPageBarHeight = 44;

int pageBarHeight(const bool hasPages) { return hasPages ? kPageBarHeight + toybox::kGutter : 0; }

fui::Rect listBand(const fui::DeviceContext& device, const bool hasDeviceName, const bool hasPages) {
  // From the whole chrome -- band, gap and rule -- not from the band alone,
  // and taken from toybox::kBodyTop rather than re-summed here so it cannot
  // drift from what headerBand() actually reserves.
  const int top = toybox::kBodyTop;
  return fui::makeRect(toybox::kMargin, top, device.width - 2 * toybox::kMargin,
                       device.height - toybox::kMargin - top - footerHeight(hasDeviceName) - pageBarHeight(hasPages));
}

Paging pagingFor(const fui::DeviceContext& device, const fui::ThemeTokens& tokens, const bool hasDeviceName,
                 const int count) {
  // Asked without the bar first. A folder that fits keeps every row it has, and
  // never draws a control that would say "1/1".
  const int whole = fui::listVisibleRows(listBand(device, hasDeviceName, false), tokens.rowHeight, tokens.listRowGap);
  if (count <= whole) return Paging{whole > 0 ? whole : 1, 1};

  // It does not fit, so the bar exists, so the rows it costs come off. Asking in
  // this order is what stops the two answers depending on each other.
  const int paged = fui::listVisibleRows(listBand(device, hasDeviceName, true), tokens.rowHeight, tokens.listRowGap);
  if (paged <= 0) return Paging{1, count};
  return Paging{paged, (count + paged - 1) / paged};
}

int pageFor(const int selected, const int rowsPerPage) {
  if (rowsPerPage <= 0 || selected <= 0) return 0;
  return selected / rowsPerPage;
}

int pageCountFor(const int itemCount, const int rowsPerPage) {
  if (rowsPerPage <= 0 || itemCount <= 0) return 1;
  return (itemCount + rowsPerPage - 1) / rowsPerPage;
}

int rowForPage(const int page, const int rowsPerPage) {
  if (rowsPerPage <= 0 || page <= 0) return 0;
  return page * rowsPerPage;
}

int resumeRowFor(const int rememberedRow, const int itemCount) {
  if (itemCount <= 0 || rememberedRow <= 0) return 0;
  return rememberedRow >= itemCount ? itemCount - 1 : rememberedRow;
}

int pageStep(const int page, const int pageCount, const int delta) {
  if (pageCount <= 1) return 0;
  // Modulo of a negative left operand is negative in C++, so the step is
  // normalised into 0..pageCount-1 before it is added.
  const int step = ((delta % pageCount) + pageCount) % pageCount;
  const int from = page < 0 ? 0 : page % pageCount;
  return (from + step) % pageCount;
}

int pageStepClamped(const int page, const int pageCount, const int delta) {
  if (pageCount <= 1) return 0;
  const int from = page < 0 ? 0 : (page >= pageCount ? pageCount - 1 : page);
  const int to = from + delta;
  if (to < 0) return 0;
  return to >= pageCount ? pageCount - 1 : to;
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  const bool choosing = model.checks != nullptr;

  fui::HeaderProps header;
  header.title = model.title;
  header.borderEdges = fui::EdgesNone;
  // The corner holds the folder's mark while browsing and DONE while choosing.
  //
  // Only one of them at a time, and only the second is a drawn control: the way
  // IN is the whole band, which the mark sits in and labels without being a
  // button. The way OUT has to be a button, because a mode whose exit is
  // invisible is a trap -- and a chip that exists only inside the mode is not
  // the permanent furniture the first design put there.
  if (choosing) {
    header.trailingLabel = kDoneChip;
    header.trailingAction = ActionChoose;
    header.trailingStyles = toybox::bandFilledStyles();
  } else {
    // Room at the right of the content for the mark this file draws by hand.
    // The component reserves it out of the title AND out of the page counter,
    // which is the whole reason it is asked for rather than assumed: the
    // counter used to be placed by hand against the same arithmetic, in a
    // second copy, and a second copy is what goes wrong when the corner
    // changes.
    header.rightReserve = static_cast<int16_t>(toybox::kIconSize + toybox::kGutter);
  }
  // The chip's label takes its COLOUR from the style's foreground -- button()
  // resolves it that way and ignores the colour on this style -- so what this
  // line is for is the font the header measures the chip's width with. Named
  // rather than left unset because the fit depends on it and a reader should
  // not have to know that headerBand substitutes the same slot by default.
  header.trailingText = screen.theme().smallText;
  header.trailingRadius = toybox::kPillRadius / 2;

  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // The band is the way into the chooser, and out of it. The whole band, not
  // the mark alone: a 32px glyph is under half a thumb, and the rest of the
  // strip carries nothing a tap could otherwise mean. Registered AFTER the
  // header, so the chip inside it is registered first -- both carry the same
  // action, so the order is belt and braces rather than load-bearing.
  //
  // A hit region and nothing else; a StyleSet left unset would be replaced by
  // the default button look and paint a slab over the title.
  {
    fui::StyleSet invisible;
    invisible.explicitlySet = true;
    fui::ButtonProps band;
    band.action = ActionChoose;
    band.styles = invisible;
    band.minTouchSize = 0;
    screen.button(band, toybox::headerBandRect(screen));
  }

  const fui::Rect panel = screen.device().screen();

  // Paper, because the band is a filled black slab, and only while browsing:
  // the corner is DONE's while choosing.
  const int16_t markX = static_cast<int16_t>(panel.width - toybox::kIconSize - toybox::kMargin);
  if (!choosing && model.mark != nullptr) {
    const fui::Rect markRect =
        fui::makeRect(markX, toybox::bandCenterY(screen, toybox::kIconSize), toybox::kIconSize, toybox::kIconSize);
    screen.target().bitmap(markRect, fui::bitmapFromIcon(*model.mark), fui::BitmapMode::Contain,
                           fui::Paint::solid(fui::Color::White));
  }

  // Which page, in the header, where the eye already is.
  //
  // The page bar answers this too, and it sits at the bottom of the panel, out
  // of the fovea while the eyes are on the rows. A cold tester did not misread
  // the bar; they never looked at it, opened a game from a row position they
  // had learned on another page, and got a different game. So the count is said
  // twice: once beside the folder's name, which is the first thing read, and
  // once on the control that changes it.
  //
  // Placed by hand, at UI size, with its INK centred in the visible band --
  // which is the same rule the mark beside it uses, and is why the two line up.
  // The component's own rightLabel slot would place it for us and sits it on
  // the TITLE's line box instead: bottom-aligned to a display cut whose line
  // box runs well below its glyphs, so a small label lands under the baseline
  // and reads as dropped. Mario saw it in one screenshot.
  //
  // What it has to stop before is whichever thing the corner is holding, and
  // those are different widths. The chip's is the header component's own
  // arithmetic -- label + 20, inset 4 from the band's right edge -- mirrored
  // here in the same order, the way toybox::headerTitleWidth mirrors the rest
  // of that layout rather than guessing at it.
  if (model.pageCount > 1) {
    int16_t cornerX = markX;
    if (choosing) {
      const int16_t chipW = static_cast<int16_t>(
          screen.target().measureText(header.trailingText.font, kDoneChip, header.trailingText).width + 20);
      cornerX = static_cast<int16_t>(panel.width - 4 - chipW);
    }
    char counter[toybox::kSlashCounterChars];
    snprintf(counter, sizeof(counter), "%d/%d", model.page + 1, model.pageCount);
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.align = fui::TextAlign::Right;
    // Paper, or it is painted black on a black band and simply is not there.
    style.color = fui::Color::White;
    const fui::Rect box = fui::makeRect(0, toybox::bandCenterY(screen, toybox::kUiCut.inkHeight),
                                        static_cast<int16_t>(cornerX - toybox::kGutter), toybox::kUiCut.inkHeight);
    screen.target().text(toybox::inkCentred(box, toybox::kUiCut), counter, style);
  }

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Taken before the list, so the list can never grow into it.
  //
  // Who this device is, and the way to change it. The bar is a single control:
  // face, name and chevron are one hit region, because three targets in a strip
  // this size would each be smaller than a thumb and two of them would do the
  // same thing.
  //
  // There is no keyboard anywhere in this fork and this is deliberately not the
  // reason to build one: a name here is a label to be recognised, not a message
  // to be written, and rolling one word at a time is faster and more fun than
  // typing on a panel that repaints in half a second.
  //
  // The band belongs to the FOLDER, not to the mode: it is taken here whenever
  // this folder shows the device name, in both modes, which is what keeps the
  // chooser showing the same games on the same page as the list it was opened
  // from. What goes IN it changes -- the bar is a browsing control, and while
  // choosing the same strip carries the mode's caption instead, an inert
  // sentence rather than a second control on a screen whose rows have just
  // changed meaning. A folder without the bar (APPS) gets no caption, because
  // taking a band for one would reflow its list.
  if (model.playerName != nullptr) {
    const fui::Rect bar = screen.takeBottom(toybox::kRowHeight, toybox::kGutter);
    if (choosing) {
      fui::TextStyle caption = screen.theme().bodyText;
      caption.color = fui::Color::Black;
      caption.align = fui::TextAlign::Center;
      screen.target().text(toybox::inkCentred(bar, toybox::kUiCut), kChoosingCaption, caption);
    } else {
      // The slab and the hit region, with NO label. The name is drawn separately
      // below, into a band that excludes the face and the chevron.
      //
      // Handing the button its label was the obvious thing and it was wrong: the
      // component centres the text across the whole rect, so at the widest name
      // the lists can roll -- twenty characters -- it ran straight through the
      // face on one side and the chevron on the other. Three things cannot share
      // one centre line.
      fui::ButtonProps you;
      you.action = ActionOpenPlayer;
      screen.button(you, bar);

      // Both marks are drawn ON the bar, which the button has just filled solid
      // black, so both are paper. Drawn in ink they would be invisible and
      // nothing would warn -- the multiplayer mark went black-on-black once
      // already.
      const fui::Paint paper = fui::Paint::solid(fui::Color::White);
      const int16_t face = player::avatarPixels(player::AvatarSize::Row);
      const int16_t inset = static_cast<int16_t>((toybox::kRowHeight - face) / 2);
      const fui::Rect faceRect =
          fui::makeRect(static_cast<int16_t>(bar.x + inset), static_cast<int16_t>(bar.y + inset), face, face);
      const fui::Rect chevron =
          fui::makeRect(static_cast<int16_t>(bar.right() - toybox::kIconSize - toybox::kGutter),
                        static_cast<int16_t>(bar.y + (toybox::kRowHeight - toybox::kIconSize) / 2), toybox::kIconSize,
                        toybox::kIconSize);
      player::drawAvatar(screen.target(), faceRect, model.playerName, player::AvatarSize::Row, fui::Color::White);
      screen.target().bitmap(chevron, fui::bitmapFromIcon(icon_opens_32), fui::BitmapMode::Contain, paper);

      // What is left between them, and the name has to live inside it whatever it
      // says. The dense cut, because twenty characters do not fit this band at UI
      // size and the alternative is the component quietly truncating -- with an
      // ellipsis glyph Jersey does not even have. Smaller is also the right
      // hierarchy here: on a door, the face says who and the name only confirms
      // it. The PLAYER screen is where the name is the subject.
      fui::TextStyle label;
      label.font = toybox::kSmallFont;
      label.align = fui::TextAlign::Center;
      label.color = fui::Color::White;
      const int16_t left = static_cast<int16_t>(faceRect.right() + toybox::kGutter);
      screen.target().text(
          fui::makeRect(left, bar.y, static_cast<int16_t>(chevron.x - toybox::kGutter - left), toybox::kRowHeight),
          model.playerName, label);
    }
  }

  // Taken after the player bar, so it sits above it, and only when there is more
  // than one page: a lone mark saying "you are on the only page" is furniture.
  //
  // **This is a position indicator that happens to be tappable, not a row of
  // buttons**, and the difference is the whole design. The first version tiled
  // the full width with page-sized targets, and what was actually missing was
  // never a control at all -- it was the *signal* that more games exist. So the
  // marks are a small centred cluster with air around them rather than a bar of
  // slabs, and the targets are a thumb wide and contiguous *within the cluster
  // only*: tapping the far edge of the screen does nothing, because out there
  // the user is not aiming at anything.
  //
  // Each mark carries its page NUMBER. They were 10px squares, filled for here
  // and outlined for there, and a cold tester called them "the size of a full
  // stop": at that size the only thing saying where you are is smaller than the
  // ink of one letter, on a screen whose rows sit in the same eight places on
  // every page. A numeral in the same cell is the same control, the same
  // cluster and the same targets, saying the same thing legibly. It also makes
  // the cluster a counter rather than a carousel, which matters now that the
  // pages step vertically: dots borrowed the iOS home screen's promise that the
  // content slides sideways, and nothing here slides at all.
  if (model.pageCount > 1) {
    const fui::Rect bar = screen.takeBottom(kPageBarHeight, toybox::kGutter);

    // A thumb, and contiguous, so no gap between adjacent pages can swallow a
    // tap. Falls back to sharing the bar when there are so many pages that a
    // thumb-wide pitch would not fit; small honest targets beat overlapping
    // ones, which is why the component is not allowed to grow them either.
    constexpr int16_t kPitch = 44;
    const int16_t pitch =
        kPitch * model.pageCount <= bar.width ? kPitch : static_cast<int16_t>(bar.width / model.pageCount);
    const int16_t clusterX = static_cast<int16_t>(bar.x + (bar.width - pitch * model.pageCount) / 2);

    // A hit region and nothing else. A StyleSet with no paints would be taken
    // for an unset one and quietly replaced by the default button look, which is
    // the filled slab the player bar wants and this does not; explicitlySet is
    // how the component is told the blankness is deliberate.
    fui::StyleSet invisible;
    invisible.explicitlySet = true;

    // Each cell draws its own box, the way a list row does: outlined for a page
    // you are not on, filled for the one you are. That is this fork's existing
    // language for "this one of these", and it is what makes the cluster read as
    // a control -- which it has to, because `BaseTheme::drawButtonHints` returns
    // immediately when `gpio.hasTouch()` and the X4 Pro has a GT911, so upstream
    // teaches a touch user nothing about the two side keys. Touch has to stay
    // complete. A single hairline capsule around the whole cluster did that job
    // before, and cannot now: a filled cell inside a capsule of this radius
    // pokes out through the curve at the two ends.
    const int16_t inset = 2;
    const int16_t cellH = static_cast<int16_t>(bar.height - 2 * inset);
    for (int p = 0; p < model.pageCount; ++p) {
      const fui::Rect target =
          fui::makeRect(static_cast<int16_t>(clusterX + p * pitch), bar.y, pitch, static_cast<int16_t>(bar.height));
      fui::ButtonProps jump;
      jump.action = ActionGoToPage;
      jump.value = static_cast<int16_t>(p);
      jump.styles = invisible;
      jump.minTouchSize = 0;
      screen.button(jump, target);

      // Drawn inside the target rather than over it, so the ink never promises a
      // hit outside the region that answers -- and so two neighbours do not
      // share a doubled edge.
      const fui::Rect cell =
          fui::makeRect(static_cast<int16_t>(target.x + inset), static_cast<int16_t>(target.y + inset),
                        static_cast<int16_t>(pitch - 2 * inset), cellH);
      const bool here = p == model.page;
      // Ink rather than a grey, because there is no grey on this panel: any
      // non-white colour is drawn solid black and a dither this small is mud.
      if (here) {
        screen.target().fill(cell, fui::Paint::solid(fui::Color::Black), 10);
      } else {
        screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), 1, 10);
      }

      char number[toybox::kIntTextChars];
      snprintf(number, sizeof(number), "%d", p + 1);
      fui::TextStyle style;
      style.font = toybox::kUiFont;
      style.align = fui::TextAlign::Center;
      style.color = here ? fui::Color::White : fui::Color::Black;
      screen.target().text(toybox::inkCentred(cell, toybox::kUiCut), number, style);
    }
  }

  const fui::Rect rows = listBand(screen.device(), model.playerName != nullptr, model.pageCount > 1);

  // A folder with nothing on its list. Reachable, deliberately: a person who
  // does not want any of the games is allowed to have none of them, and a rule
  // refusing the last one would be the device arguing with its owner.
  //
  // What it must not be is a dead end, so the whole empty band is the way back
  // in -- one hit region, the size of the hole the list left, doing what the
  // chip in the corner does. The chip is still up there for anyone who looks,
  // but nothing here depends on finding it.
  if (model.count <= 0) {
    fui::StyleSet invisible;
    // A hit region and nothing else, the same way the page marks ask for one: a
    // StyleSet left unset would be replaced by the default button look, which
    // is a black slab the size of the body.
    invisible.explicitlySet = true;
    fui::ButtonProps anywhere;
    anywhere.action = ActionChoose;
    anywhere.styles = invisible;
    anywhere.minTouchSize = 0;
    screen.button(anywhere, rows);

    fui::TextStyle headline = screen.theme().titleText;
    // OFF THE BAND, SO IT HAS TO BE INK. The display cut's token colour is
    // paper because it is otherwise only ever set on the black header; taken as
    // given here it paints white on white and the panel looks broken in exactly
    // the way this sentence exists to prevent.
    headline.color = fui::Color::Black;
    headline.align = fui::TextAlign::Center;
    fui::TextStyle hint = screen.theme().bodyText;
    hint.color = fui::Color::Black;
    hint.align = fui::TextAlign::Center;
    hint.maxLines = 3;
    // Both measured and stacked as a block. centeredText() centres in the
    // content rect and consumes nothing, so two calls draw at the same y and
    // the first is simply painted over -- the Hacker News empty shelf shipped
    // that way and nobody could see it happening.
    const int16_t headlineH = screen.target().lineHeight(headline.font);
    const int16_t hintH = fui::measureWrappedText(screen.target(), kEmptyHint, hint, rows.width).height;
    const int16_t top = static_cast<int16_t>(rows.y + (rows.height - headlineH - toybox::kGutter - hintH) / 2);
    screen.target().text(fui::makeRect(rows.x, top, rows.width, headlineH), kEmptyHeadline, headline);
    screen.target().text(
        fui::makeRect(rows.x, static_cast<int16_t>(top + headlineH + toybox::kGutter), rows.width, hintH), kEmptyHint,
        hint);
    return;
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  // Always zero: this list is exactly one page, so it never overflows its band
  // and has nothing to scroll. See MenuModel::items.
  list.topIndex = 0;
  // Never a marked row; see MenuModel for why. Set rather than left to the
  // component's default so a change to that default cannot put a cursor back on
  // a screen that has nothing to move it.
  list.selectedIndex = -1;
  list.action = choosing ? ActionToggleShown : ActionOpen;
  // The whole row is the target, not the box on it. Two hit regions per row
  // would be two of the screen's twenty-four for a control the finger is
  // already on, and the row past the twenty-fourth stops answering in silence.
  if (choosing) {
    // The list only indents a label when it is drawing an icon of its own, and
    // its icon slot draws one bitmap with the row's foreground -- which cannot
    // be a box with a knocked-out tick. So the room is taken here and the box
    // is drawn into it below, the same arrangement the app icons on the right
    // already have.
    list.sidePadding = static_cast<int16_t>(screen.theme().listSidePadding + toybox::kIconSize + toybox::kGutter);
  }
  screen.list(list);

  if (choosing) {
    const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
    for (int i = 0; i < model.count; ++i) {
      fui::Rect row;
      if (!toybox::listRowRect(screen, rows, i, 0, row)) continue;
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(row.x + screen.theme().listSidePadding),
                        static_cast<int16_t>(row.y + (row.height - kBoxSize) / 2), kBoxSize, kBoxSize);
      if (!model.checks[i]) {
        screen.target().stroke(box, ink, kBoxEdge, kBoxRadius);
        continue;
      }
      screen.target().fill(box, ink, kBoxRadius);
      // Paper on the slab. Drawn in ink it would be invisible and nothing would
      // warn; the multiplayer mark went black-on-black once already.
      screen.target().bitmap(
          fui::makeRect(static_cast<int16_t>(box.x + (kBoxSize - kTickSize) / 2),
                        static_cast<int16_t>(box.y + (kBoxSize - kTickSize) / 2), kTickSize, kTickSize),
          fui::bitmapFromIcon(icon_tick_24), fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::White));
    }
  }

  // Icons sit at the right edge; see toybox::iconAtRowRight for why not the
  // list's own left-hand slot.
  if (model.icons != nullptr) {
    for (int i = 0; i < model.count; ++i) {
      if (model.icons[i] == nullptr) continue;
      // Never inverted: no row is ever the selected one, so every icon is ink.
      toybox::iconAtRowRight(screen, rows, i, 0, *model.icons[i], false);
    }
  }
}

}  // namespace shelfui
