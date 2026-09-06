#include "StudyScreens.h"

#include <cstdio>

#include "../../components/icons/listIcons.h"

namespace studyui {

namespace {

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  // header() takes these styles as given rather than resolving them against the
  // band, so an unset style renders black on black and simply is not there.
  // Screen substitutes smallText, which is black. Same trap Connections hit.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
}

// The same corner brackets the chess board and the Connections grid wear. Two
// screens that share a bracket read as one device.
void brackets(toybox::Screen& screen, const fui::Rect& box, const int arm) {
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

// The ornament: one timeline, the fortnight behind and the fortnight ahead.
//
// docs/design-language.md asks that anything decorative be made of the app's
// own material and carry the user's own data -- the test being whether a
// screenshot would look the same on everyone's device. This is nothing but the
// user's own history and their own backlog, it is different every morning, and
// both halves come from files that are already being read.
//
// The two halves are drawn differently because they mean different things:
// history is filled, because it happened; the forecast is outlined, because it
// has not. Today sits between them and is the only solid bar in the middle,
// which is what makes the timeline readable without a legend.
//
// They share one vertical scale on purpose. Both are counts of cards, so a
// shared scale is what makes "I did forty a day last week and have five a day
// coming" legible at a glance -- which is the only question this panel exists
// to answer.
void timeline(toybox::Screen& screen, const fui::Rect& box, const DeckModel& model) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int history = model.history != nullptr ? kHistoryDays : 0;
  const int ahead = model.forecast != nullptr ? kForecastDays - 1 : 0;
  const int columns = history + ahead;
  if (columns <= 0) return;

  // Scale to the tallest column that is not today. Everything overdue piles
  // onto day zero, so scaling to it flattens every other bar into nothing --
  // which is exactly the panel that taught us this. Today clips instead: it is
  // solid and unmissable either way, and "today is the big one" survives.
  int peak = 1;
  for (int i = 1; i < kHistoryDays && model.history != nullptr; ++i) {
    if (model.history[i] > peak) peak = model.history[i];
  }
  for (int i = 1; i < kForecastDays && model.forecast != nullptr; ++i) {
    if (model.forecast[i] > peak) peak = model.forecast[i];
  }

  // Nothing recorded and nothing scheduled: say so inside the panel. A fresh
  // install with a backlog has an empty history *and* an empty forecast, and a
  // bracketed empty box reads as a panel that failed to draw rather than as one
  // with nothing yet to show. This is the first thing a new deck displays, so
  // it is the state most worth getting right.
  bool anyData = false;
  for (int i = 0; i < kHistoryDays && model.history != nullptr && !anyData; ++i) {
    if (model.history[i] > 0) anyData = true;
  }
  for (int i = 1; i < kForecastDays && model.forecast != nullptr && !anyData; ++i) {
    if (model.forecast[i] > 0) anyData = true;
  }
  if (!anyData) {
    fui::TextStyle empty;
    empty.font = toybox::kTileFont;
    empty.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(box.x, box.y + box.height / 2 - 12, box.width, 24), "NOTHING RECORDED YET",
                         empty);
    return;
  }

  const int gap = 2;
  const int barWidth = (box.width - gap * (columns - 1)) / columns;
  if (barWidth <= 0) return;
  const int baseline = box.bottom();
  // Leave headroom so a clipped bar reads as clipped rather than as one that
  // happens to reach the bracket. Today is routinely an order of magnitude
  // above the scale, so this is the common case, not the edge case.
  const int ceiling = (box.height * 9) / 10;

  for (int column = 0; column < columns; ++column) {
    // Oldest history first, then today, then the forecast reading forward.
    const bool isHistory = column < history;
    const bool isToday = column == history - 1;
    int count = 0;
    if (isHistory && model.history != nullptr) {
      count = model.history[history - 1 - column];
    } else if (model.forecast != nullptr) {
      count = model.forecast[column - history + 1];
    }
    if (count <= 0) continue;

    const int x = box.x + column * (barWidth + gap);
    int height = (count * ceiling) / peak;
    if (height < 3) height = 3;
    if (height > ceiling) height = ceiling;
    const fui::Rect bar = fui::makeRect(x, baseline - height, barWidth, height);

    if (isHistory || isToday) {
      screen.target().fill(bar, ink);
    } else {
      // Outlined: this has not happened yet.
      screen.target().fill(fui::makeRect(bar.x, bar.y, bar.width, toybox::kHairline), ink);
      screen.target().fill(fui::makeRect(bar.x, bar.y, toybox::kHairline, bar.height), ink);
      screen.target().fill(fui::makeRect(bar.right() - toybox::kHairline, bar.y, toybox::kHairline, bar.height), ink);
    }
  }

  // The ground the bars stand on, and a tick under today so the two halves can
  // be told apart even on a day with nothing either side of it.
  screen.target().fill(fui::makeRect(box.x, baseline, box.width, toybox::kHairline), ink);
  if (history > 0) {
    const int todayX = box.x + (history - 1) * (barWidth + gap);
    screen.target().fill(fui::makeRect(todayX, baseline, barWidth, toybox::kRule), ink);
  }
}

}  // namespace

void buildDeck(toybox::Screen& screen, const DeckModel& model) {
  chrome(screen, "STUDY");
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  // The headline is the count, and the whole block is the hit target: the most
  // common action is a tap on the largest thing on the screen and needs no
  // button of its own.
  char headline[32];
  const int waiting = model.due + model.fresh;
  if (waiting > 0) {
    std::snprintf(headline, sizeof(headline), "%d TO GO", waiting);
  } else {
    if (model.otherWaiting > 0) {
      std::snprintf(headline, sizeof(headline), model.reviewed > 0 ? "DECK DONE" : "DECK CLEAR");
    } else {
      std::snprintf(headline, sizeof(headline), model.reviewed > 0 ? "DONE" : "ALL CLEAR");
    }
  }

  fui::TextStyle hero;
  hero.font = toybox::kDisplayFont;
  hero.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 60), headline, hero);

  char state[64];
  if (waiting > 0) {
    std::snprintf(state, sizeof(state), "%d DUE   %d NEW", model.due, model.fresh);
  } else if (model.reviewed > 0) {
    std::snprintf(state, sizeof(state), "%d REVIEWED   %d%% RIGHT", model.reviewed,
                  model.reviewed > 0 ? model.recalled * 100 / model.reviewed : 0);
  } else {
    std::snprintf(state, sizeof(state), model.otherWaiting > 0 ? "NOTHING DUE IN THIS DECK" : "NOTHING DUE TODAY");
  }
  fui::TextStyle sub;
  sub.font = toybox::kUiFont;
  sub.align = fui::TextAlign::Left;
  screen.target().text(toybox::inkCentred(fui::makeRect(body.x, body.y + 62, body.width, 26), toybox::kUiCut), state,
                       sub);
  if (waiting > 0) {
    screen.frame().hit(fui::makeRect(body.x, body.y, body.width, 96), ActionStudy, 0);
  }

  screen.target().fill(fui::makeRect(body.x, body.y + 108, body.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  // The Record band, in the sense docs/design-language.md's front-door table
  // means it: what you have done, small, on one line.
  char record[80];
  if (model.retention >= 0) {
    std::snprintf(record, sizeof(record), "STREAK %d   %d%% RECALL   %d REVIEW%s", model.streak, model.retention,
                  model.lifetimeReviews, model.lifetimeReviews == 1 ? "" : "S");
  } else if (model.lifetimeReviews > 0) {
    std::snprintf(record, sizeof(record), "%d REVIEW%s   NONE THIS FORTNIGHT", model.lifetimeReviews,
                  model.lifetimeReviews == 1 ? "" : "S");
  } else {
    // No history at all: name the deck rather than print a row of zeroes,
    // which reads as a broken counter rather than as a fresh start.
    std::snprintf(record, sizeof(record), "%s   %d CARDS", model.name, model.total);
  }
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(body.x, body.y + 122, body.width, 24), record, small);

  // Which deck this is. The record line only names it while the deck has no
  // history, so the decks you actually study were the ones the screen stopped
  // naming -- and with several on the card nothing else says which is open.
  int nextLine = 146;
  // Also at one deck: the name was dropped entirely there, so a single-deck
  // reader never saw which deck they were in.
  if (model.lifetimeReviews > 0) {
    fui::TextStyle deckName = small;
    screen.target().text(fui::makeRect(body.x, body.y + nextLine, body.width, 22), model.name, deckName);
    nextLine += 24;
  }
  // A warning rides the scope line rather than getting one of its own. At four
  // doors this screen has no spare row: above the ornament the brackets start
  // 5px below the line before it, and below the caption is the top door. The
  // nudge it displaces is routine; a warning is neither routine nor silent.
  char warning[48] = "";
  if (model.writeFailed) {
    std::snprintf(warning, sizeof(warning), "SOME REVIEWS DID NOT SAVE");
  } else if (model.clockUnset) {
    std::snprintf(warning, sizeof(warning), "THIS READER LOST THE DATE -- SYNC TO SET IT");
  } else if (model.decksOverCap > 0) {
    std::snprintf(warning, sizeof(warning), "%d DECK%s HERE CANNOT BE OPENED OR SENT", model.decksOverCap,
                  model.decksOverCap == 1 ? "" : "S");
  }
  // Scope, on its own line so it cannot push the headline out of its box: the
  // counts above are this deck's, and with more than one on the card they read
  // as the whole account's.
  if (warning[0] != '\0') {
    fui::TextStyle warn = small;
    screen.target().text(fui::makeRect(body.x, body.y + nextLine, body.width, 22), warning, warn);
  } else if (model.deckCount > 1) {
    fui::TextStyle scope = small;
    scope.color = fui::Color::DarkGray;
    char elsewhere[64];
    // Name the work waiting one tap away. Without this the headline could say
    // ALL CLEAR truthfully about the open deck while another deck held every
    // card the user meant to study, and they would put the reader down.
    if (model.otherWaiting > 0) {
      std::snprintf(elsewhere, sizeof(elsewhere), "%d WAITING IN YOUR OTHER DECK%s", model.otherWaiting,
                    model.deckCount > 2 ? "S" : "");
    } else {
      std::snprintf(elsewhere, sizeof(elsewhere), "TODAY'S COUNTS ARE FOR THIS DECK");
    }
    screen.target().text(fui::makeRect(body.x, body.y + nextLine, body.width, 22), elsewhere, scope);
  }

  // The ornament, bracketed the way the board is. With three doors below
  // (multi-deck cards) it gives up some height so nothing overlaps.
  // Doors are bottom-anchored, so the ornament gets what they leave. Sized
  // from the real door count rather than a guess: at four doors the caption
  // was drawn straight through the top one.
  const int doorsHere = 2 + (model.deckCount > 1 ? 1 : 0) + (model.paired ? 1 : 0);
  const int doorBand = doorsHere * toybox::kRowHeight + (doorsHere - 1) * toybox::kMargin;
  const int panelTop = body.y + 210;
  // The brackets hang 36px below the panel, so the caption clears them by
  // 44 -- less and a bottom arm is drawn through the text (the ui suite
  // checks exactly this).
  const int captionH = 30;
  const int captionDrop = 44;
  int panelHeight = body.bottom() - doorBand - toybox::kMargin - captionDrop - captionH - panelTop;
  if (panelHeight > 190) panelHeight = 190;
  if (panelHeight < 84) panelHeight = 84;
  const fui::Rect panel = fui::makeRect(body.x + 10, panelTop, body.width - 20, panelHeight);
  brackets(screen, fui::makeRect(panel.x - 18, panel.y - 18, panel.width + 36, panel.height + 54), 32);
  timeline(screen, fui::makeRect(panel.x + 12, panel.y + 8, panel.width - 24, panelHeight - 16), model);

  // The caption names both halves and carries the number scheduled ahead, so
  // an empty right-hand side reads as "nothing scheduled yet" rather than as a
  // panel that failed to draw. On an all-backlog deck that number is zero.
  int ahead = 0;
  if (model.forecast != nullptr) {
    for (int i = 1; i < kForecastDays; ++i) ahead += model.forecast[i];
  }
  char caption_text[64];
  std::snprintf(caption_text, sizeof(caption_text), "2 WEEKS BACK   TODAY   %d DUE AHEAD", ahead);
  fui::TextStyle caption;
  caption.font = toybox::kTileFont;
  caption.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, panel.bottom() + captionDrop, body.width, captionH), caption_text,
                       caption);

  // The doors, bottom-anchored where thumbs live: reviewing above, the
  // bridge below it. The stacked arrangement won from three rendered
  // variants; the rows wear the same lucide glyphs the rest of the
  // firmware's lists do, and SYNC carries its own state in the value slot
  // so the door says when it last mattered.
  fui::ListItem rows[4];
  int doorCount = 0;
  rows[doorCount] = fui::ListItem{};
  // With nothing due here but work waiting elsewhere, the top door used to
  // read NOTHING TO REVIEW and sit there looking live: same box, same glyph,
  // no response. It becomes the way to that work instead.
  const bool elsewhereOnly = waiting == 0 && model.otherWaiting > 0;
  rows[doorCount].label = model.clockUnset ? "SYNC TO SET THE CLOCK"
                          : waiting > 0    ? "START REVIEWING"
                          : elsewhereOnly  ? "REVIEW ANOTHER DECK"
                                           : "NOTHING TO REVIEW";
  rows[doorCount].enabled = !model.clockUnset && (waiting > 0 || elsewhereOnly);
  rows[doorCount].actionValue = elsewhereOnly ? 3 : 1;
  rows[doorCount].icon = fui::bitmapFromIcon(icon_play_32);
  ++doorCount;
  rows[doorCount] = fui::ListItem{};
  rows[doorCount].label = "SYNC";
  // State rides the value slot, right-aligned on the same line: a subtitle
  // makes the row taller than its box and the whole door clipped away.
  // State rides the value slot, right-aligned on the same line: a subtitle
  // makes the row taller than its box and the whole door clipped away.
  rows[doorCount].value = model.syncSubtitle[0] != '\0' ? model.syncSubtitle : nullptr;
  rows[doorCount].enabled = true;
  rows[doorCount].actionValue = 2;
  rows[doorCount].icon = fui::bitmapFromIcon(icon_refresh_cw_32);
  ++doorCount;

  // The deck switcher, only when there is somewhere to go. The value names
  // the position; the deck's NAME is already the row above the ornament, and
  // repeating it here clipped the label.
  char deckValue[48] = "";
  if (model.deckCount > 1) {
    rows[doorCount] = fui::ListItem{};
    rows[doorCount].label = "CHANGE DECK";
    std::snprintf(deckValue, sizeof(deckValue), "%d OF %d", model.deckIndex + 1, model.deckCount);
    rows[doorCount].value = deckValue;
    rows[doorCount].actionValue = 3;
    rows[doorCount].icon = fui::bitmapFromIcon(icon_library_32);
    ++doorCount;
  }

  // Which Anki decks this reader carries, once paired: otherwise the choice
  // is answerable only during the first sync and a mis-tap is permanent.
  if (model.paired) {
    rows[doorCount] = fui::ListItem{};
    rows[doorCount].label = "DECKS FROM ANKI";
    rows[doorCount].value = "CHOOSE";
    rows[doorCount].actionValue = 4;
    rows[doorCount].icon = fui::bitmapFromIcon(icon_download_24);
    ++doorCount;
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(doorCount);
  list.selectedIndex = -1;
  list.action = ActionStudy;
  // The state whispers next to the label: the tile cut, not the theme's
  // smallText, which deliberately stays at UI size for list values in
  // general. The named alignment is load-bearing: FONT_SLOT_SMALL is 0 and
  // a style holding only it reads as unset to Screen::list(), which would
  // swap the theme back in. The list right-aligns values regardless.
  // valueInset keeps the same air from the right border that the icon box
  // gets from the left.
  fui::TextStyle doorValue;
  doorValue.font = toybox::kTileFont;
  doorValue.align = fui::TextAlign::Right;
  list.valueText = doorValue;
  // One unit inside and out. The block's frame is kMargin on every side,
  // and every interior distance takes the same unit: door to door, border
  // to icon, icon to label, value to border. The theme's tighter list
  // defaults (12/10) inside a 16px frame is what read as cramped. The
  // band is exactly two rows plus the gap, so no slack floats the bottom
  // door off the content margin.
  list.sidePadding = toybox::kMargin;
  list.textGap = toybox::kMargin;
  list.rowGap = toybox::kMargin;
  list.valueInset = 0;
  screen.list(list, doorBand, fui::LayoutAnchor::Bottom);
}

// ---- The sync flow surface (docs/apps/study-syncflow-ui.md). The stage
// band won the three-variant round; the ladder and line+log arrangements
// and the STUDY_SYNCFLOW_VARIANT macro died with the choice.

namespace {

const char* stageLabel(const int i) {
  static const char* kLabels[kSyncStageCount] = {"CONNECT", "SEND REVIEWS", "BUILD DECKS", "DOWNLOAD"};
  return kLabels[i];
}

// The band's segments are a quarter-width each; the full labels truncate.
const char* stageShortLabel(const int i) {
  static const char* kLabels[kSyncStageCount] = {"CONNECT", "SEND", "BUILD", "DOWNLOAD"};
  return kLabels[i];
}

const char* safetyLine(const SyncSafety safety) {
  switch (safety) {
    case SyncSafety::ReviewsSafe:
      return "YOUR REVIEWS ARE SAFE";
    case SyncSafety::ReviewsSafePartialDecks:
      return "REVIEWS SAFE, SOME DECKS ALREADY UPDATED";
    case SyncSafety::NothingSent:
      return "NOTHING WAS SENT";
    default:
      return nullptr;
  }
}

fui::TextStyle syncText(const fui::FontId font, const fui::TextAlign align, const fui::Color color = fui::Color::Black,
                        const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  style.maxLines = maxLines;
  return style;
}

// The verdict glyph in the door's icon language.
fui::BitmapRef verdictGlyph(const SyncVerdictKind kind) {
  switch (kind) {
    case SyncVerdictKind::Success:
      return fui::bitmapFromIcon(icon_check_32);
    case SyncVerdictKind::Error:
      return fui::bitmapFromIcon(icon_x_32);
    default:
      return fui::bitmapFromIcon(icon_minus_32);
  }
}

// The safety promise, anchored at the content bottom. Busy face: shown from
// the ack on, with the leave story; verdict face never calls this (safety
// leads the verdict text instead).
void syncSafetyFooter(toybox::Screen& screen, const fui::Rect& body, const bool reviewsSafe) {
  const int y = body.bottom() - 52;
  if (reviewsSafe) {
    screen.target().text(fui::makeRect(body.x, y, body.width, 20), "YOUR REVIEWS ARE SAFE",
                         syncText(toybox::kTileFont, fui::TextAlign::Center));
  }
  screen.target().text(fui::makeRect(body.x, y + 24, body.width, 20), "BACK LEAVES; THE SYNC KEEPS RUNNING",
                       syncText(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
}

}  // namespace

void buildSyncFlow(toybox::Screen& screen, const SyncFlowModel& model) {
  chrome(screen, "SYNC");
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const fui::Paint dim = fui::Paint::solid(fui::Color::DarkGray);

  int activeStage = -1;
  for (int i = 0; i < kSyncStageCount; ++i) {
    if (model.stages[i] == SyncStageState::Active) activeStage = i;
  }

  // V2 "Stage band": a horizontal four-segment band under the header; the
  // current stage as a headline beneath it.
  const int segGap = 8;
  const int segW = (body.width - segGap * (kSyncStageCount - 1)) / kSyncStageCount;
  const int bandY = body.y + 4;
  for (int i = 0; i < kSyncStageCount; ++i) {
    const fui::Rect seg = fui::makeRect(body.x + i * (segW + segGap), bandY, segW, 14);
    // The band never invents progress: a verdict shows the stages exactly as
    // the flow left them (a cancelled pairing shows one half-tone segment,
    // not four filled ones over NOTHING WAS SENT). Success alone fills the
    // strip, because success means every stage in fact completed.
    SyncStageState state = model.stages[i];
    if (model.verdict == SyncVerdictKind::Success) state = SyncStageState::Done;
    if (state == SyncStageState::Done) {
      screen.target().fill(seg, ink);
    } else if (state == SyncStageState::Active) {
      // Half-tone: in progress on the busy face, "died here" on an error
      // verdict. A heavier outline alone did not separate from pending.
      screen.target().fill(seg, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(seg, ink, 2);
    } else {
      screen.target().stroke(seg, dim, 1);
    }
    screen.target().text(fui::makeRect(seg.x - 4, bandY + 22, segW + 8, 18), stageShortLabel(i),
                         syncText(toybox::kTileFont, fui::TextAlign::Center,
                                  state == SyncStageState::Pending ? fui::Color::DarkGray : fui::Color::Black));
  }

  if (model.verdict == SyncVerdictKind::None) {
    const int headY = bandY + 96;
    if (activeStage >= 0) {
      screen.target().text(fui::makeRect(body.x, headY, body.width, 48), stageLabel(activeStage),
                           syncText(toybox::kDisplayFont, fui::TextAlign::Center));
    }
    // The active stage's own fact, under its name: on e-ink a screen that
    // never changes and a screen that is stuck look identical, and this is the
    // only live number the wait has (files fetched, reviews sent).
    if (activeStage >= 0 && model.facts[activeStage][0] != '\0') {
      screen.target().text(fui::makeRect(body.x, headY + 52, body.width, 22), model.facts[activeStage],
                           syncText(toybox::kTileFont, fui::TextAlign::Center));
    }
    if (model.caption[0] != '\0') {
      screen.target().text(fui::makeRect(body.x + 20, headY + 84, body.width - 40, 84), model.caption,
                           syncText(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));
    }
    if (model.leaveSafe) syncSafetyFooter(screen, body, model.safety >= SyncSafety::ReviewsSafe);
  } else {
    int y = bandY + 88;
    screen.target().bitmap(fui::makeRect(body.x + (body.width - 32) / 2, y, 32, 32), verdictGlyph(model.verdict),
                           fui::BitmapMode::Contain, ink);
    y += 52;
    screen.target().text(fui::makeRect(body.x, y, body.width, 48), model.title,
                         syncText(toybox::kDisplayFont, fui::TextAlign::Center));
    y += 62;
    if (const char* safety = safetyLine(model.safety)) {
      screen.target().text(fui::makeRect(body.x, y, body.width, 22), safety,
                           syncText(toybox::kTileFont, fui::TextAlign::Center));
      y += 34;
    }
    screen.target().text(fui::makeRect(body.x + 20, y, body.width - 40, 104), model.body,
                         syncText(toybox::kUiFont, fui::TextAlign::Center, fui::Color::Black, 4));
    // The facts hang from the bottom, above the what-now line, rather than
    // following the body. The body is the one element here whose height is not
    // known: it wraps to as many as four lines and overflows its own box doing
    // it, so anything stacked after it by a fixed offset is drawn THROUGH it
    // as soon as the sentence is long -- which is exactly when a verdict has
    // most to say. Everything below is anchored; only the body floats.
    const int whatNowTop = body.bottom() - toybox::kRowHeight - 52;
    if (model.whatNow[0] != '\0') {
      screen.target().text(fui::makeRect(body.x + 20, whatNowTop, body.width - 40, 44), model.whatNow,
                           syncText(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray, 2));
    }
    int factY = whatNowTop - 12 - model.factCount * 26;
    for (int i = 0; i < model.factCount; ++i) {
      screen.target().text(fui::makeRect(body.x, factY, body.width, 22), model.factLines[i],
                           syncText(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
      factY += 26;
    }
    // A verdict has to offer the next step rather than name it: telling a
    // stranded user to "press SYNC" pointed at a control that is not on this
    // screen, and on a reader with no decks yet it is not on the screen behind
    // it either.
    fui::ListItem again[1];
    again[0] = fui::ListItem{};
    // A verdict you asked for offers to close; only a failure offers a retry.
    const bool offerRetry = model.verdict == SyncVerdictKind::Error;
    again[0].label = offerRetry ? "TRY AGAIN" : "DONE";
    again[0].actionValue = offerRetry ? 1 : 2;
    again[0].icon = fui::bitmapFromIcon(offerRetry ? icon_refresh_cw_32 : icon_check_32);
    fui::ListProps door;
    door.items = again;
    door.count = 1;
    door.selectedIndex = -1;
    door.action = ActionSyncVerdict;
    door.sidePadding = toybox::kMargin;
    door.textGap = toybox::kMargin;
    screen.list(door, toybox::kRowHeight, fui::LayoutAnchor::Bottom);
  }
}

void buildDeckPicker(toybox::Screen& screen, DeckPickerModel& model) {
  chrome(screen, "SYNC");
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  screen.target().text(fui::makeRect(body.x, body.y, body.width, 52), "WHICH DECKS?",
                       syncText(toybox::kDisplayFont, fui::TextAlign::Left));

  char caption[72];
  if (model.atCap) {
    std::snprintf(caption, sizeof(caption), "%d OF %d, THE MOST THIS READER CARRIES", model.chosenCount,
                  model.maxChosen);
  } else if (model.withheld) {
    std::snprintf(caption, sizeof(caption), "%d CHOSEN, ONLY THE FIRST %d ARE LISTED", model.chosenCount, model.count);
  } else {
    std::snprintf(caption, sizeof(caption), "%d CHOSEN, UP TO %d", model.chosenCount, model.maxChosen);
  }
  screen.target().text(fui::makeRect(body.x, body.y + 58, body.width, 22), caption,
                       syncText(toybox::kTileFont, fui::TextAlign::Left, fui::Color::DarkGray));

  // The confirm door sits at the bottom, in the same language as the deck
  // screen's doors; the list takes what is left.
  const int doorH = toybox::kRowHeight;
  const int listTop = body.y + 92;
  const int listH = body.bottom() - doorH - toybox::kMargin - listTop;
  // Two lines per row: an Anki deck name is routinely longer than the width a
  // count column leaves beside it, and a truncated name loses exactly the part
  // that distinguishes "Mandarin: Vocabulary" from "Mandarin: Sentences" --
  // with no ellipsis to warn, since the toybox cuts have no glyph for one.
  const int rowH = toybox::kRowHeight;
  int visible = listH / (rowH + 6);
  if (visible < 1) visible = 1;
  if (visible > model.count - model.topIndex) visible = model.count - model.topIndex;
  model.visibleRows = visible;

  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int i = 0; i < visible; ++i) {
    const int index = model.topIndex + i;
    const DeckPickerModel::Row& row = model.rows[index];
    const fui::Rect r = fui::makeRect(body.x, listTop + i * (rowH + 6), body.width, rowH);

    // The checkbox is the state; a filled square reads at arm's length where a
    // tick does not.
    // A row with no checkbox is not a choice. Greying one is not enough at
    // 1bpp: DarkGray dithers, and at hairline weight it reads as black, so a
    // box the tap ignores looks exactly like a box that works.
    const bool choosable = row.cards > 0;
    const fui::Rect box = fui::makeRect(r.x + 2, r.y + (rowH - 22) / 2, 22, 22);
    if (choosable) {
      screen.target().stroke(box, ink, toybox::kHairline);
      if (row.chosen) {
        screen.target().fill(fui::makeRect(box.x + 5, box.y + 5, 12, 12), ink);
      }
    }

    // Genuinely empty: the count the service sends covers the deck AND its
    // subdecks, so a parent full of subdeck cards reports them and stays
    // choosable. Only a deck with nothing anywhere under it lands here, and
    // the converter refuses those. Say so on the row rather than letting the
    // choice fail three minutes later.
    char count[40];
    if (row.cards > 0) {
      std::snprintf(count, sizeof(count), "%d CARDS", row.cards);
    } else {
      std::snprintf(count, sizeof(count), "NO CARDS OF ITS OWN");
    }
    const int textX = r.x + 22 + toybox::kMargin;
    const int textW = r.right() - textX;
    // Each box gets its own line height: text is centred in its box, so a box
    // shorter than the font's line spills over the one below it.
    screen.target().text(
        fui::makeRect(textX, r.y + 2, textW, 34), row.name,
        syncText(toybox::kUiFont, fui::TextAlign::Left, choosable ? fui::Color::Black : fui::Color::DarkGray));
    screen.target().text(fui::makeRect(textX, r.y + 38, textW, 22), count,
                         syncText(toybox::kTileFont, fui::TextAlign::Left, fui::Color::DarkGray));
    if (choosable) screen.frame().hit(r, ActionPickDeck, static_cast<int16_t>(index));
  }

  if (model.count > visible) {
    char more[48];
    const int shown = model.topIndex + visible;
    if (shown < model.count) {
      std::snprintf(more, sizeof(more), "%d MORE, TAP HERE", model.count - shown);
    } else {
      std::snprintf(more, sizeof(more), "BACK TO THE START, TAP HERE");
    }
    const fui::Rect pager = fui::makeRect(body.x, listTop + visible * (rowH + 6), body.width, 24);
    screen.target().text(pager, more, syncText(toybox::kTileFont, fui::TextAlign::Left, fui::Color::DarkGray));
    screen.frame().hit(pager, ActionPickDeck, -1);  // -1 pages
  }

  fui::ListItem door[1];
  door[0] = fui::ListItem{};
  door[0].label = model.chosenCount > 0 ? "SYNC THESE" : "CHOOSE AT LEAST ONE";
  door[0].enabled = model.chosenCount > 0;
  door[0].actionValue = 1;
  door[0].icon = fui::bitmapFromIcon(icon_refresh_cw_32);
  fui::ListProps list;
  list.items = door;
  list.count = 1;
  list.selectedIndex = -1;
  list.action = ActionPickDone;
  list.sidePadding = toybox::kMargin;
  list.textGap = toybox::kMargin;
  screen.list(list, doorH, fui::LayoutAnchor::Bottom);
}

fui::Rect buildPairQr(toybox::Screen& screen, const char* code) {
  chrome(screen, "SYNC");
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  screen.target().text(fui::makeRect(body.x, body.y, body.width, 48), "PAIR THIS READER",
                       syncText(toybox::kDisplayFont, fui::TextAlign::Center));

  constexpr int16_t kQrSide = 232;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kQrSide) / 2),
                                     static_cast<int16_t>(body.y + 48 + toybox::kMargin * 2), kQrSide, kQrSide);

  // The code, said twice on purpose: the QR for phones, the letters for the
  // person typing it into the pair page by hand.
  const int codeY = qr.bottom() + toybox::kMargin;
  screen.target().text(fui::makeRect(body.x, codeY, body.width, 48), code,
                       syncText(toybox::kDisplayFont, fui::TextAlign::Center));

  screen.target().text(
      fui::makeRect(body.x + toybox::kMargin, codeY + 48 + toybox::kMargin, body.width - toybox::kMargin * 2, 116),
      "Sign in at sync.ma-r-s.com/pair first, then scan this code.",
      syncText(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));
  // Overflow is invisible in these cuts: the renderer appends U+2026 and the
  // face has no glyph for it, so a long line simply stops mid-word -- which
  // is how the clause telling a nervous user they may refuse went missing.
  screen.target().text(fui::makeRect(body.x, body.bottom() - 30, body.width, 24), "CODE LASTS 5 MIN, BACK STOPS",
                       syncText(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
  return qr;
}

PairConfirmLayout buildPairConfirm(toybox::Screen& screen) {
  chrome(screen, "SYNC");
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  screen.target().text(fui::makeRect(body.x, body.y + toybox::kMargin, body.width, 48), "IS THIS YOU?",
                       syncText(toybox::kDisplayFont, fui::TextAlign::Center));

  PairConfirmLayout layout;
  layout.username =
      fui::makeRect(body.x, static_cast<int16_t>(body.y + toybox::kMargin + 48 + toybox::kMargin), body.width, 80);

  screen.target().text(fui::makeRect(body.x + toybox::kMargin, layout.username.bottom() + toybox::kMargin,
                                     body.width - toybox::kMargin * 2, 84),
                       "Nothing is stored yet.",
                       syncText(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));

  // The confirm target: a pill the thumb can reach, tappable because on the
  // Sticky the Confirm button is the power button.
  const int16_t pillH = 56;
  // Clear of the Sticky's hint bar; the X4 Pro has no bar and just gains air.
  layout.pill = fui::makeRect(static_cast<int16_t>(body.x + 44), static_cast<int16_t>(body.bottom() - pillH - 48),
                              static_cast<int16_t>(body.width - 88), pillH);
  screen.target().stroke(layout.pill, ink, toybox::kRule, static_cast<uint8_t>(pillH / 2));
  screen.target().text(fui::makeRect(layout.pill.x, layout.pill.y + (pillH - 26) / 2, layout.pill.width, 26),
                       "YES, PAIR IT", syncText(toybox::kUiFont, fui::TextAlign::Center));
  screen.target().text(fui::makeRect(body.x, layout.pill.y - 34, body.width, 24), "NOT ME? PRESS BACK",
                       syncText(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
  return layout;
}

}  // namespace studyui
