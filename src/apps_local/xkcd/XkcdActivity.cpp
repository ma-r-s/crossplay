#include "XkcdActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kDir = "/xkcd";
constexpr const char* kIndexPath = "/xkcd/index.dat";
constexpr const char* kImagePath = "/xkcd/images.dat";
constexpr const char* kTextPath = "/xkcd/text.dat";
constexpr const char* kReadPath = "/xkcd/read.bin";

// Scratch for the update, deleted on the way out.
constexpr const char* kTmpPng = "/xkcd/.tmp.png";
constexpr const char* kTmpBmp = "/xkcd/.tmp.bmp";

// The comic is blitted a band at a time through a stack buffer rather than
// assembled whole: the tallest comic in the archive is 6370 rows, which at
// 1bpp is 74KB, and taking a heap block that size once per comic is exactly
// the churn that fragments a device with no room to spare. Sixteen rows of the
// widest possible comic is 1600 bytes -- too big for the stack under the
// 256-byte rule, so it lives in the .bss as a single fixed pool.
constexpr int kBandRows = 16;
constexpr int kMaxStride = (xkcd::kMaxComicWidth + 7) / 8;
uint8_t gBand[kBandRows * kMaxStride];

// The gap flags for one step. gapWindowFor never asks for more than
// 2*tolerance + pad + gutter + 1 rows, which at a 480px viewport is 203.
constexpr int kMaxGapRows = 256;
uint8_t gGapFlags[kMaxGapRows];

// A ByteSource over an open HalFile. Every read seeks first, because the three
// files are read out of order (an index binary search interleaves with row
// reads) and a shared position would make each one depend on the last.
class FileSource final : public xkcd::ByteSource {
 public:
  explicit FileSource(HalFile& file) : file_(file), size_(static_cast<uint32_t>(file.size())) {}

  bool read(uint32_t offset, void* dst, uint32_t length) override {
    if (length == 0) return true;
    if (offset > size_ || size_ - offset < length) return false;
    if (!file_.seek(offset)) return false;
    const int got = file_.read(dst, length);
    return got == static_cast<int>(length);
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile& file_;
  uint32_t size_;
};

void upper(const char* in, char* out, size_t cap) {
  size_t n = 0;
  for (const char* c = in; *c != '\0' && n + 1 < cap; ++c, ++n) {
    out[n] = (*c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 'a' + 'A') : *c;
  }
  out[n] = '\0';
}

}  // namespace

std::unique_ptr<Activity> XkcdActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<XkcdActivity>(renderer, mappedInput);
}

XkcdActivity::~XkcdActivity() = default;

// --- Lifecycle -----------------------------------------------------------

void XkcdActivity::onEnter() {
  Activity::onEnter();
  // Landscape, and this is the whole reason the app looks different from the
  // others. See XkcdCore.h: the widest comic ever published is 780px against
  // an 800px landscape panel, so this is the one orientation where nothing is
  // ever scaled down and nothing ever pans sideways.
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  toybox::ensureFonts(renderer);

  archiveOpen_ = openArchive();
  if (archiveOpen_) {
    loadReadState();
    // The headline offers the newest comic on the card.
    xkcd::Comic newest{};
    if (archive_.at(*indexSrc_, archive_.count() - 1, newest)) {
      xkcd::readTitle(*textSrc_, newest, title_, sizeof(title_));
    }
  }
  view_ = View::Menu;
  requestUpdate();
}

void XkcdActivity::onExit() {
  saveReadState();
  closeArchive();

  // The radio has to come down before the activity does, the same way the
  // Hacker News app does it; skipping silentRestart leaves the next app on a
  // warm radio.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Storage.remove(kTmpPng);
  Storage.remove(kTmpBmp);

  // Put the screen back the way it was found. The orientation is global, so
  // leaving it turned would rotate the Apps menu on the way out.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Activity::onExit();
}

// --- The pack ------------------------------------------------------------

bool XkcdActivity::openArchive() {
  indexFile_ = makeUniqueNoThrow<HalFile>();
  imageFile_ = makeUniqueNoThrow<HalFile>();
  textFile_ = makeUniqueNoThrow<HalFile>();
  if (!indexFile_ || !imageFile_ || !textFile_) {
    LOG_ERR("XKCD", "OOM opening the pack");
    return false;
  }

  if (!Storage.openFileForRead("XKCD", kIndexPath, *indexFile_) ||
      !Storage.openFileForRead("XKCD", kImagePath, *imageFile_) ||
      !Storage.openFileForRead("XKCD", kTextPath, *textFile_)) {
    LOG_INF("XKCD", "no pack on the card at %s", kDir);
    return false;
  }

  indexSrc_ = makeUniqueNoThrow<FileSource>(*indexFile_);
  imageSrc_ = makeUniqueNoThrow<FileSource>(*imageFile_);
  textSrc_ = makeUniqueNoThrow<FileSource>(*textFile_);
  if (!indexSrc_ || !imageSrc_ || !textSrc_) {
    LOG_ERR("XKCD", "OOM wrapping the pack");
    return false;
  }

  if (!archive_.open(*indexSrc_)) {
    LOG_ERR("XKCD", "%s is not a pack this build can read", kIndexPath);
    return false;
  }
  LOG_INF("XKCD", "pack open: %d comics, newest #%u", archive_.count(), archive_.maxNum());
  return true;
}

void XkcdActivity::closeArchive() {
  indexSrc_.reset();
  imageSrc_.reset();
  textSrc_.reset();
  // HalFile members outlive any single scope, so they are closed at the
  // intended release point rather than left to the destructor.
  if (indexFile_) indexFile_->close();
  if (imageFile_) imageFile_->close();
  if (textFile_) textFile_->close();
  indexFile_.reset();
  imageFile_.reset();
  textFile_.reset();
  archiveOpen_ = false;
}

bool XkcdActivity::loadComic(const int position) {
  if (!archiveOpen_) return false;
  xkcd::Comic c{};
  if (!archive_.at(*indexSrc_, position, c)) return false;
  comic_ = c;
  position_ = position;
  scrollY_ = 0;
  xkcd::readTitle(*textSrc_, comic_, title_, sizeof(title_));
  xkcd::readAlt(*textSrc_, comic_, alt_, sizeof(alt_));
  markRead(comic_.num);
  return true;
}

// --- Read state ----------------------------------------------------------

bool XkcdActivity::isRead(const uint16_t num) const {
  const int byte = num / 8;
  if (byte < 0 || byte >= kReadBitsBytes) return false;
  return (readBits_[byte] >> (num % 8)) & 1;
}

void XkcdActivity::markRead(const uint16_t num) {
  const int byte = num / 8;
  if (byte < 0 || byte >= kReadBitsBytes) return;
  const uint8_t bit = static_cast<uint8_t>(1 << (num % 8));
  if (readBits_[byte] & bit) return;
  readBits_[byte] |= bit;
  ++readCount_;
  readDirty_ = true;
}

void XkcdActivity::loadReadState() {
  HalFile f;
  if (!Storage.openFileForRead("XKCD", kReadPath, f)) return;
  f.read(readBits_, sizeof(readBits_));
  readCount_ = 0;
  for (int i = 0; i < kReadBitsBytes; ++i) {
    for (int b = 0; b < 8; ++b) {
      if ((readBits_[i] >> b) & 1) ++readCount_;
    }
  }
  readDirty_ = false;
}

void XkcdActivity::saveReadState() {
  // Guarded on a change, per the SPIFFS/SD write-throttling rule: this is
  // called on every exit, including the one the sleep timer triggers when the
  // reader has done nothing at all.
  if (!readDirty_) return;
  HalFile f;
  if (!Storage.openFileForWrite("XKCD", kReadPath, f)) {
    LOG_ERR("XKCD", "could not write %s", kReadPath);
    return;
  }
  f.write(readBits_, sizeof(readBits_));
  f.close();
  readDirty_ = false;
}

// --- Navigation ----------------------------------------------------------

void XkcdActivity::openList(const int firstPosition) {
  listFirst_ = firstPosition;
  if (listFirst_ > archive_.count() - kPageRows) listFirst_ = archive_.count() - kPageRows;
  if (listFirst_ < 0) listFirst_ = 0;
  listSelected_ = 0;
  fillListRows();
  view_ = View::List;
}

void XkcdActivity::fillListRows() {
  rowCount_ = 0;
  if (!archiveOpen_) return;

  // Newest first, which is the order anyone browsing a comic archive wants.
  for (int i = 0; i < kPageRows; ++i) {
    const int pos = archive_.count() - 1 - (listFirst_ + i);
    if (pos < 0) break;
    xkcd::Comic c{};
    if (!archive_.at(*indexSrc_, pos, c)) break;

    char raw[kRowTextCap];
    xkcd::readTitle(*textSrc_, c, raw, sizeof(raw));
    char shout[kRowTextCap];
    upper(raw, shout, sizeof(shout));
    snprintf(rowLabels_[rowCount_], kRowTextCap, "%u  %s", static_cast<unsigned>(c.num), shout);

    // The read mark goes in the value slot, not a subtitle: a row's title band
    // is one line tall the moment it has a subtitle, so a wrapping label would
    // be drawn straight through it.
    snprintf(rowValues_[rowCount_], sizeof(rowValues_[0]), "%s", isRead(c.num) ? "READ" : "");

    rowItems_[rowCount_] = fui::ListItem{};
    rowItems_[rowCount_].label = rowLabels_[rowCount_];
    rowItems_[rowCount_].value = rowValues_[rowCount_];
    rowItems_[rowCount_].actionValue = static_cast<int16_t>(pos);
    ++rowCount_;
  }

  snprintf(listRight_, sizeof(listRight_), "%d OF %d", listFirst_ + 1, archive_.count());
}

void XkcdActivity::openComicAt(const int position) {
  if (!loadComic(position)) {
    showNotice("MISSING", "That comic is not in the pack on this card.");
    return;
  }
  view_ = View::Reader;
}

void XkcdActivity::showNotice(const char* headline, const char* detail, const char* actionLabel,
                              const fui::ActionId action) {
  snprintf(noticeHead_, sizeof(noticeHead_), "%s", headline);
  snprintf(noticeBody_, sizeof(noticeBody_), "%s", detail);
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
}

// --- Panning -------------------------------------------------------------

void XkcdActivity::pan(const bool down) {
  const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
  const int viewportH = view.height;

  int firstRow = 0;
  int rowCount = 0;
  xkcd::gapWindowFor(comic_, viewportH, scrollY_, down, firstRow, rowCount);

  xkcd::GapWindow window;
  if (rowCount > 0 && rowCount <= kMaxGapRows && comic_.stride <= kMaxStride) {
    // Stream the rows past rowIsGap a band at a time and keep only the flags.
    // Holding the window as pixels would be ~19KB on the heap for every tap;
    // as flags it is one byte a row.
    bool ok = true;
    int done = 0;
    while (done < rowCount && ok) {
      int rows = kBandRows;
      if (rows > rowCount - done) rows = rowCount - done;
      const uint32_t at = comic_.imageOffset + static_cast<uint32_t>(firstRow + done) * comic_.stride;
      ok = imageSrc_->read(at, gBand, static_cast<uint32_t>(rows) * comic_.stride);
      if (!ok) break;
      for (int r = 0; r < rows; ++r) {
        gGapFlags[done + r] = xkcd::rowIsGap(gBand + r * comic_.stride, comic_.width) ? 1 : 0;
      }
      done += rows;
    }
    if (ok) {
      window.isGap = gGapFlags;
      window.firstRow = firstRow;
      window.rowCount = rowCount;
    } else {
      // A card that hiccups gets a plain half-screen rather than a refusal to
      // move. A null window is a supported input for exactly this reason.
      LOG_ERR("XKCD", "could not read the gap window for #%u", static_cast<unsigned>(comic_.num));
    }
  }

  scrollY_ = down ? xkcd::scrollDown(comic_, viewportH, scrollY_, window)
                  : xkcd::scrollUp(comic_, viewportH, scrollY_, window);
}

// --- Drawing -------------------------------------------------------------

void XkcdActivity::drawComic() {
  if (!archiveOpen_ || !comic_.valid() || comic_.stride > kMaxStride) return;

  fui::GfxRendererTarget probe(renderer);
  const fui::Rect view = xkcdui::readerViewport(probe.deviceContext());
  const xkcd::Placement p = xkcd::place(comic_, view.width, view.height, scrollY_);

  // A band at a time: one seek per band rather than one per row, and a fixed
  // cost whatever the comic.
  int drawn = 0;
  while (drawn < p.visibleH) {
    int rows = kBandRows;
    if (rows > p.visibleH - drawn) rows = p.visibleH - drawn;
    const uint32_t at = comic_.imageOffset + static_cast<uint32_t>(p.scrollY + drawn) * comic_.stride;
    if (!imageSrc_->read(at, gBand, static_cast<uint32_t>(rows) * comic_.stride)) {
      LOG_ERR("XKCD", "artwork read failed for #%u at row %d", static_cast<unsigned>(comic_.num), p.scrollY + drawn);
      return;
    }
    for (int r = 0; r < rows; ++r) {
      const uint8_t* line = gBand + r * comic_.stride;
      const int y = view.y + p.originY + drawn + r;
      for (int x = 0; x < comic_.width; ++x) {
        // A set bit is ink, which is toybox::blit1bpp's convention and the
        // pack's. Drawn pixel by pixel because the renderer has no 1bpp blit
        // that takes a stride, and drawIcon bakes in a portrait rotation that
        // would turn the comic on its side.
        if ((line[x >> 3] >> (7 - (x & 7))) & 1) renderer.drawPixel(p.originX + x, y, true);
      }
    }
    drawn += rows;
  }
}

void XkcdActivity::drawMosaic(const fui::Rect& band) {
  if (!archiveOpen_ || band.width <= 0 || band.height <= 0) return;

  // Ornament made of the app's own material carrying the app's own data, per
  // docs/design-language.md. The material is the thing this app is actually
  // about: every comic is a different shape, which is the whole reason it was
  // hard to put on a screen at all. So each one is drawn as a rectangle at its
  // own aspect ratio, filled if you have read it and outlined if you have not.
  //
  // A screenshot of it is different on everyone's device, which is the test.
  constexpr int kCellH = 26;
  constexpr int kGap = 4;
  const int rows = (band.height + kGap) / (kCellH + kGap);
  if (rows <= 0) return;

  int x = band.x;
  int y = band.y;
  int placed = 0;

  // The most recent comics, which are the ones a reader is most likely to have
  // an opinion about having read.
  for (int i = archive_.count() - 1; i >= 0 && placed < 240; --i) {
    xkcd::Comic c{};
    if (!archive_.at(*indexSrc_, i, c)) continue;

    int w = c.height > 0 ? (kCellH * c.width) / c.height : kCellH;
    if (w < 3) w = 3;
    if (w > band.width) w = band.width;

    if (x + w > band.x + band.width) {
      x = band.x;
      y += kCellH + kGap;
      if (y + kCellH > band.y + band.height) break;
    }

    if (isRead(c.num)) {
      renderer.fillRect(x, y, w, kCellH, true);
    } else {
      renderer.drawRect(x, y, w, kCellH, true);
    }
    x += w + kGap;
    ++placed;
  }
}

// --- Input ---------------------------------------------------------------

void XkcdActivity::loop() {
  if (updateQueued_) {
    updateQueued_ = false;
    runUpdate();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Back has two rules and no exceptions: an app returns to its folder, and
    // a screen inside the app returns to the one before it.
    switch (view_) {
      case View::Reader:
      case View::List:
      case View::Notice:
        view_ = View::Menu;
        requestUpdate();
        return;
      case View::Alt:
        view_ = View::Reader;
        requestUpdate();
        return;
      default:
        shelf::leave(renderer, mappedInput);
        return;
    }
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);
  handleAction(event.action, event.value);
}

void XkcdActivity::handleAction(const fui::ActionId action, const int16_t value) {
  switch (action) {
    case xkcdui::ActionOpenLatest:
      openComicAt(archive_.count() - 1);
      requestUpdate();
      break;
    case xkcdui::ActionBrowse:
      openList(0);
      requestUpdate();
      break;
    case xkcdui::ActionRandom: {
      // millis() is the only entropy on this device that differs between two
      // boots to the same menu. It is a seed, not a key.
      const int n = archive_.count();
      if (n > 0) openComicAt(static_cast<int>(millis() % static_cast<uint32_t>(n)));
      requestUpdate();
      break;
    }
    case xkcdui::ActionSearch:
      // Deliberately not built yet; saying so is better than a dead button.
      showNotice("SEARCH", "Not built yet. Browse steps through the archive newest first.");
      requestUpdate();
      break;
    case xkcdui::ActionUpdate:
      beginUpdate();
      break;
    case xkcdui::ActionOpenComic:
      openComicAt(value);
      requestUpdate();
      break;
    case xkcdui::ActionPageOlder:
      openList(listFirst_ + kPageRows);
      requestUpdate();
      break;
    case xkcdui::ActionPageNewer:
      openList(listFirst_ - kPageRows);
      requestUpdate();
      break;
    case xkcdui::ActionPanUp:
      pan(false);
      requestUpdate();
      break;
    case xkcdui::ActionPanDown:
      pan(true);
      requestUpdate();
      break;
    case xkcdui::ActionShowAlt:
      view_ = View::Alt;
      requestUpdate();
      break;
    case xkcdui::ActionDismiss:
      view_ = View::Reader;
      requestUpdate();
      break;
    default:
      break;
  }
}

// --- The internet --------------------------------------------------------

void XkcdActivity::beginUpdate() {
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void XkcdActivity::onWifiChosen(const bool connected) {
  wifiConnected_ = connected;
  if (!connected) {
    view_ = View::Menu;
    requestUpdate();
    return;
  }
  // Paint the busy screen now; the fetch happens one loop pass later, so the
  // panel is never blank while the radio works.
  showNotice("UPDATING", "Asking xkcd.com what is new.");
  view_ = View::Updating;
  updateQueued_ = true;
  requestUpdate();
}

// One comic: metadata, artwork, convert, append. Returns false with a reason
// the caller can show, because an update that stops with no explanation is
// indistinguishable from a crash.
bool XkcdActivity::fetchOne(const uint16_t num, char* whyNot, const int whyNotCap) {
  char url[64];
  snprintf(url, sizeof(url), "https://xkcd.com/%u/info.0.json", static_cast<unsigned>(num));

  std::string meta;
  if (!HttpDownloader::fetchUrl(url, meta)) {
    snprintf(whyNot, whyNotCap, "Could not reach xkcd.com for #%u.", static_cast<unsigned>(num));
    return false;
  }

  // The three fields that matter, scraped without a JSON parser: this is a
  // fixed, tiny document from one server, and lib/JsonParser's SAX tokens
  // truncate at 512 bytes which the alt text can exceed.
  const auto field = [&meta](const char* key, char* out, const int cap) {
    out[0] = '\0';
    const std::string needle = std::string("\"") + key + "\": \"";
    const size_t at = meta.find(needle);
    if (at == std::string::npos) return false;
    size_t i = at + needle.size();
    int n = 0;
    while (i < meta.size() && meta[i] != '"' && n + 1 < cap) {
      if (meta[i] == '\\' && i + 1 < meta.size()) {
        ++i;
        if (meta[i] == 'n' || meta[i] == 't') {
          out[n++] = ' ';
          ++i;
          continue;
        }
      }
      const char ch = meta[i++];
      // The Toybox faces are subset to ASCII and a glyph the font lacks draws
      // as nothing at all, so anything outside it is dropped here rather than
      // silently disappearing on the panel.
      if (static_cast<unsigned char>(ch) >= 32 && static_cast<unsigned char>(ch) < 127) out[n++] = ch;
    }
    out[n] = '\0';
    return true;
  };

  char img[192];
  char title[64];
  char alt[512];
  if (!field("img", img, sizeof(img))) {
    snprintf(whyNot, whyNotCap, "#%u has no artwork.", static_cast<unsigned>(num));
    return false;
  }
  field("safe_title", title, sizeof(title));
  field("alt", alt, sizeof(alt));

  if (HttpDownloader::downloadToFile(img, kTmpPng) != HttpDownloader::OK) {
    snprintf(whyNot, whyNotCap, "Could not download the artwork for #%u.", static_cast<unsigned>(num));
    return false;
  }

  // Convert through the firmware's own PNG path, at native size. The limit is
  // passed as the pack's ceiling rather than the screen's, because a comic is
  // stored at 1:1 and only ever scaled if it somehow exceeds what the format
  // can hold.
  {
    HalFile png;
    HalFile bmp;
    if (!Storage.openFileForRead("XKCD", kTmpPng, png) || !Storage.openFileForWrite("XKCD", kTmpBmp, bmp)) {
      snprintf(whyNot, whyNotCap, "No room on the card for #%u.", static_cast<unsigned>(num));
      return false;
    }
    if (!PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, bmp, xkcd::kMaxComicWidth, xkcd::kMaxComicHeight)) {
      bmp.close();
      snprintf(whyNot, whyNotCap, "#%u is in a format this build cannot decode.", static_cast<unsigned>(num));
      return false;
    }
    bmp.close();
  }

  // BMP to pack format. Two things differ and both are silent if missed: the
  // BMP's rows are padded to four bytes where ours are padded to one, and a
  // set bit there means *white* where ours means ink.
  HalFile bmp;
  if (!Storage.openFileForRead("XKCD", kTmpBmp, bmp)) {
    snprintf(whyNot, whyNotCap, "Lost the converted artwork for #%u.", static_cast<unsigned>(num));
    return false;
  }
  uint8_t head[62];
  if (bmp.read(head, sizeof(head)) != static_cast<int>(sizeof(head))) {
    snprintf(whyNot, whyNotCap, "The converted artwork for #%u is truncated.", static_cast<unsigned>(num));
    return false;
  }
  const int32_t bw = static_cast<int32_t>(head[18] | (head[19] << 8) | (head[20] << 16) | (head[21] << 24));
  int32_t bh = static_cast<int32_t>(head[22] | (head[23] << 8) | (head[24] << 16) | (head[25] << 24));
  const bool topDown = bh < 0;
  if (bh < 0) bh = -bh;
  if (bw <= 0 || bh <= 0 || bw > xkcd::kMaxComicWidth || bh > xkcd::kMaxComicHeight || !topDown) {
    snprintf(whyNot, whyNotCap, "#%u came out %dx%d, which the pack cannot hold.", static_cast<unsigned>(num),
             static_cast<int>(bw), static_cast<int>(bh));
    return false;
  }

  const int srcStride = ((bw + 31) / 32) * 4;
  const int dstStride = (bw + 7) / 8;
  if (srcStride > kMaxStride * 4 || dstStride > kMaxStride) {
    snprintf(whyNot, whyNotCap, "#%u is wider than this build supports.", static_cast<unsigned>(num));
    return false;
  }

  HalFile images;
  HalFile texts;
  HalFile index;
  if (!Storage.openFileForRead("XKCD", kImagePath, images)) {
    snprintf(whyNot, whyNotCap, "The pack is missing images.dat.");
    return false;
  }
  const uint32_t imageOffset = static_cast<uint32_t>(images.size());
  images.close();

  // Appended rather than rewritten: new comics always carry the highest
  // numbers, so appending to index.dat keeps it sorted, which is the one
  // property the binary search depends on.
  if (!Storage.openFileForAppend("XKCD", kImagePath, images)) {
    snprintf(whyNot, whyNotCap, "Could not append to images.dat.");
    return false;
  }
  auto srcRow = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(srcStride));
  if (!srcRow) {
    snprintf(whyNot, whyNotCap, "Out of memory converting #%u.", static_cast<unsigned>(num));
    return false;
  }
  for (int y = 0; y < bh; ++y) {
    if (bmp.read(srcRow.get(), srcStride) != srcStride) {
      snprintf(whyNot, whyNotCap, "#%u is truncated at row %d.", static_cast<unsigned>(num), y);
      images.close();
      return false;
    }
    for (int b = 0; b < dstStride; ++b) gBand[b] = static_cast<uint8_t>(~srcRow[b]);
    // Bits past the width are padding and must be zero, or every row would
    // look inked and the comic would have no gaps for the reader to stop at.
    const int rest = bw % 8;
    if (rest != 0) gBand[dstStride - 1] &= static_cast<uint8_t>(0xFF << (8 - rest));
    images.write(gBand, static_cast<size_t>(dstStride));
  }
  images.close();

  uint32_t textOffset = 0;
  if (Storage.openFileForRead("XKCD", kTextPath, texts)) {
    textOffset = static_cast<uint32_t>(texts.size());
    texts.close();
  }
  if (!Storage.openFileForAppend("XKCD", kTextPath, texts)) {
    snprintf(whyNot, whyNotCap, "Could not append to text.dat.");
    return false;
  }
  texts.write(reinterpret_cast<const uint8_t*>(title), strlen(title) + 1);
  texts.write(reinterpret_cast<const uint8_t*>(alt), strlen(alt) + 1);
  texts.close();

  xkcd::Comic c{};
  c.num = num;
  c.width = static_cast<uint16_t>(bw);
  c.height = static_cast<uint16_t>(bh);
  c.stride = static_cast<uint16_t>(dstStride);
  c.imageOffset = imageOffset;
  c.textOffset = textOffset;

  uint8_t rec[xkcd::kIndexRecordBytes];
  xkcd::encodeRecord(c, rec);
  if (!Storage.openFileForAppend("XKCD", kIndexPath, index)) {
    snprintf(whyNot, whyNotCap, "Could not append to index.dat.");
    return false;
  }
  index.write(rec, sizeof(rec));
  index.close();

  Storage.remove(kTmpPng);
  Storage.remove(kTmpBmp);
  return true;
}

void XkcdActivity::runUpdate() {
  char why[128] = {0};

  std::string latestJson;
  if (!HttpDownloader::fetchUrl("https://xkcd.com/info.0.json", latestJson)) {
    showNotice("NO ANSWER", "xkcd.com did not answer. The archive on the card is unchanged.");
    return;
  }
  const size_t at = latestJson.find("\"num\": ");
  if (at == std::string::npos) {
    showNotice("NO ANSWER", "xkcd.com answered with something this build did not understand.");
    return;
  }
  const uint16_t latest = static_cast<uint16_t>(atoi(latestJson.c_str() + at + 7));
  const uint16_t have = archiveOpen_ ? archive_.maxNum() : 0;

  if (!archiveOpen_) {
    showNotice("NO ARCHIVE",
               "There is no pack on this card to add to. Build one with "
               "tools_local/xkcd/build_pack.py and copy it to /xkcd.");
    return;
  }
  if (latest <= have) {
    waiting_ = 0;
    showNotice("UP TO DATE", "Nothing new since the last one on the card.");
    return;
  }

  // The header has to be rewritten after appending, because the count and the
  // highest number both live in it. Done once at the end rather than per
  // comic: a half-finished run then leaves a pack whose header is honest about
  // fewer comics than the files hold, which reads correctly and simply misses
  // the tail. The opposite order would leave a header promising records that
  // are not there.
  fetched_ = 0;
  for (uint16_t n = static_cast<uint16_t>(have + 1); n <= latest; ++n) {
    if (n == 404) continue;  // not a comic, and that is the joke
    if (!fetchOne(n, why, sizeof(why))) {
      LOG_ERR("XKCD", "stopped at #%u: %s", static_cast<unsigned>(n), why);
      break;
    }
    ++fetched_;
  }

  if (fetched_ > 0) {
    HalFile index;
    if (Storage.openFileForWrite("XKCD", kIndexPath, index)) {
      // openFileForWrite truncates, so the header is patched in place through
      // a seek instead.
      index.close();
    }
    HalFile patch;
    if (Storage.openFileForAppend("XKCD", kIndexPath, patch)) {
      const uint32_t total = static_cast<uint32_t>(archive_.count() + fetched_);
      uint8_t head[8];
      head[0] = static_cast<uint8_t>(total & 0xFF);
      head[1] = static_cast<uint8_t>((total >> 8) & 0xFF);
      head[2] = static_cast<uint8_t>((total >> 16) & 0xFF);
      head[3] = static_cast<uint8_t>((total >> 24) & 0xFF);
      const uint32_t top = latest;
      head[4] = static_cast<uint8_t>(top & 0xFF);
      head[5] = static_cast<uint8_t>((top >> 8) & 0xFF);
      head[6] = static_cast<uint8_t>((top >> 16) & 0xFF);
      head[7] = static_cast<uint8_t>((top >> 24) & 0xFF);
      patch.seek(8);
      patch.write(head, sizeof(head));
      patch.close();
    }

    // Reopen so the in-memory archive agrees with the files again.
    closeArchive();
    archiveOpen_ = openArchive();
  }

  char body[192];
  if (fetched_ == 0) {
    snprintf(body, sizeof(body), "%s", why[0] ? why : "Nothing was added.");
    showNotice("NOTHING NEW", body);
  } else {
    snprintf(body, sizeof(body), "Added %d. The newest on the card is now #%u.", fetched_,
             static_cast<unsigned>(archive_.maxNum()));
    waiting_ = 0;
    showNotice("UPDATED", body);
  }
}

// --- Render --------------------------------------------------------------

void XkcdActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingChromeFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);

  // A slimmer header than the portrait screens use: in landscape the usual 76
  // would cost a sixth of the height a comic needs.
  fui::ThemeTokens tokens = toybox::themeTokens();
  tokens.headerHeight = xkcdui::kHeaderBand;
  toybox::Screen screen(frame, tokens);

  switch (view_) {
    case View::Menu: {
      xkcdui::MenuModel model;
      model.hasArchive = archiveOpen_;
      if (archiveOpen_) {
        model.latestNum = archive_.maxNum();
        model.latestTitle = title_;
        model.comicCount = archive_.count();
        model.readCount = readCount_;
        model.waiting = waiting_;
      }
      xkcdui::buildMenu(screen, model);
      // The ornament is the app's own surface, so it is drawn by hand into the
      // band the screen reserved rather than expressed as components.
      drawMosaic(xkcdui::menuMosaicBand(target.deviceContext()));
      break;
    }
    case View::List: {
      xkcdui::ListModel model;
      model.items = rowItems_;
      model.count = rowCount_;
      model.selected = listSelected_;
      model.rightLabel = listRight_;
      model.canPageOlder = listFirst_ + kPageRows < archive_.count();
      model.canPageNewer = listFirst_ > 0;
      xkcdui::buildList(screen, model);
      break;
    }
    case View::Reader: {
      // The comic first, then its bar: the bar is chrome drawn over the page,
      // and anything drawn outside its own cell needs its own pass.
      drawComic();

      xkcdui::ReaderModel model;
      model.num = comic_.num;
      model.title = title_;
      model.pans = xkcd::maxScroll(comic_, xkcdui::readerViewport(target.deviceContext()).height) > 0;
      model.permille = xkcd::scrollPermille(comic_, xkcdui::readerViewport(target.deviceContext()).height, scrollY_);
      model.hasAlt = alt_[0] != '\0';
      xkcdui::buildReaderBar(screen, model);

      // The two halves that pan, registered from the same function that the
      // artwork was placed against. Registered after the bar so the bar's own
      // controls win where they overlap.
      if (model.pans) {
        frame.hit(xkcdui::readerPanUpHalf(target.deviceContext()), xkcdui::ActionPanUp);
        frame.hit(xkcdui::readerPanDownHalf(target.deviceContext()), xkcdui::ActionPanDown);
      }
      break;
    }
    case View::Alt: {
      xkcdui::AltModel model;
      model.num = comic_.num;
      model.title = title_;
      model.alt = alt_;
      xkcdui::buildAlt(screen, model);
      break;
    }
    case View::Updating:
    case View::Notice: {
      xkcdui::NoticeModel model;
      model.headline = noticeHead_;
      model.detail = noticeBody_;
      model.actionLabel = noticeAction_;
      model.action = noticeActionId_;
      xkcdui::buildNotice(screen, model);
      break;
    }
  }

  toybox::reportOverflow(interactions_, "Xkcd");
  interactionsReady_ = true;
  renderer.displayBuffer();
}
