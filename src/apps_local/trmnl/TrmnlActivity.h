#pragma once

// TRMNL: the reader as a TRMNL screen.
//
// The three-way split every app in this fork uses. TrmnlCore holds the rules
// (the settings file, the server's JSON, the timing) and has a host suite.
// TrmnlScreens lays out the chrome around the picture. This file keeps what
// needs hardware: the radio, the card, the clock, the phone page, and the
// picture on the panel.
//
// The settings live on the phone, because a server address and an API key are
// sixty taps on this keyboard and a paste on a phone. The reader's own screen
// has three things to do: show the picture, fetch one now, and open the phone
// page.
//
// It asks only while it is open. A refresh during the reader's own sleep would
// need the sleep path, which Live owns; while the screen is up, the reader
// stays awake (unless the setting says not to) and the radio is down between
// pictures (unless the setting says not to).

#include <cstdint>
#include <memory>
#include <string>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TrmnlCore.h"
#include "TrmnlScreens.h"
#include "TrmnlServer.h"

class TrmnlActivity final : public Activity {
 public:
  TrmnlActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Trmnl", renderer, mappedInput) {}
  ~TrmnlActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Awake with the phone page up, and with the picture up unless the setting
  // lets the reader sleep: a dashboard that dozes off is a picture frame.
  bool preventAutoSleep() override {
    return (server_ && server_->isRunning()) || (view_ == View::Screen && config_.keepAwake) || fetchQueued_;
  }
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class View : uint8_t { Home, Screen, Phone, Notice, Busy };

  // render() runs on its own task and reads everything below, so loop()
  // changes it only under a RenderLock. A fetch works on copies and swaps them
  // in at the end, which keeps the lock off the radio's time.
  void loadConfig();
  void saveState();
  std::string deviceId() const { return idFor(config_); }
  std::string idFor(const trmnl::Config& config) const;

  void setView(View view);
  void openHome() { setView(View::Home); }
  void openScreen() { setView(View::Screen); }
  void showNotice(const std::string& text);
  // Asks for the next picture on the next pass of loop(), after the screen
  // saying so has been drawn. `fromTap` may offer the Wi-Fi picker; a timed
  // one never does, because nobody is there to answer it.
  void queueFetch(bool fromTap);
  void runFetch();
  // One whole exchange: setup if there is no key, display, the picture.
  // False with `why` set when it did not end in a picture on the card.
  // `staged` is the card path of the new picture, moved over the old one by
  // the caller under the lock.
  bool exchange(trmnl::Config& config, trmnl::State& state, bool& changed, std::string& staged, std::string& why);
  bool fetchJson(const trmnl::Config& config, const trmnl::State& state, const std::string& url, std::string& body,
                 std::string& why);
  bool storePicture(const trmnl::Config& config, std::string& staged, std::string& why);
  // Reads input while a fetch waits, so Back can stop it.
  void pumpDuringFetch();
  bool joinWifi(std::string& why);
  void releaseWifi();
  void schedule(bool succeeded);  // caller holds the RenderLock

  void startPhone();
  void stopPhone();
  std::string facts() const;

  void drawPicture();
  void drawPreview(const freeink::ui::Rect& box);
  void applyOrientation();
  std::string statusLine() const;

  trmnl::Config config_;
  trmnl::State state_;
  bool configured_ = false;  // a config file exists: somebody has set this up
  std::string mac_;

  View view_ = View::Home;
  std::string notice_;
  std::string busy_;

  // Fetching.
  bool fetchQueued_ = false;
  bool fetchFromTap_ = false;
  bool cancelFetch_ = false;
  bool broughtRadioUp_ = false;
  bool yieldedDevMode_ = false;
  int failures_ = 0;
  unsigned long nextFetchAt_ = 0;  // millis(); 0 means nothing is scheduled
  unsigned long lastImageAt_ = 0;  // millis() of the last new picture; 0 for none this visit
  int shown_ = 0;                  // pictures drawn since the screen was opened

  // The phone.
  std::unique_ptr<TrmnlServer> server_;
  bool devPaused_ = false;
  bool phoneSaved_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
