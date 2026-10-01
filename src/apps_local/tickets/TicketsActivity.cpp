#include "TicketsActivity.h"

#include <ESPmDNS.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "../../DevMode.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../apps_local/notes/NotesScreens.h"
#include "../../network/CrossPointWebServer.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "TicketsScreens.h"

namespace fui = freeink::ui;

namespace {

// Pixels of white left around the code. QrUtils fills its bounds edge to edge
// and a scanner wants a quiet zone; the square stays big.
constexpr int16_t kQuietZone = 8;

}  // namespace

// Global instance pointer for static callbacks (workaround for function pointer limitation)
TicketsActivity* g_ticketsActivityInstance = nullptr;

// Static callback for delete button (workaround for function pointer limitation)
int TicketsActivity::s_deleteTicketIndex = 0;
void TicketsActivity::s_deleteTicketCallback() {
  if (g_ticketsActivityInstance) {
    g_ticketsActivityInstance->deleteTicket(s_deleteTicketIndex);
  }
}

std::unique_ptr<Activity> TicketsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TicketsActivity>(renderer, mappedInput);
}

void TicketsActivity::onEnter() {
  Activity::onEnter();
  // The Toybox cuts are registered by the app that wants them rather than by
  // main.cpp, so they cost no upstream surface. Without this every string asks
  // for a face the renderer does not have and draws nothing at all.
  toybox::ensureFonts(renderer);

  cardOk_ = library_.begin();
  openList();

  // Set global instance pointer for static callbacks (delete button)
  g_ticketsActivityInstance = this;
}

// --- Navigation ------------------------------------------------------------

void TicketsActivity::openList() {
  view_ = View::List;
  openIndex_ = -1;
  listTop_ = 0;
  // Re-read on every entry. The card can be filled from a computer between
  // sessions, and an app that trusts a cached list offers tickets that are not
  // there any more.
  library_.scan();
  rebuildRows();
  relabelList();
  interactionsReady_ = false;
  requestUpdate();
}

void TicketsActivity::openTicket(const int index) {
  if (index < 0 || index >= library_.count()) return;
  openIndex_ = index;
  view_ = View::Ticket;
  interactionsReady_ = false;
  requestUpdate();
}

void TicketsActivity::rebuildRows() {
  rows_.clear();
  rows_.reserve(library_.count());
  const auto& entries = library_.entries();
  for (size_t i = 0; i < entries.size(); ++i) {
    fui::ListItem row;
    row.label = entries[i].name.c_str();
    row.subtitle = entries[i].subtitle.empty() ? nullptr : entries[i].subtitle.c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

int TicketsActivity::listPageSize() {
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  return ticketsui::listCapacity(target.deviceContext());
}

void TicketsActivity::relabelList() {
  const int page = listPageSize();
  const int count = library_.count();
  listPage_.clear();
  if (page <= 0 || count <= page) return;
  char label[32];
  std::snprintf(label, sizeof(label), "%d / %d", listTop_ / page + 1, (count + page - 1) / page);
  listPage_ = label;
}

void TicketsActivity::startUploadServer() {
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                               showNotice("Adding a ticket from your phone needs Wi-Fi. Nothing changed.");
                               return;
                             }
                             startUploadServer();
                           });
    return;
  }
#endif

  // Dev mode holds 80, 81 and UDP 8134 for as long as its toggle is on, and
  // Mario keeps a device on it. Two binds on one port fail in a way that reads
  // as "the screen is broken", so dev mode yields for as long as this screen is
  // up -- the same latch WifiSelectionActivity and the File Transfer screen
  // already take.
  devmode::pause();
  devPaused_ = true;

  // Every failure below leaves through stopUploadServer(), so the yield is
  // released in exactly ONE place no matter which way this goes wrong.
  uploadServer_ = makeUniqueNoThrow<CrossPointWebServer>(CrossPointWebServer::Surface::TicketsOnly);
  if (!uploadServer_) {
    stopUploadServer();
    showNotice("There was not enough memory to start.");
    return;
  }
  uploadServer_->begin();
#ifndef SIMULATOR
  if (!uploadServer_->isRunning()) {
    stopUploadServer();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
#endif

#ifdef SIMULATOR
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  if (!mdnsUp) LOG_DBG("TICKETS", "mDNS did not start; the code carries the address, which does not need it");
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  const std::string ipUrl = "http://" + dotted + "/t";
  const std::string nameUrl = std::string("http://") + devicehost::mdnsName() + ".local/t";

  uploadUrl_ = ipUrl;
  uploadReadable_ = mdnsUp ? nameUrl : ipUrl;
  uploadStatus_.clear();

  view_ = View::Upload;
  interactionsReady_ = false;
  requestUpdate();
}

void TicketsActivity::showNotice(const char* message) {
  notice_ = message;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

void TicketsActivity::deleteTicket(int index) {
  if (index < 0 || index >= library_.count()) return;
  if (library_.remove(index)) openList();
}

void TicketsActivity::stopUploadServer() {
  if (uploadServer_) {
    uploadServer_->stop();
    uploadServer_.reset();
    MDNS.end();
  }
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
  uploadUrl_.clear();
  uploadReadable_.clear();
  uploadStatus_.clear();
}

// --- Input ---------------------------------------------------------------

void TicketsActivity::loop() {
  // Back is read on the per-frame path, above any "return unless a tap
  // arrived" guard: the global back-swipe arrives as Button::Back and a swipe
  // is not a tap. host-tests/backgesture enforces this fork-wide.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Ticket) {
      openList();
      return;
    }
    if (view_ == View::Upload) {
      stopUploadServer();
      view_ = View::List;
      interactionsReady_ = false;
      requestUpdate();
      return;
    }
    if (view_ == View::Notice) {
      openList();
      return;
    }
    // An app never names where Back goes; the shelf puts it back in whichever
    // folder opened it. See docs/shelf.md.
    shelf::leave(renderer, mappedInput);
    return;
  }

  // Paging is the two physical keys. They are the only buttons this device has
  // and vertical paging is what they do everywhere else in the fork.
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if ((down || up) && view_ == View::List) {
    const int page = listPageSize();
    const int count = library_.count();
    const int next = down ? listTop_ + page : listTop_ - page;
    if (page > 0 && count > page && next >= 0 && next < count) {
      listTop_ = next;
      relabelList();
      interactionsReady_ = false;
      requestUpdate();
    }
    return;
  }

  // If the upload server is running, pump it.
  if (uploadServer_ && uploadServer_->isRunning()) {
    for (int i = 0; i < 8 && uploadServer_->isRunning(); ++i) uploadServer_->handleClient();
    if (uploadServer_->takeTicketsChanged()) {
      uploadStatus_ = uploadServer_->getTicketsResult();
      interactionsReady_ = false;
      requestUpdate();
    }
  }

  int x = 0;
  int y = 0;
  // Interactions::route() refuses a tap routed against a table the panel has
  // not shown yet, which is what stops a tap aimed at the screen underneath
  // from landing on the one that replaced it during a 0.3-2s repaint.
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input{};
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent action = interactions_.route(input);

  if (view_ == View::List && action.action == ticketsui::ActionOpenTicket) {
    openTicket(action.value);
  }
  if (view_ == View::List && action.action == ticketsui::ActionOpenUpload) {
    startUploadServer();
  }
  if (view_ == View::Upload && action.action == ticketsui::ActionDismiss) {
    stopUploadServer();
    view_ = View::List;
    interactionsReady_ = false;
    requestUpdate();
  }
  if (view_ == View::Notice && action.action == notesui::ActionDismiss) {
    openList();
  }
  if (view_ == View::Ticket && action.action == ticketsui::ActionDeleteTicket) {
    deleteTicket(openIndex_);
  }
}

// --- Render ----------------------------------------------------------------

void TicketsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::List: {
      ticketsui::ListModel model;
      model.items = rows_.empty() ? nullptr : rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.firstVisible = listTop_;
      model.pageLabel = listPage_.empty() ? nullptr : listPage_.c_str();
      if (!cardOk_) {
        model.emptyHeadline = "CARD NOT READABLE";
        model.emptyMessage = "The tickets on the card could not be listed.";
      } else if (library_.count() == 0) {
        if (library_.skipped() > 0) {
          model.emptyHeadline = "NO VALID TICKETS";
          char message[96];
          std::snprintf(message, sizeof(message), "%d of the .json files in /tickets/ could not be read.",
                        library_.skipped());
          emptyMessage_ = message;
          model.emptyMessage = emptyMessage_.c_str();
        } else {
          model.emptyHeadline = "NO TICKETS";
          model.emptyMessage = "Put one .json file per ticket in /tickets/ on the card.";
        }
      }
      ticketsui::buildList(screen, model);
      break;
    }
    case View::Ticket: {
      const tickets::Ticket& ticket = library_.entries()[static_cast<size_t>(openIndex_)];
      ticketsui::TicketModel model;
      model.name = ticket.name.c_str();
      model.subtitle = ticket.subtitle.empty() ? nullptr : ticket.subtitle.c_str();
      s_deleteTicketIndex = openIndex_;
      model.onDelete = &TicketsActivity::s_deleteTicketCallback;
      const fui::Rect qr = ticketsui::buildTicket(screen, model);
      QrUtils::drawQrCode(
          renderer, Rect{qr.x + kQuietZone, qr.y + kQuietZone, qr.width - 2 * kQuietZone, qr.height - 2 * kQuietZone},
          ticket.qr);
      break;
    }
    case View::Upload: {
      ticketsui::UploadModel model;
      model.title = "ADD TICKET";
      model.url = uploadUrl_.c_str();
      model.readable = uploadReadable_.c_str();
      model.status_ = uploadStatus_.empty() ? nullptr : uploadStatus_.c_str();
      const fui::Rect qr = ticketsui::buildUpload(screen, model);
      QrUtils::drawQrCode(
          renderer, Rect{qr.x + kQuietZone, qr.y + kQuietZone, qr.width - 2 * kQuietZone, qr.height - 2 * kQuietZone},
          uploadUrl_);
      break;
    }
    case View::Notice: {
      notesui::ConfirmModel model;
      model.title = "TICKETS";
      model.prose = notice_.c_str();
      notesui::buildNotice(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  // The buffer records overflow and this reports it; a screen that silently
  // drops its last controls is how Connections lost all its buttons.
  toybox::reportOverflow(interactions_, "Tickets");
  renderer.displayBuffer();
}

void TicketsActivity::onExit() {
  stopUploadServer();
  // Clear global instance pointer for static callbacks
  if (g_ticketsActivityInstance == this) {
    g_ticketsActivityInstance = nullptr;
  }
  Activity::onExit();
}
