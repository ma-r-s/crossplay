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
#include <string>
#include <vector>

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

// The description decides the arrangement, the way the catalog's shape decides
// the entry screen. Without one there is nothing to fill the lower half, so
// the cover becomes the screen and everything centres under it. With one, the
// cover steps aside into a column so the text has room.
//
// Portrait, centered: the bookshop look, used when there is no description.
constexpr int16_t COVER_BIG_W = 200;
constexpr int16_t COVER_BIG_H = 300;
// Small, beside a column of facts, used when a description needs the space.
constexpr int16_t COVER_SMALL_W = 110;
constexpr int16_t COVER_SMALL_H = 165;

// "Herbert, Frank · 2005 · English · EPUB · 73 MB" is what catalogs pack into
// the author field. Split on the separator so a layout can lay the facts out
// as rows instead of truncating the line at "73...".
std::vector<std::string> splitFacts(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  const std::string sep = " \u00b7 ";
  // The separator is a UTF-8 middle dot; search for the raw bytes.
  static const char kSep[] = "\xc2\xb7";
  while (start <= line.size()) {
    const size_t at = line.find(kSep, start);
    std::string part = line.substr(start, at == std::string::npos ? std::string::npos : at - start);
    // Trim the spaces that surrounded the dot.
    while (!part.empty() && part.front() == ' ') part.erase(part.begin());
    while (!part.empty() && part.back() == ' ') part.pop_back();
    if (!part.empty()) out.push_back(part);
    if (at == std::string::npos) break;
    start = at + sizeof(kSep) - 1;
  }
  return out;
}
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
  // NOT fetchCover() here. It blocks on the network for seconds, and onEnter
  // runs before anything is published, so tapping a book left the results list
  // on the panel with no response at all -- indistinguishable from a crash,
  // and readers reported it as one. The screen goes up first; the cover
  // arrives in a later frame, which is what the placeholder is for.
  coverPending = !entry.coverHref.empty();
  // The author field is where the catalog packs its metadata, so it is the
  // meta line as-is rather than something reassembled here.
  metaLine = entry.author;
  requestUpdate();
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
  if (!coverAvailable) return;
  paintCoverFile(renderer, coverPath, coverRect);
}

bool OpdsDetailActivity::paintCoverFile(GfxRenderer& renderer, const std::string& coverPath,
                                        const freeink::ui::Rect& coverRect) {
  if (coverPath.empty() || coverRect.width <= 0 || coverRect.height <= 0) return false;

  // BMP first, because it needs no decoder at all: the Get Books service
  // serves 8-bit greyscale BMP for exactly that reason, and it is the only
  // path that also works in the simulator, where JPEGDEC is not built.
  {
    HalFile bmpFile;
    if (Storage.openFileForRead("OPDS", coverPath.c_str(), bmpFile)) {
      Bitmap probe(bmpFile);
      if (probe.parseHeaders() == BmpReaderError::Ok) {
        const float fit = std::min(static_cast<float>(coverRect.width) / static_cast<float>(probe.getWidth()),
                                   static_cast<float>(coverRect.height) / static_cast<float>(probe.getHeight()));
        const float scale = fit < 1.0f ? fit : 1.0f;  // matches drawBitmap: never upscale
        const int drawnW = static_cast<int>(probe.getWidth() * scale);
        const int drawnH = static_cast<int>(probe.getHeight() * scale);
        renderer.drawBitmap(probe, coverRect.x + (coverRect.width - drawnW) / 2,
                            coverRect.y + (coverRect.height - drawnH) / 2, coverRect.width, coverRect.height);
        return true;
      }
    }
  }

  // Anything else -- other catalogs serve JPEG and PNG. The EPUB reader
  // already decodes both for in-book images; this is the same decoder.
  if (ImageDecoderFactory::isFormatSupported(coverPath)) {
    ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(coverPath);
    if (decoder) {
      RenderConfig config;
      config.x = coverRect.x;
      config.y = coverRect.y;
      config.maxWidth = coverRect.width;
      config.maxHeight = coverRect.height;
      // Dithered 1-bit, not greyscale. The panel is 1-bit, and asking the
      // decoder for greyscale here produced a solid black rectangle: the
      // grey levels have nowhere to go in this draw path.
      config.useGrayscale = false;
      config.useDithering = true;
      if (decoder->decodeToFramebuffer(coverPath, renderer, config)) return true;
      LOG_DBG("OPDS", "cover decode failed: %s", decoder->getFormatName());
    }
  }
  return false;
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
  screen.setContentMarginAbsolute(
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
  const int16_t titleH = screen.target().lineHeight(title.font);
  const int16_t bodyH = screen.target().lineHeight(body.font);
  const int16_t gap = theme.spaceMd;

  // "Herbert, Frank · 2005 · English · EPUB · 731 kB" is one field as far as
  // the catalog is concerned. Split it so the author -- the part a reader
  // actually reads -- gets its own line and the rest stays on one tidy row
  // instead of wrapping into a ragged block.
  const std::vector<std::string> facts = splitFacts(metaLine);
  const std::string author = facts.empty() ? std::string() : facts.front();
  std::string details;
  for (size_t i = 1; i < facts.size(); ++i) {
    if (!details.empty()) details += " \u00b7 ";
    details += facts[i];
  }

  if (entry.summary.empty()) {
    // ---- No description: the cover is the screen ----
    screen.spacer(gap);
    const fui::Rect coverRow = screen.takeTop(COVER_BIG_H, gap);
    drawCover(screen, fui::Rect{static_cast<int16_t>(coverRow.x + (coverRow.width - COVER_BIG_W) / 2), coverRow.y,
                                COVER_BIG_W, COVER_BIG_H});

    fui::TextStyle centeredTitle = title;
    centeredTitle.align = fui::TextAlign::Center;
    screen.target().text(screen.takeTop(static_cast<int16_t>(titleH * 2), 6), entry.title.c_str(), centeredTitle);

    fui::TextStyle centeredBody = body;
    centeredBody.align = fui::TextAlign::Center;
    centeredBody.maxLines = 1;
    if (!author.empty()) {
      screen.target().text(screen.takeTop(bodyH, 2), author.c_str(), centeredBody);
    }
    if (!details.empty()) {
      screen.target().text(screen.takeTop(bodyH, gap), details.c_str(), centeredBody);
    }
  } else {
    // ---- With a description: cover steps aside, text gets the room ----
    screen.spacer(gap);
    screen.target().text(screen.takeTop(static_cast<int16_t>(titleH * 2), gap), entry.title.c_str(), title);

    const fui::Rect row = screen.takeTop(COVER_SMALL_H, gap);
    drawCover(screen, fui::Rect{row.x, row.y, COVER_SMALL_W, COVER_SMALL_H});

    // One fact per line so nothing truncates and the eye can scan down.
    const int16_t factX = static_cast<int16_t>(row.x + COVER_SMALL_W + gap);
    const int16_t factW = static_cast<int16_t>(row.width - COVER_SMALL_W - gap);
    int16_t factY = row.y;
    fui::TextStyle fact = body;
    fact.maxLines = 1;
    for (const auto& f : facts) {
      if (factY + bodyH > row.y + row.height) break;
      screen.target().text(fui::Rect{factX, factY, factW, bodyH}, f.c_str(), fact);
      factY = static_cast<int16_t>(factY + bodyH + 2);
    }

    fui::TextStyle summary = body;
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

  // Checked before the fetch below, so a Back that arrives while the cover is
  // still pending leaves immediately instead of waiting out the network.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Back is NOT a download. finish() on its own leaves the default result,
    // and ActivityResult::isCancelled defaults to false -- byte for byte what
    // downloadTrampoline() sends -- so the browser's `if (!result.isCancelled)`
    // started a multi-megabyte transfer for a reader who only wanted their
    // search results back.
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }

  if (coverPending && framePresented) {
    // Cleared first: a fetch that throws or hangs must not be retried on every
    // pass, which would wedge the screen for as long as the reader stayed.
    coverPending = false;
    fetchCover();
    coverAvailable = !coverPath.empty() && Storage.exists(coverPath.c_str());
    requestUpdate();
  }
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
  // Only now is it safe to block: this is the frame the reader is looking at.
  framePresented = true;
}
