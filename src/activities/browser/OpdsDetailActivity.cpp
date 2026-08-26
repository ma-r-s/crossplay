#include "OpdsDetailActivity.h"

#include <Bitmap.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/listIcons.h"
#include "network/HttpDownloader.h"
#include "util/UrlUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr freeink::ui::ActionId ACTION_DOWNLOAD = 1;

// The safe area runs to the bezel; text flush against it reads as a bug.
constexpr int16_t SIDE_PADDING = 16;

// The cover box. Everything the catalog sends is fitted into this, so the
// layout never moves with the image.
constexpr int16_t COVER_W = 120;
constexpr int16_t COVER_H = 180;
}  // namespace

OpdsDetailActivity::OpdsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsEntry entry,
                                       const OpdsServer& server, std::string feedUrl)
    : Activity("OpdsDetail", renderer, mappedInput),
      UiAppHost(renderer),
      entry(std::move(entry)),
      server(server),
      feedUrl(std::move(feedUrl)) {}

void OpdsDetailActivity::downloadTrampoline(const fui::ActionEvent&, void* user) {
  auto* const self = static_cast<OpdsDetailActivity*>(user);
  // Downloading is the browser's job -- it owns the destination folder, the
  // filename format and the progress screen. Finishing uncancelled is the
  // signal to start.
  self->setResult(ActivityResult{});
  self->finish();
}

void OpdsDetailActivity::fetchCover() {
  if (entry.coverHref.empty()) return;
  const std::string url = UrlUtils::buildUrl(feedUrl, entry.coverHref);
  // Keep the source extension: the decoder picks its implementation from it,
  // and a ".tmp" cover is simply never drawn.
  std::string extension = ".bmp";
  const size_t dot = entry.coverHref.find_last_of('.');
  if (dot != std::string::npos && entry.coverHref.size() - dot <= 5) {
    extension = entry.coverHref.substr(dot);
    const size_t query = extension.find('?');
    if (query != std::string::npos) extension.resize(query);
  }
  const std::string dest = "/.crosspoint/opds-cover" + extension;
  Storage.remove(dest.c_str());  // a stale file would silently show the last book's art
  if (HttpDownloader::downloadToFile(url, dest, nullptr, nullptr, server.username, server.password) ==
      HttpDownloader::OK) {
    coverPath = dest;
  } else {
    LOG_DBG("OPDS", "cover fetch failed: %s", url.c_str());
  }
}

void OpdsDetailActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_DOWNLOAD, &OpdsDetailActivity::downloadTrampoline, this);
  app.setScreen(&OpdsDetailActivity::screenTrampoline, this);
  fetchCover();
  coverAvailable = !coverPath.empty() && Storage.exists(coverPath.c_str());
  // The author field is where the catalog packs its metadata, so it is the
  // meta line as-is rather than something reassembled here.
  metaLine = entry.author;
  requestUpdate();
}

fui::Rect OpdsDetailActivity::coverBox(const fui::Rect& content) const {
  const int16_t x = static_cast<int16_t>(content.x + (content.width - COVER_W) / 2);
  return fui::Rect{x, content.y, COVER_W, COVER_H};
}

void OpdsDetailActivity::drawPlaceholder(UiScreen& screen, const fui::Rect& box) {
  // One placeholder for every failure: no cover link, a fetch that timed out,
  // and a format we cannot decode. Distinguishing them on screen would only
  // teach the reader vocabulary for things they cannot act on.
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);

  char initial[5] = {0};
  if (!entry.title.empty()) {
    // First UTF-8 codepoint, not first byte: a title starting with an accented
    // letter would otherwise draw a broken glyph.
    const unsigned char lead = static_cast<unsigned char>(entry.title[0]);
    const size_t len = lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    for (size_t i = 0; i < len && i < entry.title.size(); ++i) initial[i] = entry.title[i];
  }
  fui::TextStyle big = screen.theme().titleText;
  big.align = fui::TextAlign::Center;
  const int16_t lh = screen.target().lineHeight(big.font);
  screen.target().text(fui::Rect{box.x, static_cast<int16_t>(box.y + (box.height - lh) / 2), box.width, lh}, initial,
                       big);
}

void OpdsDetailActivity::drawCover(UiScreen& screen, const fui::Rect& box) {
  coverRect = box;  // painted after the screen tree is flushed

  if (!coverAvailable) {
    drawPlaceholder(screen, box);
    return;
  }
}

void OpdsDetailActivity::paintCover() {
  if (!coverAvailable || coverRect.width <= 0 || coverRect.height <= 0) return;
  HalFile file;
  if (!Storage.openFileForRead("OPDS", coverPath.c_str(), file)) return;
  Bitmap bitmap(file);
  // The constructor only stores the handle; dimensions are zero until the
  // headers are parsed.
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_DBG("OPDS", "cover unreadable: %s", Bitmap::errorToString(err));
    return;
  }
  // Fits within the box preserving aspect, so a landscape or square cover
  // letterboxes rather than stretching, and a small one is not blown up --
  // a 90px thumbnail scaled to 240 is mush at 1 bit.
  const float fit = std::min(static_cast<float>(coverRect.width) / static_cast<float>(bitmap.getWidth()),
                             static_cast<float>(coverRect.height) / static_cast<float>(bitmap.getHeight()));
  const float scale = fit < 1.0f ? fit : 1.0f;  // matches drawBitmap: never upscale
  const int drawnW = static_cast<int>(bitmap.getWidth() * scale);
  const int drawnH = static_cast<int>(bitmap.getHeight() * scale);
  const int x = coverRect.x + (coverRect.width - drawnW) / 2;
  const int y = coverRect.y + (coverRect.height - drawnH) / 2;
  renderer.drawBitmap(bitmap, x, y, coverRect.width, coverRect.height);
}

void OpdsDetailActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<OpdsDetailActivity*>(user)->buildScreen(screen);
}

void OpdsDetailActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Without a content margin the body rect is empty, every takeTop() returns a
  // zero-height slice, and the screen draws nothing at all.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width) + SIDE_PADDING),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x + SIDE_PADDING)});

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_OPDS_BROWSER));

  fui::TextStyle title = theme.titleText;
  // Titles from a catalog are long and unpredictable; an ellipsis on the first
  // line hides the part that distinguishes editions.
  title.maxLines = 2;
  fui::TextStyle body = theme.bodyText;
  body.maxLines = 2;
  const int16_t titleH = screen.target().lineHeight(title.font);
  const int16_t bodyH = screen.target().lineHeight(body.font);
  const int16_t gap = theme.spaceMd;

  // Cover left, text right. With no cover the text column simply widens, so
  // the absent case is not a hole in the layout -- the arrangement that put a
  // full-width cover box on top left a third of the screen as an empty frame.
  screen.spacer(gap);
  const fui::Rect row = screen.takeTop(COVER_H, gap);
  drawCover(screen, fui::Rect{row.x, row.y, COVER_W, COVER_H});

  const int16_t textX = static_cast<int16_t>(row.x + COVER_W + gap);
  const int16_t textW = static_cast<int16_t>(row.width - COVER_W - gap);
  screen.target().text(fui::Rect{textX, row.y, textW, static_cast<int16_t>(titleH * 2)}, entry.title.c_str(), title);
  screen.target().text(
      fui::Rect{textX, static_cast<int16_t>(row.y + titleH * 2 + 4), textW, static_cast<int16_t>(bodyH * 2)},
      metaLine.c_str(), body);

  // Summary fills what is left. Most of the screen was empty without it, and
  // it is the thing that tells two editions apart when the covers do not.
  if (!entry.summary.empty()) {
    fui::TextStyle summary = theme.bodyText;
    const fui::Rect remaining = screen.body();
    const int16_t summaryH = static_cast<int16_t>(remaining.height - theme.rowHeight - gap * 2);
    if (summaryH > bodyH) {
      summary.maxLines = static_cast<uint8_t>(summaryH / bodyH);
      screen.target().text(screen.takeTop(summaryH, gap), entry.summary.c_str(), summary);
    }
  }

  // Download is the only action; it sits at the bottom in every arrangement so
  // the thumb lands in the same place whatever the cover did above.
  const fui::Rect footer = screen.takeBottom(static_cast<int16_t>(theme.rowHeight + gap));
  fui::ButtonProps download;
  download.label = tr(STR_DOWNLOAD);
  download.icon = fui::bitmapFromIcon(icon_download_24);
  download.action = ACTION_DOWNLOAD;
  fui::button(screen.frame(), footer.inset(fui::Insets{0, 40, 8, 40}), download);
}

void OpdsDetailActivity::loop() {
  // routeTouch pumps input and opens UiAppHost's routing handshake; without it
  // the app never publishes a frame.
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
}

void OpdsDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  paintCover();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DOWNLOAD), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Each activity publishes its own frame; without this the screen is drawn
  // into the buffer and never shown.
  renderer.displayBuffer();
}
