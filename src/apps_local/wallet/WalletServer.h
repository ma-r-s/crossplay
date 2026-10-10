#pragma once

// The phone page's server. GET /cards is the page, /cards/zxing.js the decoder
// it loads, and /cards/card lists, adds and deletes. Nothing else is routed, so
// the address in the QR code reaches the cards and nothing else on the reader.
//
// Its own small WebServer rather than another surface on CrossPointWebServer,
// so adding an app costs no upstream file -- the Workouts arrangement.

#include <WebServer.h>

#include <memory>

class WalletServer {
 public:
  ~WalletServer() { stop(); }

  // False when the server could not be made or there is no network to put it
  // on. The simulator has neither, and the screen is still drawn there.
  bool begin();
  void stop();
  bool isRunning() const { return running_; }
  void handleClient();

  // True once a phone has added or deleted a card, so the app re-reads /cards.
  // Cleared by the reader of it.
  bool takeChanged() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
  }
  // Cards added since begin().
  int added() const { return added_; }

 private:
  void handlePage();
  void handleDecoder();
  void handleList();
  void handleAdd();
  void handleDelete();

  std::unique_ptr<WebServer> server_;
  bool running_ = false;
  bool changed_ = false;
  int added_ = 0;
};
