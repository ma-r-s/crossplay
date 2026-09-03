#pragma once
#include <OpdsParser.h>

#include <string>
#include <utility>
#include <vector>

#include "OpdsServerStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

/**
 * Activity for browsing and downloading books from an OPDS server.
 * Supports navigation through catalog hierarchy and downloading EPUBs.
 */
class OpdsBookBrowserActivity final : public Activity, private UiAppHost {
 public:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR, SEARCH_INPUT };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server);

  // Shelf factory: Get Books is a row in APPS, and the shelf hands every app
  // the same two arguments. Picking which catalog to open is this class's
  // business, so ActivityManager::goToBrowser() calls it too rather than
  // keeping a second copy of the rule.
  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  BrowserState state = BrowserState::LOADING;
  std::vector<OpdsEntry> entries;
  // Row buffer, built whenever entries changes (fetchFeed()/releaseEntries())
  // so buildBrowsingScreen() reuses it on every repaint instead of rebuilding
  // a ListItem vector per render.
  std::vector<freeink::ui::ListItem> rowItems;
  void rebuildRowItems();
  std::vector<std::string> navigationHistory;
  std::string currentPath;
  // Cleared once consumed; see the tail of fetchFeed().
  // A catalog with a search link and nothing to browse (LibGen). Derived from
  // the feed, not configured.
  bool searchOnlyCatalog = false;
  bool openSearchOnArrival = true;
  // True while the visible feed is a search result set.
  bool showingSearchResults = false;
  std::string searchTemplate;
  // The feed's <subtitle>, drawn when it has no entries to draw instead.
  std::string feedSubtitle;
  // The "Preparing" tick, for the stretch before the server sends any bytes.
  static constexpr uint32_t WAIT_TICK_MS = 2000;
  // Resolved once per server from an OpenSearch description document and
  // reused while navigating, since subfeeds rarely repeat the search link.
  std::string resolvedDescriptionUrl;
  std::string resolvedSearchTemplate;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  // The rest of what the wait screen shows; statusMessage already holds the
  // title. The cover is the file the detail screen just fetched -- a download
  // cannot be reached without passing through that screen, so it is already on
  // the card and costs no second request.
  std::string downloadAuthor;
  std::string downloadCoverPath;
  // Reserved by buildDownloadScreen, painted after renderUi() flushes the
  // screen tree, which would otherwise paint over the bitmap.
  freeink::ui::Rect prepCoverRect{};
  void paintPrepareCover();
  static std::string cachedCoverPath();

  OpdsServer server;  // Copied at construction — safe even if the store changes during browsing

  // Viewport memory (top/visibleRows) for the browsing list; `selected` is
  // mirrored from selectorIndex at build/move time.
  freeink::ui::ListNav listNav;
  // Read by HttpDownloader between chunks; set by the Cancel button handler or
  // a Back press, both pumped from the download's progress callback.
  bool cancelDownload = false;
  // Set when the cancel came from the home gesture (consumed by the download
  // callback's own input pump); exit to home after the abort unwinds.
  bool goHomeAfterCancel = false;

  // Single screen fn dispatching on `state`: every state shares the themed
  // header and gets built through FreeInkUI.
  static void rootScreen(UiScreen& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onSearchEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onSettingsEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  void screenHeader(UiScreen& screen, bool withSearch);
  void buildBrowsingScreen(UiScreen& screen);
  void buildDownloadScreen(UiScreen& screen);
  void buildStatusScreen(UiScreen& screen);
  void activateSelected();

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed(const std::string& path);
  void releaseEntries();
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book);
  void launchSearch();
  void openSettings();
  std::string fetchSearchTemplate(const std::string& descriptionUrl);
  void openDetail(const OpdsEntry& entry);
  void performSearch(const std::string& query);
  bool preventAutoSleep() override { return true; }
};
