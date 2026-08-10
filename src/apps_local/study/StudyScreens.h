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
inline constexpr fui::ActionId ActionSwitchDeck = 3;

// How many days either side of today the ornament shows. Two weeks back is far
// enough to see a habit, two weeks forward far enough to see a backlog coming,
// and twenty-seven bars is as many as read as separate at this width.
inline constexpr int kForecastDays = 14;
inline constexpr int kHistoryDays = 14;

struct DeckModel {
  const char* name = "";
  int due = 0;       // cards waiting today
  int fresh = 0;     // new cards available today
  int reviewed = 0;  // answered in this session
  int recalled = 0;  // of those, not Again
  int total = 0;     // cards in the deck
  // Cards falling due on each of the next kForecastDays days, today first.
  // This is the ornament's data: it comes from the same pass over cards.dat
  // that builds the queue, so it costs nothing extra.
  const int* forecast = nullptr;
  // What actually happened, from revlog.dat: index 0 is today, and there are
  // kHistoryDays of it. Drawn beside the forecast as one timeline, because
  // "what I have been doing" and "what is coming" are the same question asked
  // in two directions.
  //
  // Passed as plain numbers rather than as the study::Stats that produced them,
  // so this header stays free of the deck layer. docs/shelf.md is explicit that
  // a screen knows FreeInkUI and Toybox tokens and nothing else -- that is what
  // keeps host-tests/ui able to build it with no src/ on the include path.
  const int* history = nullptr;
  int streak = 0;
  int retention = -1;  // percent, or -1 when there is nothing to average
  int lifetimeReviews = 0;
  bool sessionOver = false;  // nothing left to answer right now
  bool writeFailed = false;
  // Which of the card's decks this is. With one deck the row is a label; with
  // more it is the switcher, and tapping it cycles. Cycling rather than a list
  // because a card realistically holds two or three decks, and a dedicated
  // screen for choosing among three things is a screen too many.
  int deckIndex = 0;
  int deckCount = 1;
};

void buildDeck(toybox::Screen& screen, const DeckModel& model);

}  // namespace studyui
