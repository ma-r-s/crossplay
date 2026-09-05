#include "WallpapersActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../CrossPointSettings.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxText.h"
#include "../ui/ToyboxTheme.h"
#include "Bitmap.h"
#include "WallpapersCore.h"

namespace fui = freeink::ui;

namespace {
constexpr int kMaxLibrary = 256;
constexpr size_t kNameMax = 128;
constexpr size_t kCopyChunk = 4096;
// The selection marker's dimensions live in WallpapersScreens.cpp beside
// kMarkerRoom, the clearance they have to fit inside.

// A page dot: a small square, filled for the current page.
constexpr int16_t kDotSize = 12;
constexpr int16_t kDotGap = 10;

// Ordered 8x8 Bayer thresholds. A thumbnail is an AREA AVERAGE of the source
// re-dithered at thumbnail size: a 50% threshold collapses a dense engraving
// into a flat blob ("grey mush"), while re-dithering keeps its tone as texture,
// which is the difference between a picker that looks designed and one that
// looks broken. Ordered rather than error diffusion, the house rule: error
// diffusion is what makes flat fields look dirty.
constexpr uint8_t kBayer8[64] = {
    0,  48, 12, 60, 3,  51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31, 8,  56, 4,  52, 11, 59,
    7,  55, 40, 24, 36, 20, 43, 27, 39, 23, 2,  50, 14, 62, 1,  49, 13, 61, 34, 18, 46, 30,
    33, 17, 45, 29, 10, 58, 6,  54, 9,  57, 5,  53, 42, 26, 38, 22, 41, 25, 37, 21,
};

// Extract a 2-bpp pixel (0=black .. 3=white) from readNextRow's packed output.
inline uint8_t px2(const uint8_t* data, int x) {
  return static_cast<uint8_t>((data[x >> 2] >> (6 - ((x & 3) * 2))) & 0x3);
}
}  // namespace

std::unique_ptr<Activity> WallpapersActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WallpapersActivity>(renderer, mappedInput);
}

void WallpapersActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  Storage.mkdir(wallpapers::kLibraryDir);
  scanLibrary();
  loadActive();
  computeWarning();
  // Open on the page holding the set wallpaper, so the border is on screen.
  cachedPage_ = -1;
  page_ = 0;
  requestUpdate();
}

void WallpapersActivity::scanLibrary() {
  names_.clear();
  auto dir = Storage.open(wallpapers::kLibraryDir);
  if (!dir || !dir.isDirectory()) return;
  auto name = makeUniqueNoThrow<char[]>(kNameMax);
  if (!name) {
    LOG_ERR("WALL", "OOM: wallpaper name buffer");
    return;
  }
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax);
    if (!wallpapers::isSupportedWallpaper(std::string_view{name.get()})) continue;
    names_.emplace_back(name.get());
    if (static_cast<int>(names_.size()) >= kMaxLibrary) break;
  }
  std::sort(names_.begin(), names_.end());
}

void WallpapersActivity::loadActive() {
  activeIndex_ = -1;
  if (SETTINGS.sleepScreen != CrossPointSettings::CUSTOM) return;
  if (!Storage.exists(wallpapers::kPinnedSleep)) return;
  char marker[kNameMax] = {};
  if (Storage.readFileToBuffer(wallpapers::kActiveMarker, marker, sizeof(marker)) == 0) return;
  for (char* p = marker; *p; ++p) {
    if (*p == '\n' || *p == '\r') {
      *p = '\0';
      break;
    }
  }
  for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
    if (names_[i] == marker) {
      activeIndex_ = i;
      break;
    }
  }
}

void WallpapersActivity::computeWarning() {
  warning_.clear();
  uint64_t free = 0;
  const bool ok = Storage.freeBytes(free);
  switch (wallpapers::roomFor(ok, free, wallpapers::kCardFloorBytes)) {
    case wallpapers::Room::Ok:
      break;
    case wallpapers::Room::TooFull:
      warning_ = "Card is low on space. New wallpapers or books may not save.";
      break;
    case wallpapers::Room::Unknown:
      warning_ = "Could not check card space.";
      break;
  }
}

int WallpapersActivity::pageCount() const {
  const int per = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext()).perPage;
  if (names_.empty() || per <= 0) return 1;
  const int total = 1 + static_cast<int>(names_.size());  // the + Add tile plus the wallpapers
  return (total + per - 1) / per;
}

void WallpapersActivity::clampPage() {
  const int pages = pageCount();
  if (page_ < 0) page_ = 0;
  if (page_ >= pages) page_ = pages - 1;
}

bool WallpapersActivity::setWallpaper(int index) {
  if (index < 0 || index >= static_cast<int>(names_.size())) return false;
  std::string src = std::string(wallpapers::kLibraryDir) + "/" + names_[static_cast<size_t>(index)];
  HalFile in;
  if (!Storage.openFileForRead("WALL", src, in)) {
    LOG_ERR("WALL", "Cannot open wallpaper %s", src.c_str());
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("WALL", wallpapers::kPinnedSleep, out)) {
    LOG_ERR("WALL", "Cannot open %s for write", wallpapers::kPinnedSleep);
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kCopyChunk);
  if (!buffer) {
    LOG_ERR("WALL", "OOM: copy buffer");
    return false;
  }
  for (;;) {
    const int got = in.read(buffer.get(), kCopyChunk);
    if (got < 0) {
      LOG_ERR("WALL", "Read error copying wallpaper");
      return false;
    }
    if (got == 0) break;
    if (out.write(buffer.get(), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
      LOG_ERR("WALL", "Write error pinning wallpaper (card full?)");
      return false;
    }
  }
  out.close();
  in.close();

  SETTINGS.sleepScreen = CrossPointSettings::CUSTOM;
  SETTINGS.saveToFile();

  HalFile marker;
  if (Storage.openFileForWrite("WALL", wallpapers::kActiveMarker, marker)) {
    const std::string& n = names_[static_cast<size_t>(index)];
    marker.write(reinterpret_cast<const uint8_t*>(n.c_str()), n.size());
    marker.close();
  }
  activeIndex_ = index;
  LOG_INF("WALL", "Set sleep wallpaper: %s", names_[static_cast<size_t>(index)].c_str());
  return true;
}

WallpapersActivity::Thumb WallpapersActivity::decodeThumb(const std::string& path, int16_t cellW, int16_t cellH) const {
  Thumb t;
  HalFile file;
  if (!Storage.openFileForRead("WALL", path, file)) return t;
  Bitmap bmp(file);
  if (bmp.parseHeaders() != BmpReaderError::Ok) return t;

  const int sw = bmp.getWidth();
  const int sh = bmp.getHeight();
  if (sw <= 0 || sh <= 0) return t;
  const bool topDown = bmp.isTopDown();

  // Fit the wallpaper's aspect inside the cell (contain), centred.
  const float scale = std::min(static_cast<float>(cellW) / sw, static_cast<float>(cellH) / sh);
  int dw = std::max(1, static_cast<int>(sw * scale));
  int dh = std::max(1, static_cast<int>(sh * scale));
  if (dw > cellW) dw = cellW;
  if (dh > cellH) dh = cellH;
  t.w = static_cast<int16_t>(dw);
  t.h = static_cast<int16_t>(dh);
  t.ox = static_cast<int16_t>((cellW - dw) / 2);
  t.oy = static_cast<int16_t>((cellH - dh) / 2);

  const int bytesPerRow = (dw + 7) / 8;
  t.bits.assign(static_cast<size_t>(bytesPerRow) * dh, 0);

  auto data = makeUniqueNoThrow<uint8_t[]>((sw + 3) / 4);
  auto rowBuffer = makeUniqueNoThrow<uint8_t[]>(bmp.getRowBytes());
  std::vector<uint16_t> acc(static_cast<size_t>(dw), 0);
  std::vector<uint16_t> cnt(static_cast<size_t>(dw), 0);
  if (!data || !rowBuffer) return t;

  auto flush = [&](int dy) {
    if (dy < 0 || dy >= dh) return;
    uint8_t* outRow = t.bits.data() + static_cast<size_t>(bytesPerRow) * dy;
    for (int dx = 0; dx < dw; ++dx) {
      if (cnt[dx] == 0) continue;
      // How much of the source box was ink, 0..255, then dithered against the
      // ordered matrix so a half-dark box becomes texture rather than a blob.
      const int ink = static_cast<int>(acc[dx]) * 255 / static_cast<int>(cnt[dx]);
      const int threshold = (static_cast<int>(kBayer8[(dy & 7) * 8 + (dx & 7)]) * 255) / 64;
      if (ink > threshold) outRow[dx >> 3] |= static_cast<uint8_t>(0x80 >> (dx & 7));
    }
    std::fill(acc.begin(), acc.end(), 0);
    std::fill(cnt.begin(), cnt.end(), 0);
  };

  int curDy = -1;
  for (int sy = 0; sy < sh; ++sy) {
    if (bmp.readNextRow(data.get(), rowBuffer.get()) != BmpReaderError::Ok) break;
    const int srcRow = topDown ? sy : (sh - 1 - sy);
    int dy = srcRow * dh / sh;
    if (dy >= dh) dy = dh - 1;
    if (dy != curDy) {
      if (curDy >= 0) flush(curDy);
      curDy = dy;
    }
    for (int sx = 0; sx < sw; ++sx) {
      const int dx = sx * dw / sw;
      if (dx >= dw) continue;
      // px2: 0=black .. 3=white; treat the darker half as ink.
      if (px2(data.get(), sx) <= 1) acc[static_cast<size_t>(dx)]++;
      cnt[static_cast<size_t>(dx)]++;
    }
  }
  if (curDy >= 0) flush(curDy);
  t.ok = true;
  return t;
}

void WallpapersActivity::ensureThumbsForPage() {
  const wallpapersui::GridGeom geom = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext());
  if (cachedPage_ == page_ && cachedPerPage_ == geom.perPage && !thumbs_.empty()) return;

  thumbs_.assign(static_cast<size_t>(geom.perPage), Thumb{});
  const int base = page_ * geom.perPage;
  for (int slot = 0; slot < geom.perPage; ++slot) {
    const int combined = base + slot;
    if (combined == 0) continue;  // the + Add tile has no thumbnail
    const int idx = combined - 1;
    if (idx >= static_cast<int>(names_.size())) break;
    std::string path = std::string(wallpapers::kLibraryDir) + "/" + names_[static_cast<size_t>(idx)];
    thumbs_[static_cast<size_t>(slot)] = decodeThumb(path, geom.cellW, geom.cellH);
  }
  cachedPage_ = page_;
  cachedPerPage_ = geom.perPage;
}

void WallpapersActivity::drawGrid(const wallpapersui::GridGeom& geom) {
  const int base = page_ * geom.perPage;
  const int total = 1 + static_cast<int>(names_.size());

  for (int slot = 0; slot < geom.perPage; ++slot) {
    const int combined = base + slot;
    if (combined >= total) break;
    const fui::Rect th = wallpapersui::thumbRect(geom, slot);
    if (combined == 0) {
      drawAddTile(geom, th);  // the first cell is + Add a wallpaper
      continue;
    }
    const int idx = combined - 1;
    const Thumb& t = thumbs_[static_cast<size_t>(slot)];

    // The thumbnail. A set bit is ink.
    if (t.ok) {
      const int bytesPerRow = (t.w + 7) / 8;
      for (int y = 0; y < t.h; ++y) {
        const uint8_t* row = t.bits.data() + static_cast<size_t>(bytesPerRow) * y;
        for (int x = 0; x < t.w; ++x) {
          if (row[x >> 3] & (0x80 >> (x & 7))) renderer.drawPixel(th.x + t.ox + x, th.y + t.oy + y, true);
        }
      }
    } else {
      // A wallpaper that would not decode still gets a mark, never a blank cell.
      renderer.drawLine(th.x, th.y, th.right() - 1, th.bottom() - 1, true);
      renderer.drawLine(th.x, th.bottom() - 1, th.right() - 1, th.y, true);
    }

    // A hairline on every cell so a mostly-white wallpaper still reads as a
    // framed tile. This is not the selection signal: it is on every cell.
    renderer.drawRect(th.x, th.y, th.width, th.height, 1, true);
    if (idx == activeIndex_) drawMarker(th);

    // Caption (variants with one): the file name, fitted so it never truncates
    // into a missing glyph.
    if (geom.captionH > 0) {
      const fui::Rect cap = wallpapersui::captionRect(geom, slot);
      fui::GfxRendererTarget target = toybox::makeTarget(renderer);
      fui::TextStyle style = toybox::themeTokens().smallText;
      // smallText maps to the BODY cut; the caption wants the actual small cut
      // (toybox_10) so it fits the caption row instead of towering over it.
      style.font = fui::FONT_SLOT_SMALL;
      style.align = fui::TextAlign::Center;
      style.color = fui::Color::Black;
      // A real name, not the file name: a picker showing "blake-door.bmp" cut
      // mid-word looks unfinished. When the long form does not fit the cell we
      // fall back to a shorter NAME rather than an ellipsis, and log which was
      // used so the fit is measured rather than eyeballed.
      const wallpapers::DisplayName name = wallpapers::displayName(names_[static_cast<size_t>(idx)]);
      std::string fitted = toybox::fitLines(target, name.full.c_str(), cap.width, 1, style);
      if (fitted != name.full) {
        fitted = toybox::fitLines(target, name.brief.c_str(), cap.width, 1, style);
        LOG_DBG("WALL", "caption fell back to brief: %s -> %s", name.full.c_str(), fitted.c_str());
      }
      if (fitted != name.full && fitted != name.brief) {
        LOG_ERR("WALL", "caption STILL cut: %s", fitted.c_str());
      }
      target.text(cap, fitted.c_str(), style);
    }
  }

  // Page dots, when the library spans more than one page.
  const int pages = pageCount();
  if (pages > 1) {
    const int16_t totalW = static_cast<int16_t>(pages * kDotSize + (pages - 1) * kDotGap);
    const fui::Rect panel = toybox::makeTarget(renderer).deviceContext().screen();
    int16_t x = static_cast<int16_t>((panel.width - totalW) / 2);
    for (int p = 0; p < pages; ++p) {
      if (p == page_) {
        renderer.fillRect(x, geom.pageDotsY, kDotSize, kDotSize, true);
      } else {
        renderer.drawRect(x, geom.pageDotsY, kDotSize, kDotSize, 1, true);
      }
      x = static_cast<int16_t>(x + kDotSize + kDotGap);
    }
  }
}

void WallpapersActivity::drawMarker(const fui::Rect& th) const {
  // Four corner brackets in the cell's padding. The rectangles come from
  // wallpapersui::markerRects so the shape the panel draws is the same shape
  // host-tests/wallcaption proves clear of the artwork and of every caption:
  // a marker whose geometry lived only here could drift from its own proof.
  // Nothing inside a picture looks like this, which is why the mark cannot be
  // mistaken for the artwork's own frame -- several plates carry real borders.
  const wallpapersui::MarkerRects m = wallpapersui::markerRects(th);
  for (const fui::Rect& r : m.r) renderer.fillRect(r.x, r.y, r.width, r.height, true);
}

void WallpapersActivity::drawAddTile(const wallpapersui::GridGeom& geom, const fui::Rect& th) {
  // A 2px frame so the add tile reads as a control distinct from a wallpaper.
  renderer.drawRect(th.x, th.y, th.width, th.height, 2, true);

  // A big plus in the upper part of the tile.
  const int cx = th.x + th.width / 2;
  const int cy = th.y + th.height * 2 / 5;
  const int len = th.width * 2 / 5;
  const int wgt = std::max(6, th.width / 12);
  renderer.fillRect(cx - len / 2, cy - wgt / 2, len, wgt, true);
  renderer.fillRect(cx - wgt / 2, cy - len / 2, wgt, len, true);

  // The label, inside the tile below the plus, at the actual small cut.
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  fui::TextStyle style = toybox::themeTokens().smallText;
  style.font = fui::FONT_SLOT_SMALL;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::Black;
  const fui::Rect label = fui::makeRect(th.x, static_cast<int16_t>(th.y + th.height * 3 / 5), th.width,
                                        static_cast<int16_t>(th.height / 3));
  // The dense grid's cell is too narrow for the long label, and a truncated
  // "Add..." reads as a bug rather than a control. Take the long form when it
  // fits whole, the short one when it does not; the plus carries the meaning
  // either way.
  static constexpr const char* kLong = "Add wallpaper";
  std::string fitted = toybox::fitLines(target, kLong, label.width, 1, style);
  if (fitted != kLong) fitted = "Add";
  target.text(label, fitted.c_str(), style);
  (void)geom;
}

void WallpapersActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (showingHelp_) {
      showingHelp_ = false;
      requestUpdate();
      return;
    }
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (showingHelp_ || names_.empty()) return;

  const int pages = pageCount();
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (page_ + 1 < pages) {
      ++page_;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (page_ > 0) {
      --page_;
      requestUpdate();
    }
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  const wallpapersui::GridGeom geom = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext());

  // A page dot?
  if (pages > 1) {
    const int16_t totalW = static_cast<int16_t>(pages * kDotSize + (pages - 1) * kDotGap);
    const fui::Rect panel = toybox::makeTarget(renderer).deviceContext().screen();
    int16_t x = static_cast<int16_t>((panel.width - totalW) / 2);
    for (int p = 0; p < pages; ++p) {
      // A generous vertical band so the small dots are easy to hit.
      if (tapX >= x - kDotGap / 2 && tapX < x + kDotSize + kDotGap / 2 && tapY >= geom.pageDotsY - 16 &&
          tapY < geom.pageDotsY + kDotSize + 16) {
        if (p != page_) {
          page_ = p;
          requestUpdate();
        }
        return;
      }
      x = static_cast<int16_t>(x + kDotSize + kDotGap);
    }
  }

  // A grid cell? The first cell of page 0 is the + Add a wallpaper tile.
  const int slot = wallpapersui::cellAt(geom, tapX, tapY);
  if (slot < 0) return;
  if (!surfaceRevealed()) return;  // ignore a tap on a surface not yet seen
  const int combined = page_ * geom.perPage + slot;
  const int total = 1 + static_cast<int>(names_.size());
  if (combined >= total) return;
  if (combined == 0) {
    showingHelp_ = true;
    requestUpdate();
    return;
  }
  const int idx = combined - 1;
  if (idx == activeIndex_) return;  // already the sleep screen
  if (setWallpaper(idx)) requestUpdate();
}

void WallpapersActivity::render(RenderLock&&) {
  clampPage();
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen surface(frame);

  if (showingHelp_) {
    wallpapersui::buildHelp(surface);
  } else if (names_.empty()) {
    wallpapersui::EmptyModel model;
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    wallpapersui::buildEmpty(surface, model);
  } else {
    const wallpapersui::GridGeom geom = wallpapersui::gridGeom(device);
    const int pages = pageCount();
    if (pages > 1) {
      char label[40];
      snprintf(label, sizeof(label), "PAGE %d / %d", page_ + 1, pages);
      rightLabel_ = label;
    } else {
      char label[40];
      snprintf(label, sizeof(label), "%d SAVED", static_cast<int>(names_.size()));
      rightLabel_ = label;
    }
    wallpapersui::GridChromeModel model;
    model.rightLabel = rightLabel_.c_str();
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    model.hasActive = activeIndex_ >= 0;
    wallpapersui::buildGridChrome(surface, model);

    // The chrome is a screen tree; the grid is the app's own surface, drawn
    // after it into the body the same way chess draws its board.
    ensureThumbsForPage();
    drawGrid(geom);
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Wallpapers");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

uint32_t WallpapersActivity::surfaceMeaning() const {
  uint32_t m = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(page_));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(activeIndex_ + 1));
  m = paintclock::mixMeaning(m, showingHelp_ ? 1u : 0u);
  return paintclock::mixMeaning(m, static_cast<uint32_t>(names_.size()));
}
