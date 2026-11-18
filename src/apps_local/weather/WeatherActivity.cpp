#include "WeatherActivity.h"

#include <Arduino.h>
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
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
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

// The device clock, when it holds a present-day time; 0 when it does not, and
// the forecast's own reading time then dates it.
constexpr int64_t kClockFloor = 1700000000;
int64_t nowOrZero() {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  return now > kClockFloor ? now : 0;
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

// One GET through HttpDownloader, the firmware's one door for requests (the
// radio check, the agent string, the status), into a bounded string.
bool fetchAnswer(const std::string& url, std::string& out) {
  out.clear();
  bool overLimit = false;
  const unsigned long started = millis();
  const bool ok = HttpDownloader::fetchUrl(url, [&out, &overLimit](const uint8_t* data, const size_t len) {
    if (out.size() + len > kMaxAnswerBytes) {
      overLimit = true;
      return false;
    }
    out.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  LOG_INF(kTag, "GET %s: %s, status %d, %u bytes, %lums", url.c_str(), ok && !overLimit ? "ok" : "failed",
          HttpDownloader::lastStatus(), static_cast<unsigned>(out.size()), millis() - started);
  return ok && !overLimit;
}

// The faces the main screen is drawn in, pass by pass (WeatherScreens.h).
const toybox::Faces kPlaceFaces{toybox::kReadingBoldSmallFontId, toybox::kReadingBoldFontId, toybox::kDisplayFontId};
const toybox::Faces kDataFaces{toybox::kButtonFontId, toybox::kUiFontId, toybox::kHugeFontId};
const toybox::Faces kSearchFaces{toybox::kReadingFontId, toybox::kReadingBoldFontId, toybox::kDisplayFontId};
const toybox::Faces kMenuFaces{toybox::kButtonFontId, toybox::kReadingBoldFontId, toybox::kDisplayFontId};

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
    // The forecast on the card goes up first, whatever the radio says; the
    // first loop pass then asks for a fresh one.
    refreshOnFirstPass_ = true;
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
            searchFailure_ = "Not connected to Wi-Fi. Tap the field and press GO to try again.";
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
  if (!parsed) {
    if (fetched) LOG_ERR(kTag, "geocoder answer was not a list: %.80s", answer.c_str());
    searchState_ = weatherui::SearchModel::State::Failed;
    searchFailure_ = WiFi.status() == WL_CONNECTED
                         ? "Open-Meteo did not answer. Tap the field and press GO to try again."
                         : "Not connected to Wi-Fi. Tap the field and press GO to try again.";
    return;
  }
  matches_ = std::move(found);
  searchState_ = matches_.empty() ? weatherui::SearchModel::State::NoMatch : weatherui::SearchModel::State::Results;
  LOG_INF(kTag, "search \"%s\": %d places", query.c_str(), static_cast<int>(matches_.size()));
}

// --- Moving between screens -----------------------------------------------------------

void WeatherActivity::openSearch() {
  view_ = View::Search;
  query_.clear();
  searched_.clear();
  matches_.clear();
  searchState_ = weatherui::SearchModel::State::Typing;
  shifted_ = true;  // a city starts with a capital
  symbols_ = false;
  keyboardShown_ = true;
  kbGate_.arm();
  interactionsReady_ = false;
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
      if (!keyboardShown_) {
        keyboardShown_ = true;
        kbGate_.arm();
      }
      break;
    case weatherui::ActionMatch:
      choose(value);
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
    requestUpdate();
    return;
  }

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    back();
    return;
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
  model.place = placeLabel_.c_str();
  switch (status_) {
    case Status::Updating:
      model.status = "UPDATING...";
      break;
    case Status::Offline:
      model.status = "OFFLINE.";
      break;
    case Status::Failed:
      model.status = "COULD NOT UPDATE.";
      break;
    case Status::Current:
      break;
  }
  if (!haveCache_) {
    model.hasForecast = false;
    if (status_ == Status::Updating || status_ == Status::Current) {
      model.emptyHeadline = "GETTING THE FORECAST";
      model.emptyMessage = "From Open-Meteo, over Wi-Fi. It stays on the card for when you are offline.";
      model.status = "";
    } else {
      model.emptyHeadline = "NO FORECAST YET";
      model.emptyMessage = status_ == Status::Offline
                               ? "Connect to Wi-Fi to get one. It stays on the card for when you are offline."
                               : "Open-Meteo did not answer. Try again in a minute.";
      model.status = "";
      model.offerRetry = true;
    }
    return model;
  }

  const weather::Forecast& f = cache_.forecast;
  const weather::Units units = place_.units;
  const int64_t now = nowOrZero();
  const int first = weather::firstDay(cache_, now);
  model.hasForecast = true;
  model.hasNow = f.nowC != weather::kNoTemp;
  if (model.hasNow) model.now = weather::displayTemp(f.nowC, units);
  model.nowIcon = weather::skyIcon(f.nowCode, f.isDay);
  model.sky = weather::skyWords(f.nowCode);
  model.today = rowFor(f.days[first], units);
  for (int i = first + 1; i < f.dayCount && model.dayCount < weatherui::kRows; ++i) {
    model.days[model.dayCount++] = rowFor(f.days[i], units);
  }
  updated_ = weather::updatedLabel(cache_, now, SETTINGS.clockFormat == 1);
  model.updated = updated_.c_str();
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
  switch (view_) {
    case View::Main: {
      // Two passes, one rebind: the place in a cut that can draw its letters,
      // then the numbers and the days in Jersey (WeatherScreens.h).
      fui::GfxRendererTarget target = toybox::makeTarget(renderer, kPlaceFaces);
      toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
      toybox::Screen screen(frame);
      const weatherui::MainModel model = mainModel();
      weatherui::buildMainTop(screen, model);
      target.setFont(fui::GfxRendererTarget::FONT_SMALL, kDataFaces.small);
      target.setFont(fui::GfxRendererTarget::FONT_BODY, kDataFaces.body);
      target.setFont(fui::GfxRendererTarget::FONT_TITLE, kDataFaces.title);
      weatherui::buildMainBody(screen, model);
      toybox::reportOverflow(interactions_, "Weather");
      break;
    }
    case View::Search: {
      fui::GfxRendererTarget target = toybox::makeTarget(renderer, kSearchFaces);
      toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
      toybox::Screen screen(frame);
      weatherui::SearchModel model;
      model.query = query_.c_str();
      model.state = searchState_;
      model.firstRun = !havePlace_;
      model.failure = searchFailure_.c_str();
      if (searchState_ == weatherui::SearchModel::State::NoMatch) model.query = searched_.c_str();
      model.rowCount = static_cast<int>(matches_.size());
      for (int i = 0; i < model.rowCount; ++i) {
        model.rows[i].name = matches_[static_cast<size_t>(i)].name.c_str();
        model.rows[i].region = matches_[static_cast<size_t>(i)].region.c_str();
      }
      if (keyboardShown_) {
        model.keyboardHeight = static_cast<int16_t>(renderer.getScreenHeight() - keyboardRect().y + 8);
      }
      weatherui::buildSearch(screen, model);
      toybox::reportOverflow(interactions_, "Weather search");
      break;
    }
    case View::Menu: {
      fui::GfxRendererTarget target = toybox::makeTarget(renderer, kMenuFaces);
      toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
      toybox::Screen screen(frame);
      weatherui::MenuModel model;
      placeLabel_ = weather::placeLabel(place_.name, place_.region, place_.lat, place_.lon);
      model.place = placeLabel_.c_str();
      model.fahrenheit = place_.units == weather::Units::Fahrenheit;
      updated_ = haveCache_ ? weather::updatedLabel(cache_, nowOrZero(), SETTINGS.clockFormat == 1) : std::string();
      model.updated = updated_.c_str();
      weatherui::buildMenu(screen, model);
      toybox::reportOverflow(interactions_, "Weather menu");
      break;
    }
  }
  interactionsReady_ = true;
  if (view_ == View::Search && keyboardShown_) drawKeyboard();
  renderer.displayBuffer();
}
