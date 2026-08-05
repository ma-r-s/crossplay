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
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
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

// The ornament: what is coming, for the next fortnight.
//
// docs/design-language.md asks that anything decorative be made of the app's
// own material and carry the user's own data -- the test being whether a
// screenshot of it would look the same on everyone's device. A review forecast
// passes by construction: it is nothing but the user's own backlog, it is
// different every morning, and it comes from the same pass over cards.dat that
// builds the queue, so it costs nothing to produce.
//
// Bars are outlined rather than filled. This panel repaints whenever a session
// ends, and a block of solid black that changes is exactly what ghosts on
// e-ink; the one bar that is filled is today's, which is the one worth the ink.
void forecast(toybox::Screen& screen, const fui::Rect& box, const DeckModel& model) {
  if (model.forecast == nullptr) return;
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  // Scale to the tallest of the days *ahead*, not to today. Everything overdue
  // piles onto day zero, so on a deck with a backlog today's bar is an order of
  // magnitude taller than the rest and scaling to it flattens the forecast into
  // one column and thirteen empty slots -- which is exactly the panel that
  // taught us this. Today clips instead: it is solid and unmissable either way,
  // and "today is the big one" survives the clipping intact.
  int peak = 1;
  for (int i = 1; i < kForecastDays; ++i) {
    if (model.forecast[i] > peak) peak = model.forecast[i];
  }
  if (peak == 1 && model.forecast[0] > 1) peak = model.forecast[0];

  const int gap = 3;
  const int barWidth = (box.width - gap * (kForecastDays - 1)) / kForecastDays;
  const int baseline = box.bottom();

  for (int i = 0; i < kForecastDays; ++i) {
    const int x = box.x + i * (barWidth + gap);
    const int count = model.forecast[i];
    // Scale to the tallest bar, and give any non-zero day at least a visible
    // stub: a day with three cards due must not read as a day with none.
    int height = count > 0 ? (count * box.height) / peak : 0;
    if (count > 0 && height < 4) height = 4;
    if (height > box.height) height = box.height;

    if (height > 0) {
      const fui::Rect bar = fui::makeRect(x, baseline - height, barWidth, height);
      if (i == 0) {
        screen.target().fill(bar, ink);  // today, solid: the one worth the ink
      } else {
        screen.target().fill(fui::makeRect(bar.x, bar.y, bar.width, toybox::kHairline), ink);
        screen.target().fill(fui::makeRect(bar.x, bar.y, toybox::kHairline, bar.height), ink);
        screen.target().fill(fui::makeRect(bar.right() - toybox::kHairline, bar.y, toybox::kHairline, bar.height), ink);
      }
    }
  }
  // The ground the bars stand on.
  screen.target().fill(fui::makeRect(box.x, baseline, box.width, toybox::kHairline), ink);
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
  screen.target().text(fui::makeRect(body.x, body.y + 62, body.width, 26), state, sub);
  if (waiting > 0) {
    screen.frame().hit(fui::makeRect(body.x, body.y, body.width, 96), ActionStudy, 0);
  }

  screen.target().fill(fui::makeRect(body.x, body.y + 108, body.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  char record[64];
  std::snprintf(record, sizeof(record), "%s   %d CARDS", model.name, model.total);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(body.x, body.y + 122, body.width, 24), record, small);

  // The ornament, bracketed the way the board is.
  const int panelTop = body.y + 210;
  const int panelHeight = 190;
  const fui::Rect panel = fui::makeRect(body.x + 10, panelTop, body.width - 20, panelHeight);
  brackets(screen, fui::makeRect(panel.x - 18, panel.y - 18, panel.width + 36, panel.height + 54), 32);
  forecast(screen, fui::makeRect(panel.x + 12, panel.y + 8, panel.width - 24, panelHeight - 16), model);

  // The caption carries the number, so an empty panel reads as "nothing is
  // scheduled yet" rather than as a panel that failed to draw. On a deck that
  // is all backlog every bar but today's is genuinely zero, and that is worth
  // saying out loud instead of leaving a bracketed box looking broken.
  int ahead = 0;
  if (model.forecast != nullptr) {
    for (int i = 1; i < kForecastDays; ++i) ahead += model.forecast[i];
  }
  char caption_text[48];
  std::snprintf(caption_text, sizeof(caption_text), "NEXT 14 DAYS   %d SCHEDULED", ahead);
  fui::TextStyle caption;
  caption.font = toybox::kTileFont;
  caption.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, panel.bottom() + 44, body.width, 24), caption_text, caption);

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
