#include "XkcdActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <Utf8.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxTheme.h"
#include "DevMode.h"

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
constexpr int kMaxStride = (xkcd::kMaxArtWidth + 7) / 8;
uint8_t gBand[kBandRows * kMaxStride];

// The gap flags for one step, sized from the same arithmetic the window is
// derived from rather than from a number written down beside it. That
// distinction is not pedantic: this was a hardcoded 256 justified by a comment
// computing 203 rows "at a 480px viewport", the reader's viewport is 756, and
// so `pan()` refused every window as too large and no step ever snapped to a
// gap. See xkcd::gapRowsFor.
// The panel less the bar: an upper bound for the buffers below. The runtime
// viewport (xkcdui::readerViewport) is further shrunk by the bezel insets, so
// the bound only over-allocates, never under.
constexpr int kReaderViewportH = 800 - 44;
constexpr int kMaxGapRows = xkcd::gapRowsFor(kReaderViewportH);
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
  // **The panel never rotates.** A wide comic is stored already turned on its
  // side, so the reader turns the *device* to read it while the screen layout
  // stays exactly where it was -- the bar in the same place, the controls in
  // the same place. An earlier version called setOrientation per comic, which
  // moved the whole UI around underneath the reader and was rightly rejected:
  // it is the comic that is sideways, not the app. See
  // docs/apps/xkcd-pack-format.md.
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
  // Not ours to drop if Developer Mode raised it. Every other wifi user here
  // either tracks its own ownership flag or asks this; a bare getMode() check
  // means "somebody has the radio", not "I do".
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Storage.remove(kTmpPng);
  Storage.remove(kTmpBmp);
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
  //
  // isOpen(), not the pointer. The pointer says an object exists; it says
  // nothing about whether the file behind it was ever opened, and close() on an
  // unopened HalFile asserts (HalStorage.cpp:176, impl != nullptr).
  //
  // openArchive() short-circuits: with no pack on the card the first open fails
  // and the second and third are never even attempted, so those two HalFiles
  // still hold the null impl they were default-constructed with. Backing out of
  // the app then closed them and panicked the device. Reported from hardware,
  // github.com/ma-r-s/crossplay/issues/5, and not reproducible in the simulator
  // because the simulator ships its own HalStorage without that assert.
  if (indexFile_ && indexFile_->isOpen()) indexFile_->close();
  if (imageFile_ && imageFile_->isOpen()) imageFile_->close();
  if (textFile_ && textFile_->isOpen()) textFile_->close();
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
  at_ = xkcd::Position{};
  // **A comic opens in whatever view can actually be read.** The builder only
  // makes a closer rendition for comics whose page view is shrunk past
  // legibility, so if there is one, that is the one to open in. Opening on a
  // comic too small to read and making the reader ask for the readable version
  // is the wrong way round -- OK pulls back to the whole comic, which is the
  // thing you want occasionally, not the thing you want first.
  // **A comic opens as the artwork itself**, at the size its lettering needs
  // and in the posture that costs the fewest panning axes. The builder settled
  // both; there is nothing left to decide here. OK pulls back to the whole
  // comic on one screen, for the 15% that pan at all.
  //
  // It starts at the comic's own first panel, which for a comic stored turned
  // is not the stored image's top left. Reading one from the wrong corner
  // starts you at what you see, holding the device turned, as the bottom of
  // the strip.
  {
    const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
    at_ = xkcd::startOf(xkcd::renditionFor(comic_, xkcd::Lens::Art), view.width);
    at_.lens = xkcd::Lens::Art;
  }
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
    snprintf(rowLabels_[rowCount_], kRowLabelCap, "%u  %s", static_cast<unsigned>(c.num), shout);

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

void XkcdActivity::typeDigit(const int digit) {
  if (digit < 0 || digit > 9) return;
  // A leading zero would make "0" and "00" different strings for the same
  // number, and there is no comic 0.
  if (typedLen_ == 0 && digit == 0) return;
  if (typedLen_ >= static_cast<int>(sizeof(typed_)) - 1) return;
  typed_[typedLen_++] = static_cast<char>('0' + digit);
  typed_[typedLen_] = '\0';
}

void XkcdActivity::backspace() {
  if (typedLen_ <= 0) return;
  typed_[--typedLen_] = '\0';
}

bool XkcdActivity::typedIsOnCard() const {
  if (typedLen_ <= 0 || !archiveOpen_) return false;
  const int n = atoi(typed_);
  if (n <= 0 || n > 65535) return false;
  // Asked of the index rather than of the range, because the archive has
  // holes: 404 is not a comic, and a pack can be built from any slice.
  return archive_.find(*indexSrc_, static_cast<uint16_t>(n)) >= 0;
}

void XkcdActivity::goToTyped() {
  if (typedLen_ <= 0 || !archiveOpen_) return;
  const int n = atoi(typed_);
  const int pos = archive_.find(*indexSrc_, static_cast<uint16_t>(n));
  if (pos < 0) {
    // GO is dimmed in this state, so arriving here means the index moved under
    // us. Say which number rather than failing silently.
    char why[64];
    snprintf(why, sizeof(why), "#%d is not in the pack on this card.", n);
    showNotice("NOT HERE", why);
    return;
  }
  openComicAt(pos);
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
  // See TriviaActivity::showNotice: passing the current body back in is an
  // overlapping self-copy, and aliasing means "keep it".
  if (detail != noticeBody_) {
    snprintf(noticeBody_, sizeof(noticeBody_), "%s", detail);
  }
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
}

// --- Panning -------------------------------------------------------------

void XkcdActivity::pan(const bool down) {
  const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
  const int viewportH = view.height;
  const int viewportW = view.width;

  int firstRow = 0;
  int rowCount = 0;
  const xkcd::Rendition r = xkcd::renditionFor(comic_, at_.lens);
  // The window for the panel the step is heading to. Which panel that is comes
  // straight from the walk, so the rows read are the rows the step will judge.
  const int target = down ? at_.row + 1 : at_.row - 1;
  xkcd::gapWindowFor(r, viewportH, target, firstRow, rowCount);

  xkcd::GapWindow window;
  if (rowCount > 0 && rowCount <= kMaxGapRows && r.stride <= kMaxStride) {
    // Stream the rows past rowIsGap a band at a time and keep only the flags.
    // Holding the window as pixels would be ~19KB on the heap for every tap;
    // as flags it is one byte a row.
    bool ok = true;
    int done = 0;
    while (done < rowCount && ok) {
      int rows = kBandRows;
      if (rows > rowCount - done) rows = rowCount - done;
      const uint32_t at = r.offset + static_cast<uint32_t>(firstRow + done) * r.stride;
      ok = imageSrc_->read(at, gBand, static_cast<uint32_t>(rows) * r.stride);
      if (!ok) break;
      for (int row = 0; row < rows; ++row) {
        gGapFlags[done + row] = xkcd::rowIsGap(gBand + row * r.stride, r.width) ? 1 : 0;
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

  // One control, reading order: across the columns of this band, then down.
  // See xkcd::stepForward.
  at_ = down ? xkcd::stepForward(r, viewportW, viewportH, at_, window)
             : xkcd::stepBack(r, viewportW, viewportH, at_, window);
}

// --- Drawing -------------------------------------------------------------

void XkcdActivity::drawComic() {
  if (!archiveOpen_ || !comic_.valid()) return;
  const xkcd::Rendition r = xkcd::renditionFor(comic_, at_.lens);
  if (!r.valid() || r.stride > kMaxStride) return;

  const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
  const xkcd::Placement p = xkcd::place(r, view.width, view.height, at_);

  // A band at a time: one seek per band rather than one per row, and a fixed
  // cost whatever the comic.
  int drawn = 0;
  while (drawn < p.visibleH) {
    int rows = kBandRows;
    if (rows > p.visibleH - drawn) rows = p.visibleH - drawn;
    const uint32_t at = r.offset + static_cast<uint32_t>(p.scrollY + drawn) * r.stride;
    if (!imageSrc_->read(at, gBand, static_cast<uint32_t>(rows) * r.stride)) {
      LOG_ERR("XKCD", "artwork read failed for #%u at row %d", static_cast<unsigned>(comic_.num), p.scrollY + drawn);
      return;
    }
    for (int band = 0; band < rows; ++band) {
      const uint8_t* line = gBand + band * r.stride;
      const int y = view.y + p.originY + drawn + band;
      for (int x = 0; x < p.visibleW; ++x) {
        // A set bit is ink, which is toybox::blit1bpp's convention and the
        // pack's. Drawn pixel by pixel because the renderer has no 1bpp blit
        // that takes a stride, and drawIcon bakes in a portrait rotation that
        // would turn the comic on its side.
        // Viewport-relative; view.x is added at the drawPixel. It was 0 until
        // the viewport started honoring the bezel's side columns.
        const int sx = p.originX + x;
        if (sx >= view.width) break;
        // Offset into the row by the column on screen: a comic with more than
        // one column shows a different slice of every row. Clipped rather than
        // trusted, so a pack built for a different panel cannot blit off the
        // framebuffer.
        const int ix = p.scrollX + x;
        if (ix >= static_cast<int>(r.width)) break;
        if ((line[ix >> 3] >> (7 - (ix & 7))) & 1) renderer.drawPixel(view.x + sx, y, true);
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
  if (downloadQueued_) {
    downloadQueued_ = false;
    runPackDownload();
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
      case View::Number:
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

  // The physical buttons, routed through the same handleAction() the taps
  // reach. Two paths would drift, and on a device with both inputs the drift
  // is invisible until somebody uses the one you did not test.
  //
  // **Buttons move between comics; taps move within one.** Two axes that never
  // overlap, so there is nothing to learn beyond that and no state where the
  // same press means two things. The alternative -- page buttons that pan and
  // then spill into the next comic at the bottom -- reads as magic the first
  // time it happens and as a bug the second.
  const bool forward = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool backward = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Up);

  // **No Confirm binding.** docs/buttons.md section 4: Up and Down page,
  // nothing else is a button, because on this device nothing else IS a button.
  // The overview toggle lived here until the tap target replaced it, and
  // leaving the dead binding behind is precisely what let an unreachable
  // control ship in the first place -- the simulator synthesises Confirm, so
  // it looked fine forever.

  if (view_ == View::Reader && (forward || backward)) {
    handleAction(forward ? xkcdui::ActionPanDown : xkcdui::ActionPanUp, 0);
    return;
  }
  if (view_ == View::List && (forward || backward)) {
    handleAction(forward ? xkcdui::ActionPageOlder : xkcdui::ActionPageNewer, 0);
    return;
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
    case xkcdui::ActionGoToNumber:
      typed_[0] = '\0';
      typedLen_ = 0;
      view_ = View::Number;
      requestUpdate();
      break;
    case xkcdui::ActionDigit:
      typeDigit(value);
      requestUpdate();
      break;
    case xkcdui::ActionBackspace:
      backspace();
      requestUpdate();
      break;
    case xkcdui::ActionGo:
      goToTyped();
      requestUpdate();
      break;
    case xkcdui::ActionUpdate:
      beginUpdate();
      break;
    case xkcdui::ActionDownloadPack:
      // WiFi is already up: this action only exists on the notice that the
      // update flow shows after its own WiFi step found no pack.
      showNotice("DOWNLOADING", "The archive is on its way.");
      downloadQueued_ = true;
      requestUpdate();
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
    // **Forward means forward.** One action, on the side key and on the tap
    // half alike: walk this comic's views, and when there are none left,
    // continue into the next comic. docs/buttons.md asks for exactly this --
    // Up and Down page, and paging by button is never the only route, so the
    // artwork halves do the same thing.
    //
    // The old split (buttons change comic, taps pan within one) left next/prev
    // comic reachable ONLY by button, which the doc forbids, and left the
    // device's moulded page keys not paging the comic they are labelled for.
    // It also made the keys do nothing on the 92% of comics that are a single
    // view.
    case xkcdui::ActionPanDown: {
      const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
      const xkcd::Rendition r = xkcd::renditionFor(comic_, at_.lens);
      if (xkcd::canStepForward(r, view.width, view.height, at_)) {
        pan(true);
        requestUpdate();
        break;
      }
      handleAction(xkcdui::ActionNextComic, 0);
      break;
    }
    case xkcdui::ActionPanUp: {
      const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
      const xkcd::Rendition r = xkcd::renditionFor(comic_, at_.lens);
      if (xkcd::canStepBack(r, view.width, view.height, at_)) {
        pan(false);
        requestUpdate();
        break;
      }
      handleAction(xkcdui::ActionPrevComic, 0);
      break;
    }
    case xkcdui::ActionToggleOverview: {
      if (!comic_.hasOverview()) break;
      const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
      const xkcd::Lens to = at_.lens == xkcd::Lens::Whole ? xkcd::Lens::Art : xkcd::Lens::Whole;
      // Switching views keeps your place: the two renditions are the same
      // artwork at different scales, so the row under the top of the screen
      // maps straight across. Landing back at the top of a 3000-row comic
      // because you looked closer at the bottom of it would be its own bug.
      at_ = xkcd::mapAcross(comic_, view.width, view.height, at_, to);
      requestUpdate();
      break;
    }
    case xkcdui::ActionNextComic:
      // Positions ascend by comic number, so the next position is the newer
      // comic. Clamped rather than wrapped: arriving back at #1 from the
      // newest reads as a fault, not as a feature.
      if (archiveOpen_ && position_ + 1 < archive_.count()) {
        openComicAt(position_ + 1);
        requestUpdate();
      }
      break;
    case xkcdui::ActionPrevComic:
      if (archiveOpen_ && position_ > 0) {
        openComicAt(position_ - 1);
        // At its END, not its start: backing off the top of one comic should
        // show you the bottom of the one before, exactly as a reader shows the
        // last page of the chapter you just stepped into. Free, because a
        // position is a panel index rather than something to walk to.
        {
          const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
          const xkcd::Lens lens = at_.lens;
          at_ = xkcd::endOf(xkcd::renditionFor(comic_, lens), view.width, view.height);
          at_.lens = lens;
        }
        requestUpdate();
      }
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

// The whole pack, from the rolling `xkcd-pack` release. Three sequential
// downloads to .part names; the real names appear only when all three are
// complete, so a torn download can never masquerade as a corrupt pack -- the
// app just finds no pack, exactly as before the attempt.
//
// Synchronous, like runUpdate(): loop() is blocked, but the render task is
// not, so progress paints through requestUpdateAndWait(). The input pump in
// the progress callback is the sanctioned exception to the one-pump rule --
// nothing else pumps while this blocks, and without it Back could not cancel
// a multi-minute download.
void XkcdActivity::runPackDownload() {
  constexpr const char* kPackBase = "https://github.com/ma-r-s/crossplay/releases/download/xkcd-pack/";
  struct Part {
    const char* file;   // asset name and final basename
    const char* label;  // what the progress line calls it
  };
  constexpr Part kParts[] = {
      {"index.dat", "the index"},
      {"text.dat", "the text"},
      {"images.dat", "the comics"},
  };

  if (!Storage.mkdir(kDir)) {
    showNotice("NO ROOM", "Could not create /xkcd on the card. Is the card in, and writable?");
    return;
  }

  downloadCancel_ = false;
  bool homeAfterCancel = false;
  for (const Part& part : kParts) {
    char url[128];
    char dest[48];
    snprintf(url, sizeof(url), "%s%s", kPackBase, part.file);
    snprintf(dest, sizeof(dest), "%s/%s.part", kDir, part.file);

    size_t lastPainted = 0;
    const auto progress = [this, &part, &lastPainted, &homeAfterCancel](const size_t got, const size_t total) {
      // Repaint every ~8MB: each paint is an e-ink refresh, and 140MB at
      // finer steps would spend more time refreshing than downloading.
      if (got - lastPainted >= 8u * 1024u * 1024u || (total > 0 && got == total)) {
        lastPainted = got;
        if (total > 0) {
          snprintf(noticeBody_, sizeof(noticeBody_), "Fetching %s: %u of %u MB. Back stops it.", part.label,
                   static_cast<unsigned>(got >> 20), static_cast<unsigned>(total >> 20));
        } else {
          snprintf(noticeBody_, sizeof(noticeBody_), "Fetching %s: %u MB so far. Back stops it.", part.label,
                   static_cast<unsigned>(got >> 20));
        }
        requestUpdateAndWait();
      }
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) downloadCancel_ = true;
      // The pump above consumes the one-shot home event before ActivityManager
      // can see it; treat it as a cancel too rather than dropping the intent.
      // The user lands on the STOPPED notice and their next Home works.
      if (mappedInput.wasHomeGesture()) {
        downloadCancel_ = true;
        homeAfterCancel = true;
      }
    };

    const auto err = HttpDownloader::downloadToFile(url, dest, progress, &downloadCancel_);
    if (err != HttpDownloader::OK) {
      for (const Part& p : kParts) {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "%s/%s.part", kDir, p.file);
        Storage.remove(tmp);
      }
      if (err == HttpDownloader::ABORTED) {
        showNotice("STOPPED", homeAfterCancel ? "Download stopped. Nothing was kept; Home works now."
                                              : "Download stopped. Nothing was kept.");
      } else if (err == HttpDownloader::FILE_ERROR) {
        showNotice("CARD TROUBLE", "The card would not take the file. Nothing was kept.");
      } else {
        showNotice("NO ANSWER", "The download did not answer. The card is unchanged; try again later.");
      }
      return;
    }
  }

  for (const Part& part : kParts) {
    char tmp[48];
    char fin[48];
    snprintf(tmp, sizeof(tmp), "%s/%s.part", kDir, part.file);
    snprintf(fin, sizeof(fin), "%s/%s", kDir, part.file);
    Storage.remove(fin);  // a half pack from some earlier era must not block the rename
    if (!Storage.rename(tmp, fin)) {
      showNotice("CARD TROUBLE", "Downloaded, but the card refused the final rename. Try again.");
      return;
    }
  }

  if (!openArchive()) {
    showNotice("BAD PACK", "Downloaded, but the pack did not open. Try again later.");
    return;
  }
  archiveOpen_ = true;
  char body[96];
  snprintf(body, sizeof(body), "%d comics, through #%u. UPDATE fetches anything newer.", archive_.count(),
           static_cast<unsigned>(archive_.maxNum()));
  showNotice("ALL OF IT", body, "READ", xkcdui::ActionOpenLatest);
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
  // `fold` says whether this field is a sentence or an address. The lambda
  // serves all three, and one of them is the artwork URL: folding a character
  // in a URL does not mangle it visibly, it points it somewhere else.
  const auto field = [&meta](const char* key, char* out, const int cap, const bool fold) {
    out[0] = '\0';
    const std::string needle = std::string("\"") + key + "\": \"";
    const size_t at = meta.find(needle);
    if (at == std::string::npos) return false;
    size_t i = at + needle.size();
    // Collected whole first, THEN folded, THEN filtered. Folding needs the
    // multi-byte sequence intact -- an em dash is three bytes and the old loop
    // threw all three away one at a time, which is why "sonnets - a study" came
    // off the wire as "sonnets  a study" with a gap where the dash was. Now it
    // becomes "--".
    std::string raw;
    // A generous read bound rather than a tight one. The old loop bounded the
    // OUTPUT at `cap`, which it can no longer do, because folding needs whole
    // multi-byte sequences before it can decide anything. Four times the
    // caller's buffer is far past the longest real title or alt text and stops
    // a malformed response walking the whole document.
    const size_t rawCap = static_cast<size_t>(cap) * 4;
    while (i < meta.size() && meta[i] != '"' && raw.size() < rawCap) {
      if (meta[i] == '\\' && i + 1 < meta.size()) {
        ++i;
        if (meta[i] == 'n' || meta[i] == 't') {
          raw.push_back(' ');
          ++i;
          continue;
        }
        // \uXXXX, which is how this server sends every non-ASCII character it
        // has -- twenty sampled comics carried no raw high byte at all and one
        // \u00b1. Decoded here because without it the backslash was dropped and
        // the four hex digits kept, so a plus-or-minus reached the panel as the
        // literal text "u00b1" and an em dash as "u2014". Surrogate pairs are
        // not joined: this server has never sent one, and half a pair decodes
        // to a codepoint no cut can draw, which the ASCII filter below removes.
        if (meta[i] == 'u' && i + 4 < meta.size()) {
          uint32_t codepoint = 0;
          bool hex = true;
          for (int d = 1; d <= 4 && hex; ++d) {
            const char digit = meta[i + d];
            if (digit >= '0' && digit <= '9') {
              codepoint = codepoint * 16 + static_cast<uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
              codepoint = codepoint * 16 + static_cast<uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
              codepoint = codepoint * 16 + static_cast<uint32_t>(digit - 'A' + 10);
            } else {
              hex = false;
            }
          }
          if (hex && codepoint != 0) {
            utf8AppendCodepoint(codepoint, raw);
            i += 5;
            continue;
          }
        }
      }
      raw.push_back(meta[i++]);
    }
    const std::string folded = fold ? utf8FoldTypography(raw) : raw;
    int n = 0;
    for (const char ch : folded) {
      if (n + 1 >= cap) break;
      // What the fold could not spell in ASCII is still dropped: this app's
      // header title is the TITLE slot, which is a Jersey cut subset to
      // U+0020..U+007E, and a glyph the font lacks draws as nothing at all.
      // An accented letter is lost here and that is a known gap, recorded in
      // host-tests/typefold/ rather than left to be rediscovered.
      if (static_cast<unsigned char>(ch) >= 32 && static_cast<unsigned char>(ch) < 127) out[n++] = ch;
    }
    out[n] = '\0';
    return true;
  };

  char img[192];
  char title[64];
  char alt[512];
  if (!field("img", img, sizeof(img), false)) {
    snprintf(whyNot, whyNotCap, "#%u has no artwork.", static_cast<unsigned>(num));
    return false;
  }
  field("safe_title", title, sizeof(title), true);
  field("alt", alt, sizeof(alt), true);

  // THE PUMP LIVES HERE, not only in runUpdate()'s loop, and that is not
  // belt-and-braces. InputManager::update() debounces by COMMITTING A STATE
  // CHANGE ON A LATER CALL: the first update() that sees a new level only
  // stamps lastDebounceTime, and the commit needs a second call more than
  // DEBOUNCE_DELAY (5ms) afterwards. Nothing else in the firmware polls during
  // this run -- InputManager::beginAsync() is never called anywhere in the
  // fork -- so a pump that fires once per comic sees a press only if it is
  // still held at the next comic, and a normal tap between two pumps is
  // invisible to it entirely. "Back stops it" would have been a lie.
  //
  // HttpDownloader's abortPoll() invokes this every 50ms through the connect,
  // the headers and the body, so Back is live for the long part of each comic.
  // Same mechanism as runPackDownload(); the metadata fetch above still has no
  // hook (fetchUrl takes no progress callback -- card #120).
  const auto pump = [this](const size_t, const size_t) {
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) updateCancel_ = true;
    // The pump consumes the one-shot home event before ActivityManager can see
    // it; treat it as a cancel rather than dropping the intent.
    if (mappedInput.wasHomeGesture()) {
      updateCancel_ = true;
      updateHomeCancel_ = true;
    }
  };
  if (HttpDownloader::downloadToFile(img, kTmpPng, pump, &updateCancel_) != HttpDownloader::OK) {
    snprintf(whyNot, whyNotCap, "Could not download the artwork for #%u.", static_cast<unsigned>(num));
    return false;
  }

  // Convert through the firmware's own PNG path, fitted to the panel width so
  // a fetched comic is the same kind of thing as a packed one: a page
  // rendition with no horizontal axis.
  //
  // Two things a fetched comic does NOT get, both honest limitations rather
  // than oversights. It never gets a **closer view**, because that needs the
  // greyscale original resampled a second time and the device has thrown it
  // away by now; `overviewWidth` stays zero and the OK mark simply does not
  // appear. And it is never stored **sideways**, because PngToBmpConverter
  // cannot rotate; a wide comic fetched over wifi arrives upright and small
  // until the next host pack build.
  {
    HalFile png;
    HalFile bmp;
    if (!Storage.openFileForRead("XKCD", kTmpPng, png) || !Storage.openFileForWrite("XKCD", kTmpBmp, bmp)) {
      snprintf(whyNot, whyNotCap, "No room on the card for #%u.", static_cast<unsigned>(num));
      return false;
    }
    if (!PngToBmpConverter::pngFileTo1BitBmpStreamFitWithin(png, bmp, xkcd::kPanelWidth, xkcd::kMaxComicHeight)) {
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
  if (bw <= 0 || bh <= 0 || bw > xkcd::kPanelWidth || bh > xkcd::kMaxComicHeight || !topDown) {
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
    // First run. Nobody copies files to a card: the pre-built pack (see
    // docs/apps/xkcd-pack-format.md for why the bulk cannot be converted
    // on-device) is a release asset, and the device fetches it itself.
    showNotice("THE ARCHIVE",
               "The whole xkcd archive is about 3300 comics, a 140MB download "
               "onto this card. It takes a few minutes on WiFi, once.",
               "DOWNLOAD", xkcdui::ActionDownloadPack);
    return;
  }
  if (latest <= have) {
    waiting_ = 0;
    showNotice("UP TO DATE", "Nothing new since the last one on the card.");
    return;
  }

  // How many there are to get, COUNTED rather than written down as
  // latest - have: 404 is not a comic and the difference is off by one
  // whenever it is in range. A denominator the reader is shown has to be
  // derived from the same walk that produces the numerator; see the memory
  // derived-facts-written-as-literals.
  int toGet = 0;
  for (uint16_t n = static_cast<uint16_t>(have + 1); n <= latest; ++n) {
    if (n != 404) ++toGet;
  }

  fetched_ = 0;
  // The highest number that ACTUALLY arrived, not the highest that exists.
  // The header patch below publishes this as the archive's maxNum, and maxNum
  // is what the next update subtracts from. See the comment there.
  uint16_t lastGot = have;
  updateCancel_ = false;
  updateHomeCancel_ = false;
  int attempted = 0;
  for (uint16_t n = static_cast<uint16_t>(have + 1); n <= latest; ++n) {
    if (n == 404) continue;  // not a comic, and that is the joke

    // SAY WHAT IS HAPPENING BEFORE EACH COMIC. fetchOne() is a metadata fetch,
    // an artwork download and a PNG decode -- seconds each, nothing repainting
    // inside it -- and this loop runs once per comic missed since the last
    // update. xkcd publishes three a week, so a reader a season behind waits
    // minutes on one frame that says "Asking xkcd.com what is new", which
    // stopped being true at the first iteration. A screen that says nothing for
    // minutes reads as a crash (memory a-silent-screen-reads-as-a-crash); this
    // is the last of card #306's list still silent.
    //
    // requestUpdateAndWait(), not requestUpdate(): loop() is blocked for the
    // whole run, so a deferred update never reaches the tail of
    // ActivityManager::loop() that consumes it and nothing would repaint at
    // all. Same shape as runPackDownload() above.
    //
    // WHAT THE WAIT DOES NOT BUY, said plainly so nobody relies on it.
    // renderTaskLoop() takes its notification at the TOP of an iteration and
    // claims waitingTaskHandle at the BOTTOM, so a wait registered while a
    // render is ALREADY in flight is satisfied by the frame that began before
    // it asked. That happens here on the first pass -- onWifiChosen() left a
    // deferred update pending and displayBuffer() is 0.3-2s -- so the caption
    // can run one comic behind what is being fetched. Fixing it means changing
    // that handshake in ActivityManager, which is CrossPoint's file and not
    // ours to patch for a caption. What the wait does buy is the thing that was
    // missing: the panel changes once per comic instead of once per run.
    ++attempted;
    {
      // The render task reads noticeBody_ while building this frame, and the
      // wait above may return with one still in flight. Scoped so the lock is
      // released before requestUpdateAndWait(), which asserts against being
      // called under it. Same shape as HackerNewsActivity::request().
      RenderLock lock(*this);
      snprintf(noticeBody_, sizeof(noticeBody_), "Comic %d of %d: #%u. Back stops it.", attempted, toGet,
               static_cast<unsigned>(n));
    }
    requestUpdateAndWait();
    // Between comics as well as inside the download, because the PNG decode
    // and the index append are ours and have no callback to hang a pump on.
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) updateCancel_ = true;
    if (mappedInput.wasHomeGesture()) {
      updateCancel_ = true;
      updateHomeCancel_ = true;
    }
    // Before the fetch, not after: stopping is only worth offering if it does
    // not first sit through the download it was meant to skip.
    if (updateCancel_) break;

    if (!fetchOne(n, why, sizeof(why))) {
      // A cancel arrives here too: fetchOne()'s download is handed
      // &updateCancel_, so Back aborts the transfer and it returns false.
      LOG_ERR("XKCD", "stopped at #%u: %s", static_cast<unsigned>(n), why);
      break;
    }
    ++fetched_;
    lastGot = n;
  }

  // The header has to be rewritten after appending, because the count and the
  // highest number both live in it. Done once at the end rather than per
  // comic: a half-finished run then leaves a pack whose header is honest about
  // fewer comics than the files hold, which reads correctly and simply misses
  // the tail. The opposite order would leave a header promising records that
  // are not there.
  if (fetched_ > 0) {
    // **Not openFileForWrite.** It carries O_TRUNC, and calling it here -- with
    // a comment saying why it must not be called -- emptied index.dat before
    // the patch ran. The pack survived as an 8-byte file: a correct header for
    // an archive with no records in it.
    //
    // Not openFileForAppend either: on hosts where that is O_APPEND the seek
    // below is ignored and the header lands at EOF. openFileForUpdate is plain
    // O_RDWR, which is what patching in place actually needs.
    HalFile patch;
    if (Storage.openFileForUpdate("XKCD", kIndexPath, patch)) {
      const uint32_t total = static_cast<uint32_t>(archive_.count() + fetched_);
      uint8_t head[8];
      head[0] = static_cast<uint8_t>(total & 0xFF);
      head[1] = static_cast<uint8_t>((total >> 8) & 0xFF);
      head[2] = static_cast<uint8_t>((total >> 16) & 0xFF);
      head[3] = static_cast<uint8_t>((total >> 24) & 0xFF);
      // lastGot, NOT latest. This field IS the archive's maxNum (XkcdCore reads
      // it back from offset 12), and the next update computes what to fetch as
      // everything above maxNum. Publishing `latest` after a run that stopped
      // early -- a failed comic, and now a cancelled one -- claims comics the
      // card does not hold and makes every later update answer UP TO DATE. The
      // gap would then be unreachable short of deleting the pack.
      const uint32_t top = lastGot;
      head[4] = static_cast<uint8_t>(top & 0xFF);
      head[5] = static_cast<uint8_t>((top >> 8) & 0xFF);
      head[6] = static_cast<uint8_t>((top >> 16) & 0xFF);
      head[7] = static_cast<uint8_t>((top >> 24) & 0xFF);
      patch.seek(8);
      patch.write(head, sizeof(head));
      patch.close();
    }

    // Reopen so the in-memory archive agrees with the files again, under the
    // lock: requestUpdateAndWait() above can return with a render still in
    // flight, and closeArchive() frees the three HalFiles and the index source
    // that the Menu and List views read. Nothing reads them from the Updating
    // view we are in, so this is the hazard closed rather than a bug fixed --
    // but a line added to the notice would open it, and nothing would say so.
    RenderLock lock(*this);
    closeArchive();
    archiveOpen_ = openArchive();
  }

  char body[192];
  if (updateCancel_) {
    // Kept, not discarded: each comic was committed as it arrived, and the
    // header above now names the last one that really did. The next update
    // starts from there.
    snprintf(body, sizeof(body), "Stopped. %d of %d kept; the rest are still there next time.%s", fetched_, toGet,
             updateHomeCancel_ ? " Home works now." : "");
    showNotice("STOPPED", body);
  } else if (fetched_ == 0) {
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
  tokens.headerHeight = xkcdui::kHeaderBand;  // the fork's standard band; see XkcdScreens.h
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
      const fui::Rect view = xkcdui::readerViewport(fui::GfxRendererTarget(renderer).deviceContext());
      const xkcd::Rendition r = xkcd::renditionFor(comic_, at_.lens);
      // The map comes out of the same placement the artwork was drawn from,
      // rather than being recomputed from the comic. Two functions deriving
      // the same geometry separately is the defect this fork has hit more than
      // any other.
      const xkcd::Placement p = xkcd::place(r, view.width, view.height, at_);
      model.imageW = r.width;
      model.imageH = r.height;
      model.viewX = p.scrollX;
      model.viewY = p.scrollY;
      model.viewW = p.visibleW;
      model.viewH = p.visibleH;
      model.hasOverview = comic_.hasOverview();
      model.showingWhole = at_.lens == xkcd::Lens::Whole;
      model.hasAlt = alt_[0] != '\0';
      xkcdui::buildReaderBar(screen, model);

      // The two halves that move you through the comic, registered from the
      // same function the artwork was placed against. Registered after the bar
      // so the bar's own control wins where they overlap.
      // Always registered, not only when the comic pans: forward now always
      // has somewhere to go, so a dead tap half would be the odd case.
      frame.hit(xkcdui::readerPanUpHalf(fui::GfxRendererTarget(renderer).deviceContext()), xkcdui::ActionPanUp);
      frame.hit(xkcdui::readerPanDownHalf(fui::GfxRendererTarget(renderer).deviceContext()), xkcdui::ActionPanDown);
      break;
    }
    case View::Number: {
      xkcdui::NumberModel model;
      model.typed = typed_;
      model.maxNum = archiveOpen_ ? archive_.maxNum() : 0;
      if (archiveOpen_) {
        xkcd::Comic first{};
        if (archive_.at(*indexSrc_, 0, first)) model.firstNum = first.num;
      }
      model.valid = typedIsOnCard();
      xkcdui::buildNumber(screen, model);
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
