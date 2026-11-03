#include "RssServer.h"

#include <Logging.h>
#include <WebServer.h>

#include <new>
#include <string>

#include "RssPageHtml.generated.h"

RssServer::RssServer(rss::Desk& desk) : desk_(desk) {}

RssServer::~RssServer() { stop(); }

bool RssServer::begin() {
  if (server_) return true;
  server_.reset(new (std::nothrow) WebServer(80));
  if (!server_) {
    LOG_ERR("RSS", "OOM: web server");
    return false;
  }
  // A bare address typed into a browser lands on the page rather than on a
  // 404, because the QR code is not the only way here: the address under it is
  // printed to be typed.
  server_->on("/", HTTP_GET, [this] {
    server_->sendHeader("Location", "/r", true);
    server_->send(302, "text/plain", "");
  });
  server_->on("/r", HTTP_GET, [this] { page(); });
  server_->on("/r/state", HTTP_GET, [this] { state(); });
  // PUT, the way Notes saves: this core hands one callback to both its
  // multipart and its raw paths, and a plain body under PUT reaches neither --
  // it arrives whole as arg("plain"), which is all an address needs.
  server_->on("/r/add", HTTP_PUT, [this] { request(true); });
  server_->on("/r/remove", HTTP_PUT, [this] { request(false); });
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not here."); });
  server_->begin();
  LOG_INF("RSS", "phone page up");
  return true;
}

void RssServer::stop() {
  if (!server_) return;
  server_->stop();
  server_.reset();
  LOG_INF("RSS", "phone page down");
}

void RssServer::handleClient() {
  if (server_) server_->handleClient();
}

void RssServer::page() {
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", RssPageHtml, sizeof(RssPageHtml));
}

void RssServer::state() {
  const std::string json = desk_.stateJson();
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "application/json", json.c_str());
}

void RssServer::request(const bool add) {
  const String body = server_->arg("plain");
  if (body.length() > rss::Desk::kMaxRequestBytes) {
    server_->send(413, "text/plain", "That is too long for an address.");
    return;
  }
  const std::string text(body.c_str(), body.length());
  const int job = add ? desk_.requestAdd(text) : desk_.requestRemove(text);
  if (job < 0) {
    server_->send(text.empty() ? 400 : 429, "text/plain",
                  text.empty() ? "Paste an address first." : "The reader is still busy. Try again in a moment.");
    return;
  }
  LOG_INF("RSS", "phone asked to %s job %d", add ? "add" : "remove", job);
  const std::string json = "{\"job\":" + std::to_string(job) + "}";
  server_->send(202, "application/json", json.c_str());
}
