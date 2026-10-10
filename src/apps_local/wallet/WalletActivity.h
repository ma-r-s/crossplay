#pragma once

// Cards: codes to show at a counter, kept on the reader.
//
// The three-way split every app in this fork uses. WalletCore holds the rules
// (the files and what the phone sends) and has a host suite. WalletScreens lays
// out and can be driven by host-tests/ui. This file keeps what needs hardware:
// the SD card, the phone page, drawing the code, and which screen is up.
//
// Cards are made on the phone, because a card starts life as a screenshot on
// the phone: the page reads the code out of the picture there and sends only
// what it says. The device's own edit is the one that needs no typing, delete.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WalletCore.h"
#include "WalletScreens.h"
#include "WalletServer.h"
#include "WalletSleep.h"

class WalletActivity final : public Activity {
 public:
  WalletActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Cards", renderer, mappedInput) {}
  ~WalletActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Not asleep with the phone page up: it would stop answering mid-save and the
  // phone would blame the network.
  bool preventAutoSleep() override { return server_ && server_->isRunning(); }
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class View : uint8_t { List, Card, ConfirmDelete, Phone, Notice };

  void reload();
  void openList();
  void openCard(int index);
  void step(int delta);
  void askDelete();
  void deleteOpen();
  void showNotice(const char* text);
  void startPhone();
  void stopPhone();
  void relabel();
  int indexOf(const std::string& file) const;
  bool isShownAsleep() const;
  void toggleAsleep();
  void takeOffSleep(const wallet::AsleepChoice& choice);

  std::vector<wallet::Card> cards_;
  std::vector<walletui::ListRow> rows_;

  View view_ = View::List;
  int open_ = -1;  // index into cards_ while a card is open
  int listTop_ = 0;
  std::string pageLabel_;
  std::string position_;
  std::string notice_;
  std::string confirm_;

  std::unique_ptr<WalletServer> server_;
  bool devPaused_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
