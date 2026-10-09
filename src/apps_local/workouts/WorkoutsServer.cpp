#include "WorkoutsServer.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <sys/time.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "WorkoutsCore.h"
#include "WorkoutsPageHtml.generated.h"
#include "WorkoutsStore.h"

namespace {

constexpr uint16_t kPort = 80;

// Below this the clock was never set: a device whose RTC was cleared restarts
// near 1970. Study and Live use the same floor.
constexpr int64_t kClockFloor = 1700000000;

// The page sends the phone's own clock with every save. A reader that lost its
// time otherwise has no date to put a workout on until something else syncs
// it, and the person setting up their schedules is holding the one device in
// the room that certainly knows. Only ever forward, and only from an unset
// clock: this is not a timekeeper, and stepping a set clock for a phone's
// drift would only move "today" under the app.
void adoptPhoneTime(const String& value) {
  if (value.length() == 0) return;
  const int64_t phone = std::strtoll(value.c_str(), nullptr, 10);
  if (phone < kClockFloor) return;
  if (static_cast<int64_t>(time(nullptr)) >= kClockFloor) return;
#ifndef SIMULATOR
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(phone);
  settimeofday(&tv, nullptr);
  LOG_INF("WORKOUTS", "clock set from the phone");
#endif
}

}  // namespace

bool WorkoutsServer::begin() {
  if (running_) return true;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  server_ = makeUniqueNoThrow<WebServer>(kPort);
  if (!server_) {
    LOG_ERR("WORKOUTS", "OOM: WebServer");
    return false;
  }
  // The same two settings the reader's own server makes, for the same reason:
  // a sleeping radio drops the phone's request and the page blames the network.
  WiFi.setSleep(false);
  server_->on("/gym", HTTP_GET, [this] { handlePage(); });
  server_->on("/gym/plan", HTTP_GET, [this] { handleRead(); });
  // PUT, not POST: this core hands one callback to both the multipart and the
  // raw paths, and a plan is small enough to arrive as a plain body.
  server_->on("/gym/plan", HTTP_PUT, [this] { handleSave(); });
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not found"); });
  static const char* kHeaders[] = {"If-None-Match", "X-Phone-Time"};
  server_->collectHeaders(kHeaders, 2);
  server_->begin();
  running_ = true;
  return true;
}

void WorkoutsServer::stop() {
  if (!server_) return;
  running_ = false;
  server_->stop();
  server_.reset();
}

void WorkoutsServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void WorkoutsServer::handlePage() {
  // Baked into flash at build time, so the ETag is stable for the image.
  if (server_->header("If-None-Match") == WorkoutsPageHtmlETag) {
    server_->sendHeader("ETag", WorkoutsPageHtmlETag);
    server_->send(304);
    return;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", WorkoutsPageHtmlETag);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", WorkoutsPageHtml, sizeof(WorkoutsPageHtml));
}

void WorkoutsServer::handleRead() {
  server_->sendHeader("Cache-Control", "no-store");
  // Normalised on the way out, so the page parses the one shape the reader
  // writes rather than whatever a computer left in the file.
  const std::string plan = workouts::formatPlan(
      workouts::parsePlan(workouts::store::read(workouts::store::kPlanPath, workouts::kMaxPlanBytes)));
  server_->send(200, "text/plain; charset=utf-8", plan.c_str());
}

void WorkoutsServer::handleSave() {
  const String raw = server_->arg("plain");
  if (raw.length() > workouts::kMaxPlanBytes) {
    server_->send(413, "text/plain", "That plan is too long for the reader.");
    return;
  }
  adoptPhoneTime(server_->header("X-Phone-Time"));
  // Parsed and written back out, so every limit in WorkoutsCore applies to a
  // phone exactly as it does to a file: what lands on the card is what the
  // reader will show.
  const std::string text = workouts::formatPlan(workouts::parsePlan(std::string(raw.c_str(), raw.length())));
  if (!workouts::store::write(workouts::store::kPlanPath, text)) {
    server_->send(500, "text/plain", "The card would not take it.");
    return;
  }
  changed_ = true;
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}
