#include "StudyScreens.h"

#include <cstdio>

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
  screen.header(header);
  toybox::headerRule(screen);
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
    std::snprintf(headline, sizeof(headline), model.reviewed > 0 ? "DONE" : "ALL CLEAR");
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
    std::snprintf(state, sizeof(state), "NOTHING DUE TODAY");
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
    std::snprintf(record, sizeof(record), "STREAK %d   %d%% RECALL   %d REVIEWS", model.streak, model.retention,
                  model.lifetimeReviews);
  } else if (model.lifetimeReviews > 0) {
    std::snprintf(record, sizeof(record), "%d REVIEWS   NONE THIS FORTNIGHT", model.lifetimeReviews);
  } else {
    // No history at all: name the deck rather than print a row of zeroes,
    // which reads as a broken counter rather than as a fresh start.
    std::snprintf(record, sizeof(record), "%s   %d CARDS", model.name, model.total);
  }
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(body.x, body.y + 122, body.width, 24), record, small);

  // The deck row. One deck: a quiet label naming what is open. More than one:
  // the switcher, and the count says there is somewhere to go -- the arrow
  // alone would say "this goes somewhere" without saying how many somewheres.
  if (model.deckCount > 1) {
    char deckRow[64];
    std::snprintf(deckRow, sizeof(deckRow), "DECK %d/%d   %s   >", model.deckIndex + 1, model.deckCount, model.name);
    const fui::Rect deckRect = fui::makeRect(body.x, body.y + 152, body.width, 26);
    screen.target().text(deckRect, deckRow, small);
    screen.frame().hit(deckRect, ActionSwitchDeck, 0);
  }

  // The ornament, bracketed the way the board is.
  const int panelTop = body.y + 210;
  const int panelHeight = 190;
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
  screen.target().text(fui::makeRect(body.x, panel.bottom() + 44, body.width, 24), caption_text, caption);

  // The bridge door, in the quiet band between the ornament and the big one.
  // Always drawn: before pairing it is the way in, after it is the way to
  // stay current.
  {
    const fui::Rect syncRect = fui::makeRect(body.x, panel.bottom() + 92, body.width, 26);
    screen.target().text(syncRect, "SYNC   >", small);
    screen.frame().hit(syncRect, ActionSync, 0);
  }

  // The lesser door, bottom-anchored: that is where a thumb rests, and it keeps
  // it from competing with the headline.
  fui::ListItem rows[1];
  rows[0] = fui::ListItem{};
  rows[0].label = waiting > 0 ? "START REVIEWING" : "NOTHING TO REVIEW";
  rows[0].enabled = waiting > 0;
  rows[0].actionValue = 1;
  fui::ListProps list;
  list.items = rows;
  list.count = 1;
  list.selectedIndex = -1;
  list.action = ActionStudy;
  screen.list(list, toybox::kRowHeight + 4, fui::LayoutAnchor::Bottom);

  if (model.writeFailed) {
    // Loud, and above the door rather than inside it: a review the user gave
    // that did not reach the card is the one failure this app must never
    // swallow.
    fui::TextStyle warn;
    warn.font = toybox::kTileFont;
    warn.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(body.x, panel.bottom() + 72, body.width, 24), "SOME REVIEWS DID NOT SAVE", warn);
  }
}

}  // namespace studyui
