#include "TrmnlServer.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>

#include "TrmnlCore.h"
// The generated page is declared PROGMEM, which a static analyser without the
// Arduino headers does not know.
#ifndef PROGMEM
#define PROGMEM
#endif
#include "TrmnlPageHtml.generated.h"
#include "TrmnlStore.h"

namespace {
constexpr uint16_t kPort = 80;
}  // namespace

bool TrmnlServer::begin() {
  if (running_) return true;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  server_ = makeUniqueNoThrow<WebServer>(kPort);
  if (!server_) {
    LOG_ERR("TRMNL", "OOM: WebServer");
    return false;
  }
  // A sleeping radio drops the phone's request and the page blames the network.
  WiFi.setSleep(false);
  server_->on("/trmnl", HTTP_GET, [this] { handlePage(); });
  server_->on("/trmnl/config", HTTP_GET, [this] { handleRead(); });
  // PUT, not POST: this core hands one callback to both the multipart and the
  // raw paths, and the settings are small enough to arrive as a plain body.
  server_->on("/trmnl/config", HTTP_PUT, [this] { handleSave(); });
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not found"); });
  static const char* kHeaders[] = {"If-None-Match"};
  server_->collectHeaders(kHeaders, 1);
  server_->begin();
  running_ = true;
  return true;
}

void TrmnlServer::stop() {
  if (!server_) return;
  running_ = false;
  server_->stop();
  server_.reset();
}

void TrmnlServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void TrmnlServer::handlePage() {
  // Baked into flash at build time, so the ETag is stable for the image.
  if (server_->header("If-None-Match") == TrmnlPageHtmlETag) {
    server_->sendHeader("ETag", TrmnlPageHtmlETag);
    server_->send(304);
    return;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", TrmnlPageHtmlETag);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", TrmnlPageHtml, sizeof(TrmnlPageHtml));
}

void TrmnlServer::handleRead() {
  server_->sendHeader("Cache-Control", "no-store");
  // Normalised on the way out, so the page parses the one shape the reader
  // writes rather than whatever a computer left in the file.
  const std::string text =
      trmnl::formatConfig(trmnl::parseConfig(trmnl::store::read(trmnl::store::kConfigPath, trmnl::kMaxConfigBytes))) +
      facts_;
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}

void TrmnlServer::handleSave() {
  const String raw = server_->arg("plain");
  if (raw.length() > trmnl::kMaxConfigBytes) {
    server_->send(413, "text/plain", "Those settings are too long for the reader.");
    return;
  }
  // A form post or an empty body would parse as all defaults and drop the key.
  if (raw.indexOf("server=") < 0) {
    server_->send(400, "text/plain", "Send the settings as plain key=value lines.");
    return;
  }
  // Parsed and written back out, so every limit in TrmnlCore applies to a
  // phone exactly as it does to a file.
  const std::string text = trmnl::formatConfig(trmnl::parseConfig(std::string(raw.c_str(), raw.length())));
  if (!trmnl::store::write(trmnl::store::kConfigPath, text)) {
    server_->send(500, "text/plain", "The card would not take it.");
    return;
  }
  changed_ = true;
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", (text + facts_).c_str());
}
