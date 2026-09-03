#include "OpdsBookBrowserActivity.h"

#include <Arduino.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <OpenSearchParser.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "DevMode.h"
#include "MappedInputManager.h"
#include "OpdsDetailActivity.h"
#include "OpdsLanguages.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/OpdsFilterActivity.h"
#include "activities/settings/OpdsServerListActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/search32.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr fui::ActionId ACTION_SEARCH = 2;
constexpr fui::ActionId ACTION_SETTINGS = 4;
constexpr fui::ActionId ACTION_CANCEL = 3;
constexpr fui::ActionId ACTION_SAVED_DONE = 5;
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;
// While no bytes have arrived there is no bar to advance, so the only thing
// that can say "alive" is the tick -- and at the 5s bar interval it reads as a
// still screen. abortPoll() calls the progress callback every 50ms throughout
// the wait, so the repaint is available; it was simply never asked for.
constexpr unsigned long DOWNLOAD_WAIT_UPDATE_MS = 1000;

}  // namespace

OpdsBookBrowserActivity::OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 OpdsServer server)
    : Activity("OpdsBookBrowser", renderer, mappedInput),
      UiAppHost(renderer),
      buttonNavigator(),
      server(std::move(server)) {}

std::unique_ptr<Activity> OpdsBookBrowserActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  const auto& servers = OPDS_STORE.getServers();
  if (servers.empty()) {
    // Nothing to open yet, so the list is the only useful destination.
    return std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true);
  }
  // Straight into the catalog last used -- never a picker. Clamped because
  // catalogs get deleted.
  const size_t index = SETTINGS.opdsLastServer < servers.size() ? SETTINGS.opdsLastServer : 0;
  return std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[index]);
}

void OpdsBookBrowserActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  searchTemplate = "";
  currentPath = "";
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);

  listNav.reset();
  resetUi();
  app.on(ACTION_ROW, &OpdsBookBrowserActivity::onRowEvent, this);
  app.on(ACTION_SEARCH, &OpdsBookBrowserActivity::onSearchEvent, this);
  app.on(ACTION_SETTINGS, &OpdsBookBrowserActivity::onSettingsEvent, this);
  app.on(ACTION_CANCEL, &OpdsBookBrowserActivity::onCancelEvent, this);
  app.on(ACTION_SAVED_DONE, &OpdsBookBrowserActivity::onSavedDoneEvent, this);
  app.setScreen(&OpdsBookBrowserActivity::rootScreen, this);
  requestUpdate();

  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
  Activity::onExit();
  entries.clear();
  navigationHistory.clear();

  // Not ours to put down if Developer Mode brought it up: this branch reboots
  // the device, and doing that every time the OPDS browser exits while dev mode
  // is on is indistinguishable from a crash.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OpdsBookBrowserActivity::activateSelected() {
  if (entries.empty() || selectorIndex < 0 || selectorIndex >= static_cast<int>(entries.size())) return;
  const auto& entry = entries[selectorIndex];
  entry.type == OpdsEntryType::BOOK ? openDetail(entry) : navigateToEntry(entry);
}

void OpdsBookBrowserActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  if (self->state != BrowserState::BROWSING) return;
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->entries.size())) return;
  self->selectorIndex = event.value;
  // The tapped row leaves the screen either way (new feed or download view);
  // a lingering tap flash would gray an unrelated row on the next list.
  self->app.clearTapFlash();
  self->activateSelected();
}

void OpdsBookBrowserActivity::onSettingsEvent(const fui::ActionEvent&, void* user) {
  auto* const self = static_cast<OpdsBookBrowserActivity*>(user);
  // ERROR as well as BROWSING: see screenHeader(). Still refused mid-fetch and
  // mid-download, where swapping the catalog underneath the work would strand
  // it.
  if (self->state != BrowserState::BROWSING && self->state != BrowserState::ERROR) return;
  self->app.clearTapFlash();
  self->openSettings();
}

void OpdsBookBrowserActivity::openSettings() {
  auto* const self = this;
  // Catalog choice and language filters are one screen: they are the same
  // question -- what am I searching -- asked two ways.
  self->startActivityForResult(
      std::make_unique<OpdsFilterActivity>(self->renderer, self->mappedInput), [self](const ActivityResult&) {
        // The catalog may have changed underneath us.
        const auto& servers = OPDS_STORE.getServers();
        const size_t index = SETTINGS.opdsLastServer < servers.size() ? SETTINGS.opdsLastServer : 0;
        if (!servers.empty() && servers[index].url != self->server.url) {
          // Switching catalog re-enters Get Books, so the new catalog's
          // own shape decides the screen: rows, or the keyboard.
          activityManager.goToBrowser();
          return;
        }
        self->rebuildRowItems();
        self->requestUpdate();
      });
}

void OpdsBookBrowserActivity::onSearchEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  if (self->state != BrowserState::BROWSING) return;
  self->app.clearTapFlash();
  self->launchSearch();
}

void OpdsBookBrowserActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  if (self->state != BrowserState::DOWNLOADING) return;
  self->app.clearTapFlash();
  self->cancelDownload = true;
}

void OpdsBookBrowserActivity::onSavedDoneEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  if (self->state != BrowserState::SAVED) return;
  self->app.clearTapFlash();
  self->leaveSavedScreen();
}

void OpdsBookBrowserActivity::leaveSavedScreen() {
  state = BrowserState::BROWSING;
  savedName.clear();
  savedFolder.clear();
  savedDwell.arm();
  // The list's first rows land on the pixels the verdict just occupied, so
  // shut routing until the panel has SHOWN the list. Same gate the rest of
  // the fork uses for a screen whose meaning changed under a finger; nothing
  // new is invented here.
  resetUi();
  requestUpdate();
}

void OpdsBookBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION || state == BrowserState::SEARCH_INPUT) {
    return;
  }

  if (state == BrowserState::ERROR) {
    // The settings button gets first refusal on the touch. The rest of this
    // screen is one big "tap to retry" target, so an unrouted tap on the
    // button would just re-run the fetch that failed and leave the reader
    // exactly where they were.
    const auto route = routeTouch(mappedInput);
    if (route.routed) {
      if (app.invalidated()) requestUpdate();
      if (route) return;  // dispatched to onSettingsEvent
    }

    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed(currentPath);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state == BrowserState::CHECK_WIFI ? onGoHome() : navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    // Nothing to do here: this function does not run during a download at all.
    // downloadBook() blocks inside the result handler that ActivityManager's
    // own loop() invokes, so the activity's loop() cannot be re-entered until
    // the transfer returns. Anything the wait needs must be driven from the
    // progress callback or computed when the screen is built.
    return;
  }

  if (state == BrowserState::SAVED) {
    // Nothing here acts until the panel has SHOWN the verdict. That is not
    // politeness, it is the only thing standing between this screen and the
    // input the download screen was still collecting: the progress callback
    // pumps mappedInput itself, so a Back held across the last chunk, or a
    // finger resting on Cancel, arrives here as a release the instant this
    // state begins. routingReady() is the fork's existing reveal gate, armed
    // by the resetUi() in downloadBook(); it latches open on the first paint.
    const bool shown = routingReady();

    if (shown) {
      const auto route = routeTouch(mappedInput);
      if (route.routed) {
        if (app.invalidated()) requestUpdate();
        if (route) return;  // dispatched to onSavedDoneEvent
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
          mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        leaveSavedScreen();
        return;
      }
    }

    // The dwell counts only time the reader could actually have spent looking:
    // shown, and with nobody's hand on the glass. A screen that starts its
    // timer when the code decides to draw is measuring the refresh, and one
    // that keeps counting under a thumb is measuring nothing at all.
    int touchX = 0;
    int touchY = 0;
    const bool touching = mappedInput.isScreenTouchHeld(touchX, touchY);
    if (savedDwell.expired(millis(), shown, touching, SAVED_DWELL_MS)) leaveSavedScreen();
    return;
  }

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelected();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (!searchTemplate.empty() && selectorIndex == 0) launchSearch();
    }

    // Touch goes through the FreeInkApp: render() registered every tap target
    // (rows, header search button); route the snapshot and let the registered
    // handlers dispatch.
    const auto route = routeTouch(mappedInput);
    if (route.routed) {
      // No pressed-state repaint: the render it triggers would drop a slow
      // tap's release inside the uiReady window (tap-to-activate needed two
      // taps), and it costs a second e-ink refresh per tap.
      if (app.invalidated()) requestUpdate();
      if (route) return;  // dispatched to onRowEvent/onSearchEvent
      if (state != BrowserState::BROWSING) return;
    }

    if (!entries.empty()) {
      // Swipes scroll the viewport; the selection stays put (it may scroll
      // off-screen) and button navigation pulls the view back to it.
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
        const int delta = swipe == MappedInputManager::SwipeDir::Up ? listNav.visibleRows : -listNav.visibleRows;
        if (listNav.scrollBy(delta, static_cast<int>(entries.size()))) requestUpdate();
        return;
      }

      const auto moveSelection = [this](const int index) {
        selectorIndex = index;
        listNav.selected = index;
        listNav.follow(static_cast<int>(entries.size()));
        requestUpdate();
      };
      buttonNavigator.onNextRelease(
          [this, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, entries.size())); });
      buttonNavigator.onPreviousRelease(
          [this, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, entries.size())); });
      buttonNavigator.onNextContinuous([this, &moveSelection] {
        moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), listNav.visibleRows));
      });
      buttonNavigator.onPreviousContinuous([this, &moveSelection] {
        moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), listNav.visibleRows));
      });
    }
  }
}

void OpdsBookBrowserActivity::rootScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  switch (self->state) {
    case BrowserState::BROWSING:
      self->buildBrowsingScreen(screen);
      break;
    case BrowserState::DOWNLOADING:
      self->buildDownloadScreen(screen);
      break;
    case BrowserState::SAVED:
      self->buildSavedScreen(screen);
      break;
    default:
      self->buildStatusScreen(screen);
      break;
  }
}

// Shared chrome for every state: reserve the firmware's button-hint band and
// draw the themed header (padding, centering, and rule come from the theme).
void OpdsBookBrowserActivity::screenHeader(UiScreen& screen, const bool withSearch) {
  screen.takeBottom(static_cast<int16_t>(UITheme::getInstance().getMetrics().buttonHintsHeight));
  // Same top offset as every GUI.drawHeader caller, so the band lines up with
  // the rest of the firmware's screens.
  screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().topPadding));
  fui::HeaderProps header;
  header.title = server.name.empty() ? tr(STR_OPDS_BROWSER) : server.name.c_str();
  header.borderEdges = fui::EdgeBottom;
  // Optically align the buttons with the title glyphs: text hangs low in its
  // line cell by the font's internal leading, so drop the buttons to match.
  const int titleFontId = uiScaleSpec().titleFontId;
  header.actionOffsetY =
      static_cast<int16_t>((renderer.getLineHeight(titleFontId) - renderer.getTextHeight(titleFontId)) / 2);

  if (withSearch && !searchTemplate.empty()) {
    header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
    header.trailingAction = ACTION_SEARCH;
  }
  // Catalog and language live behind one button, on every screen of the
  // catalog: which catalog and which languages are the same question.
  // Library rather than a gear: the screen behind it is "which catalog, in
  // which languages". There is also no gear in the list icon set --
  // UIIcon::Settings falls through listIconFor's default and returns an
  // empty bitmap, which draws nothing and reports nothing.
  //
  // Shown on the error screen too, and that is the point: a catalog that will
  // not load (moved, gone, or asking for a password nobody has) leaves the
  // reader on a screen whose only useful action is to pick another one.
  // Gating this on a loaded catalog made the failure a dead end.
  if (state == BrowserState::BROWSING || state == BrowserState::ERROR) {
    header.leadingIcon = listIconFor(UIIcon::Library, 32);
    header.leadingAction = ACTION_SETTINGS;
  }
  screen.header(header);
  // Same breathing room between header and content as the legacy screens.
  screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().verticalSpacing));
}

void OpdsBookBrowserActivity::buildBrowsingScreen(UiScreen& screen) {
  screenHeader(screen, true);

  if (entries.empty()) {
    // A lookup-only catalog has nothing to list; say what to do rather than
    // reporting the empty feed as a fault.
    // The catalog's own words first: an empty feed's <subtitle> is where a
    // server says WHY it is empty ("No EPUBs found for dune"), and it beats
    // anything generic we could write. Otherwise: a lookup-only catalog with
    // nothing typed yet is waiting for a search, and everything else is empty.
    const char* empty = feedSubtitle.empty()
                            ? (searchOnlyCatalog && !showingSearchResults ? tr(STR_SEARCH) : tr(STR_NO_ENTRIES))
                            : feedSubtitle.c_str();
    screen.centeredText(empty, screen.theme().bodyText);
    return;
  }

  // Transient per-render: sized once via reserve, points into `entries`
  // strings, freed on scope exit.
  // rowItems is built whenever entries changes (see rebuildRowItems(), called
  // from fetchFeed()/releaseEntries()) and reused here on every repaint.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the nav chevron and the row edge
  listNav.selected = selectorIndex;
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser row height
    // instead of FreeInkUI's touch-target-sized default (see
    // UiListActivity::syncListViewport; this screen predates that base and
    // syncs its own viewport directly). Book rows carry an author subtitle.
    rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listWithSubtitleRowHeight);
    props.rowHeight = rowHeight;
  }
  listNav.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, static_cast<int>(entries.size()), props);
  screen.list(props);
}

void OpdsBookBrowserActivity::buildDownloadScreen(UiScreen& screen) {
  screenHeader(screen, false);

  const auto& theme = screen.theme();
  const bool preparing = downloadTotal == 0;

  fui::TextStyle centered = theme.bodyText;
  centered.align = fui::TextAlign::Center;
  fui::TextStyle titleStyle = centered;
  titleStyle.font = theme.fontTitle;
  titleStyle.maxLines = 2;
  fui::TextStyle quiet = centered;
  quiet.font = theme.fontSmall;

  const int16_t lh = screen.target().lineHeight(centered.font);
  const int16_t tlh = screen.target().lineHeight(titleStyle.font);
  const int16_t slh = screen.target().lineHeight(quiet.font);
  const int16_t gap = theme.spaceMd;
  const int16_t barH = 16;
  const int16_t btnH = theme.rowHeight;
  const fui::Rect body = screen.body();

  prepCoverRect = fui::Rect{};
  const bool haveCover = !downloadCoverPath.empty();

  // Derived at build time, not from a counter in loop(): loop() is exactly
  // what the download blocks, so a tick advanced there could never fire during
  // the wait it was written for.
  char waiting[64];
  if (preparing) {
    const uint8_t dots = static_cast<uint8_t>((millis() / WAIT_TICK_MS) % 4);
    snprintf(waiting, sizeof(waiting), "%s%.*s", tr(STR_PREPARING_BOOK), dots, "...");
  } else {
    snprintf(waiting, sizeof(waiting), "%s", tr(STR_DOWNLOADING));
  }

  // The slot under the status line: a bar once a total is known, the reason
  // for the wait before that. They are not the same height, and measuring the
  // block with the wrong one mis-centres the screen.
  const int16_t slotH = preparing ? lh : barH;

  const int16_t coverW = 168, coverH = 252;
  const int16_t drawnCoverH = haveCover ? coverH : 0;
  const int16_t blockH =
      static_cast<int16_t>(drawnCoverH + tlh * 2 + slh + lh + slotH + btnH + gap * (haveCover ? 6 : 5));
  if (body.height > blockH) screen.spacer(static_cast<int16_t>((body.height - blockH) / 2));
  if (haveCover) {
    const fui::Rect slot = screen.takeTop(coverH, gap);
    prepCoverRect = fui::Rect{static_cast<int16_t>(slot.x + (slot.width - coverW) / 2), slot.y, coverW, coverH};
  }
  screen.target().text(screen.takeTop(static_cast<int16_t>(tlh * 2), gap), statusMessage.c_str(), titleStyle);
  screen.target().text(screen.takeTop(slh, gap), downloadAuthor.c_str(), quiet);
  screen.spacer(gap);
  screen.target().text(screen.takeTop(lh, gap), waiting, centered);

  if (preparing) {
    // Its own full line, NOT the 16px bar slot. A line box is clipped by the
    // box it is handed rather than by the font, so the hint lost every
    // descender it had -- "catalog" printed without the tail of its g. Full
    // body width too: the sentence does not wrap, so a 50px inset each side
    // was spending budget it did not have.
    screen.target().text(screen.takeTop(lh, gap), tr(STR_PREPARING_HINT), quiet);
  } else {
    const fui::Rect bar = screen.takeTop(barH, gap).inset(fui::Insets{0, 40, 0, 40});
    fui::ProgressBarProps progress;
    progress.value = static_cast<int32_t>(downloadProgress);
    progress.max = static_cast<int32_t>(downloadTotal);
    progress.border = fui::Paint::solid(fui::Color::Black);
    progress.borderWidth = 1;
    fui::progressBar(screen.frame(), bar, progress);
  }

  // Wide enough to read as a control. At width/3 it was a bordered word, and
  // on a screen whose only other affordance is a progress bar that is not
  // enough to say "this is the way out".
  const fui::Rect btnArea = screen.takeTop(btnH);
  const int16_t btnW = static_cast<int16_t>(btnArea.width * 3 / 5);
  fui::ButtonProps cancel;
  cancel.label = tr(STR_CANCEL);
  cancel.action = ACTION_CANCEL;
  cancel.text = centered;
  screen.button(cancel, fui::Rect{static_cast<int16_t>(btnArea.x + (btnArea.width - btnW) / 2), btnArea.y, btnW, btnH});
}

void OpdsBookBrowserActivity::buildSavedScreen(UiScreen& screen) {
  screenHeader(screen, false);

  const auto& theme = screen.theme();
  fui::TextStyle centered = theme.bodyText;
  centered.align = fui::TextAlign::Center;

  fui::TextStyle verdict = centered;
  verdict.font = theme.fontTitle;
  verdict.bold = true;

  // Three lines, because the name is the message. opdsBookFilename budgets 100
  // bytes and one 480px line holds roughly half that, so a single-line style
  // would cut most real names. The SDK wraps on spaces and only ellipsises a
  // word it cannot break, which is the honest last resort rather than a silent
  // stop mid-title.
  fui::TextStyle nameStyle = centered;
  nameStyle.maxLines = 3;

  fui::TextStyle quiet = centered;
  quiet.font = theme.fontSmall;

  const int16_t vlh = screen.target().lineHeight(verdict.font);
  const int16_t lh = screen.target().lineHeight(centered.font);
  const int16_t slh = screen.target().lineHeight(quiet.font);
  const int16_t gap = theme.spaceMd;
  const int16_t air = theme.spaceLg;
  const int16_t btnH = theme.rowHeight;

  const fui::Rect body = screen.body();
  // Air at the sides: the body reaches within 3px of the panel edge, and the
  // X4 Pro's glass hides a further pixel of it.
  const int16_t nameW = static_cast<int16_t>(body.width > air * 2 ? body.width - air * 2 : body.width);
  const int16_t measured = fui::measureWrappedText(screen.target(), savedName.c_str(), nameStyle, nameW).height;
  const int16_t nameH = measured > 0 ? measured : lh;
  const int16_t blockH = static_cast<int16_t>(vlh + air + nameH + gap + slh + air + btnH);

  // Centred in the TOP HALF of the body, not in the body. That is a placement
  // rule about the reader's finger, not about balance: this screen arrives
  // under a hand that was last on one of two controls, and both of them live
  // lower down. Measured on the X4 Pro at 480x800, body (3,114) 474x683:
  //
  //   download screen  Cancel    y 471..537
  //   detail screen    Download  y 723..789
  //   this screen      Done      y 327..393  (3 lines of name: 341..407)
  //
  // A finger still resting on either of those finds nothing here when it
  // lifts. The reveal gate in loop() covers the same finger for the length of
  // the refresh; it cannot cover it afterwards, and this does.
  const int16_t half = static_cast<int16_t>(body.height / 2);
  if (half > blockH) screen.spacer(static_cast<int16_t>((half - blockH) / 2));

  screen.target().text(screen.takeTop(vlh, air), tr(STR_BOOK_SAVED), verdict);
  screen.target().text(screen.takeTop(nameH, gap).inset(fui::Insets{0, air, 0, air}), savedName.c_str(), nameStyle);
  // Where to look. The folder is the one ACTUALLY used, so the SD-root
  // fallback taken when a configured folder could not be created says so --
  // precisely the case where a reader searching the folder they set would come
  // up empty.
  char location[96];
  snprintf(location, sizeof(location), tr(STR_SAVED_IN_FORMAT),
           savedFolder.empty() ? tr(STR_OPDS_SD_ROOT) : savedFolder.c_str());
  screen.target().text(screen.takeTop(slh, air), location, quiet);

  const fui::Rect btnArea = screen.takeTop(btnH);
  const int16_t btnW = static_cast<int16_t>(btnArea.width * 3 / 5);
  fui::ButtonProps done;
  done.label = tr(STR_DONE);
  done.action = ACTION_SAVED_DONE;
  done.text = centered;
  // Outlined explicitly rather than left to the theme: the core theme leaves
  // tokens.button unset, and defaultButtonStyles() paints white on white with
  // no border in the normal state -- a label, not a control, on the one screen
  // whose only affordance this is.
  done.styles = fui::defaultButtonStyles();
  done.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  done.styles.normal.borderWidth = 1;
  done.styles.focused.border = fui::Paint::solid(fui::Color::Black);
  done.styles.focused.borderWidth = 1;
  done.radius = 4;
  screen.button(done, fui::Rect{static_cast<int16_t>(btnArea.x + (btnArea.width - btnW) / 2), btnArea.y, btnW, btnH});
}

void OpdsBookBrowserActivity::buildStatusScreen(UiScreen& screen) {
  screenHeader(screen, false);

  fui::TextStyle centered = screen.theme().bodyText;
  centered.align = fui::TextAlign::Center;
  if (state == BrowserState::ERROR) {
    const int16_t lh = screen.target().lineHeight(centered.font);
    const int16_t gap = screen.theme().spaceMd;
    const bool showTapHint = mappedInput.hasTouch();
    const int16_t blockH = static_cast<int16_t>(lh * (showTapHint ? 3 : 2) + gap * (showTapHint ? 2 : 1));
    const fui::Rect body = screen.body();
    if (body.height > blockH) screen.spacer(static_cast<int16_t>((body.height - blockH) / 2));
    screen.target().text(screen.takeTop(lh, gap), tr(STR_ERROR_MSG), centered);
    screen.target().text(screen.takeTop(lh, gap), errorMessage.c_str(), centered);
    if (showTapHint) screen.target().text(screen.takeTop(lh), tr(STR_TAP_TO_RETRY), centered);
    return;
  }
  // CHECK_WIFI / LOADING (and the brief child-activity handoff states).
  screen.centeredText(statusMessage.c_str(), centered);
}

void OpdsBookBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  MappedInputManager::Labels labels;
  switch (state) {
    case BrowserState::BROWSING: {
      const char* confirmLabel =
          (!entries.empty() && entries[selectorIndex].type == OpdsEntryType::BOOK) ? tr(STR_DOWNLOAD) : tr(STR_OPEN);
      const char* searchLabel = (!searchTemplate.empty() && selectorIndex == 0) ? tr(STR_SEARCH) : tr(STR_DIR_UP);
      labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, searchLabel, tr(STR_DIR_DOWN));
      break;
    }
    case BrowserState::DOWNLOADING:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    case BrowserState::SAVED:
      // Both front buttons, because either one is the reader saying "read it".
      // There is nothing else on this screen to press.
      labels = mappedInput.mapLabels(tr(STR_DONE), tr(STR_DONE), "", "");
      break;
    case BrowserState::ERROR:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderUi();
  if (state == BrowserState::DOWNLOADING) paintPrepareCover();
  renderer.displayBuffer();
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (server.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  std::string url = UrlUtils::buildUrl(server.url, path);
  LOG_DBG("OPDS", "Fetching: %s", url.c_str());
  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) {
      state = BrowserState::ERROR;
      // A catalog that wants credentials is not a broken catalog, and saying
      // "failed to fetch" sends the reader looking at their Wi-Fi instead of
      // at the server's username and password.
      const int status = HttpDownloader::lastStatus();
      errorMessage = (status == 401 || status == 403) ? tr(STR_AUTH_FAILED) : tr(STR_FETCH_FEED_FAILED);
      // The number the server actually sent, appended raw. It is the one fact
      // that separates "the catalog is down" from "the catalog moved" from
      // "we never got a reply", and diagnosing it over a USB cable is not
      // something a reader can do. Digits need no translation; 0 means the
      // request got no response at all.
      errorMessage += " (" + std::to_string(status) + ")";
      requestUpdate();
      return;
    }
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  feedSubtitle = parser.getSubtitle();
  searchTemplate = parser.getSearchTemplate();
  if (searchTemplate.empty()) {
    // Most real catalogs advertise search by pointing at an OpenSearch
    // description document rather than inlining {searchTerms}, so resolve it
    // once per server and carry the result across navigation -- subfeeds
    // usually omit the link entirely, and losing the icon on the way into a
    // folder reads as the feature breaking.
    const std::string& descriptionUrl = parser.getSearchDescriptionUrl();
    if (!descriptionUrl.empty() && descriptionUrl != resolvedDescriptionUrl) {
      resolvedDescriptionUrl = descriptionUrl;
      resolvedSearchTemplate = fetchSearchTemplate(descriptionUrl);
    }
    searchTemplate = resolvedSearchTemplate;
  }
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  const bool feedTruncated = parser.truncated();
  // Reset the selection before the swap: the render task reads
  // entries[selectorIndex] under only an empty() guard, and the new feed can
  // be shorter than the old selection.
  selectorIndex = 0;
  listNav.reset();
  entries = std::move(parser).getEntries();

  // Language filter. Navigation rows are never dropped -- hiding the way into a
  // folder because the folder itself carries a language tag would strand the
  // user -- and neither are books the feed did not tag. See opdsLanguageAllowed.
  const uint32_t languageMask = opdsLanguageMaskFromCodes(SETTINGS.opdsLanguages);
  if (languageMask != 0 && languageMask != opdsAllLanguagesMask()) {
    const size_t before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [languageMask](const OpdsEntry& entry) {
                                   return entry.type == OpdsEntryType::BOOK &&
                                          !opdsLanguageAllowed(entry.language, languageMask);
                                 }),
                  entries.end());
    if (before != entries.size()) {
      LOG_DBG("OPDS", "Language filter hid %zu of %zu entries", before - entries.size(), before);
    }
  }

  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  if (!prevUrl.empty()) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""});
  }
  if (feedTruncated) {
    LOG_INF("OPDS", "Feed truncated to fit memory");
  }

  // A catalog with nothing to browse but a search link is not broken -- that is
  // what a lookup-only catalog looks like, and LibGen is one. The feed itself
  // tells us which shape it is, so no per-catalog setting is needed.
  searchOnlyCatalog = entries.empty() && !searchTemplate.empty();

  state = (entries.empty() && !searchOnlyCatalog) ? BrowserState::ERROR : BrowserState::BROWSING;
  if (entries.empty() && !searchOnlyCatalog) errorMessage = tr(STR_NO_ENTRIES);
  rebuildRowItems();
  requestUpdate();

  // Where the reader lands depends on the catalog's shape, not on a
  // preference: a lookup-only catalog opens the keyboard, a browsable one
  // opens its own rows (Recent, Popular, By Subject) with search in the header.
  if (openSearchOnArrival) {
    openSearchOnArrival = false;
    if (searchOnlyCatalog) {
      launchSearch();
    }
  }
}

// Derives rowItems from entries. Called whenever entries changes
// (fetchFeed()/releaseEntries()) so buildBrowsingScreen() reuses the cached
// rows on every repaint instead of rebuilding them per render.
void OpdsBookBrowserActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(entries.size());
  for (const auto& entry : entries) {
    fui::ListItem item;
    item.label = entry.title.c_str();
    if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) item.subtitle = entry.author.c_str();
    // Shape tells a book from a folder faster than reading the row does.
    item.icon = listIconFor(entry.type == OpdsEntryType::BOOK ? UIIcon::Book : UIIcon::Folder,
                            entry.type == OpdsEntryType::BOOK ? 32 : 24);
    if (entry.type == OpdsEntryType::NAVIGATION) item.value = ">";
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void OpdsBookBrowserActivity::releaseEntries() {
  // The app's interaction table holds row indices (and hit rects) for the old
  // entries; stop routing touches against it until the next render.
  closeRouting();
  std::vector<OpdsEntry>().swap(entries);
  rebuildRowItems();
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  // Back out of a result list into the keyboard rather than into the root
  // feed's browse rows: the search is what the reader was doing, and landing
  // on a directory listing reads as having lost their place. One more Back
  // from the keyboard leaves for home.
  if (showingSearchResults && !searchTemplate.empty()) {
    showingSearchResults = false;
    // On a lookup-only catalog the keyboard IS the home screen, so dismissing
    // it leaves for Home; on a browsable one it sits above the rows.
    launchSearch();
    return;
  }
  if (navigationHistory.empty()) {
    onGoHome();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    showingSearchResults = false;
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    releaseEntries();
    selectorIndex = 0;
    requestUpdate();
    fetchFeed(currentPath);
  }
}

std::string OpdsBookBrowserActivity::cachedCoverPath() {
  // Whatever extension the detail screen saved it under.
  static constexpr const char* kExtensions[] = {".bmp", ".jpg", ".jpeg", ".png"};
  for (const char* extension : kExtensions) {
    std::string path = std::string("/.crosspoint/opds-cover") + extension;
    if (Storage.exists(path.c_str())) return path;
  }
  return "";
}

void OpdsBookBrowserActivity::paintPrepareCover() {
  if (prepCoverRect.width <= 0 || prepCoverRect.height <= 0) return;
  if (OpdsDetailActivity::paintCoverFile(renderer, downloadCoverPath, prepCoverRect)) return;
  // The file existed but would not decode -- Gutenberg serves JPEG, and a
  // format this build cannot read is indistinguishable from a corrupt one.
  // The space is already reserved by then, so draw the outline rather than
  // leave a 250px hole in the middle of the screen.
  renderer.drawRect(prepCoverRect.x, prepCoverRect.y, prepCoverRect.width, prepCoverRect.height, 1, true);
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadAuthor = book.author;
  downloadCoverPath = cachedCoverPath();
  downloadProgress = downloadTotal = 0;
  cancelDownload = false;
  goHomeAfterCancel = false;
  requestUpdate(true);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);
  // opdsDownloadFolder is already a null-terminated char[64]; use it directly —
  // no std::string copy. exists()/mkdir() take const char*.
  const char* folder = SETTINGS.opdsDownloadFolder;  // "" => SD root
  bool haveFolder = folder[0] != '\0';
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    // exists()-guard first: mkdir's return-on-existing is unconfirmed, and every
    // existing caller checks exists() before mkdir. On real failure, fall back
    // to SD root so the download is never lost.
    LOG_ERR("OPDS", "mkdir failed for %s, using SD root", folder);
    haveFolder = false;
  }

  // Composed by opdsBookPath rather than concatenated here, so the verdict
  // screen below can split THIS string and be reporting the path that was
  // written rather than a second guess at it.
  const std::string filename = opdsBookPath(haveFolder ? folder : "", book.author, book.title,
                                            static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  LOG_DBG("OPDS", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;
  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this, &lastRenderedPercent, &lastProgressUpdateMs](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        // The activity loop is blocked for the whole download; pump input here
        // so the Cancel button or a Back press can abort mid-transfer.
        mappedInput.update();
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelDownload = true;
        // This update() consumes the one-shot home event before the central
        // ActivityManager dispatch can see it, so honor it here: abort the
        // download, then exit to home once the abort unwinds.
        if (mappedInput.wasHomeGesture()) {
          cancelDownload = true;
          goHomeAfterCancel = true;
        }
        routeTouch(mappedInput);
        const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
        const unsigned long now = millis();
        if (percent >= 100 || lastRenderedPercent < 0 ||
            percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
            now - lastProgressUpdateMs >= (total > 0 ? DOWNLOAD_PROGRESS_MIN_UPDATE_MS : DOWNLOAD_WAIT_UPDATE_MS)) {
          lastRenderedPercent = percent;
          lastProgressUpdateMs = now;
          requestUpdate(true);
        }
      },
      &cancelDownload, server.username, server.password);

  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
    // Split off the path that was actually written. downloadToFile() writes to
    // exactly this string and renames nothing, so this is the name the card
    // holds -- not the catalog's title, which opdsBookFilename() routinely
    // rewrites (one author out of a ';'-joined list, two separate byte
    // budgets, illegal characters replaced).
    savedName = opdsPathBasename(filename);
    savedFolder = opdsPathFolder(filename);
    state = BrowserState::SAVED;
    savedDwell.arm();
    // Arms the reveal gate: the download screen's Cancel button was live under
    // whatever finger is on the glass, and this screen must not answer that
    // same contact.
    resetUi();
  } else if (result == HttpDownloader::ABORTED) {
    // User cancelled; the partial file is already removed. Back to the list,
    // or straight home when the abort came from the home gesture.
    LOG_INF("OPDS", "Download cancelled");
    if (goHomeAfterCancel) {
      onGoHome();
      return;
    }
    state = BrowserState::BROWSING;
  } else {
    LOG_ERR("OPDS", "Download failed: %d", static_cast<int>(result));
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
  }
  requestUpdate();
}

void OpdsBookBrowserActivity::launchSearch() {
  state = BrowserState::SEARCH_INPUT;
  requestUpdate();

  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  // A lookup-only catalog opens straight onto this keyboard, so the settings
  // button that lives in the browsing header would be unreachable without
  // first backing out of the app's only screen. Put it on the keyboard too.
  if (searchOnlyCatalog) keyboard->setHeaderAction(listIconFor(UIIcon::Library, 32));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    state = BrowserState::BROWSING;
    if (!result.isCancelled) {
      const auto& entered = std::get<KeyboardResult>(result.data);
      if (entered.headerAction) {
        openSettings();
        return;
      }
      performSearch(entered.text);
      return;
    }
    // Back from the search box leaves Get Books, always -- not into the rows
    // behind it and never into an earlier search. The keyboard is the top of
    // this app's stack, and settings stays reachable from the icon on the
    // keyboard itself rather than from a screen behind it.
    onGoHome();
  });
}

std::string OpdsBookBrowserActivity::fetchSearchTemplate(const std::string& descriptionUrl) {
  const std::string url = UrlUtils::buildUrl(server.url, descriptionUrl);
  LOG_DBG("OPDS", "Resolving OpenSearch description: %s", url.c_str());

  // These documents are a few hundred bytes, so a string fetch is cheaper than
  // standing up a streaming adapter for them.
  std::string document;
  if (!HttpDownloader::fetchUrl(url, document, server.username, server.password)) {
    LOG_DBG("OPDS", "OpenSearch description fetch failed");
    return "";
  }

  OpenSearchParser openSearch;
  openSearch.feed(document.data(), document.size());
  openSearch.finish();
  if (openSearch.error()) {
    LOG_DBG("OPDS", "OpenSearch description parse failed");
    return "";
  }
  return openSearch.getSearchTemplate();
}

void OpdsBookBrowserActivity::openDetail(const OpdsEntry& entry) {
  // A tap opens the details rather than starting a download: covers, summary
  // and size are how a reader tells two editions apart, and an accidental tap
  // used to cost a multi-megabyte transfer.
  startActivityForResult(std::make_unique<OpdsDetailActivity>(renderer, mappedInput, entry, server,
                                                              UrlUtils::buildUrl(server.url, currentPath)),
                         [this, entry](const ActivityResult& result) {
                           if (!result.isCancelled) downloadBook(entry);
                           requestUpdate();
                         });
}

void OpdsBookBrowserActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }

  auto urlEncode = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        out += static_cast<char>(c);
      else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
    return out;
  };

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), urlEncode(query));

  // Real templates carry more than the search terms -- Standard Ebooks sends
  // {count} and {startPage} -- and leaving those braces in the URL sends the
  // literal text to the server. Fill in the paging ones and drop the rest;
  // OpenSearch lets a client ignore any parameter it does not implement.
  const struct {
    const char* name;
    const char* value;
  } SUBSTITUTIONS[] = {{"{count}", "30"}, {"{startPage}", "1"}, {"{startIndex}", "1"}};
  for (const auto& substitution : SUBSTITUTIONS) {
    const size_t at = url.find(substitution.name);
    if (at != std::string::npos) url.replace(at, strlen(substitution.name), substitution.value);
  }
  for (size_t open = url.find('{'); open != std::string::npos; open = url.find('{')) {
    const size_t close = url.find('}', open);
    if (close == std::string::npos) break;
    url.erase(open, close - open + 1);
  }

  // Deliberately NOT pushed onto navigationHistory. A search is a mode, not a
  // place in the catalog: stacking them made Back walk backwards through every
  // query the reader had ever typed instead of returning to the keyboard.
  // Back out of results is handled by showingSearchResults in navigateBack().
  currentPath = url;
  showingSearchResults = true;

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}

void OpdsBookBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed(currentPath);
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
