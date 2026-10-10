#include "WalletActivity.h"

#include <ESPmDNS.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "../../CrossPointSettings.h"
#include "../../DevMode.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../live/LiveBridge.h"
#include "../live/LiveEngine.h"
#include "../live/LiveStore.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WalletSleep.h"
#include "WalletStore.h"

namespace fui = freeink::ui;

std::unique_ptr<Activity> WalletActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WalletActivity>(renderer, mappedInput);
}

void WalletActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  if (!wallet::store::begin()) {
    showNotice("The SD card would not open, so there are no cards to show.");
    return;
  }
  reload();
  openList();
}

void WalletActivity::onExit() {
  stopPhone();
  Activity::onExit();
}

// --- Data ----------------------------------------------------------------

void WalletActivity::reload() {
  cards_ = wallet::store::loadAll();
  rows_.clear();
  rows_.reserve(cards_.size());
  for (const wallet::Card& card : cards_) {
    walletui::ListRow row;
    row.title = card.title.empty() ? "UNTITLED" : card.title.c_str();
    row.caption = card.caption.c_str();
    row.barcode = card.kind != wallet::CodeKind::Qr;
    rows_.push_back(row);
  }
  // A card deleted from the phone page while it was on the sleep screen.
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CARD) {
    wallet::AsleepChoice choice;
    if (wallet::readAsleep(choice) && indexOf(choice.file) < 0) takeOffSleep(choice);
  }
}

int WalletActivity::indexOf(const std::string& file) const {
  for (size_t i = 0; i < cards_.size(); i++) {
    if (cards_[i].file == file) return static_cast<int>(i);
  }
  return -1;
}

// --- The sleep screen ----------------------------------------------------

bool WalletActivity::isShownAsleep() const {
  if (open_ < 0 || open_ >= static_cast<int>(cards_.size())) return false;
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CARD) return false;
  wallet::AsleepChoice choice;
  return wallet::readAsleep(choice) && choice.file == cards_[static_cast<size_t>(open_)].file;
}

// Puts back both settings a card replaced: the mode it replaced, unless
// nothing was recorded or what was recorded is Card itself, and Quick Resume
// on Timeout only when the choice recorded it.
void WalletActivity::takeOffSleep(const wallet::AsleepChoice& choice) {
  wallet::clearAsleep();
  const int mode = choice.previousMode;
  SETTINGS.sleepScreen = mode >= 0 && mode < CrossPointSettings::SLEEP_SCREEN_MODE_COUNT &&
                                 mode != CrossPointSettings::SLEEP_SCREEN_MODE::CARD
                             ? static_cast<uint8_t>(mode)
                             : static_cast<uint8_t>(CrossPointSettings::SLEEP_SCREEN_MODE::DARK);
  if (choice.previousQuickResume == 1) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  } else if (choice.previousQuickResume == 0) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
  }
  SETTINGS.saveToFile();
  LOG_INF("CARDS", "%s off the sleep screen; mode back to %d", choice.file.c_str(), SETTINGS.sleepScreen);
}

void WalletActivity::toggleAsleep() {
  if (open_ < 0 || open_ >= static_cast<int>(cards_.size())) return;
  wallet::AsleepChoice current;
  const bool hadChoice = wallet::readAsleep(current);
  if (isShownAsleep()) {
    takeOffSleep(current);
    requestUpdate();
    return;
  }
  wallet::AsleepChoice choice;
  choice.file = cards_[static_cast<size_t>(open_)].file;
  // Moving from one card to another keeps what the FIRST card replaced, so
  // taking it off later still puts back the person's own screen.
  const bool alreadyCard = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CARD;
  choice.previousMode = alreadyCard ? (hadChoice ? current.previousMode : -1) : static_cast<int>(SETTINGS.sleepScreen);
  choice.previousQuickResume =
      alreadyCard ? (hadChoice ? current.previousQuickResume : -1) : static_cast<int>(SETTINGS.quickResumeSleepScreen);
  if (!wallet::writeAsleep(choice)) {
    showNotice("The SD card would not take the change. Nothing was changed.");
    return;
  }
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CARD;
  // Quick resume on an idle sleep shows the last screen and skips the sleep
  // screen, so the card would never appear on an ordinary sleep. Notes and
  // Wallpapers make the same trade.
  SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
  SETTINGS.saveToFile();
  LOG_INF("CARDS", "%s on the sleep screen (replaced mode %d)", choice.file.c_str(), choice.previousMode);

  // Live and a card are mutually exclusive, as Live and a note are: left on,
  // Live would go on waking the device for pictures the card hides. The
  // pairing is kept.
  live::State liveState;
  if (!live::load(liveState) || !liveState.on) {
    requestUpdate();
    return;
  }
  liveState.on = false;
  live::save(liveState);
  LOG_INF("CARDS", "a card is on the sleep screen, so Live is off; its pairing is kept");
  if (!liveState.paired()) {
    requestUpdate();
    return;
  }
  // Tell the phone's page after the moon is filled in. A courtesy: a failure
  // costs the page its "off on the reader" line and nothing else.
  requestUpdateAndWait();
  std::string message;
  live::engine::RadioLease radio(message);
  if (!radio.held()) return;
  live::reportOff(liveState.deviceToken, message);
}

// "1/2" for the list when it runs past a page, and the top row snapped onto a
// page that exists after the cards under it changed.
void WalletActivity::relabel() {
  pageLabel_.clear();
  const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const int page = walletui::listCapacity(target.deviceContext());
  const int count = static_cast<int>(rows_.size());
  if (page <= 0 || count <= page) {
    listTop_ = 0;
    return;
  }
  if (listTop_ >= count) listTop_ = ((count - 1) / page) * page;
  listTop_ = (listTop_ / page) * page;
  char label[24];
  std::snprintf(label, sizeof(label), "%d/%d", listTop_ / page + 1, (count + page - 1) / page);
  pageLabel_ = label;
}

// --- Navigation ----------------------------------------------------------

void WalletActivity::openList() {
  view_ = View::List;
  open_ = -1;
  relabel();
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::openCard(const int index) {
  if (index < 0 || index >= static_cast<int>(cards_.size())) return;
  open_ = index;
  char position[24];
  std::snprintf(position, sizeof(position), "%d/%d", index + 1, static_cast<int>(cards_.size()));
  position_ = position;
  view_ = View::Card;
  interactionsReady_ = false;
  requestUpdate();
}

// Next and previous stop at the ends rather than wrapping: at a till, landing
// back on the first card reads as "that was the wrong button".
void WalletActivity::step(const int delta) {
  const int next = open_ + delta;
  if (next < 0 || next >= static_cast<int>(cards_.size())) return;
  openCard(next);
}

void WalletActivity::showNotice(const char* text) {
  notice_ = text;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::askDelete() {
  if (open_ < 0 || open_ >= static_cast<int>(cards_.size())) return;
  confirm_ = "Delete this card from the reader? It cannot be brought back here; add it again from your phone.";
  view_ = View::ConfirmDelete;
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::deleteOpen() {
  if (open_ < 0 || open_ >= static_cast<int>(cards_.size())) return;
  if (!wallet::store::remove(cards_[static_cast<size_t>(open_)].file)) {
    showNotice("The SD card would not delete it, so the card is still there.");
    return;
  }
  const int was = open_;
  // reload() takes it off the sleep screen if it was there.
  reload();
  if (cards_.empty()) {
    openList();
    return;
  }
  // The card that slid into its place, or the new last one.
  openCard(was < static_cast<int>(cards_.size()) ? was : static_cast<int>(cards_.size()) - 1);
}

// --- The phone -----------------------------------------------------------

void WalletActivity::startPhone() {
#ifndef SIMULATOR
  // Never launch the picker unconditionally: it disconnects a working
  // association on every path. The same guard Notes and Workouts use.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                               showNotice("Adding cards from your phone needs Wi-Fi. Nothing changed.");
                               return;
                             }
                             startPhone();
                           });
    return;
  }
#endif

  // Developer Mode holds port 80 while its toggle is on; it yields while this
  // screen is up, and every way out of here goes through stopPhone().
  devmode::pause();
  devPaused_ = true;

  server_ = makeUniqueNoThrow<WalletServer>();
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
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
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  // The code carries the address, which depends on no service; the name, which
  // does, is only what a person reads.
  phoneUrl_ = "http://" + dotted + "/cards";
#ifdef SIMULATOR
  phoneReadable_ = phoneUrl_;
#else
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/cards" : phoneUrl_;
#endif
  view_ = View::Phone;
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
}

// --- Input ---------------------------------------------------------------

void WalletActivity::loop() {
  // Back is read above the tap guard: the global back-swipe arrives as
  // Button::Back, and a swipe is not a tap.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::List:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Phone:
        stopPhone();
        reload();
        openList();
        return;
      case View::ConfirmDelete:
        openCard(open_);
        return;
      case View::Card:
      case View::Notice:
        openList();
        return;
    }
  }

  // The side keys: a page of the list, or the next and previous card.
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (down || up) {
    if (view_ == View::Card) {
      step(down ? 1 : -1);
    } else if (view_ == View::List) {
      const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
      const int page = walletui::listCapacity(target.deviceContext());
      const int count = static_cast<int>(rows_.size());
      const int next = down ? listTop_ + page : listTop_ - page;
      if (page > 0 && count > page && next >= 0 && next < count) {
        listTop_ = next;
        relabel();
        interactionsReady_ = false;
        requestUpdate();
      }
    }
    return;
  }

  if (server_ && server_->isRunning()) {
    // Pumped from loop(): there are no background threads in this firmware.
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    if (server_->takeChanged()) {
      reload();
      interactionsReady_ = false;
      requestUpdate();
    }
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input{};
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent action = interactions_.route(input);

  switch (action.action) {
    case walletui::ActionOpenCard:
      openCard(action.value);
      return;
    case walletui::ActionPrev:
      step(-1);
      return;
    case walletui::ActionNext:
      step(1);
      return;
    case walletui::ActionDelete:
      askDelete();
      return;
    case walletui::ActionSleep:
      toggleAsleep();
      return;
    case walletui::ActionDeleteConfirm:
      deleteOpen();
      return;
    case walletui::ActionDeleteKeep:
      openCard(open_);
      return;
    case walletui::ActionUsePhone:
      startPhone();
      return;
    case walletui::ActionDismiss:
      stopPhone();
      reload();
      openList();
      return;
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------

void WalletActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  const bool cardOpen = open_ >= 0 && open_ < static_cast<int>(cards_.size());
  switch (view_) {
    case View::List: {
      walletui::ListModel model;
      model.rows = rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.firstVisible = listTop_;
      model.pageLabel = pageLabel_.empty() ? nullptr : pageLabel_.c_str();
      walletui::buildList(screen, model);
      break;
    }
    case View::Card: {
      if (!cardOpen) break;
      const wallet::Card& card = cards_[static_cast<size_t>(open_)];
      walletui::CardModel model;
      model.title = card.title.empty() ? "UNTITLED" : card.title.c_str();
      model.caption = card.caption.c_str();
      model.position = position_.c_str();
      model.hasPrev = open_ > 0;
      model.hasNext = open_ + 1 < static_cast<int>(cards_.size());
      model.shownAsleep = isShownAsleep();
      wallet::showCard(renderer, screen, model, card);
      break;
    }
    case View::ConfirmDelete:
      walletui::buildDeleteConfirm(screen, cardOpen ? cards_[static_cast<size_t>(open_)].title.c_str() : "CARDS",
                                   confirm_.c_str());
      break;
    case View::Phone: {
      walletui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.added = server_ ? server_->added() : 0;
      const fui::Rect qr = walletui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice:
      walletui::buildNotice(screen, notice_.c_str());
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Cards");
  renderer.displayBuffer();
}
