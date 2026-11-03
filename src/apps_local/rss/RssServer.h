#pragma once

// The phone page's server: one page, its state, and two requests.
//
// Its own small server rather than another surface on CrossPointWebServer,
// because nothing here needs that one's machinery and every app added there is
// an upstream file growing (LOCAL_SCOPE.md). What it serves is narrower than
// Notes' surface, for the same reason Notes' is narrower than the file
// manager: the address is printed in a QR code and anyone in the room can scan
// it, so behind it there is a list of feed addresses and nothing else -- no
// file, no setting, no card.
//
// It does no work. Requests go onto the Desk and the app answers them from its
// own loop; see RssDesk.h for why.

#include <memory>

#include "RssDesk.h"

class WebServer;

class RssServer {
 public:
  explicit RssServer(rss::Desk& desk);
  ~RssServer();
  RssServer(const RssServer&) = delete;
  RssServer& operator=(const RssServer&) = delete;

  bool begin();
  void stop();
  void handleClient();
  bool running() const { return server_ != nullptr; }

 private:
  void page();
  void state();
  void request(bool add);

  rss::Desk& desk_;
  std::unique_ptr<WebServer> server_;
};
