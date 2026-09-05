#include "WallpapersActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../CrossPointSettings.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../network/HttpDownloader.h"
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

// ---------------------------------------------------------------------------
// The thumbnail cache.
//
// A thumbnail is a full 480x800 1-bit BMP read off the card and box-downscaled
// (the renderer's 1-bit blit cannot scale). cachedPage_ resets on every
// onEnter, so before this the app re-did that work on EVERY open and on every
// page turn -- harmless on an empty card, and the whole cost once 21 wallpapers
// are on it.
//
// Cached per wallpaper, not per set, so one changed or deleted file invalidates
// its own thumb and nothing else. The key is the source's SIZE plus a sample of
// its bytes plus the cell size: every device wallpaper is exactly 48062 bytes,
// so size alone cannot see a replaced file, and a cell-size change (the hint
// gap moved it) must invalidate everything. The sample is three 64-byte reads
// rather than a full hash, because hashing the whole file would cost what the
// decode costs and save nothing.
constexpr char kThumbDir[] = "/wallpapers/.thumbs";
constexpr uint32_t kThumbMagic = 0x31485457;  // "WTH1"

struct ThumbHeader {
  uint32_t magic;
  uint32_t sourceBytes;
  uint32_t sampleHash;
  int16_t cellW, cellH, w, h, ox, oy;
};

uint32_t sampleHashOf(HalFile& f, const uint64_t size) {
  uint32_t h = 2166136261u;
  const uint64_t offsets[3] = {62, size / 2, size > 128 ? size - 128 : 0};
  uint8_t buf[64];
  for (const uint64_t off : offsets) {
    if (!f.seekSet(static_cast<size_t>(off))) continue;
    const int got = f.read(buf, sizeof(buf));
    for (int i = 0; i < got; ++i) {
      h ^= buf[i];
      h *= 16777619u;
    }
  }
  return h;
}

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
  // Timed per phase because "the app takes ten seconds to open" is not a
  // diagnosis, and this fork's recurring failure is fixing the thing in front
  // of you rather than the thing that is slow. One line names the cost.
  const uint32_t tEnter = millis();
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  const uint32_t tFonts = millis();
  Storage.mkdir(wallpapers::kLibraryDir);
  sweepPartFiles();
  const uint32_t tSweep = millis();
  scanLibrary();
  const uint32_t tScan = millis();
  loadActive();
  const uint32_t tActive = millis();
  // The free-space probe is NOT here any more. HalStorage::freeBytes() walks the
  // FAT cluster chain (SDCardManager::refreshFreeClusters: "seconds on a large
  // card"), it is cached for 20s afterwards, and it was the only thing on this
  // path that could take seconds -- which is exactly the shape Mario saw: ten
  // seconds once, fast on every open inside the TTL, on an EMPTY card where no
  // thumbnail work exists at all. Ten seconds of blank screen before the offer
  // appears is the "reads as crashed" failure one layer earlier than the paint-
  // before-block work, so the screen is painted first and the walk happens
  // after, in loop(), with content already on the glass.
  warning_.clear();
  warningPending_ = true;
  LOG_INF("WALL", "onEnter %ums: fonts=%u sweep=%u scan=%u active=%u (free-space deferred)", tActive - tEnter,
          tFonts - tEnter, tSweep - tFonts, tScan - tSweep, tActive - tScan);
  // Open on the page holding the set wallpaper, so the border is on screen.
  cachedPage_ = -1;
  page_ = 0;
  // Grid or Offer, decided by what is on the card. An empty grid is never a
  // state this app shows.
  pickView();
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
  // A user's own wallpapers first, then the built-ins. NOT a plain sort any
  // more, so the built-in count below counts rather than binary_searching -- a
  // binary_search over this order would silently return wrong answers.
  std::sort(names_.begin(), names_.end(),
            [](const std::string& a, const std::string& b) { return wallpapers::sortsBefore(a, b); });

  int have = 0;
  for (const std::string& n : names_) {
    if (wallpapers::isBuiltInFile(n)) ++have;
  }
  builtInsMissing_ = static_cast<int>(wallpapers::builtInCount()) - have;
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

  // Through a .part name, renamed only when the whole file is written.
  //
  // Writing straight to /sleep.bmp truncates the user's CURRENT sleep image on
  // the first byte, so a card that fills (or a power cut) halfway leaves a
  // short file where a good one used to be. Bitmap::parseHeaders seeks to
  // bfOffBits and never checks that the pixel data is complete, and
  // SleepActivity::findNextValidSleepImage accepts a file on exactly that
  // check -- so a truncated 480x800 image is a VALID sleep image and gets
  // drawn, half-rendered, on every sleep, with nothing on screen to say why.
  // The rename is the only thing between a failed copy and a permanently
  // broken sleep screen.
  const std::string part = std::string(wallpapers::kPinnedSleep) + ".part";
  Storage.remove(part.c_str());

  HalFile in;
  if (!Storage.openFileForRead("WALL", src, in)) {
    LOG_ERR("WALL", "Cannot open wallpaper %s", src.c_str());
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("WALL", part, out)) {
    LOG_ERR("WALL", "Cannot open %s for write", part.c_str());
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kCopyChunk);
  if (!buffer) {
    LOG_ERR("WALL", "OOM: copy buffer");
    return false;
  }
  uint64_t copied = 0;
  for (;;) {
    const int got = in.read(buffer.get(), kCopyChunk);
    if (got < 0) {
      LOG_ERR("WALL", "Read error copying wallpaper");
      out.close();
      in.close();
      Storage.remove(part.c_str());
      return false;
    }
    if (got == 0) break;
    if (out.write(buffer.get(), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
      LOG_ERR("WALL", "Write error pinning wallpaper (card full?)");
      out.close();
      in.close();
      Storage.remove(part.c_str());
      return false;
    }
    copied += static_cast<uint64_t>(got);
  }
  out.close();
  in.close();

  // The size is knowable and exact, so check it rather than trusting that the
  // writes returned what they claimed.
  if (copied != wallpapers::kWallpaperFileBytes) {
    LOG_ERR("WALL", "Pinned wallpaper is %u bytes, expected %u -- not swapping it in", static_cast<unsigned>(copied),
            static_cast<unsigned>(wallpapers::kWallpaperFileBytes));
    Storage.remove(part.c_str());
    return false;
  }

  Storage.remove(wallpapers::kPinnedSleep);
  if (!Storage.rename(part.c_str(), wallpapers::kPinnedSleep)) {
    LOG_ERR("WALL", "Card refused the final rename of %s", wallpapers::kPinnedSleep);
    Storage.remove(part.c_str());
    return false;
  }

  // Taking a QUICK_RESUME user off that mode is the ONE thing this app does
  // that changes the whole device rather than its own screen, and it is the only
  // wallpaper code anywhere near the boot path -- nothing of ours executes at
  // wake at all.
  //
  // main.cpp::enterDeepSleep saves a retained frame ONLY when the sleep is a
  // quick-resume sleep, and the wake branch that restores it is also the branch
  // that draws the LoadingIcon. Flipping the mode to CUSTOM therefore cost two
  // things at once: the fast frame restore, and the only sign of life the user
  // gets while the device boots (wake is a chip reset). A decorative feature
  // must not buy itself a slower wake for the whole device.
  //
  // quickResumeSleepScreen is left ON so timeout sleeps -- the common case, and
  // the one Mario was waiting on -- keep the fast path and the icon. The
  // combination is supported: SettingsActivity::syncQuickResumeTimeoutForSleepScreen
  // preserves an explicitly-enabled timeout flag across a sleep-screen change.
  // The trade is that the wallpaper then shows on manual sleeps rather than on
  // timeout ones.
  if (SETTINGS.sleepScreen == CrossPointSettings::QUICK_RESUME) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
    LOG_INF("WALL", "was QUICK_RESUME; keeping quick wake on timeout sleeps so wake does not get slower");
  }
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

  const uint32_t tThumbs = millis();
  int decoded = 0;
  thumbs_.assign(static_cast<size_t>(geom.perPage), Thumb{});
  const int base = page_ * geom.perPage;
  // specialTiles(), not a literal 1: the decode and the draw must agree about
  // which wallpaper is in a cell, and when they did not, one tile drew its
  // neighbour's picture and the next drew a decode-failure cross.
  const int specials = specialTiles();
  for (int slot = 0; slot < geom.perPage; ++slot) {
    const int combined = base + slot;
    if (combined < specials) continue;  // the chrome tiles have no thumbnail
    const int idx = combined - specials;
    if (idx >= static_cast<int>(names_.size())) break;
    std::string path = std::string(wallpapers::kLibraryDir) + "/" + names_[static_cast<size_t>(idx)];
    thumbs_[static_cast<size_t>(slot)] =
        thumbFor(names_[static_cast<size_t>(idx)], path, geom.cellW, geom.cellH, &decoded);
  }
  LOG_INF("WALL", "thumbs page=%d decoded=%d (rest from cache) in %ums", page_, decoded, millis() - tThumbs);
  cachedPage_ = page_;
  cachedPerPage_ = geom.perPage;
}

// Cache hit or a decode plus a write. `decoded` counts only the real decodes so
// the log says which of the two happened.
WallpapersActivity::Thumb WallpapersActivity::thumbFor(const std::string& name, const std::string& path,
                                                       const int16_t cellW, const int16_t cellH, int* decoded) {
  uint64_t sourceBytes = 0;
  uint32_t sample = 0;
  {
    HalFile src;
    if (Storage.openFileForRead("WALL", path, src)) {
      sourceBytes = static_cast<uint64_t>(src.size());
      sample = sampleHashOf(src, sourceBytes);
      src.close();
    }
  }

  const std::string cachePath = std::string(kThumbDir) + "/" + name + ".thb";
  HalFile cf;
  if (Storage.openFileForRead("WALL", cachePath, cf)) {
    ThumbHeader head{};
    if (cf.read(&head, sizeof(head)) == static_cast<int>(sizeof(head)) && head.magic == kThumbMagic &&
        head.sourceBytes == sourceBytes && head.sampleHash == sample && head.cellW == cellW && head.cellH == cellH &&
        head.w > 0 && head.h > 0) {
      Thumb t;
      t.w = head.w;
      t.h = head.h;
      t.ox = head.ox;
      t.oy = head.oy;
      const size_t bytes = static_cast<size_t>((head.w + 7) / 8) * static_cast<size_t>(head.h);
      t.bits.resize(bytes);
      if (cf.read(t.bits.data(), bytes) == static_cast<int>(bytes)) {
        t.ok = true;
        cf.close();
        return t;
      }
    }
    cf.close();
  }

  if (decoded != nullptr) ++(*decoded);
  Thumb t = decodeThumb(path, cellW, cellH);
  if (!t.ok) return t;

  // Best effort: a cache that cannot be written must never break the picture.
  if (!Storage.exists(kThumbDir)) Storage.mkdir(kThumbDir);
  const std::string part = cachePath + ".part";
  HalFile out;
  if (Storage.openFileForWrite("WALL", part, out)) {
    ThumbHeader head{};
    head.magic = kThumbMagic;
    head.sourceBytes = static_cast<uint32_t>(sourceBytes);
    head.sampleHash = sample;
    head.cellW = cellW;
    head.cellH = cellH;
    head.w = t.w;
    head.h = t.h;
    head.ox = t.ox;
    head.oy = t.oy;
    const bool wrote =
        out.write(&head, sizeof(head)) == sizeof(head) && out.write(t.bits.data(), t.bits.size()) == t.bits.size();
    out.close();
    if (wrote) {
      Storage.remove(cachePath.c_str());
      if (!Storage.rename(part.c_str(), cachePath.c_str())) Storage.remove(part.c_str());
    } else {
      Storage.remove(part.c_str());
    }
  }
  return t;
}

// Build every thumbnail now, while the user is already watching a progress bar
// and expects to wait. Without this the cost merely MOVES to the first open,
// which is the screen that has to feel instant. Cancelling is honoured: a
// half-warmed cache is not wrong, only less warm, and the rest fills in lazily.
void WallpapersActivity::prewarmThumbs() {
  const wallpapersui::GridGeom geom = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext());
  fetchPhase_ = 2;
  fetchDone_ = 0;
  fetchTotal_ = static_cast<int>(names_.size());
  const uint32_t t0 = millis();
  int built = 0;
  for (size_t i = 0; i < names_.size(); ++i) {
    if (fetchCancel_) break;
    const std::string path = std::string(wallpapers::kLibraryDir) + "/" + names_[i];
    thumbFor(names_[i], path, geom.cellW, geom.cellH, &built);
    fetchDone_ = static_cast<int>(i) + 1;
    if ((i % 5) == 0 || i + 1 == names_.size()) {
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) fetchCancel_ = true;
      requestUpdateAndWait();
    }
  }
  LOG_INF("WALL", "prewarmed %d thumbnails in %ums", built, millis() - t0);
  // The grid's own cache is per-page and still cold; let it re-read from disk.
  cachedPage_ = -1;
}

void WallpapersActivity::drawGrid(const wallpapersui::GridGeom& geom) {
  const int base = page_ * geom.perPage;
  const int specials = specialTiles();
  const int total = specials + static_cast<int>(names_.size());

  for (int slot = 0; slot < geom.perPage; ++slot) {
    const int combined = base + slot;
    if (combined >= total) break;
    const fui::Rect th = wallpapersui::thumbRect(geom, slot);
    if (combined == 0) {
      drawAddTile(geom, th);  // cell 0 is always + Add a wallpaper
      continue;
    }
    // PARTIAL: the user has their own wallpapers but not the built-in set, so
    // the offer stays on screen as a tile rather than vanishing because one
    // wallpaper exists. It retires itself when the set is complete.
    //
    // Cell 1, never cell 0: moving + Add would put a different action under a
    // pixel people have already learned (same-pixel-different-action).
    if (specials > 1 && combined == 1) {
      drawGetSetTile(geom, th);
      continue;
    }
    const int idx = combined - specials;
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

// How many chrome tiles sit in front of the wallpapers. One (+ Add) always,
// two while the built-in set is incomplete. Read by BOTH the drawing and the
// hit-test, because a grid whose two halves disagree about what is in a cell
// opens the wrong thing -- the bug this fork has caught more often than any
// other.
int WallpapersActivity::specialTiles() const { return builtInsMissing_ > 0 ? 2 : 1; }

void WallpapersActivity::drawGetSetTile(const wallpapersui::GridGeom& geom, const fui::Rect& th) const {
  renderer.drawRect(th.x, th.y, th.width, th.height, 3, true);
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  fui::TextStyle style = toybox::themeTokens().smallText;
  style.font = fui::FONT_SLOT_SMALL;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::Black;
  style.maxLines = 2;

  char label[48];
  // One line of text, wrapped by width. An embedded newline is not a break
  // this renderer honours: it vanished and joined the words into "GET THE21".
  std::snprintf(label, sizeof(label), "GET THE %d BUILT-INS", builtInsMissing_);
  const fui::Rect box =
      fui::makeRect(th.x + 6, static_cast<int16_t>(th.y + th.height / 2 - 30), static_cast<int16_t>(th.width - 12), 60);
  target.text(box, label, style);

  const fui::Rect cap = wallpapersui::captionRect(geom, 1);
  fui::TextStyle capStyle = style;
  capStyle.maxLines = 1;
  target.text(cap, "Tap to fetch", capStyle);
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

// The whole set as one asset. Twenty-one separate downloads would be 42 TLS
// handshakes, because GitHub redirects release assets to a CDN host and
// HttpDownloader opens a fresh connection per hop with no session reuse: about
// a minute of dead time against roughly ten seconds for one file. Per-file
// "resume" would not have paid for it either, since downloadToFile has no Range
// support and deletes its destination before the first byte arrives.
//
// The pack needs no format: every wallpaper is exactly kWallpaperFileBytes, so
// it is a bare concatenation, image i lives at i * kWallpaperFileBytes, and the
// count is the file size divided by it. Built by tools_local/wallpapers/build_pack.py
// in the same order as the built-in table, which host-tests/wallpack asserts.
constexpr const char* kPackUrl = "https://github.com/ma-r-s/crossplay/releases/download/wallpapers/wallpapers.dat";
constexpr const char* kPackPart = "/wallpapers.dat.part";

// The HAL exposes a size only through an open file, and the difference between
// "absent" and "there but the wrong length" is the whole resume rule, so it is
// worth the open. A short file is a torn write, and Bitmap would accept it.

uint64_t fileSizeOf(const std::string& path) {
  HalFile f;
  if (!Storage.openFileForRead("WALL", path, f)) return 0;
  const uint64_t n = static_cast<uint64_t>(f.size());
  f.close();
  return n;
}

// A power cut mid-unpack leaves <name>.bmp.part behind. They are unambiguously
// ours and unambiguously incomplete, so they go on entry. Nothing else is
// touched: a .bmp of an unexpected length might be a wallpaper the user made
// elsewhere, and deleting a user's file to tidy up is not this app's call.
void WallpapersActivity::sweepPartFiles() {
  auto dir = Storage.open(wallpapers::kLibraryDir);
  if (!dir) return;
  std::vector<std::string> stale;
  for (;;) {
    auto entry = dir.openNextFile();
    if (!entry) break;
    char nameBuf[kNameMax] = {};
    entry.getName(nameBuf, sizeof(nameBuf));
    entry.close();
    const std::string name(nameBuf);
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0) stale.push_back(name);
  }
  dir.close();
  for (const std::string& name : stale) {
    const std::string path = std::string(wallpapers::kLibraryDir) + "/" + name;
    Storage.remove(path.c_str());
    LOG_INF("WALL", "Swept incomplete %s", name.c_str());
  }
}

void WallpapersActivity::showNotice(const char* headline, const char* body, const char* actionLabel,
                                    const fui::ActionId action) {
  noticeHead_ = headline;
  noticeBody_ = body;
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

int WallpapersActivity::builtInsPresent() const {
  return static_cast<int>(wallpapers::builtInCount()) - builtInsMissing_;
}

// The screen is a function of the card, with no remembered "already offered"
// flag to go stale or to make two identical cards show different things.
void WallpapersActivity::pickView() { view_ = names_.empty() ? View::Offer : View::Grid; }

void WallpapersActivity::startSetDownload() {
  // The radio first: entering the TLS stack with WiFi never started fails in a
  // way whose message says nothing about WiFi.
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void WallpapersActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    showNotice("NO WIFI", "The wallpapers need WiFi to download. The card is unchanged.", "TRY AGAIN",
               wallpapersui::ActionRetry);
    return;
  }
  // Queued, not run here: this is the result handler of an activity that is
  // still unwinding, and the fetch blocks.
  fetchQueued_ = true;
}

void WallpapersActivity::runSetDownload() {
  // exists() first: mkdir returns false for a directory that is already there,
  // and treating that as failure means every attempt after the first reports a
  // full card. Trivia shipped exactly that bug.
  if (!Storage.exists(wallpapers::kLibraryDir) && !Storage.mkdir(wallpapers::kLibraryDir)) {
    showNotice("NO ROOM", "Could not create the wallpapers folder on the card. Is the card in, and writable?",
               "TRY AGAIN", wallpapersui::ActionRetry);
    return;
  }

  // Sized for the WHOLE set plus the floor that protects other apps, not for one
  // file: a check that passes for one wallpaper and fills the card at number
  // nine costs Study its review log, silently, later.
  uint64_t freeNow = 0;
  const bool queryOk = Storage.freeBytes(freeNow);
  switch (wallpapers::roomFor(queryOk, freeNow, wallpapers::kPackFloorBytes)) {
    case wallpapers::Room::Unknown:
      // NOT the same screen as NO ROOM: freeBytes() returns false for "could not
      // answer", never for "full", and saying the card is full when we do not
      // know that is the conflation the call exists to prevent.
      showNotice("CAN'T TELL",
                 "The card did not answer when asked how much room is left, so nothing was written. "
                 "Trying again usually works.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
      return;
    case wallpapers::Room::TooFull: {
      char body[192];
      std::snprintf(body, sizeof(body),
                    "The wallpapers need about %u MB free and the card has %u MB. "
                    "Delete something from the card, then try again. Nothing was written.",
                    static_cast<unsigned>(wallpapers::kPackFloorBytes >> 20), static_cast<unsigned>(freeNow >> 20));
      showNotice("NO ROOM", body, "TRY AGAIN", wallpapersui::ActionRetry);
      return;
    }
    case wallpapers::Room::Ok:
      break;
  }

  fetchCancel_ = false;
  fetchDone_ = 0;
  fetchPhase_ = 0;
  fetchTotal_ = static_cast<int>(wallpapers::kBuiltInCount);
  view_ = View::Fetching;
  interactionsReady_ = false;
  // requestUpdateAndWait, not requestUpdate: a plain request is DEFERRED and
  // never reaches the render task while a blocking call sits in the same call
  // stack, so the app would freeze on the previous screen for the whole
  // transfer. This is the #306 family; PR #123 is the reference.
  requestUpdateAndWait();

  size_t lastPainted = 0;
  const auto progress = [this, &lastPainted](const size_t got, const size_t total) {
    // Every ~200KB: five honest steps across a ~1MB pack. Each paint is an
    // e-ink refresh, so finer steps would spend longer refreshing than fetching.
    if (got - lastPainted >= 200u * 1024u || (total > 0 && got == total)) {
      lastPainted = got;
      const uint64_t per = wallpapers::kWallpaperFileBytes;
      fetchDone_ = static_cast<int>(got / (per > 0 ? per : 1));
      if (fetchDone_ > fetchTotal_) fetchDone_ = fetchTotal_;
      requestUpdateAndWait();
    }
    // The sanctioned exception to the one-pump rule: nothing else pumps while
    // this blocks, and without it Back could not stop a download at all.
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) fetchCancel_ = true;
    if (mappedInput.wasHomeGesture()) fetchCancel_ = true;
    if (fetchCancel_) {
      fetchCancel_ = true;
      requestUpdateAndWait();
    }
  };

  const auto err = HttpDownloader::downloadToFile(kPackUrl, kPackPart, progress, &fetchCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(kPackPart);
    // Every one of these offers TRY AGAIN. A screen that reports a failure and
    // gives nothing to press is a dead end whose only exit is unmarked.
    if (err == HttpDownloader::ABORTED) {
      showNotice("STOPPED", "Download stopped. Nothing was kept, and the card is unchanged.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
    } else if (err == HttpDownloader::FILE_ERROR) {
      showNotice("CARD TROUBLE", "The card would not take the file. Nothing was kept.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
    } else if (HttpDownloader::lastStatus() == 404) {
      // A 404 is not a network problem, and saying "did not answer" for one
      // sends people to check their WiFi for a fault that is ours: the asset is
      // not published. lastStatus() exists precisely because "failed to fetch"
      // reads the same for a dead server and for a server that answered.
      showNotice("NOT THERE YET",
                 "The wallpapers are not on the server yet. That is a problem at our end, not with your "
                 "WiFi or your card. Nothing was written.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
    } else {
      showNotice("NO ANSWER", "The download did not answer. The card is unchanged.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
    }
    return;
  }

  if (!unpackSet()) return;

  Storage.remove(kPackPart);
  scanLibrary();
  prewarmThumbs();
  loadActive();
  computeWarning();
  page_ = 0;
  cachedPage_ = -1;
  pickView();
  interactionsReady_ = false;
  requestUpdate();
}

// Pack -> one .bmp per wallpaper. Resumable for free: an image already on the
// card at exactly the right size is skipped, so a torn unpack costs seconds of
// SD work on retry rather than another download.
bool WallpapersActivity::unpackSet() {
  // Second phase, same bar, its second third.
  fetchPhase_ = 1;
  fetchDone_ = 0;
  HalFile pack;
  if (!Storage.openFileForRead("WALL", kPackPart, pack)) {
    showNotice("CARD TROUBLE", "The download arrived but could not be read back.", "TRY AGAIN",
               wallpapersui::ActionRetry);
    return false;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(kCopyChunk);
  if (!buffer) {
    showNotice("OUT OF MEMORY", "Not enough memory to unpack the wallpapers.", "TRY AGAIN", wallpapersui::ActionRetry);
    return false;
  }

  const size_t count = wallpapers::builtInCount();
  for (size_t i = 0; i < count; ++i) {
    if (fetchCancel_) {
      pack.close();
      showNotice("STOPPED", "Stopped. The wallpapers that already arrived are on the card.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
      return false;
    }

    const std::string target = std::string(wallpapers::kLibraryDir) + "/" + wallpapers::builtInStem(i) + ".bmp";
    if (fileSizeOf(target) == wallpapers::kWallpaperFileBytes) {
      // Already here and the right length: skip the bytes and move on.
      pack.seekCur(static_cast<size_t>(wallpapers::kWallpaperFileBytes));
      fetchDone_ = static_cast<int>(i) + 1;
      continue;
    }

    const std::string part = target + ".part";
    HalFile out;
    if (!Storage.openFileForWrite("WALL", part, out)) {
      pack.close();
      showNotice("CARD TROUBLE", "The card would not take a wallpaper. The ones already written are kept.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
      return false;
    }

    uint64_t left = wallpapers::kWallpaperFileBytes;
    bool ok = true;
    while (left > 0) {
      const size_t want = left < kCopyChunk ? static_cast<size_t>(left) : kCopyChunk;
      const int got = pack.read(buffer.get(), want);
      if (got <= 0 || out.write(buffer.get(), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
        ok = false;
        break;
      }
      left -= static_cast<uint64_t>(got);
    }
    out.close();

    if (!ok || left != 0) {
      Storage.remove(part.c_str());
      pack.close();
      showNotice("CARD TROUBLE", "A wallpaper did not write completely. The ones already written are kept.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
      return false;
    }

    Storage.remove(target.c_str());
    if (!Storage.rename(part.c_str(), target.c_str())) {
      Storage.remove(part.c_str());
      pack.close();
      showNotice("CARD TROUBLE", "The card refused to name a wallpaper. The ones already written are kept.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
      return false;
    }

    fetchDone_ = static_cast<int>(i) + 1;
    // Every few files, not every file: 21 e-ink refreshes would take longer
    // than the unpack they are reporting on.
    if ((i % 5) == 0 || i + 1 == count) {
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) fetchCancel_ = true;
      requestUpdateAndWait();
    }
  }

  pack.close();
  return true;
}

void WallpapersActivity::loop() {
  // The deferred free-space walk, once the panel already shows something.
  // Guarded on painted_ rather than run straight from onEnter: rendering is
  // notification-driven, so a blocking call in the same breath as
  // requestUpdate() would run BEFORE the render task ever painted and the
  // deferral would buy nothing (the #306 shape).
  if (warningPending_ && painted_) {
    warningPending_ = false;
    const uint32_t t0 = millis();
    computeWarning();
    LOG_INF("WALL", "free-space probe took %ums", millis() - t0);
    if (!warning_.empty()) requestUpdate();
    return;
  }

  // Started here rather than in the action handler: the fetch blocks for a
  // while and pumps input itself, which must not happen while a tap is still
  // being routed (the Trivia precedent, and the whole of the #306 family).
  if (fetchQueued_) {
    fetchQueued_ = false;
    runSetDownload();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Help || view_ == View::Notice) {
      pickView();
      requestUpdate();
      return;
    }
    shelf::leave(renderer, mappedInput);
    return;
  }
  // The Offer and Notice screens carry real buttons, so their taps go through
  // Interactions rather than the grid's geometry hit-test.
  if (view_ == View::Offer || view_ == View::Notice) {
    int ax = 0;
    int ay = 0;
    if (!mappedInput.wasScreenTapped(ax, ay) || !interactionsReady_) return;
    fui::InputSnapshot input{};
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(ax);
    input.touchY = static_cast<int16_t>(ay);
    const fui::ActionEvent action = interactions_.route(input);
    switch (action.action) {
      case wallpapersui::ActionGetSet:
      case wallpapersui::ActionRetry:
        startSetDownload();
        return;
      case wallpapersui::ActionAddOwn:
        view_ = View::Help;
        requestUpdate();
        return;
      case wallpapersui::ActionDismiss:
        pickView();
        requestUpdate();
        return;
      default:
        return;
    }
  }
  if (view_ != View::Grid) return;

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
  const int specials = specialTiles();
  const int total = specials + static_cast<int>(names_.size());
  if (combined >= total) return;
  if (combined == 0) {
    view_ = View::Help;
    requestUpdate();
    return;
  }
  if (specials > 1 && combined == 1) {
    startSetDownload();
    return;
  }
  const int idx = combined - specials;
  if (idx == activeIndex_) return;  // already the sleep screen
  if (setWallpaper(idx)) requestUpdate();
}

void WallpapersActivity::render(RenderLock&&) {
  const uint32_t tPaint = millis();
  clampPage();
  renderer.clearScreen();
  // Faces per view, not per app. The grid is a menu and wants the Jersey cut it
  // shares with the shelf; the offer, the progress and the notices are
  // SENTENCES, and at the 20px UI cut a sentence runs off the panel and is cut
  // with an ellipsis. Trivia carries the same split for the same reason.
  const bool prose = view_ == View::Offer || view_ == View::Fetching || view_ == View::Notice || view_ == View::Help;
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, prose ? toybox::readingChromeFaces() : toybox::proseMenuFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen surface(frame);

  if (view_ == View::Help) {
    wallpapersui::buildHelp(surface);
  } else if (view_ == View::Fetching) {
    wallpapersui::FetchingModel model;
    model.done = fetchDone_;
    model.total = fetchTotal_;
    model.cancelling = fetchCancel_;
    model.phase = fetchPhase_;
    wallpapersui::buildFetching(surface, model);
  } else if (view_ == View::Notice) {
    wallpapersui::NoticeModel model;
    model.headline = noticeHead_.c_str();
    model.body = noticeBody_.c_str();
    model.actionLabel = noticeAction_;
    model.action = noticeActionId_;
    wallpapersui::buildNotice(surface, model);
  } else if (view_ == View::Offer) {
    // BEFORE: the set is not here. Never an empty grid -- a screen showing
    // nothing reads as a crash, confirmed twice by cold testers.
    wallpapersui::OfferModel model;
    model.count = static_cast<int>(wallpapers::kBuiltInCount);
    model.bytes = wallpapers::builtInPackBytes();
    model.alreadyHave = builtInsPresent();
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    wallpapersui::buildOffer(surface, model);
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
  // The offer screen draws its truchet band live, so the paint cost is worth a
  // number rather than an assumption about a 240MHz part.
  LOG_INF("WALL", "render view=%d took %ums", static_cast<int>(view_), millis() - tPaint);
  renderer.displayBuffer();
  painted_ = true;
}

uint32_t WallpapersActivity::surfaceMeaning() const {
  uint32_t m = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(page_));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(activeIndex_ + 1));
  m = paintclock::mixMeaning(m, static_cast<uint32_t>(view_));
  return paintclock::mixMeaning(m, static_cast<uint32_t>(names_.size()));
}
