#include "WalletServer.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>
#include <vector>

#include "WalletCore.h"
#include "WalletPageHtml.generated.h"
#include "WalletStore.h"
#include "WalletZxingJs.generated.h"

namespace {

constexpr uint16_t kPort = 80;

// Both assets are baked into flash at build time, so each ETag is stable for
// the image and a phone that already has them gets a 304.
void sendGzip(WebServer& server, const char* type, const char* body, const size_t size, const char* etag) {
  if (server.header("If-None-Match") == etag) {
    server.sendHeader("ETag", etag);
    server.send(304);
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("ETag", etag);
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, body, size);
}

}  // namespace

bool WalletServer::begin() {
  if (running_) return true;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  server_ = makeUniqueNoThrow<WebServer>(kPort);
  if (!server_) {
    LOG_ERR("CARDS", "OOM: WebServer");
    return false;
  }
  // The same setting the reader's own server makes: a sleeping radio drops the
  // phone's request and the page blames the network.
  WiFi.setSleep(false);
  server_->on("/cards", HTTP_GET, [this] { handlePage(); });
  server_->on("/cards/zxing.js", HTTP_GET, [this] { handleDecoder(); });
  server_->on("/cards/card", HTTP_GET, [this] { handleList(); });
  // PUT, not POST: this core hands one callback to both the multipart and the
  // raw paths, and a card is small enough to arrive as a plain body.
  server_->on("/cards/card", HTTP_PUT, [this] { handleAdd(); });
  server_->on("/cards/card", HTTP_DELETE, [this] { handleDelete(); });
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not found"); });
  static const char* kHeaders[] = {"If-None-Match"};
  server_->collectHeaders(kHeaders, 1);
  server_->begin();
  running_ = true;
  added_ = 0;
  return true;
}

void WalletServer::stop() {
  if (!server_) return;
  running_ = false;
  server_->stop();
  server_.reset();
}

void WalletServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void WalletServer::handlePage() {
  sendGzip(*server_, "text/html", WalletPageHtml, sizeof(WalletPageHtml), WalletPageHtmlETag);
}

void WalletServer::handleDecoder() {
  sendGzip(*server_, "application/javascript", WalletZxingJs, sizeof(WalletZxingJs), WalletZxingJsETag);
}

void WalletServer::handleList() {
  server_->sendHeader("Cache-Control", "no-store");
  const std::string listing = wallet::formatListing(wallet::store::loadAll());
  server_->send(200, "text/plain; charset=utf-8", listing.c_str());
}

void WalletServer::handleAdd() {
  const String raw = server_->arg("plain");
  // Two hex digits a byte, plus the title and caption lines.
  if (raw.length() > wallet::kMaxPayload * 2 + wallet::kMaxTitle * 4 + wallet::kMaxCaption * 4 + 8) {
    server_->send(413, "text/plain", "That code holds too much to draw on the reader.");
    return;
  }
  if (wallet::store::count() >= static_cast<size_t>(wallet::kMaxCards)) {
    server_->send(409, "text/plain", "The reader is full. Delete a card first.");
    return;
  }
  wallet::Card card;
  std::string error;
  if (!wallet::parseUpload(std::string(raw.c_str(), raw.length()), card, error)) {
    server_->send(400, "text/plain", error.c_str());
    return;
  }
  if (!wallet::store::add(card)) {
    server_->send(500, "text/plain", "The SD card would not take it.");
    return;
  }
  changed_ = true;
  added_++;
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", card.file.c_str());
}

void WalletServer::handleDelete() {
  const String file = server_->arg("file");
  if (wallet::numberOf(std::string(file.c_str())) < 0) {
    server_->send(400, "text/plain", "There is no such card.");
    return;
  }
  if (!wallet::store::remove(std::string(file.c_str()))) {
    server_->send(500, "text/plain", "The SD card would not delete it.");
    return;
  }
  changed_ = true;
  server_->send(204);
}
