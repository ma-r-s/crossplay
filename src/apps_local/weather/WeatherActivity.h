#pragma once

// WEATHER: today and the next six days for one city, from Open-Meteo.
//
// ---------------------------------------------------------------------------
// The decisions worth knowing (docs/apps/weather.md has the rest).
//
// 1. The forecast is read when the app opens, and when the menu's UPDATE NOW is
//    tapped, over Wi-Fi, and at no other time: nothing in the background,
//    no timer wake. The forks that refreshed on a timer drained batteries.
//
// 2. The last forecast and when it was read stay on the card, so the screen
//    opens on it with no radio at all, and keeps it, labelled with its age,
//    when Wi-Fi is declined or the request fails. Never a blank panel.
//
// 3. The city is typed once, on the keyboard Wikipedia's search uses, and
//    picked from Open-Meteo's own geocoder. The menu changes it.
//
// 4. Celsius or Fahrenheit is a toggle in the menu. Until the player touches
//    it, it follows the chosen city's country.
//
// The three-way split every app here uses: WeatherCore is the rules and the
// parsing (host-tests/weather), WeatherScreens lays out (host-tests/ui), and
// this file keeps what needs hardware: the radio, the card and the panel.
// ---------------------------------------------------------------------------

#include <PaintClock.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WeatherCore.h"
#include "WeatherScreens.h"

class WeatherActivity final : public Activity {
 public:
  WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  // Not while a request is asked for or out: a device that sleeps mid-request
  // wakes with nothing to show for it.
  bool preventAutoSleep() override {
    return pending_ != Pending::None || status_ == Status::Updating ||
           searchState_ == weatherui::SearchModel::State::Searching;
  }

 private:
  enum class View : uint8_t { Main, Search, Menu };
  // The work the next loop pass does, set beside the repaint that announces
  // it so the panel says UPDATING before the radio is busy.
  enum class Pending : uint8_t { None, Forecast, Search };
  // What the last attempt at the forecast came to.
  enum class Status : uint8_t { Current, Updating, Offline, Failed };

  // --- The card ---------------------------------------------------------
  void loadPlace();
  void loadCache();
  void savePlace();
  void saveCache();

  // --- The radio --------------------------------------------------------
  // Runs `what` once Wi-Fi is up, raising the picker first when it is not.
  // Declining leaves the cached forecast, labelled OFFLINE.
  void ensureConnected(Pending what);
  void request(Pending what);
  void fetchForecast();
  void fetchMatches();
  // One GET into a bounded string; false when it failed or was abandoned.
  bool fetchAnswer(const std::string& url, std::string& out);
  // Asked every 50 ms while a request holds the loop: pumps input, and answers
  // true once Back or Home asks to abandon it.
  bool pollDuringRequest();

  // --- Moving between screens ---------------------------------------------
  void openSearch();
  // Up for typing: on opening the search, on tapping the field, and after a
  // search that found nothing, failed or was abandoned, since each of those is
  // answered by typing or by GO.
  void raiseKeyboard();
  // The places found, `delta` pages on, stopping at either end.
  void turnPage(int delta);
  void choose(int match);
  void back();
  void routeAction(int action, int value);
  void handleKey(int value);

  // --- Drawing ------------------------------------------------------------
  weatherui::MainModel mainModel();
  freeink::ui::Rect keyboardRect() const;
  void drawKeyboard();

  View view_ = View::Main;
  Pending pending_ = Pending::None;
  Pending afterConnect_ = Pending::None;
  Status status_ = Status::Current;
  // Opening asks for the forecast on the first loop pass, not in onEnter,
  // because the Wi-Fi picker is an activity and onEnter is too early for one.
  bool refreshOnFirstPass_ = false;
  // A Back release only counts when this activity also saw the press: the
  // picker acts on the press and hands the release back here.
  bool backPressSeen_ = false;
  // What abandoned the request in hand, acted on once it has returned. Back
  // only abandons; Home is a finished Home gesture, which then goes Home;
  // HomeKey is the Home key going down, whose gesture finishes in the main
  // loop.
  enum class Interrupt : uint8_t { None, Back, Home, HomeKey };
  Interrupt interrupt_ = Interrupt::None;
  static const char* interruptName(Interrupt interrupt);

  weather::Place place_;
  bool havePlace_ = false;
  weather::Cache cache_;
  bool haveCache_ = false;

  // Strings the models point into, kept alive for the paint.
  std::string placeLabel_;
  std::string placeQualifier_;
  std::string updated_;

  // Search.
  std::string query_;
  std::string searched_;  // the query the results answer
  weatherui::SearchModel::State searchState_ = weatherui::SearchModel::State::Typing;
  bool searchOffline_ = false;  // a failed search failed for want of Wi-Fi
  std::vector<weather::Match> matches_;
  // The page of places asked for, and how many the last paint drew.
  int searchPage_ = 0;
  int searchPages_ = 0;
  bool shifted_ = false;
  bool symbols_ = false;
  bool keyboardShown_ = false;
  freeink::ui::InteractionBuffer<56> kbInteractions_;
  paintclock::RevealGate kbGate_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
