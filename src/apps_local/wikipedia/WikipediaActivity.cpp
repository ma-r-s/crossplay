#include "WikipediaActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../CrossPointSettings.h"
#include "../../SilentRestart.h"
#include "../../activities/RenderLock.h"
#include "../../activities/reader/EpubReaderUtils.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kTag = "WIKI";
constexpr const char* kCacheRoot = "/.crosspoint/wikipedia";
constexpr const char* kCacheMarker = "/.crosspoint/wikipedia/pack";
constexpr const char* kLruPath = "/.crosspoint/wikipedia/lru";
constexpr const char* kInstallUrl = "https://crossplay.ma-r-s.com/wikipedia";
constexpr const char* kInstallUrlShown = "crossplay.ma-r-s.com/wikipedia";
constexpr int kCachedArticles = 32;
constexpr int16_t kPageSide = 18;
constexpr int16_t kPageTop = 6;
constexpr size_t kBuildMinHeap = 40 * 1024;

// "7,238,251"
void withCommas(char* out, const size_t cap, const uint32_t n) {
  char raw[16];
  snprintf(raw, sizeof(raw), "%lu", static_cast<unsigned long>(n));
  const size_t len = strlen(raw);
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap; ++i) {
    if (i > 0 && (len - i) % 3 == 0 && o + 1 < cap) out[o++] = ',';
    out[o++] = raw[i];
  }
  out[o] = '\0';
}

// "2026-05-13" -> "May 2026"
void snapshotWords(char* out, const size_t cap, const std::string& snapshot) {
  static const char* const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                        "July",    "August",   "September", "October", "November", "December"};
  int year = 0, month = 0;
  if (sscanf(snapshot.c_str(), "%d-%d", &year, &month) == 2 && month >= 1 && month <= 12) {
    snprintf(out, cap, "%s %d", kMonths[month - 1], year);
  } else {
    snprintf(out, cap, "%s", snapshot.c_str());
  }
}

bool ensureDir(const char* path) { return Storage.exists(path) || Storage.mkdir(path); }

bool writeFile(const char* path, const std::string& data) {
  HalFile file;
  if (!Storage.openFileForWrite(kTag, path, file)) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
  size_t done = 0;
  while (done < data.size()) {
    const size_t want = std::min<size_t>(4096, data.size() - done);
    if (file.write(p + done, want) != want) return false;
    done += want;
  }
  return true;
}

std::string readSmallFile(const char* path) {
  HalFile file;
  if (!Storage.exists(path) || !Storage.openFileForRead(kTag, path, file)) return {};
  const size_t size = file.size();
  if (size == 0 || size > 4096) return {};
  std::string out(size, '\0');
  if (file.read(out.data(), size) != static_cast<int>(size)) return {};
  return out;
}

// Removes a cache directory and the files it holds (one level deep is all it has).
void removeArticleCache(const std::string& dir) {
  const char* const names[] = {"/article.html", "/sections/0.bin", "/sections/0.bin.part"};
  for (const char* n : names) {
    const std::string p = dir + n;
    if (Storage.exists(p.c_str())) Storage.remove(p.c_str());
  }
  const std::string sections = dir + "/sections";
  if (Storage.exists(sections.c_str())) Storage.rmdir(sections.c_str());
  if (Storage.exists(dir.c_str())) Storage.rmdir(dir.c_str());
}

}  // namespace

std::unique_ptr<Activity> WikipediaActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WikipediaActivity>(renderer, mappedInput);
}

WikipediaActivity::~WikipediaActivity() = default;

void WikipediaActivity::onEnter() {
  Activity::onEnter();
  packOpen_ = pack_.open();
  if (packOpen_) {
    pack_.loadState(state_);
    char count[16];
    withCommas(count, sizeof(count), pack_.manifest().articles);
    char when[32];
    snapshotWords(when, sizeof(when), pack_.manifest().snapshot);
    footer_ = std::string(count) + " articles, " + when;
    if (pack_.shardsPresent() < pack_.shardsTotal()) {
      char line[64];
      snprintf(line, sizeof(line), "%d of %d parts on the card", pack_.shardsPresent(), pack_.shardsTotal());
      partsLine_ = line;
    }
    // A cache laid out from another snapshot must not answer for this one.
    const std::string marker = pack_.manifest().pack + " " + pack_.manifest().snapshot;
    if (readSmallFile(kCacheMarker) != marker) {
      const std::string lru = readSmallFile(kLruPath);
      size_t pos = 0;
      while (pos < lru.size()) {
        const size_t nl = lru.find('\n', pos);
        const std::string id = lru.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!id.empty()) removeArticleCache(std::string(kCacheRoot) + "/" + id);
        if (nl == std::string::npos) break;
        pos = nl + 1;
      }
      ensureDir("/.crosspoint");
      ensureDir(kCacheRoot);
      writeFile(kLruPath, "");
      writeFile(kCacheMarker, marker);
    }
    go(View::Search);
  } else {
    enterInstall();
  }
}

void WikipediaActivity::onExit() {
  if (usbActive_) leaveInstall();
  if (packOpen_) saveState();
  closeArticle();
  pack_.close();
  Activity::onExit();
}

bool WikipediaActivity::skipLoopDelay() {
  return view_ == View::Article && section_ && section_->isBuilding() && !section_->isBuildComplete();
}

void WikipediaActivity::go(const View next) {
  view_ = next;
  interactionsReady_ = false;
  if (next == View::Search) kbGate_.arm();
  requestUpdate();
}

void WikipediaActivity::showNotice(const char* headline, const char* body, const char* actionLabel,
                                   const fui::ActionId action) {
  noticeHead_ = headline;
  noticeBody_ = body;
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  go(View::Notice);
}

// ---------------------------------------------------------------- search

void WikipediaActivity::refreshResults() {
  results_.clear();
  if (query_.empty() || !packOpen_) return;
  pack_.prefix(query_, wikiui::kMaxResults, results_);
}

void WikipediaActivity::handleKey(const int value) {
  const fui::KeyboardLayout& layout =
      fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted_, symbols_, true, false);
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      shifted_ = !shifted_;
      break;
    case fui::QWERTY_KEY_MODE:
      symbols_ = !symbols_;
      shifted_ = false;
      break;
    case fui::QWERTY_KEY_LANG:
      return;
    case fui::QWERTY_KEY_ENTER:
      if (!results_.empty()) {
        openLocator(results_.front().locator, 0, "");
        return;
      }
      if (!query_.empty()) openTitle(query_);
      return;
    case fui::QWERTY_KEY_BACKSPACE: {
      if (query_.empty()) break;
      size_t pos = query_.size() - 1;
      while (pos > 0 && (static_cast<uint8_t>(query_[pos]) & 0xC0) == 0x80) --pos;
      query_.erase(pos);
      refreshResults();
      break;
    }
    default: {
      const char* out = fui::keyboardOutputFor(layout, static_cast<int16_t>(value));
      if (!out) return;
      if (query_.size() + strlen(out) > 120) return;
      query_ += out;
      if (shifted_ && !symbols_) shifted_ = false;
      refreshResults();
      break;
    }
  }
  requestUpdate();
}

void WikipediaActivity::openTitle(const std::string& title) {
  wikipedia::IndexEntry entry;
  if (!pack_.find(title, entry)) {
    showNotice("NOT FOUND", tr(STR_WIKI_NO_MATCH), "BACK", wikiui::ActionBack);
    return;
  }
  if (!pack_.onCard(entry.locator)) {
    showNotice("NOT YET", tr(STR_WIKI_NOT_ON_CARD), "BACK", wikiui::ActionBack);
    return;
  }
  openLocator(entry.locator, 0, "");
}

void WikipediaActivity::openRandom() {
  wikipedia::IndexEntry entry;
  if (!pack_.random(entry)) {
    showNotice("NOTHING YET", tr(STR_WIKI_NOT_ON_CARD), "BACK", wikiui::ActionBack);
    return;
  }
  openLocator(entry.locator, 0, "");
}

fui::Rect WikipediaActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const fui::KeyboardLayout& layout =
      fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted_, symbols_, true, false);
  const int rows = layout.rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int height = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  const int x = (pageWidth - width) / 2;
  const int y = pageHeight - 8 - height;
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

// The keyboard has its own table: it registers more hit rects than a toybox
// screen holds, and it is routed first.
void WikipediaActivity::drawKeyboard() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  kbInteractions_.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<56> frame(target, device, noInput, kbInteractions_);

  fui::KeyboardProps props;
  const fui::KeyboardLayout& layout =
      fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted_, symbols_, true, false);
  props.layout = &layout;
  props.keyAction = wikiui::ActionKey;
  props.okLabel = "GO";
  props.shiftLabel = tr(STR_KEY_SHIFT);
  props.modeLabel = symbols_ ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.selectedIndex = -1;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  const fui::Rect kb = keyboardRect();
  props.bottomHitOverflow = static_cast<int16_t>(renderer.getScreenHeight() - kb.bottom());
  fui::keyboard(frame, kb, props);
  kbInteractions_.publish();
  kbGate_.markBuilt();
}

// --------------------------------------------------------------- article

bool WikipediaActivity::stageArticle(const uint32_t locator) {
  const char* error = nullptr;
  if (!pack_.readArticle(locator, article_, &error)) {
    LOG_ERR(kTag, "article %lu: %s", static_cast<unsigned long>(locator), error ? error : "");
    return false;
  }
  cacheDir_ = std::string(kCacheRoot) + "/" + std::to_string(locator);
  if (!ensureDir("/.crosspoint") || !ensureDir(kCacheRoot) || !ensureDir(cacheDir_.c_str())) return false;
  const std::string html = cacheDir_ + "/article.html";
  bool fresh = true;
  {
    HalFile file;
    if (Storage.exists(html.c_str()) && Storage.openFileForRead(kTag, html.c_str(), file)) {
      fresh = file.size() != article_.xhtml.size();
    }
  }
  if (fresh && !writeFile(html.c_str(), article_.xhtml)) return false;
  // Most recently used first; the tail is pruned.
  std::string lru = readSmallFile(kLruPath);
  const std::string id = std::to_string(locator);
  std::string rebuilt = id + "\n";
  int kept = 1;
  size_t pos = 0;
  while (pos < lru.size()) {
    const size_t nl = lru.find('\n', pos);
    const std::string entry = lru.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (!entry.empty() && entry != id) {
      if (kept < kCachedArticles) {
        rebuilt += entry + "\n";
        ++kept;
      } else {
        removeArticleCache(std::string(kCacheRoot) + "/" + entry);
      }
    }
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  writeFile(kLruPath, rebuilt);
  return true;
}

bool WikipediaActivity::openLocator(const uint32_t locator, const int page, const std::string& anchor) {
  // The render task may be mid-layout on the old section; the lock is what the
  // reader takes before touching its own.
  RenderLock lock;
  closeArticle();
  if (!stageArticle(locator)) {
    showNotice("SORRY", tr(STR_WIKI_OPEN_FAILED), "BACK", wikiui::ActionBack);
    return false;
  }
  locator_ = locator;
  targetPage_ = page;
  pendingAnchor_ = anchor;
  buildFailed_ = false;
  headingPages_.assign(article_.headings.size(), -1);
  headingPagesFinal_ = false;
  state_.touch({article_.title, locator});
  state_.current = {article_.title, locator};
  state_.currentPage = page;
  saveState();
  go(View::Article);
  return true;
}

void WikipediaActivity::closeArticle() {
  section_.reset();
  links_.clear();
  article_ = wikipedia::Article{};
}

// Lays out up to the page or anchor wanted, creating the section on first use.
// Called from render(), which owns the geometry the spec is made from.
bool WikipediaActivity::ensureBuilt() {
  if (!section_) return false;
  while (section_->isBuilding() && !section_->isBuildComplete()) {
    if (!pendingAnchor_.empty()) {
      if (section_->findAnchor(pendingAnchor_)) break;
    } else if (section_->pageCount > targetPage_) {
      break;
    }
    if (!section_->buildSomeMore(8)) {
      buildFailed_ = true;
      return false;
    }
  }
  if (!pendingAnchor_.empty()) {
    if (const auto p = section_->findAnchor(pendingAnchor_)) targetPage_ = *p;
    pendingAnchor_.clear();
  }
  if (section_->pageCount > 0 && targetPage_ >= section_->pageCount) targetPage_ = section_->pageCount - 1;
  if (targetPage_ < 0) targetPage_ = 0;
  section_->currentPage = targetPage_;
  return true;
}

void WikipediaActivity::turnPage(const int delta) {
  RenderLock lock;
  if (!section_) return;
  int next = section_->currentPage + delta;
  if (next < 0) next = 0;
  if (section_->isBuildComplete() && next >= section_->pageCount) return;
  targetPage_ = next;
  state_.currentPage = next;
  requestUpdate();
}

void WikipediaActivity::pushHistory() {
  if (!section_) return;
  if (static_cast<int>(history_.size()) >= kHistoryDepth) history_.erase(history_.begin());
  history_.push_back({locator_, section_->currentPage});
}

void WikipediaActivity::popHistory() {
  if (history_.empty()) {
    closeArticle();
    go(View::Search);
    return;
  }
  const Visit back = history_.back();
  history_.pop_back();
  openLocator(back.locator, back.page, "");
}

void WikipediaActivity::refreshHeadingPages() {
  if (!section_ || headingPagesFinal_) return;
  bool all = true;
  for (size_t i = 0; i < headingPages_.size(); ++i) {
    if (headingPages_[i] >= 0) continue;
    const std::string anchor = "s" + std::to_string(i + 1);
    if (const auto p = section_->findAnchor(anchor)) {
      headingPages_[i] = *p;
    } else {
      all = false;
    }
  }
  headingPagesFinal_ = all && section_->isBuildComplete();
}

int WikipediaActivity::headingForPage(const int page) const {
  int best = -1;
  for (size_t i = 0; i < headingPages_.size(); ++i) {
    if (headingPages_[i] >= 0 && headingPages_[i] <= page) best = static_cast<int>(i);
  }
  return best;
}

void WikipediaActivity::renderArticle(toybox::Screen& screen) {
  char left[32] = "";
  char right[96] = "";
  if (section_) {
    refreshHeadingPages();
    const int shown = section_->currentPage + 1;
    if (section_->isBuildComplete()) {
      snprintf(left, sizeof(left), "%d of %d", shown, section_->pageCount);
    } else {
      snprintf(left, sizeof(left), "%d", shown);
    }
    const int h = headingForPage(section_->currentPage);
    if (h >= 0 && h < static_cast<int>(article_.headings.size())) {
      snprintf(right, sizeof(right), "%s", article_.headings[h].c_str());
    }
  }
  wikiui::ArticleChromeModel model;
  model.title = article_.title.c_str();
  model.footerLeft = left;
  model.footerRight = right;
  model.contents = !article_.headings.empty();
  const fui::Rect body = wikiui::buildArticleChrome(screen, model);

  const int pageX = body.x + kPageSide;
  const int pageY = body.y + kPageTop;
  const uint16_t viewportWidth = static_cast<uint16_t>(body.width - kPageSide * 2);
  const uint16_t viewportHeight = static_cast<uint16_t>(body.height - kPageTop);

  if (!section_) {
    ReaderRenderSpec spec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);
    spec.imageRendering = 2;
    spec.embeddedStyle = false;
    std::vector<std::string> anchors;
    if (article_.xhtml.size() > kFreshPageBytes) {
      anchors.reserve(article_.headings.size());
      for (size_t i = 0; i < article_.headings.size(); ++i) anchors.push_back("s" + std::to_string(i + 1));
    }
    section_ = makeUniqueNoThrow<Section>(cacheDir_ + "/article.html", cacheDir_, 0, renderer, std::move(anchors),
                                          false);
    if (!section_) {
      LOG_ERR(kTag, "OOM: Section");
      buildFailed_ = true;
    } else if (!section_->loadSectionFile(spec) || section_->isPartial()) {
      if (!section_->startBuild(spec, nullptr)) {
        LOG_ERR(kTag, "layout could not start");
        buildFailed_ = true;
      }
    }
  }
  if (!buildFailed_) ensureBuilt();

  links_.clear();
  if (section_ && !buildFailed_ && section_->pageCount > 0) {
    auto page = section_->loadPage(section_->currentPage);
    if (page) {
      links_ = std::move(page->links);
      linkMarginLeft_ = pageX;
      linkMarginTop_ = pageY;
      const int fontId = SETTINGS.getReaderFontId();
      auto* fcm = renderer.getFontCacheManager();
      auto scope = fcm->createPrewarmScope();
      page->render(renderer, fontId, pageX, pageY);
      scope.endScanAndPrewarm();
      page->render(renderer, fontId, pageX, pageY);
    }
  }
  // The footer was drawn before the page count was known on first open; the
  // next render carries the right numbers, and the trickle asks for one.
}

void WikipediaActivity::saveState() {
  if (!packOpen_) return;
  pack_.saveState(state_);
}

void WikipediaActivity::pruneCache() {}

// --------------------------------------------------------------- install

void WikipediaActivity::enterInstall() {
  // Everything the page needs to know, written before the card changes hands;
  // then no file of ours stays open while the host owns the card.
  uint64_t free = 0;
  const bool freeKnown = Storage.freeBytes(free);
  pack_.writeInstallJson(freeKnown ? static_cast<int64_t>(free) : -1, CROSSPOINT_VERSION, "X4 Pro");
  closeArticle();
  if (packOpen_) saveState();
  pack_.close();
  packOpen_ = false;
  view_ = View::Install;
  interactionsReady_ = false;
  requestUpdateAndWait();
  if (!Storage.beginUsbDrive()) {
    LOG_ERR(kTag, "USB drive did not start");
    stage_ = wikiui::InstallModel::Stage::Failed;
  } else {
    usbActive_ = true;
    stage_ = wikiui::InstallModel::Stage::Waiting;
  }
  stageAt_ = millis();
  requestUpdate();
}

void WikipediaActivity::leaveInstall() {
  if (!usbActive_) return;
  Storage.endUsbDrive();
  usbActive_ = false;
}

// ------------------------------------------------------------------ loop

void WikipediaActivity::routeAction(const int action, const int value) {
  switch (action) {
    case wikiui::ActionResult:
      if (value >= 0 && value < static_cast<int>(results_.size())) {
        const auto entry = results_[value];
        if (!pack_.onCard(entry.locator)) {
          showNotice("NOT YET", tr(STR_WIKI_NOT_ON_CARD), "BACK", wikiui::ActionBack);
        } else {
          history_.clear();
          openLocator(entry.locator, 0, "");
        }
      }
      return;
    case wikiui::ActionRecent:
      if (value >= 0 && value < static_cast<int>(state_.recent.size())) {
        history_.clear();
        openLocator(state_.recent[value].locator, 0, "");
      }
      return;
    case wikiui::ActionContinue:
      history_.clear();
      openLocator(state_.current.locator, state_.currentPage, "");
      return;
    case wikiui::ActionRandom:
      history_.clear();
      openRandom();
      return;
    case wikiui::ActionClear:
      query_.clear();
      results_.clear();
      requestUpdate();
      return;
    case wikiui::ActionInstall:
      enterInstall();
      return;
    case wikiui::ActionContents:
      if (view_ == View::Article && !article_.headings.empty()) {
        refreshHeadingPages();
        const int h = section_ ? headingForPage(section_->currentPage) : 0;
        contentsFirst_ = h > 0 ? (h / wikiui::kContentsRows) * wikiui::kContentsRows : 0;
        go(View::Contents);
      }
      return;
    case wikiui::ActionHeading:
      if (value >= 0 && value < static_cast<int>(article_.headings.size())) {
        pendingAnchor_ = "s" + std::to_string(value + 1);
        if (value < static_cast<int>(headingPages_.size()) && headingPages_[value] >= 0) {
          targetPage_ = headingPages_[value];
          pendingAnchor_.clear();
        }
        go(View::Article);
      }
      return;
    case wikiui::ActionClose:
      go(View::Article);
      return;
    case wikiui::ActionRetry:
      enterInstall();
      return;
    case wikiui::ActionBack:
      if (section_) {
        go(View::Article);
      } else {
        go(View::Search);
      }
      return;
    default:
      return;
  }
}

void WikipediaActivity::loop() {
  if (view_ == View::Install) {
    if (restartRequested_) return;
    const auto state = Storage.usbDriveState();
    if (usbActive_) {
      if (state == UsbDriveState::Connected && stage_ != wikiui::InstallModel::Stage::Connected) {
        stage_ = wikiui::InstallModel::Stage::Connected;
        requestUpdate();
      } else if (state == UsbDriveState::Ejected || state == UsbDriveState::Disconnected) {
        // The host let go: come back into this app on the pack it wrote.
        restartRequested_ = true;
        Storage.endUsbDrive();
        usbActive_ = false;
        delay(20);
        restartToAppAfterStorageHandoff();
        return;
      } else if (state == UsbDriveState::IoError && stage_ != wikiui::InstallModel::Stage::Failed) {
        LOG_ERR(kTag, "USB drive I/O error");
        Storage.disconnectUsbDriveHost();
        stage_ = wikiui::InstallModel::Stage::Failed;
        requestUpdate();
      }
      if (stage_ == wikiui::InstallModel::Stage::Waiting && millis() - stageAt_ >= kHostWaitMs) {
        restartRequested_ = true;
        Storage.endUsbDrive();
        usbActive_ = false;
        restartToAppAfterStorageHandoff();
        return;
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
      restartRequested_ = true;
      if (usbActive_) {
        Storage.endUsbDrive();
        usbActive_ = false;
      }
      restartToAppAfterStorageHandoff();
      return;
    }
    fui::InputSnapshot input;
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty) && interactionsReady_) {
      input.touchReleased = true;
      input.touchX = static_cast<int16_t>(tx);
      input.touchY = static_cast<int16_t>(ty);
      const fui::ActionEvent ev = interactions_.route(input);
      routeAction(static_cast<int>(ev.action), static_cast<int>(ev.value));
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Search:
        if (!query_.empty()) {
          query_.clear();
          results_.clear();
          requestUpdate();
        } else {
          shelf::leave(renderer, mappedInput);
        }
        return;
      case View::Article:
        saveState();
        popHistory();
        return;
      case View::Contents:
        go(View::Article);
        return;
      case View::Notice:
        routeAction(wikiui::ActionBack, 0);
        return;
      default:
        return;
    }
  }

  if (view_ == View::Article) {
    // The rest of the article lays out between renders, a couple of pages a
    // tick, so a page turn is a seek rather than a wait.
    // Under the render lock, as the reader does: render() lays out on the
    // other task, and two callers inside one expat parser end in a mismatched
    // tag that no document contains.
    if (section_ && section_->isBuilding() && !section_->isBuildComplete() && !buildFailed_ &&
        !RenderLock::peek() && ESP.getFreeHeap() > kBuildMinHeap) {
      RenderLock lock;
      if (section_->isBuilding() && !section_->isBuildComplete()) {
        if (!section_->buildSomeMore(2)) {
          buildFailed_ = true;
        } else if (section_->isBuildComplete()) {
          requestUpdate();
        }
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      turnPage(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      turnPage(1);
      return;
    }
  } else if (view_ == View::Contents) {
    const int rows = wikiui::kContentsRows;
    const int count = static_cast<int>(article_.headings.size());
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (contentsFirst_ + rows < count) {
        contentsFirst_ += rows;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (contentsFirst_ > 0) {
        contentsFirst_ = std::max(0, contentsFirst_ - rows);
        requestUpdate();
      }
      return;
    }
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);

  if (view_ == View::Search && kbGate_.revealed()) {
    const fui::ActionEvent key = kbInteractions_.routePublished(input);
    if (key.action == wikiui::ActionKey) {
      handleKey(static_cast<int>(key.value));
      return;
    }
  }
  if (!interactionsReady_) return;

  if (view_ == View::Article) {
    if (!links_.empty()) {
      const auto* link = EpubReaderUtils::linkAtPoint(links_, tapX, tapY, linkMarginLeft_, linkMarginTop_);
      if (link) {
        wikipedia::IndexEntry entry;
        if (!pack_.find(link->href, entry)) {
          showNotice("NOT FOUND", tr(STR_WIKI_NO_MATCH), "BACK", wikiui::ActionBack);
        } else if (!pack_.onCard(entry.locator)) {
          showNotice("NOT YET", tr(STR_WIKI_NOT_ON_CARD), "BACK", wikiui::ActionBack);
        } else {
          pushHistory();
          openLocator(entry.locator, 0, "");
        }
        return;
      }
    }
    const fui::ActionEvent ev = interactions_.route(input);
    if (ev.action != fui::NO_ACTION) {
      routeAction(static_cast<int>(ev.action), static_cast<int>(ev.value));
      return;
    }
    // Tap zones: the left third turns back, the rest turns forward.
    if (tapY > toybox::kChromeHeight) turnPage(tapX < renderer.getScreenWidth() / 3 ? -1 : 1);
    return;
  }

  const fui::ActionEvent ev = interactions_.route(input);
  routeAction(static_cast<int>(ev.action), static_cast<int>(ev.value));
}

// ---------------------------------------------------------------- render

void WikipediaActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const bool prose = view_ == View::Article || view_ == View::Contents;
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, prose ? toybox::readingChromeFaces() : toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Search: {
      wikiui::SearchModel model;
      model.query = query_.c_str();
      model.resultCount = static_cast<int>(std::min<size_t>(results_.size(), wikiui::kMaxResults));
      for (int i = 0; i < model.resultCount; ++i) {
        model.results[i].title = results_[i].title.c_str();
        model.results[i].redirect = results_[i].redirect;
      }
      model.noMatch = !query_.empty() && results_.empty();
      model.continueTitle = state_.current.title.empty() ? nullptr : state_.current.title.c_str();
      model.recentCount = static_cast<int>(std::min<size_t>(state_.recent.size(), wikiui::kMaxRecent));
      for (int i = 0; i < model.recentCount; ++i) model.recent[i].title = state_.recent[i].title.c_str();
      model.footer = footer_.c_str();
      model.partsLine = partsLine_.empty() ? nullptr : partsLine_.c_str();
      const fui::Rect kb = keyboardRect();
      model.keyboardHeight = static_cast<int16_t>(renderer.getScreenHeight() - kb.y + 8);
      wikiui::buildSearch(screen, model);
      break;
    }
    case View::Article:
      renderArticle(screen);
      break;
    case View::Contents: {
      std::vector<const char*> names;
      names.reserve(article_.headings.size());
      for (const auto& h : article_.headings) names.push_back(h.c_str());
      wikiui::ContentsModel model;
      model.title = article_.title.c_str();
      model.headings = names.data();
      model.count = static_cast<int>(names.size());
      model.current = section_ ? headingForPage(section_->currentPage) : -1;
      model.first = contentsFirst_;
      wikiui::buildContents(screen, model);
      break;
    }
    case View::Install: {
      wikiui::InstallModel model;
      model.stage = stage_;
      model.url = kInstallUrlShown;
      model.partsLine = partsLine_.empty() ? nullptr : partsLine_.c_str();
      const fui::Rect qr = wikiui::buildInstall(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, kInstallUrl);
      break;
    }
    case View::Notice: {
      wikiui::NoticeModel model;
      model.headline = noticeHead_.c_str();
      model.body = noticeBody_.c_str();
      model.actionLabel = noticeAction_;
      model.action = noticeActionId_;
      wikiui::buildNotice(screen, model);
      break;
    }
  }
  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Wikipedia");
  if (view_ == View::Search) drawKeyboard();
  renderer.displayBuffer();
}
