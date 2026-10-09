#pragma once

// The phone page's server. One page and one file: GET /gym is the editor, and
// /gym/plan reads and writes the plan. Nothing else is routed, so the address in
// the QR code reaches the schedules and not the card -- the same reason Notes
// and Wallpapers each serve one surface of their own.
//
// Its own small WebServer rather than another surface on CrossPointWebServer,
// so adding an app costs no upstream file.

#include <WebServer.h>

#include <memory>

class WorkoutsServer {
 public:
  ~WorkoutsServer() { stop(); }

  // False when the server could not be made or there is no network to put it
  // on. The simulator has neither, and the screen is still drawn there.
  bool begin();
  void stop();
  bool isRunning() const { return running_; }
  void handleClient();

  // True once a phone has saved, so the app re-reads the plan rather than
  // polling the card. Cleared by the reader of it.
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
  bool running_ = false;
  bool changed_ = false;
};
