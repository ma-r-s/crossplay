#include "TrmnlActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <ESPmDNS.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <WiFi.h>
#if !defined(SIMULATOR)
#include <esp_mac.h>
#endif

#ifndef SIMULATOR
#include <BatteryMonitor.h>
#endif

#include <cstdio>
#include <vector>

#include "../../DevMode.h"
#include "../../WifiCredentialStore.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../network/HttpDownloader.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "TrmnlStore.h"

namespace fui = freeink::ui;

namespace {

// How long a join may take before the reader gives up; Live allows the same.
constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr size_t kReplyMax = 8 * 1024;
constexpr size_t kStateMax = 2 * 1024;

#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "dev"
#endif

GfxRenderer::Orientation panelOrientation(const trmnl::Orientation orientation) {
  switch (orientation) {
    case trmnl::Orientation::Landscape:
      return GfxRenderer::LandscapeCounterClockwise;
    case trmnl::Orientation::LandscapeFlipped:
      return GfxRenderer::LandscapeClockwise;
    case trmnl::Orientation::Portrait:
      return GfxRenderer::Portrait;
    case trmnl::Orientation::PortraitFlipped:
      return GfxRenderer::PortraitInverted;
  }
  return GfxRenderer::LandscapeCounterClockwise;
}

// Volts, as a TRMNL panel reports them; empty when the gauge did not answer.
std::string batteryVolts() {
#ifdef SIMULATOR
  return "";
#else
  static const BatteryMonitor battery;
  const uint16_t millivolts = battery.readMillivolts();
  if (millivolts == 0) return "";
  char text[24];
  std::snprintf(text, sizeof(text), "%u.%02u", static_cast<unsigned>(millivolts / 1000),
                static_cast<unsigned>((millivolts % 1000) / 10));
  return text;
#endif
}

// The picture's placement: centred, scaled down to fit when it is larger than
// the panel, never up.
struct Placement {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

Placement fit(const int imageW, const int imageH, const int boxX, const int boxY, const int boxW, const int boxH) {
  Placement out;
  float scale = 1.0f;
  if (imageW > boxW) scale = static_cast<float>(boxW) / static_cast<float>(imageW);
  if (imageH > boxH) {
    const float h = static_cast<float>(boxH) / static_cast<float>(imageH);
    if (h < scale) scale = h;
  }
  out.width = static_cast<int>(static_cast<float>(imageW) * scale);
  out.height = static_cast<int>(static_cast<float>(imageH) * scale);
  out.x = boxX + (boxW - out.width) / 2;
  out.y = boxY + (boxH - out.height) / 2;
  return out;
}

}  // namespace

std::unique_ptr<Activity> TrmnlActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TrmnlActivity>(renderer, mappedInput);
}

void TrmnlActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
#ifdef SIMULATOR
  mac_ = "5E:AD:00:00:00:01";
#else
  // From eFuse: WiFi.macAddress() reads all zeros until the radio has started,
  // and an all-zero ID gets whatever device a server first saw under it.
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) WiFi.macAddress(mac);
  mac_ = trmnl::formatMac(mac);
#endif
  if (!trmnl::store::begin()) {
    showNotice("The card would not open, so nothing can be saved.");
    return;
  }
  {
    RenderLock lock(*this);
    loadConfig();
    state_ = trmnl::parseState(trmnl::store::read(trmnl::store::kStatePath, kStateMax));
  }
  // Somebody who has set this up came here for the picture.
  if (configured_) {
    openScreen();
    queueFetch(true);
  } else {
    openHome();
  }
}

void TrmnlActivity::onExit() {
  stopPhone();
  releaseWifi();
  renderer.setOrientation(GfxRenderer::Portrait);
  Activity::onExit();
}

// --- Data ----------------------------------------------------------------

void TrmnlActivity::loadConfig() {
  configured_ = trmnl::store::exists(trmnl::store::kConfigPath);
  config_ = trmnl::parseConfig(trmnl::store::read(trmnl::store::kConfigPath, trmnl::kMaxConfigBytes));
}

void TrmnlActivity::saveState() {
  if (!trmnl::store::write(trmnl::store::kStatePath, trmnl::formatState(state_))) {
    LOG_ERR("TRMNL", "state.txt was not written");
  }
}

std::string TrmnlActivity::idFor(const trmnl::Config& config) const { return trmnl::effectiveDeviceId(config, mac_); }

// --- Navigation ----------------------------------------------------------

void TrmnlActivity::applyOrientation() {
  renderer.setOrientation(view_ == View::Screen ? panelOrientation(config_.orientation) : GfxRenderer::Portrait);
}

void TrmnlActivity::setView(const View view) {
  {
    RenderLock lock(*this);
    if (view == View::Screen && view_ != View::Screen) shown_ = 0;
    view_ = view;
    interactionsReady_ = false;
  }
  requestUpdate();
}

void TrmnlActivity::showNotice(const std::string& text) {
  {
    RenderLock lock(*this);
    notice_ = text;
  }
  setView(View::Notice);
}

// --- Fetching ------------------------------------------------------------

void TrmnlActivity::queueFetch(const bool fromTap) {
  if (fetchQueued_) return;
  fetchQueued_ = true;
  fetchFromTap_ = fromTap;
  // With no picture to keep showing, say what is happening. With one, the
  // picture stays until the next replaces it, the way a TRMNL does it.
  if (!trmnl::store::exists(trmnl::store::kImagePath) || view_ != View::Screen) {
    {
      RenderLock lock(*this);
      busy_ = "Asking " + trmnl::serverLabel(config_.server) + " for your screen. Back stops it.";
    }
    setView(View::Busy);
  }
}

void TrmnlActivity::runFetch() {
  fetchQueued_ = false;
  cancelFetch_ = false;
  std::string why;
  if (!joinWifi(why)) {
    if (cancelFetch_) {
      releaseWifi();
      openHome();
      return;
    }
#ifndef SIMULATOR
    if (fetchFromTap_) {
      // Somebody pressed a button, so somebody can pick a network.
      WiFi.mode(WIFI_STA);
      startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                                 showNotice("Your TRMNL screen needs Wi-Fi. Nothing changed.");
                                 return;
                               }
                               queueFetch(true);
                             });
      return;
    }
#endif
    {
      RenderLock lock(*this);
      state_.message = why;
      schedule(false);
    }
    if (view_ == View::Busy) showNotice(why);
    return;
  }

  trmnl::Config config = config_;
  trmnl::State state = state_;
  bool changed = false;
  std::string staged;
  bool ok = exchange(config, state, changed, staged, why);
  if (config_.wifiOff) releaseWifi();
  if (cancelFetch_) {
    // Stopped by hand: keep what the exchange learned (a new key is still the
    // reader's key), drop the half-fetched picture, and wait for a tap.
    {
      RenderLock lock(*this);
      config_ = config;
      state_ = state;
      state_.message.clear();
      nextFetchAt_ = 0;
    }
    saveState();
    openHome();
    return;
  }
  {
    RenderLock lock(*this);
    if (ok && changed && !Storage.replaceFile(staged.c_str(), trmnl::store::kImagePath)) {
      ok = false;
      why = "The card would not take the picture.";
    }
    config_ = config;
    state_ = state;
    if (!ok) state_.filename.clear();
    state_.message = ok ? std::string() : why;
    schedule(ok);
    if (ok && changed) lastImageAt_ = millis();
  }
  saveState();

  if (!ok) {
    LOG_ERR("TRMNL", "fetch failed: %s", why.c_str());
    if (view_ == View::Busy) {
      if (trmnl::store::exists(trmnl::store::kImagePath) && !fetchFromTap_) {
        openScreen();
      } else {
        showNotice(why);
      }
    } else if (view_ == View::Home) {
      requestUpdate();
    }
    return;
  }
  if (view_ == View::Busy) {
    openScreen();
  } else if (view_ == View::Screen && changed) {
    requestUpdate();
  } else if (view_ == View::Home) {
    requestUpdate();
  }
}

void TrmnlActivity::pumpDuringFetch() {
  mappedInput.update();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) cancelFetch_ = true;
}

void TrmnlActivity::schedule(const bool succeeded) {
  const uint32_t interval = trmnl::intervalSeconds(config_, state_.refreshRate);
  failures_ = succeeded ? 0 : failures_ + 1;
  const uint32_t wait = succeeded ? interval : trmnl::retrySeconds(failures_, interval);
  nextFetchAt_ = millis() + wait * 1000UL;
  if (nextFetchAt_ == 0) nextFetchAt_ = 1;
}

bool TrmnlActivity::fetchJson(const trmnl::Config& config, const trmnl::State& state, const std::string& url,
                              std::string& body, std::string& why) {
  std::vector<HttpDownloader::Header> headers;
  headers.reserve(10);
  headers.emplace_back("ID", idFor(config));
  if (!config.apiKey.empty()) headers.emplace_back("Access-Token", config.apiKey);
  headers.emplace_back("Refresh-Rate", std::to_string(trmnl::intervalSeconds(config, state.refreshRate)));
  const std::string volts = batteryVolts();
  if (!volts.empty()) headers.emplace_back("Battery-Voltage", volts);
  headers.emplace_back("FW-Version", CROSSPOINT_VERSION);
#ifndef SIMULATOR
  headers.emplace_back("RSSI", std::to_string(WiFi.RSSI()));
#endif
  headers.emplace_back("Width", std::to_string(trmnl::requestWidth(config.orientation)));
  headers.emplace_back("Height", std::to_string(trmnl::requestHeight(config.orientation)));
  headers.emplace_back("Accept", "application/json");

  const auto result = HttpDownloader::downloadToFile(
      url, trmnl::store::kReplyPath, [this](size_t, size_t) { pumpDuringFetch(); }, &cancelFetch_, "", "", headers,
      false);
  if (result != HttpDownloader::OK) {
    const int status = HttpDownloader::lastStatus();
    char text[160];
    if (status == 0) {
      std::snprintf(text, sizeof(text),
                    "%s did not answer. Check the address, and that the reader's Wi-Fi can reach it.",
                    trmnl::serverLabel(config.server).c_str());
    } else if (status == 401 || status == 403) {
      std::snprintf(text, sizeof(text), "%s refused this reader's key (%d). Check the API key on the phone page.",
                    trmnl::serverLabel(config.server).c_str(), status);
    } else if (status == 404) {
      std::snprintf(text, sizeof(text),
                    "%s does not know this reader (404). Add device %s on the server, or check the address.",
                    trmnl::serverLabel(config.server).c_str(), idFor(config).c_str());
    } else {
      std::snprintf(text, sizeof(text), "%s answered with an error (%d).", trmnl::serverLabel(config.server).c_str(),
                    status);
    }
    why = text;
    return false;
  }
  body = trmnl::store::read(trmnl::store::kReplyPath, kReplyMax);
  if (body.empty()) {
    why = "The server's answer was empty or too long to read.";
    return false;
  }
  return true;
}

bool TrmnlActivity::exchange(trmnl::Config& config, trmnl::State& state, bool& changed, std::string& staged,
                             std::string& why) {
  changed = false;
  std::string body;
  const std::string id = idFor(config);
  if (!trmnl::usableDeviceId(id)) {
    why = "This reader's Wi-Fi address could not be read. Set a device ID on the phone page.";
    return false;
  }
  if (!config.apiKey.empty() && !trmnl::keyBelongsTo(config, state, id)) {
    // Issued for another ID, or saved before the reader kept track: sending it
    // would show that device's screen, so it is asked for again.
    LOG_INF("TRMNL", "dropping a key issued for '%s', not %s", state.keyFor.c_str(), id.c_str());
    config.apiKey.clear();
    state.keyFor.clear();
    state.friendlyId.clear();
    state.filename.clear();
    if (!trmnl::store::write(trmnl::store::kConfigPath, trmnl::formatConfig(config))) {
      LOG_ERR("TRMNL", "config.txt was not written");
    }
  }

  // No key yet: ask for one, the way a new TRMNL does on its first boot.
  if (config.apiKey.empty()) {
    if (!fetchJson(config, state, trmnl::apiUrl(config.server, "/api/setup"), body, why)) return false;
    const trmnl::SetupReply setup = trmnl::parseSetup(body);
    if (!setup.ok) {
      why = setup.message.empty() ? "The server would not set this reader up." : "The server said: " + setup.message;
      why += " Device ID " + id + ".";
      return false;
    }
    config.apiKey = setup.apiKey;
    state.keyFor = id;
    state.friendlyId = setup.friendlyId;
    if (!trmnl::store::write(trmnl::store::kConfigPath, trmnl::formatConfig(config))) {
      LOG_ERR("TRMNL", "the new API key was not saved");
    }
    configured_ = true;
    LOG_INF("TRMNL", "set up as %s", setup.friendlyId.c_str());
  }

  if (!fetchJson(config, state, trmnl::apiUrl(config.server, "/api/display"), body, why)) return false;
  const trmnl::DisplayReply reply = trmnl::parseDisplay(body);
  if (reply.refreshRate > 0) state.refreshRate = reply.refreshRate;
  if (reply.resetCredentials) {
    // The server has forgotten this reader; the next attempt sets it up again.
    config.apiKey.clear();
    state.keyFor.clear();
    trmnl::store::write(trmnl::store::kConfigPath, trmnl::formatConfig(config));
  }
  if (!reply.ok) {
    why = reply.message.empty() ? "The server sent no picture." : "The server said: " + reply.message;
    return false;
  }

  // The same picture as last time is not fetched again: TRMNL's own firmware
  // compares the filename for the same reason.
  if (!reply.filename.empty() && reply.filename == state.filename && trmnl::store::exists(trmnl::store::kImagePath)) {
    return true;
  }

  const std::string imageUrl = trmnl::resolveUrl(config.server, reply.imageUrl);
  std::vector<HttpDownloader::Header> headers;
  if (!config.apiKey.empty()) headers.emplace_back("Access-Token", config.apiKey);
  if (HttpDownloader::downloadToFile(
          imageUrl, trmnl::store::kDownloadPath, [this](size_t, size_t) { pumpDuringFetch(); }, &cancelFetch_, "", "",
          headers, false) != HttpDownloader::OK) {
    char text[96];
    std::snprintf(text, sizeof(text), "The picture did not download (%d).", HttpDownloader::lastStatus());
    why = text;
    return false;
  }
  if (!storePicture(config, staged, why)) return false;
  state.filename = reply.filename.empty() ? reply.imageUrl : reply.filename;
  changed = true;
  return true;
}

bool TrmnlActivity::storePicture(const trmnl::Config& config, std::string& staged, std::string& why) {
  uint8_t head[8] = {};
  {
    HalFile file;
    if (!Storage.openFileForRead("TRMNL", trmnl::store::kDownloadPath, file) ||
        file.read(head, sizeof(head)) != static_cast<int>(sizeof(head))) {
      why = "The picture could not be read back from the card.";
      return false;
    }
  }
  switch (trmnl::sniffImage(head, sizeof(head))) {
    case trmnl::ImageKind::Bmp: {
      {
        HalFile file;
        if (!Storage.openFileForRead("TRMNL", trmnl::store::kDownloadPath, file)) {
          why = "The picture could not be read back from the card.";
          return false;
        }
        Bitmap bitmap(file);
        const BmpReaderError err = bitmap.parseHeaders();
        if (err != BmpReaderError::Ok) {
          why = std::string("The server's BMP cannot be drawn: ") + Bitmap::errorToString(err) + ".";
          return false;
        }
      }
      staged = trmnl::store::kDownloadPath;
      return true;
    }
    case trmnl::ImageKind::Png: {
      {
        HalFile png;
        HalFile bmp;
        if (!Storage.openFileForRead("TRMNL", trmnl::store::kDownloadPath, png) ||
            !Storage.openFileForWrite("TRMNL", trmnl::store::kConvertPath, bmp)) {
          why = "The card would not take the picture.";
          return false;
        }
        if (!PngToBmpConverter::pngFileTo1BitBmpStreamFitWithin(png, bmp, trmnl::requestWidth(config.orientation),
                                                                trmnl::requestHeight(config.orientation))) {
          bmp.close();
          Storage.remove(trmnl::store::kConvertPath);
          why = "The server's PNG could not be decoded.";
          return false;
        }
        bmp.close();
      }
      Storage.remove(trmnl::store::kDownloadPath);
      staged = trmnl::store::kConvertPath;
      return true;
    }
    case trmnl::ImageKind::Unknown:
      break;
  }
  why = "The server sent something that is not a BMP or a PNG.";
  return false;
}

// --- The radio -------------------------------------------------------------

// Joins the network the reader last used, the way Live does from inside a
// sleep, and remembers whether it was this app that brought the radio up so it
// puts down only what it picked up.
bool TrmnlActivity::joinWifi(std::string& why) {
#ifdef SIMULATOR
  (void)why;
  if (WiFi.status() != WL_CONNECTED) WiFi.begin();
  return WiFi.status() == WL_CONNECTED;
#else
  if (WiFi.status() == WL_CONNECTED) return true;
  if (devmode::holdsRadio()) {
    why = "Wi-Fi is busy with Developer Mode. The reader will try again.";
    return false;
  }
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    why = "This reader has no Wi-Fi network saved.";
    return false;
  }
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential.has_value()) {
    why = "The saved Wi-Fi password is gone. Connect again from Settings.";
    return false;
  }
  if (!yieldedDevMode_) {
    devmode::pause();
    yieldedDevMode_ = true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), credential->password.empty() ? nullptr : credential->password.c_str());
  broughtRadioUp_ = true;
  const unsigned long deadline = millis() + kJoinTimeoutMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) return true;
    pumpDuringFetch();
    if (cancelFetch_) {
      why = "Stopped.";
      return false;
    }
    delay(100);
  }
  why = "Could not join " + ssid + ". The reader will try again.";
  return false;
#endif
}

void TrmnlActivity::releaseWifi() {
#ifndef SIMULATOR
  // Never under the phone page, which is being served over it.
  if (server_ && server_->isRunning()) return;
  if (broughtRadioUp_) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    broughtRadioUp_ = false;
  }
  if (yieldedDevMode_ && !devPaused_) {
    devmode::resume();
    yieldedDevMode_ = false;
  }
#endif
}

// --- The phone -----------------------------------------------------------

std::string TrmnlActivity::facts() const {
  return "mac=" + mac_ + "\nfriendly_id=" + state_.friendlyId + "\nstatus=" + statusLine() + "\n";
}

void TrmnlActivity::startPhone() {
  cancelFetch_ = false;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) {
    std::string why;
    if (!joinWifi(why)) {
      WiFi.mode(WIFI_STA);
      startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                                 showNotice("Setting up on your phone needs Wi-Fi. Nothing changed.");
                                 return;
                               }
                               startPhone();
                             });
      return;
    }
  }
#endif

  // Developer Mode holds port 80 while its toggle is on; it yields while this
  // screen is up, and every way out of here goes through stopPhone().
  if (!devPaused_) {
    devmode::pause();
    devPaused_ = true;
  }

  server_ = makeUniqueNoThrow<TrmnlServer>();
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
  server_->setFacts(facts());
  const bool started = server_->begin();
#ifndef SIMULATOR
  if (!started) {
    stopPhone();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
#else
  (void)started;
#endif

#ifdef SIMULATOR
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  {
    RenderLock lock(*this);
    // The code carries the address, which depends on no service; the name,
    // which does, is only what a person reads.
    phoneUrl_ = "http://" + dotted + "/trmnl";
#ifdef SIMULATOR
    phoneReadable_ = phoneUrl_;
    (void)mdnsUp;
#else
    phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/trmnl" : phoneUrl_;
#endif
    phoneSaved_ = false;
  }
  setView(View::Phone);
}

void TrmnlActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  if (devPaused_) {
    devPaused_ = false;
    if (!yieldedDevMode_) devmode::resume();
  }
}

// --- Input ---------------------------------------------------------------

void TrmnlActivity::loop() {
  if (fetchQueued_) {
    runFetch();
    return;
  }

  // Back is read above the tap guard: the global back-swipe arrives as
  // Button::Back, and a swipe is not a tap.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Home:
      case View::Screen:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Phone:
        stopPhone();
        openHome();
        return;
      case View::Notice:
      case View::Busy:
        openHome();
        return;
    }
  }

  // Either side key asks for the next picture now, from the picture or from
  // the app's own screen.
  if ((mappedInput.wasReleased(MappedInputManager::Button::Up) ||
       mappedInput.wasReleased(MappedInputManager::Button::Down)) &&
      (view_ == View::Screen || view_ == View::Home)) {
    {
      RenderLock lock(*this);
      state_.filename.clear();
    }
    queueFetch(true);
    return;
  }

  if (server_ && server_->isRunning()) {
    // Pumped from loop(): there are no background threads in this firmware.
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    if (server_->takeChanged()) {
      {
        RenderLock lock(*this);
        const trmnl::Config before = config_;
        loadConfig();
        if (trmnl::applyPhoneSave(before, config_, state_, mac_) &&
            !trmnl::store::write(trmnl::store::kConfigPath, trmnl::formatConfig(config_))) {
          LOG_ERR("TRMNL", "config.txt was not written");
        }
        saveState();
        failures_ = 0;
        nextFetchAt_ = 0;
        phoneSaved_ = true;
        interactionsReady_ = false;
      }
      server_->setFacts(facts());
      requestUpdate();
    }
  }

  if (view_ == View::Screen && nextFetchAt_ != 0 && static_cast<long>(millis() - nextFetchAt_) >= 0) {
    nextFetchAt_ = 0;
    queueFetch(false);
    return;
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  // The picture has no controls: a tap anywhere is the way back to them.
  if (view_ == View::Screen) {
    openHome();
    return;
  }
  fui::ActionEvent action{};
  {
    RenderLock lock(*this);
    if (!interactionsReady_) return;
    fui::InputSnapshot input{};
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    action = interactions_.route(input);
  }

  switch (action.action) {
    case trmnlui::ActionShow:
      if (trmnl::store::exists(trmnl::store::kImagePath)) {
        openScreen();
        if (nextFetchAt_ == 0) queueFetch(false);
      } else {
        queueFetch(true);
      }
      return;
    case trmnlui::ActionRefresh: {
      RenderLock lock(*this);
      state_.filename.clear();
    }
      queueFetch(true);
      return;
    case trmnlui::ActionUsePhone:
      startPhone();
      return;
    case trmnlui::ActionDismiss:
      stopPhone();
      if (!configured_) {
        RenderLock lock(*this);
        loadConfig();
      }
      openHome();
      return;
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------

std::string TrmnlActivity::statusLine() const {
  if (!configured_) {
    return "Not set up yet. Tap the phone above to choose a server, or SHOW to try TRMNL's cloud as " + mac_ + ".";
  }
  std::string line;
  if (!state_.message.empty()) {
    line = state_.message;
    if (nextFetchAt_ != 0 && view_ != View::Busy) {
      const long left = static_cast<long>(nextFetchAt_ - millis());
      line += " Trying again " + trmnl::inPhrase(left > 0 ? static_cast<uint32_t>(left / 1000) : 0) + ".";
    }
    return line;
  }
  if (lastImageAt_ != 0) {
    const unsigned long ago = (millis() - lastImageAt_) / 1000;
    line =
        ago < 60 ? "Updated just now." : "Updated " + trmnl::inPhrase(static_cast<uint32_t>(ago)).substr(3) + " ago.";
  } else if (trmnl::store::exists(trmnl::store::kImagePath)) {
    line = "Showing the last screen saved on the card.";
  } else {
    return "No screen yet. SHOW asks the server for one.";
  }
  if (nextFetchAt_ != 0) {
    const long left = static_cast<long>(nextFetchAt_ - millis());
    line += " The next one comes " + trmnl::inPhrase(left > 0 ? static_cast<uint32_t>(left / 1000) : 0) + ".";
  }
  return line;
}

void TrmnlActivity::drawPicture() {
  renderer.clearScreen();
  HalFile file;
  bool drawn = false;
  if (Storage.openFileForRead("TRMNL", trmnl::store::kImagePath, file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      const Placement at = fit(bitmap.getWidth(), bitmap.getHeight(), 0, 0, width, height);
      drawn = renderer.drawBitmap(bitmap, at.x, at.y, at.width, at.height);
      if (drawn) renderer.preserveImagePolarity(at.x, at.y, at.width, at.height);
    }
  }
  if (!drawn) {
    LOG_ERR("TRMNL", "the picture on the card could not be drawn");
  }
  shown_++;
  renderer.displayBuffer(trmnl::cleanRefresh(shown_, config_.cleanEvery) ? HalDisplay::HALF_REFRESH
                                                                         : HalDisplay::FAST_REFRESH);
}

void TrmnlActivity::drawPreview(const fui::Rect& box) {
  if (box.width <= 0 || box.height <= 0) return;
  HalFile file;
  if (!Storage.openFileForRead("TRMNL", trmnl::store::kImagePath, file)) return;
  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return;
  const Placement at = fit(bitmap.getWidth(), bitmap.getHeight(), box.x, box.y, box.width, box.height);
  if (renderer.drawBitmap(bitmap, at.x, at.y, at.width, at.height)) {
    renderer.preserveImagePolarity(at.x, at.y, at.width, at.height);
  }
}

void TrmnlActivity::render(RenderLock&&) {
  // Here and nowhere else: turning the panel from loop() could land halfway
  // through a draw in the other orientation.
  applyOrientation();
  if (view_ == View::Screen) {
    interactionsReady_ = false;
    drawPicture();
    return;
  }

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Home: {
      const std::string server = trmnl::serverLabel(config_.server);
      const std::string id = deviceId();
      const std::string device = state_.friendlyId.empty() ? id : state_.friendlyId + "  /  " + id;
      const std::string cadence = trmnl::everyPhrase(trmnl::intervalSeconds(config_, state_.refreshRate));
      const std::string status = statusLine();
      trmnlui::HomeModel model;
      model.server = server.c_str();
      model.device = device.c_str();
      model.cadence = cadence.c_str();
      model.status = status.c_str();
      model.hasImage = trmnl::store::exists(trmnl::store::kImagePath);
      const fui::Rect picture = trmnlui::buildHome(screen, model);
      drawPreview(picture);
      break;
    }
    case View::Phone: {
      trmnlui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.saved = phoneSaved_;
      const fui::Rect qr = trmnlui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice:
      trmnlui::buildNotice(screen, notice_.c_str());
      break;
    case View::Busy:
      trmnlui::buildBusy(screen, busy_.c_str());
      break;
    case View::Screen:
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Trmnl");
  renderer.displayBuffer();
}
