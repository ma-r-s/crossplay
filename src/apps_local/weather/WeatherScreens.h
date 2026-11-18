#pragma once

// WEATHER on screen. Freestanding builders over plain models, host-testable
// through host-tests/ui: no renderer, no Activity, no card, no radio.
//
// Three screens:
//
//   main    the place, the reading now (temperature, sky, high and low, the
//           chance of rain or snow), the days after today as rows, and when it
//           was read
//   search  the field the city is typed into and the places the geocoder
//           found, a page at a time; the activity draws the keyboard into the
//           space it leaves
//   menu    the city (a door back to search), Celsius or Fahrenheit, and an
//           update
//
// A SCREEN BINDS ITS FACES ITSELF, pass by pass, through the Rebind it is
// handed. A target binds three faces and the main screen needs five: the
// band's display cut, the Jersey cuts for the numbers and the days, the huge
// cut for the temperature now, and two bold reading cuts for the place,
// because a place name is somebody else's text and Jersey carries ASCII only
// ("Zurich" would print its u-umlaut as a hole). The faces each pass binds
// are the Pass constants below and the order is in the builders, so the host
// suite binds exactly what the device binds: a menu that once reached Jersey
// for a long name (its fitting ladder stepped down into the button cut) is now
// a test failure, not a screenshot.

#include <cstdint>
#include <string>

#include "../ui/ToyboxScreen.h"
#include "WeatherCore.h"

namespace weatherui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenu = 1,        // the band's gear
  ActionRetry = 2,       // TRY AGAIN or UPDATE NOW on a main screen with no week
  ActionField = 3,       // the search field: raises the keyboard
  ActionKey = 4,         // a keyboard key (the activity's own table); VALUE is the key
  ActionMatch = 5,       // a place the geocoder found; VALUE is its index
  ActionChangeCity = 6,  // the menu's city row
  ActionCelsius = 7,     // the menu's units
  ActionFahrenheit = 8,  //
  ActionRefresh = 9,     // the menu's UPDATE NOW
  ActionCloseMenu = 10,  // the menu's DONE
  ActionPage = 11,       // a page of places; VALUE is the page
};

// --- Faces -------------------------------------------------------------------

enum class Face : uint8_t { ReadingSmall, Reading, ReadingBoldSmall, ReadingBold, Button, Ui, Display, Huge };

// Whether a face draws past ASCII. The reading cuts carry Latin-1 and the
// letters readingCutsCarry() answers for; the Jersey cuts carry ASCII and
// nothing else.
constexpr bool carriesAccents(const Face face) { return face <= Face::ReadingBold; }

// What one pass binds into the target's three slots.
struct Pass {
  Face small;
  Face body;
  Face title;
};

// A place, on any screen: bold reading cuts in both sizes, so a long name
// steps down a size without losing its weight or its letters.
constexpr Pass kPlacePass{Face::ReadingBoldSmall, Face::ReadingBold, Face::Display};
// The main screen's numbers and words: Jersey, with the huge cut for now.
constexpr Pass kDataPass{Face::Button, Face::Ui, Face::Huge};
// The menu's band, labels and buttons: Jersey.
constexpr Pass kMenuPass{Face::Button, Face::Ui, Face::Display};
// The search: what was typed and the names bold, regions and sentences
// regular, the band in Jersey.
constexpr Pass kSearchPass{Face::Reading, Face::ReadingBold, Face::Display};

// Binds a pass's faces into the target the screen is drawing on: a function
// and its context rather than a std::function, which would heap-allocate a
// closure on every paint.
struct Rebind {
  void* context = nullptr;
  void (*bind)(void* context, const Pass& pass) = nullptr;
  void operator()(const Pass& pass) const {
    if (bind != nullptr) bind(context, pass);
  }
};

// --- Main --------------------------------------------------------------------

// Today is the reading; these are the days after it.
constexpr int kRows = weather::kDays - 1;

// What the radio did about the forecast on the card.
enum class Status : uint8_t {
  Current,   // nothing to say: it is the latest, or a request was abandoned
  Updating,  // a request is out
  Offline,   // no Wi-Fi
  Failed,    // a request went out and nothing usable came back
};

// One day, already in the units the player chose.
struct DayRow {
  const char* name = "";                     // "WED"
  weather::Icon icon = weather::Icon::None;  // None draws no sky
  int high = 0;
  int low = 0;
  bool hasHigh = false;
  bool hasLow = false;
  int rain = -1;  // percent, -1 when not given
};

struct MainModel {
  // "Springfield, Illinois": the place and what tells it from another of the
  // name (weather::placeTitle). Never empty.
  const char* place = "";

  // A forecast with today in it is on the card. Without one the screen says
  // why, and offers a way to try again when that can help.
  bool hasForecast = false;
  // A forecast is on the card but every day of it is past: the place, when it
  // was read and UPDATE NOW, and no week, because a week that has happened is
  // not the coming one.
  bool stale = false;

  int now = 0;
  bool hasNow = false;
  weather::Icon nowIcon = weather::Icon::None;
  const char* sky = "";     // "PARTLY CLOUDY"
  DayRow today;             // today's high, low and chance; its name is not drawn
  const char* chance = "";  // what today's chance is a chance of: "RAIN" or "SNOW"
  DayRow days[kRows];
  int dayCount = 0;
  // "UPDATED 14:07", on a line of its own under the reading it dates; empty
  // when nothing is on the card.
  const char* updated = "";
  Status status = Status::Current;
};

// Binds kPlacePass, kDataPass, kPlacePass, in that order: the place measured,
// everything else laid out by ink and drawn, then the place drawn where the
// layout put it.
void drawMain(toybox::Screen& screen, const MainModel& model, const Rebind& bind);

// --- Search ------------------------------------------------------------------

struct MatchRow {
  const char* name = "";
  const char* region = "";  // empty when it would only repeat the name
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
  // Failed with the radio down, rather than with no usable answer.
  bool offline = false;
  // Height the activity reserves at the bottom for the keyboard; 0 when it is
  // down. With the keyboard up the field is being edited, so the places found
  // for what it said before are not shown.
  int16_t keyboardHeight = 0;
  // No city chosen yet: the band says what this screen is for.
  bool firstRun = false;
  // The page of places asked for; clamped to the pages there are.
  int page = 0;
};

// Which places the search showed: the page drawn, how many pages the places
// take, and the rows on it.
struct SearchLayout {
  int page = 0;
  int pageCount = 0;
  int first = 0;
  int shown = 0;
};

// Binds kSearchPass.
SearchLayout drawSearch(toybox::Screen& screen, const SearchModel& model, const Rebind& bind);

// --- Menu ----------------------------------------------------------------------

struct MenuModel {
  const char* place = "";  // as on the main screen
  bool fahrenheit = false;
  const char* updated = "";  // "UPDATED 14:07", or empty before any forecast
};

// Binds kPlacePass, kMenuPass, kPlacePass, as the main screen does.
void drawMenu(toybox::Screen& screen, const MenuModel& model, const Rebind& bind);

// The bitmap for a sky at one of the generated sizes (32 or 88); null for
// Icon::None.
const freeink::Icon* skyBitmap(weather::Icon icon, int size);

}  // namespace weatherui
