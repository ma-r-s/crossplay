#pragma once

// WEATHER on screen. Freestanding builders over plain models, host-testable
// through host-tests/ui: no renderer, no Activity, no card, no radio.
//
// Three screens:
//
//   main    the place, the reading now (temperature, sky, high and low, rain),
//           the next six days as rows, and when it was read
//   search  the field the city is typed into and the places the geocoder
//           found; the activity draws the keyboard into the space it leaves
//   menu    the city (a door back to search), Celsius or Fahrenheit, a
//           refresh, and where the forecast comes from
//
// THE MAIN SCREEN IS BUILT IN THREE PASSES ON ONE SCREEN. It needs four
// faces and a target binds three: the band's display cut, the Jersey UI and
// button cuts for the numbers and the days, the huge cut for the temperature
// now, and a reading cut for the place, because a place name is somebody
// else's text and Jersey carries ASCII only ("Zurich" would lose its u-umlaut
// as a hole). buildMainTop() draws the band and MEASURES the place in
// placeFaces; the activity rebinds to dataFaces and buildMainBody() lays the
// whole screen out by ink and draws it; the activity rebinds back and
// drawMainPlace() draws the place where the layout put it. Connections
// rebinds between passes the same way.

#include <cstdint>
#include <string>

#include "../ui/ToyboxScreen.h"
#include "WeatherCore.h"

namespace weatherui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenu = 1,        // the band's gear
  ActionRetry = 2,       // an empty main screen's way to try again
  ActionField = 3,       // the search field: raises the keyboard
  ActionKey = 4,         // a keyboard key (the activity's own table); VALUE is the key
  ActionMatch = 5,       // a place the geocoder found; VALUE is its row
  ActionChangeCity = 6,  // the menu's city row
  ActionCelsius = 7,     // the menu's units
  ActionFahrenheit = 8,  //
  ActionRefresh = 9,     // the menu's refresh
  ActionCloseMenu = 10,  // the menu's DONE
};

// Today is the reading; these are the days after it.
constexpr int kRows = weather::kDays - 1;

// One day, already in the units the player chose.
struct DayRow {
  const char* name = "";  // "WED"
  weather::Icon icon = weather::Icon::Cloud;
  int high = 0;
  int low = 0;
  bool hasHigh = false;
  bool hasLow = false;
  int rain = -1;  // percent, -1 when not given
};

struct MainModel {
  // The place's name, cleaned where it entered. Never empty: placeLabel()
  // falls back to the region and then to the coordinates.
  const char* place = "";

  // A forecast is on the card. Without one the screen says why in
  // emptyHeadline and emptyMessage, and offers RETRY when offerRetry is set.
  bool hasForecast = false;
  int now = 0;
  bool hasNow = false;
  weather::Icon nowIcon = weather::Icon::Cloud;
  const char* sky = "";  // "PARTLY CLOUDY"
  DayRow today;          // today's high, low and rain; its name is not drawn
  DayRow days[kRows];
  int dayCount = 0;
  // "UPDATED 14:07". Drawn under the reading, because that is what it dates.
  const char* updated = "";
  // What the radio is doing about it, in front of `updated`: "UPDATING...",
  // "OFFLINE.", "COULD NOT UPDATE."; empty when the forecast is fresh.
  const char* status = "";

  const char* emptyHeadline = "";
  const char* emptyMessage = "";
  bool offerRetry = false;
};

// The place, fitted and measured in pass one but drawn in pass three, because
// where it goes depends on everything under it.
struct MainPlace {
  std::string text;
  fui::TextStyle style;
  int lines = 1;
  int16_t inkHeight = 0;
};

// Pass one, in placeFaces (SMALL the bold small reading cut, BODY the bold
// reading cut, TITLE the display cut): the band, and the place measured.
MainPlace buildMainTop(toybox::Screen& screen, const MainModel& model);
// Pass two, after the rebind to dataFaces (SMALL the Jersey button cut, BODY
// the Jersey UI cut, TITLE the huge cut): lays the whole screen out by ink,
// draws all of it but the place, and returns where the place's ink starts.
int16_t buildMainBody(toybox::Screen& screen, const MainModel& model, const MainPlace& place);
// Pass three, in placeFaces again: the place, with its cap top on `inkTop`.
void drawMainPlace(toybox::Screen& screen, const MainPlace& place, int16_t inkTop);

// --- Search ------------------------------------------------------------------

struct MatchRow {
  const char* name = "";
  const char* region = "";
};

struct SearchModel {
  const char* query = "";
  enum class State : uint8_t {
    Typing,     // nothing asked yet
    Searching,  // asked; painted before the request goes out
    Results,    // rows below
    NoMatch,    // the geocoder knows no such place
    Failed,     // no Wi-Fi, no answer, or an answer that was not one
  };
  State state = State::Typing;
  MatchRow rows[weather::kMaxMatches];
  int rowCount = 0;
  // Failed: why, in a sentence ("No Wi-Fi.").
  const char* failure = "";
  // Height the activity reserves at the bottom for the keyboard; 0 when it is
  // down.
  int16_t keyboardHeight = 0;
  // No city chosen yet: Back leaves the app rather than returning to a
  // forecast, and the band says what this screen is for.
  bool firstRun = false;
};

// Faces: SMALL the reading cut, BODY the bold reading cut, TITLE the display
// cut. Returns how many rows fitted; the rest are announced, not drawn.
int buildSearch(toybox::Screen& screen, const SearchModel& model);

// --- Menu ----------------------------------------------------------------------

struct MenuModel {
  const char* place = "";
  bool fahrenheit = false;
  const char* updated = "";  // "UPDATED 14:07", or empty before any forecast
};

// Faces: SMALL the Jersey button cut, BODY the bold reading cut, TITLE the
// display cut.
void buildMenu(toybox::Screen& screen, const MenuModel& model);

// The bitmap for a sky at one of the generated sizes (32 or 88).
const freeink::Icon& skyBitmap(weather::Icon icon, int size);

}  // namespace weatherui
