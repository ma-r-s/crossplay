// The TRMNL screens, built against a fake target: what they draw where, and
// what they make tappable.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "TrmnlScreens.h"

namespace fui = freeink::ui;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

class FakeTarget final : public fui::DrawTarget {
 public:
  struct TextRun {
    fui::Rect rect;
    std::string text;
  };
  std::vector<TextRun> texts;

  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(text == nullptr ? 0 : std::strlen(text) * 10), 20};
  }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle) override {
    texts.push_back({rect, text == nullptr ? "" : text});
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}

  bool drew(const char* needle) const {
    for (const TextRun& run : texts) {
      if (run.text.find(needle) != std::string::npos) return true;
    }
    return false;
  }
};

struct Rendered {
  FakeTarget target;
  toybox::Interactions interactions;

  bool has(const fui::ActionId action) const {
    for (size_t i = 0; i < interactions.count(); i++) {
      if (interactions.data()[i].action == action) return true;
    }
    return false;
  }
  bool allOnScreen(const fui::DeviceContext& device) const {
    for (size_t i = 0; i < interactions.count(); i++) {
      const fui::Rect& r = interactions.data()[i].rect;
      if (r.x < 0 || r.y < 0 || r.x + r.width > device.width || r.y + r.height > device.height) return false;
    }
    for (const FakeTarget::TextRun& run : target.texts) {
      if (run.rect.x < 0 || run.rect.x + run.rect.width > device.width) return false;
      if (run.rect.y < 0 || run.rect.y + run.rect.height > device.height) return false;
    }
    return true;
  }
};

fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

trmnlui::HomeModel homeModel(const bool hasImage) {
  trmnlui::HomeModel model;
  model.server = "trmnl.app";
  model.device = "DE:AD:BE:EF:00:01";
  model.cadence = "every 15 min";
  model.status = "Updated at 14:05. The next one comes in 12 min.";
  model.hasImage = hasImage;
  return model;
}

void testHome() {
  for (const bool hasImage : {false, true}) {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    const fui::Rect picture = trmnlui::buildHome(screen, homeModel(hasImage));
    CHECK(out.has(trmnlui::ActionShow));
    CHECK(out.has(trmnlui::ActionRefresh));
    CHECK(out.has(trmnlui::ActionUsePhone));
    CHECK(!out.interactions.overflowed());
    CHECK(out.allOnScreen(ctx));
    CHECK(out.target.drew("trmnl.app"));
    CHECK(out.target.drew("DE:AD:BE:EF:00:01"));
    if (hasImage && picture.width > 0) {
      CHECK(picture.x >= 0 && picture.x + picture.width <= ctx.width);
      CHECK(picture.y > 0 && picture.height > 0);
    }
  }
}

void testPhone() {
  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame);
  trmnlui::PhoneModel model;
  model.url = "http://192.168.1.40/trmnl";
  model.readable = "http://crossplay.local/trmnl";
  const fui::Rect qr = trmnlui::buildPhone(screen, model);
  CHECK(qr.width == qr.height);
  CHECK(qr.width >= 120);
  CHECK(qr.x >= 0 && qr.x + qr.width <= ctx.width);
  CHECK(out.has(trmnlui::ActionDismiss));
  CHECK(out.target.drew("crossplay.local"));
  CHECK(out.target.drew("WAITING FOR YOUR PHONE"));
  CHECK(out.allOnScreen(ctx));
  // The code never runs under the footer's DONE.
  for (size_t i = 0; i < out.interactions.count(); i++) {
    const fui::Interaction& hit = out.interactions.data()[i];
    if (hit.action == trmnlui::ActionDismiss) CHECK(hit.rect.y >= qr.y + qr.height);
  }
}

void testNoticeAndBusy() {
  {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    trmnlui::buildNotice(screen, "The server said: Device not found.");
    CHECK(out.has(trmnlui::ActionDismiss));
    CHECK(out.allOnScreen(ctx));
  }
  {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    trmnlui::buildBusy(screen, "Asking trmnl.app for your screen.");
    CHECK(out.interactions.count() == 0);
    CHECK(out.target.drew("Asking"));
  }
}

}  // namespace

int main() {
  testHome();
  testPhone();
  testNoticeAndBusy();
  std::printf("trmnl screens: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
