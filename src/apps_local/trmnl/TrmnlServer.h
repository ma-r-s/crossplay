#pragma once

// The phone page's server. One page and one file: GET /trmnl is the settings
// page, and /trmnl/config reads and writes the settings. Nothing else is
// routed, so the address in the QR code reaches the settings and not the card.
//
// Its own small WebServer rather than another surface on CrossPointWebServer,
// so adding an app costs no upstream file.

#include <WebServer.h>

#include <memory>
#include <string>

class TrmnlServer {
 public:
  ~TrmnlServer() { stop(); }

  // False when the server could not be made or there is no network to put it
  // on. The simulator has neither, and the screen is still drawn there.
  bool begin();
  void stop();
  bool isRunning() const { return running_; }
  void handleClient();

  // Read-only key=value lines the page shows under the settings: the MAC, the
  // friendly id, the server's last word.
  void setFacts(const std::string& facts) { facts_ = facts; }

  // True once a phone has saved. Cleared by the reader of it.
  bool takeChanged() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
  }

 private:
  void handlePage();
  void handleRead();
  void handleSave();

  std::unique_ptr<WebServer> server_;
  std::string facts_;
  bool running_ = false;
  bool changed_ = false;
};
