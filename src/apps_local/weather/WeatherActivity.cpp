#include "WeatherActivity.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../../CrossPointSettings.h"
#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "../ClockFloor.h"
#include "../Shelf.h"
#include "../rss/RssFetch.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kTag = "WTHR";

// Two plain files in a folder of their own: readable in an editor, and the
// forecast is still there with the radio off.
constexpr const char* kDir = "/.crosspoint/weather";
constexpr const char* kPlacePath = "/.crosspoint/weather/place.txt";
constexpr const char* kCachePath = "/.crosspoint/weather/forecast.txt";
// Both files are a few hundred bytes; this only ever stops a damaged one.
constexpr size_t kMaxFileBytes = 4096;
// A forecast answer is about 1.5KB and a geocoder answer for ten places about
// 6KB. Anything past this is not an answer to either question.
constexpr size_t kMaxAnswerBytes = 32 * 1024;

constexpr const char* kForecastBase = "https://api.open-meteo.com/v1/forecast";
constexpr const char* kGeocodeBase = "https://geocoding-api.open-meteo.com/v1/search";

// The simulator points both at a local server for deterministic shots and the
// failure cases (tools_local/weather/fake_open_meteo.py). Never on a device.
std::string endpoint(const char* env, const char* fallback) {
#ifdef SIMULATOR
  if (const char* override = std::getenv(env)) return override;
#else
  (void)env;
#endif
  return fallback;
}

// The device clock, when it can be today (the fork's one floor, ClockFloor.h:
// no earlier than the day this firmware was built); 0 when it cannot, and the
// forecast's own reading time then dates it. A never-set RTC reading a
// plausible February would otherwise say UPDATED 17 NOV against it and pick
// the wrong today out of the week.
int64_t nowOrZero() {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  return clockfloor::trusted(now) ? now : 0;
}

bool readText(const char* path, std::string& out) {
  out.clear();
  if (Storage.exists(path)) return Storage.readFileToString(kTag, path, kMaxFileBytes, out);
  // A write that lost its rename leaves the new copy under .tmp.
  const std::string tmp = std::string(path) + ".tmp";
  return Storage.exists(tmp.c_str()) && Storage.readFileToString(kTag, tmp, kMaxFileBytes, out);
}

// Beside itself, then moved over the old one: openFileForWrite truncates at
// open, so a write in place that lost power would leave nothing.
bool writeText(const char* path, const std::string& text) {
  Storage.ensureDirectoryExists(kDir);
  const std::string tmp = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite(kTag, tmp.c_str(), file)) return false;
    if (file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(tmp.c_str());
      return false;
    }
    file.flush();
  }
  if (!Storage.replaceFile(tmp.c_str(), path)) {
    if (Storage.exists(path)) Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

// The font each of the screens' faces names (WeatherScreens.h says which
// pass binds which).
int fontFor(const weatherui::Face face) {
  switch (face) {
    case weatherui::Face::ReadingSmall:
      return toybox::kReadingSmallFontId;
    case weatherui::Face::Reading:
      return toybox::kReadingFontId;
    case weatherui::Face::ReadingBoldSmall:
      return toybox::kReadingBoldSmallFontId;
    case weatherui::Face::ReadingBold:
      return toybox::kReadingBoldFontId;
    case weatherui::Face::Button:
      return toybox::kButtonFontId;
    case weatherui::Face::Ui:
      return toybox::kUiFontId;
    case weatherui::Face::Display:
      return toybox::kDisplayFontId;
    case weatherui::Face::Huge:
      return toybox::kHugeFontId;
  }
  return toybox::kUiFontId;
}

void bindPass(void* context, const weatherui::Pass& pass) {
  auto& target = *static_cast<fui::GfxRendererTarget*>(context);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, fontFor(pass.small));
  target.setFont(fui::GfxRendererTarget::FONT_BODY, fontFor(pass.body));
  target.setFont(fui::GfxRendererTarget::FONT_TITLE, fontFor(pass.title));
}

weatherui::DayRow rowFor(const weather::Day& day, const weather::Units units) {
  weatherui::DayRow row;
  row.name = weather::weekdayShort(day);
  row.icon = weather::skyIcon(day.code, true);
  row.hasHigh = day.maxC != weather::kNoTemp;
  row.hasLow = day.minC != weather::kNoTemp;
  if (row.hasHigh) row.high = weather::displayTemp(day.maxC, units);
  if (row.hasLow) row.low = weather::displayTemp(day.minC, units);
  row.rain = day.rainPct;
  return row;
}

}  // namespace

std::unique_ptr<Activity> WeatherActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WeatherActivity>(renderer, mappedInput);
}

// --- Lifecycle -----------------------------------------------------------------

void WeatherActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadPlace();
  loadCache();
  status_ = Status::Current;
  pending_ = Pending::None;
  if (havePlace_) {
    view_ = View::Main;
    // The forecast on the card goes up first, whatever the radio says, already
    // saying it is being updated: the first loop pass asks for a fresh one.
    refreshOnFirstPass_ = true;
    status_ = Status::Updating;
  } else {
    openSearch();
  }
  LOG_INF(kTag, "opened: place %s, cached forecast %s", havePlace_ ? place_.name.c_str() : "(none)",
          haveCache_ ? "yes" : "no");
  requestUpdate();
}

void WeatherActivity::onExit() {
  Activity::onExit();
  // The radio comes down with the app, unless Developer Mode raised it: see
  // HackerNewsActivity::onExit, whose teardown this is.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// --- The card ----------------------------------------------------------------------

void WeatherActivity::loadPlace() {
  havePlace_ = false;
  std::string text;
  if (!readText(kPlacePath, text)) return;
  weather::Place place;
  if (!weather::parsePlace(text, place)) {
    LOG_ERR(kTag, "place.txt is on the card and could not be read; asking for a city again");
    return;
  }
  // Cleaned again on the way in: the file is on a card anyone can edit.
  place.name = weather::placeLabel(weather::cleanName(place.name, toybox::readingCutsCarry),
                                   weather::cleanName(place.region, toybox::readingCutsCarry), place.lat, place.lon);
  place.region = weather::cleanName(place.region, toybox::readingCutsCarry);
  place_ = place;
  havePlace_ = true;
}

void WeatherActivity::loadCache() {
  haveCache_ = false;
  std::string text;
  if (!havePlace_ || !readText(kCachePath, text)) return;
  weather::Cache cache;
  if (!weather::parseCache(text, cache)) {
    LOG_ERR(kTag, "forecast.txt is on the card and could not be read; it will be replaced");
    return;
  }
  if (!weather::cacheIsFor(cache, place_)) {
    LOG_INF(kTag, "the forecast on the card is for another place; not shown");
    return;
  }
  cache_ = cache;
  haveCache_ = true;
}

void WeatherActivity::savePlace() {
  if (!writeText(kPlacePath, weather::serializePlace(place_))) LOG_ERR(kTag, "could not write place.txt");
}

void WeatherActivity::saveCache() {
  if (!writeText(kCachePath, weather::serializeCache(cache_))) LOG_ERR(kTag, "could not write forecast.txt");
}

// --- The radio ---------------------------------------------------------------------

void WeatherActivity::ensureConnected(const Pending what) {
  // Asked of the radio every time rather than latched; see HackerNewsActivity.
  if (WiFi.status() == WL_CONNECTED) {
    request(what);
    return;
  }
  afterConnect_ = what;
  backPressSeen_ = false;
  WiFi.mode(WIFI_STA);
  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
        const Pending what = afterConnect_;
        afterConnect_ = Pending::None;
        if (result.isCancelled) {
          // Declining is not wanting out: the forecast on
          // the card still stands, now marked offline.
          LOG_INF(kTag, "no Wi-Fi; %s", what == Pending::Search ? "search not sent" : "showing what is on the card");
          RenderLock lock(*this);
          if (what == Pending::Search) {
            searchState_ = weatherui::SearchModel::State::Failed;
            searchOffline_ = true;
            raiseKeyboard();
          } else {
            status_ = Status::Offline;
          }
          interactionsReady_ = false;
          requestUpdate();
          return;
        }
        request(what);
      });
}

void WeatherActivity::request(const Pending what) {
  if (what == Pending::None) return;
  {
    RenderLock lock(*this);
    pending_ = what;
    if (what == Pending::Forecast) {
      status_ = Status::Updating;
    } else {
      searchState_ = weatherui::SearchModel::State::Searching;
    }
  }
  requestUpdate();
}

// One GET that can be abandoned: RSS's cancellable fetch (RssFetch.h), so the
// radio check, the agent string and a 20 s timeout are the ones RSS uses, and
// Back is live for the whole wait. HttpDownloader's door could not do this: it
// waits 60 s a socket operation and takes no poll, so a hung host held the
// app until it gave up.
bool WeatherActivity::fetchAnswer(const std::string& url, std::string& out) {
  out.clear();
  interrupt_ = Interrupt::None;
  bool overLimit = false;
  const unsigned long started = millis();
  const rss::FetchOutcome outcome = rss::fetchStreaming(
      url,
      [&out, &overLimit](const uint8_t* data, const size_t len) {
        if (out.size() + len > kMaxAnswerBytes) {
          overLimit = true;
          return false;
        }
        out.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      [this] { return pollDuringRequest(); });
  const bool ok = outcome.complete && !outcome.aborted && !overLimit;
  // The endpoint without its query: the whole URL fills the log line and cut
  // off the outcome after it, which is the part a log is read for.
  const std::string endpointOnly = url.substr(0, url.find('?'));
  LOG_INF(kTag, "GET %s: %s, status %d, %u bytes, %lums", endpointOnly.c_str(),
          outcome.aborted ? "abandoned" : (ok ? "ok" : "failed"), outcome.status, static_cast<unsigned>(out.size()),
          millis() - started);
  return ok;
}

bool WeatherActivity::pollDuringRequest() {
  // The request holds the loop, so nothing else is sampling input: this is the
  // one place this app pumps it, and only while a request is out (the
  // input-pump rule).
  mappedInput.update();
  if (mappedInput.wasHomeGesture()) {
    // A finished Home gesture (the bottom-edge swipe, or a Home key whose tap
    // resolved during the wait) is one-shot and this pump has eaten it, so it
    // is honoured here, after the abort.
    interrupt_ = Interrupt::Home;
  } else if (gpio.hasHomeKey() && gpio.wasHomeKeyPressed()) {
    // The Home key abandons the request the moment it goes down. Its tap is
    // told from a double tap only after the key comes up and a window passes,
    // and waiting for that inside this poll never ends where input edges last
    // a frame (the simulator); the gesture finishes in the main loop instead
    // and does whatever Home is set to do there.
    interrupt_ = Interrupt::HomeKey;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
             mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    interrupt_ = Interrupt::Back;
  }
  return interrupt_ != Interrupt::None;
}

void WeatherActivity::fetchForecast() {
  if (!havePlace_) return;
  const std::string url =
      weather::forecastUrl(endpoint("WEATHER_FORECAST_URL", kForecastBase).c_str(), place_.lat, place_.lon);
  std::string answer;
  weather::Forecast forecast;
  const bool fetched = fetchAnswer(url, answer);
  const bool parsed = fetched && weather::parseForecast(answer, forecast);
  if (fetched && !parsed) {
    LOG_ERR(kTag, "forecast answer was not a forecast (%u bytes): %.80s", static_cast<unsigned>(answer.size()),
            answer.c_str());
  }
  RenderLock lock(*this);
  if (interrupt_ != Interrupt::None) {
    // Abandoned, not failed: the forecast on the card stands as it was.
    status_ = Status::Current;
    LOG_INF(kTag, "forecast request abandoned by %s", interruptName(interrupt_));
    return;
  }
  if (!parsed) {
    status_ = WiFi.status() == WL_CONNECTED ? Status::Failed : Status::Offline;
    LOG_INF(kTag, "forecast not updated; showing %s", haveCache_ ? "the one on the card" : "nothing yet");
    return;
  }
  cache_.forecast = forecast;
  cache_.fetchedUtc = nowOrZero();
  cache_.lat = place_.lat;
  cache_.lon = place_.lon;
  haveCache_ = true;
  status_ = Status::Current;
  saveCache();
  LOG_INF(kTag, "forecast for %s: %d days, now %d tenths C, code %d", place_.name.c_str(), forecast.dayCount,
          forecast.nowC, forecast.nowCode);
}

void WeatherActivity::fetchMatches() {
  const std::string query = weather::cleanQuery(query_);
  const std::string url = weather::geocodeUrl(endpoint("WEATHER_GEOCODE_URL", kGeocodeBase).c_str(), query);
  std::string answer;
  std::vector<weather::Match> found;
  const bool fetched = fetchAnswer(url, answer);
  const bool parsed = fetched && weather::parseMatches(answer, toybox::readingCutsCarry, found);
  RenderLock lock(*this);
  searched_ = query;
  if (interrupt_ != Interrupt::None) {
    // Back to typing what was typed, the keyboard up as it was before GO.
    searchState_ = weatherui::SearchModel::State::Typing;
    raiseKeyboard();
    LOG_INF(kTag, "search abandoned by %s", interruptName(interrupt_));
    return;
  }
  if (!parsed) {
    if (fetched) LOG_ERR(kTag, "geocoder answer was not a list: %.80s", answer.c_str());
    searchState_ = weatherui::SearchModel::State::Failed;
    searchOffline_ = WiFi.status() != WL_CONNECTED;
    raiseKeyboard();
    return;
  }
  matches_ = std::move(found);
  searchPage_ = 0;
  searchState_ = matches_.empty() ? weatherui::SearchModel::State::NoMatch : weatherui::SearchModel::State::Results;
  // Nothing found is a spelling to fix: the keyboard comes back for it.
  if (matches_.empty()) raiseKeyboard();
  LOG_INF(kTag, "search \"%s\": %d places", query.c_str(), static_cast<int>(matches_.size()));
}

// --- Moving between screens -----------------------------------------------------------

void WeatherActivity::openSearch() {
  view_ = View::Search;
  query_.clear();
  searched_.clear();
  matches_.clear();
  searchPage_ = 0;
  searchPages_ = 0;
  searchState_ = weatherui::SearchModel::State::Typing;
  shifted_ = true;  // a city starts with a capital
  symbols_ = false;
  raiseKeyboard();
}

void WeatherActivity::raiseKeyboard() {
  if (!keyboardShown_) {
    keyboardShown_ = true;
    kbGate_.arm();
  }
  interactionsReady_ = false;
}

const char* WeatherActivity::interruptName(const Interrupt interrupt) {
  switch (interrupt) {
    case Interrupt::Back:
      return "Back";
    case Interrupt::Home:
      return "Home";
    case Interrupt::HomeKey:
      return "the Home key";
    case Interrupt::None:
      break;
  }
  return "nothing";
}

void WeatherActivity::turnPage(const int delta) {
  {
    RenderLock lock(*this);
    const int last = searchPages_ - 1;
    const int page = searchPage_ + delta;
    if (last <= 0 || page < 0 || page > last) return;
    searchPage_ = page;
    interactionsReady_ = false;
  }
  requestUpdate();
}

void WeatherActivity::choose(const int index) {
  if (index < 0 || index >= static_cast<int>(matches_.size())) return;
  const weather::Match& match = matches_[static_cast<size_t>(index)];
  weather::Place place;
  place.name = match.name;
  place.region = match.region;
  std::memcpy(place.country, match.country, sizeof(place.country));
  place.lat = match.lat;
  place.lon = match.lon;
  // A choice the player made about units outlives the city; until there is
  // one, the units are the new country's.
  place.unitsChosen = havePlace_ && place_.unitsChosen;
  place.units = place.unitsChosen ? place_.units : weather::defaultUnitsFor(match.country);
  LOG_INF(kTag, "chose %s (%s) %.4f,%.4f, %s", place.name.c_str(), place.country, place.lat, place.lon,
          place.units == weather::Units::Fahrenheit ? "F" : "C");
  {
    RenderLock lock(*this);
    place_ = place;
    havePlace_ = true;
    haveCache_ = false;
    status_ = Status::Current;
    view_ = View::Main;
    keyboardShown_ = false;
    interactionsReady_ = false;
  }
  savePlace();
  ensureConnected(Pending::Forecast);
  requestUpdate();
}

void WeatherActivity::back() {
  switch (view_) {
    case View::Main:
      shelf::leave(renderer, mappedInput);
      return;
    case View::Menu:
      view_ = View::Main;
      break;
    case View::Search:
      if (!havePlace_) {
        shelf::leave(renderer, mappedInput);
        return;
      }
      keyboardShown_ = false;
      view_ = View::Main;
      break;
  }
  interactionsReady_ = false;
  requestUpdate();
}

void WeatherActivity::routeAction(const int action, const int value) {
  switch (action) {
    case weatherui::ActionMenu:
      view_ = View::Menu;
      break;
    case weatherui::ActionCloseMenu:
      view_ = View::Main;
      break;
    case weatherui::ActionRetry:
      ensureConnected(Pending::Forecast);
      break;
    case weatherui::ActionRefresh:
      view_ = View::Main;
      ensureConnected(Pending::Forecast);
      break;
    case weatherui::ActionChangeCity:
      openSearch();
      break;
    case weatherui::ActionCelsius:
    case weatherui::ActionFahrenheit:
      place_.units = action == weatherui::ActionFahrenheit ? weather::Units::Fahrenheit : weather::Units::Celsius;
      place_.unitsChosen = true;
      savePlace();
      LOG_INF(kTag, "units now %s", place_.units == weather::Units::Fahrenheit ? "F" : "C");
      break;
    case weatherui::ActionField:
      raiseKeyboard();
      break;
    case weatherui::ActionMatch:
      choose(value);
      return;
    case weatherui::ActionPage:
      turnPage(value - searchPage_);
      return;
    default:
      return;
  }
  interactionsReady_ = false;
  requestUpdate();
}

void WeatherActivity::handleKey(const int value) {
  const fui::KeyboardLayout& layout =
      fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted_, symbols_, true, false);
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      shifted_ = !shifted_;
      break;
    case fui::QWERTY_KEY_MODE:
      symbols_ = !symbols_;
      shifted_ = false;
      break;
    case fui::QWERTY_KEY_LANG:
      return;
    case fui::QWERTY_KEY_ENTER: {
      // GO: the keyboard goes down and the places it found get the panel.
      if (weather::cleanQuery(query_).size() < weather::kMinQuery) return;
      keyboardShown_ = false;
      interactionsReady_ = false;
      ensureConnected(Pending::Search);
      return;
    }
    case fui::QWERTY_KEY_BACKSPACE:
      if (query_.empty()) break;
      query_.pop_back();
      searchState_ = weatherui::SearchModel::State::Typing;
      break;
    default: {
      const char* out = fui::keyboardOutputFor(layout, static_cast<int16_t>(value));
      if (!out) return;
      if (query_.size() + strlen(out) > weather::kMaxQuery) return;
      query_ += out;
      if (shifted_ && !symbols_) shifted_ = false;
      searchState_ = weatherui::SearchModel::State::Typing;
      break;
    }
  }
  requestUpdate();
}

// --- Input -----------------------------------------------------------------------------

void WeatherActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen_ = true;

  if (refreshOnFirstPass_) {
    refreshOnFirstPass_ = false;
    ensureConnected(Pending::Forecast);
    return;
  }

  if (pending_ != Pending::None) {
    const Pending what = pending_;
    pending_ = Pending::None;
    if (what == Pending::Forecast) {
      fetchForecast();
    } else {
      fetchMatches();
    }
    interactionsReady_ = false;
    // The Back that abandoned a request did only that: the screen stays where
    // it was, the forecast on the card or the search as typed, and the next
    // Back leaves. A finished Home gesture goes Home; a Home key abandons and
    // its gesture finishes in the main loop, which goes Home in its own time.
    const Interrupt interrupt = interrupt_;
    interrupt_ = Interrupt::None;
    backPressSeen_ = false;
    if (interrupt == Interrupt::Home) {
      onGoHome();
      return;
    }
    requestUpdate();
    return;
  }

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    back();
    return;
  }

  // The places found, a page at a time: the side keys and a vertical swipe,
  // the same way round as every list here.
  if (view_ == View::Search && searchState_ == weatherui::SearchModel::State::Results && !keyboardShown_) {
    const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward) || swipe == MappedInputManager::SwipeDir::Up) {
      turnPage(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageBack) || swipe == MappedInputManager::SwipeDir::Down) {
      turnPage(-1);
      return;
    }
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);

  if (view_ == View::Search && keyboardShown_ && kbGate_.revealed()) {
    const fui::ActionEvent key = kbInteractions_.routePublished(input);
    if (key.action == weatherui::ActionKey) {
      handleKey(static_cast<int>(key.value));
      return;
    }
  }
  if (!interactionsReady_) return;
  const fui::ActionEvent event = interactions_.route(input);
  routeAction(static_cast<int>(event.action), static_cast<int>(event.value));
}

// --- Drawing ---------------------------------------------------------------------------

weatherui::MainModel WeatherActivity::mainModel() {
  weatherui::MainModel model;
  placeLabel_ = weather::placeLabel(place_.name, place_.region, place_.lat, place_.lon);
  placeQualifier_ = weather::placeQualifier(place_.name, place_.region);
  model.place = placeLabel_.c_str();
  model.region = placeQualifier_.c_str();
  switch (status_) {
    case Status::Current:
      model.status = weatherui::Status::Current;
      break;
    case Status::Updating:
      model.status = weatherui::Status::Updating;
      break;
    case Status::Offline:
      model.status = weatherui::Status::Offline;
      break;
    case Status::Failed:
      model.status = weatherui::Status::Failed;
      break;
  }
  if (!haveCache_) return model;

  const weather::Forecast& f = cache_.forecast;
  const weather::Units units = place_.units;
  const int64_t now = nowOrZero();
  updated_ = weather::updatedLabel(cache_, now, SETTINGS.clockFormat == 1);
  model.updated = updated_.c_str();
  const int first = weather::firstDay(cache_, now);
  if (first < 0) {
    // Every day on the card is past: no week, only how old it is.
    model.stale = true;
    return model;
  }
  model.hasForecast = true;
  // The reading is now only on the day it was taken; on a later one, after a
  // failed update or with the radio declined, today's row stands in.
  const weather::Now reading = weather::nowFor(cache_, now);
  model.hasNow = reading.reading && reading.tempC != weather::kNoTemp;
  if (model.hasNow) model.now = weather::displayTemp(reading.tempC, units);
  model.nowIcon = weather::skyIcon(reading.code, reading.isDay);
  model.sky = weather::skyWords(reading.code);
  model.today = rowFor(f.days[first], units);
  model.chance = weather::chanceWord(f.days[first].code);
  for (int i = first + 1; i < f.dayCount && model.dayCount < weatherui::kRows; ++i) {
    model.days[model.dayCount++] = rowFor(f.days[i], units);
  }
  return model;
}

fui::Rect WeatherActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const fui::KeyboardLayout& layout =
      fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted_, symbols_, true, false);
  const int rows = layout.rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int height = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  return fui::Rect{static_cast<int16_t>((pageWidth - width) / 2), static_cast<int16_t>(pageHeight - 8 - height),
                   static_cast<int16_t>(width), static_cast<int16_t>(height)};
}

// The keyboard Wikipedia's search draws, with its own table: it registers more
// hit rects than a toybox screen holds, and it is routed first.
void WeatherActivity::drawKeyboard() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  kbInteractions_.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<56> frame(target, device, noInput, kbInteractions_);

  fui::KeyboardProps props;
  const fui::KeyboardLayout& layout =
      fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted_, symbols_, true, false);
  props.layout = &layout;
  props.keyAction = weatherui::ActionKey;
  props.okLabel = "GO";
  props.shiftLabel = tr(STR_KEY_SHIFT);
  props.modeLabel = symbols_ ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.selectedIndex = -1;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  const fui::Rect kb = keyboardRect();
  props.bottomHitOverflow = static_cast<int16_t>(renderer.getScreenHeight() - kb.bottom());
  fui::keyboard(frame, kb, props);
  kbInteractions_.publish();
  kbGate_.markBuilt();
}

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const fui::InputSnapshot noInput{};
  // Each screen binds its own faces, pass by pass (WeatherScreens.h).
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const weatherui::Rebind bind{&target, &bindPass};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  switch (view_) {
    case View::Main: {
      const weatherui::MainModel model = mainModel();
      weatherui::drawMain(screen, model, bind);
      toybox::reportOverflow(interactions_, "Weather");
      break;
    }
    case View::Search: {
      weatherui::SearchModel model;
      model.query = query_.c_str();
      model.state = searchState_;
      model.firstRun = !havePlace_;
      model.offline = searchOffline_;
      model.page = searchPage_;
      if (searchState_ == weatherui::SearchModel::State::NoMatch) model.query = searched_.c_str();
      model.rowCount = static_cast<int>(matches_.size());
      for (int i = 0; i < model.rowCount; ++i) {
        model.rows[i].name = matches_[static_cast<size_t>(i)].name.c_str();
        model.rows[i].region = matches_[static_cast<size_t>(i)].region.c_str();
      }
      if (keyboardShown_) {
        model.keyboardHeight = static_cast<int16_t>(renderer.getScreenHeight() - keyboardRect().y + 8);
      }
      const weatherui::SearchLayout layout = weatherui::drawSearch(screen, model, bind);
      searchPage_ = layout.page;
      searchPages_ = layout.pageCount;
      toybox::reportOverflow(interactions_, "Weather search");
      break;
    }
    case View::Menu: {
      weatherui::MenuModel model;
      placeLabel_ = weather::placeLabel(place_.name, place_.region, place_.lat, place_.lon);
      placeQualifier_ = weather::placeQualifier(place_.name, place_.region);
      model.place = placeLabel_.c_str();
      model.region = placeQualifier_.c_str();
      model.fahrenheit = place_.units == weather::Units::Fahrenheit;
      updated_ = haveCache_ ? weather::updatedLabel(cache_, nowOrZero(), SETTINGS.clockFormat == 1) : std::string();
      model.updated = updated_.c_str();
      weatherui::drawMenu(screen, model, bind);
      toybox::reportOverflow(interactions_, "Weather menu");
      break;
    }
  }
  interactionsReady_ = true;
  if (view_ == View::Search && keyboardShown_) drawKeyboard();
  renderer.displayBuffer();
}
