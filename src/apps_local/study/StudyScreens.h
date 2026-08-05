#pragma once

// The study app's chrome, as free functions over plain models.
//
// Nothing here touches the renderer, storage or the Activity, so
// host-tests/ui can build these against a fake draw target and assert on what
// they drew and what they made tappable. See docs/shelf.md for why that split
// is worth keeping, and ToyboxScreen.h for why everything goes through
// `toybox::Screen` rather than calling FreeInkUI components directly.
//
// The card face is deliberately NOT here: it is the app's own surface, drawn by
// hand into the body rect, in the same sense that a chess board is.

#include "../ui/ToyboxScreen.h"

namespace fui = freeink::ui;

namespace studyui {

// Actions the deck screen can produce.
inline constexpr fui::ActionId ActionStudy = 1;
inline constexpr fui::ActionId ActionForecast = 2;

// How many days of the forecast the ornament shows. Two weeks is the span over
// which a review backlog is actually actionable, and fourteen bars is as many
// as read as separate at this width.
inline constexpr int kForecastDays = 14;

struct DeckModel {
  const char* name = "";
  int due = 0;        // cards waiting today
  int fresh = 0;      // new cards available today
  int reviewed = 0;   // answered in this session
  int recalled = 0;   // of those, not Again
  int total = 0;      // cards in the deck
  // Cards falling due on each of the next kForecastDays days, today first.
  // This is the ornament's data: it comes from the same pass over cards.dat
  // that builds the queue, so it costs nothing extra.
  const int* forecast = nullptr;
  bool sessionOver = false;  // nothing left to answer right now
  bool writeFailed = false;
};

void buildDeck(toybox::Screen& screen, const DeckModel& model);

}  // namespace studyui
