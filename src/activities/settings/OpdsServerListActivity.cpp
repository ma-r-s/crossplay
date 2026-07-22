#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsSettingsActivity.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/OpdsFilename.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;

// Normalizes a user-typed folder: trims spaces, "" => SD root, otherwise a
// single leading '/' and no trailing '/'. Cold path (runs once per edit).
std::string normalizeFolder(std::string v) {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
  if (v.empty()) return "";
  if (v.front() != '/') v.insert(v.begin(), '/');
  while (v.size() > 1 && v.back() == '/') v.pop_back();
  if (v == "/") return "";  // a bare slash is SD root, same as empty
  return v;
}

// Label shown for the current OPDS filename format in the list subtitle.
StrId opdsFormatLabel(uint8_t format) {
  switch (format) {
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleAuthor):
      return StrId::STR_FMT_TITLE_AUTHOR;
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleOnly):
      return StrId::STR_FMT_TITLE;
    default:
      return StrId::STR_FMT_AUTHOR_TITLE;
  }
}
}  // namespace

int OpdsServerListActivity::getItemCount() const {
  int count = static_cast<int>(OPDS_STORE.getCount());
  // Settings mode appends three virtual items: "Add Server", "Download folder"
  // and "Filename format".
  if (!pickerMode) {
    count += 3;
  }
  return count;
}

OpdsServerListActivity::OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const bool pickerMode)
    : Activity("OpdsServerList", renderer, mappedInput),
      pickerMode(pickerMode),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void OpdsServerListActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<OpdsServerListActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->getItemCount())) return;
  self->selectedIndex = event.value;
  // Activation opens an editor/browser or repaints a new value; a lingering
  // flash would gray an unrelated row.
  self->app.clearTapFlash();
  self->handleSelection();
  self->requestUpdate();
}

void OpdsServerListActivity::onEnter() {
  Activity::onEnter();

  // Reload from disk in case servers were added/removed by a subactivity or the web UI
  OPDS_STORE.loadFromFile();
  selectedIndex = 0;
  uiReady = false;
  visibleRows = 1;
  topIndex = 0;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &OpdsServerListActivity::onRowEvent, this);
  app.setScreen(&OpdsServerListActivity::listScreen, this);
  requestUpdate();
}

void OpdsServerListActivity::onExit() { Activity::onExit(); }

void OpdsServerListActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  auto activateSelected = [this] { handleSelection(); };

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (pickerMode) {
      activityManager.goHome(HomeMenuItem::OPDS_BROWSER);
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let onRowEvent dispatch.
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;  // dispatched to onRowEvent
    }
  }

  const int itemCount = getItemCount();
  if (itemCount > 0) {
    // Swipes scroll the viewport; the selection stays put and button
    // navigation pulls the view back to it.
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
      const int next = scrollListBy(topIndex, delta, visibleRows, itemCount);
      if (next != topIndex) {
        topIndex = next;
        requestUpdate();
      }
      return;
    }

    const auto moveSelection = [this, itemCount](const int index) {
      selectedIndex = index;
      topIndex = followListSelection(selectedIndex, topIndex, visibleRows, itemCount);
      requestUpdate();
    };
    buttonNavigator.onNext(
        [this, itemCount, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, itemCount)); });
    buttonNavigator.onPrevious(
        [this, itemCount, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, itemCount)); });
  }
}

void OpdsServerListActivity::handleSelection() {
  const auto serverCount = static_cast<int>(OPDS_STORE.getCount());

  if (pickerMode) {
    // Picker mode: selecting a server navigates to the OPDS browser
    if (selectedIndex < serverCount) {
      const auto* server = OPDS_STORE.getServer(static_cast<size_t>(selectedIndex));
      if (server) {
        activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, *server));
      }
    }
    return;
  }

  // Index layout: [servers 0..serverCount-1], [Add Server], [Download folder], [Filename format].
  if (selectedIndex == serverCount + 1) {
    auto folderHandler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        const std::string norm = normalizeFolder(kb.text);
        strncpy(SETTINGS.opdsDownloadFolder, norm.c_str(), sizeof(SETTINGS.opdsDownloadFolder) - 1);
        SETTINGS.opdsDownloadFolder[sizeof(SETTINGS.opdsDownloadFolder) - 1] = '\0';
        SETTINGS.saveToFile();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_DOWNLOAD_FOLDER),
                                                std::string(SETTINGS.opdsDownloadFolder), 63, InputType::Text),
        folderHandler);
    return;
  }

  // "Filename format": picker like every other multi-option setting.
  if (selectedIndex == serverCount + 2) {
    static const StrId formatLabels[] = {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR,
                                         StrId::STR_FMT_TITLE};
    optionPopup.show(StrId::STR_OPDS_FILENAME_FORMAT, formatLabels, static_cast<int>(OpdsFilenameFormat::Count),
                     SETTINGS.opdsFilenameFormat, [this](int idx) {
                       SETTINGS.opdsFilenameFormat = static_cast<uint8_t>(idx);
                       SETTINGS.saveToFile();
                     });
    requestUpdate();
    return;
  }

  // Settings mode: open editor for selected server, or create a new one
  auto resultHandler = [this](const ActivityResult&) {
    // Reload server list when returning from editor
    OPDS_STORE.loadFromFile();
    selectedIndex = 0;
  };

  if (selectedIndex < serverCount) {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, selectedIndex), resultHandler);
  } else {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void OpdsServerListActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<OpdsServerListActivity*>(user)->buildListScreen(screen);
}

void OpdsServerListActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int itemCount = getItemCount();
  if (itemCount == 0) {
    screen.centeredText(tr(STR_NO_SERVERS), screen.theme().bodyText);
    return;
  }

  const auto& servers = OPDS_STORE.getServers();
  const auto serverCount = static_cast<int>(servers.size());

  // Primary label: server name (falling back to URL if unnamed); subtitle is
  // the URL when a name is set, or the current folder/format values.
  std::vector<fui::ListItem> items;
  items.reserve(itemCount);
  for (int i = 0; i < serverCount; i++) {
    fui::ListItem item;
    item.label = servers[i].name.empty() ? servers[i].url.c_str() : servers[i].name.c_str();
    if (!servers[i].name.empty()) item.subtitle = servers[i].url.c_str();
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }
  if (!pickerMode) {
    fui::ListItem addServer;
    addServer.label = tr(STR_ADD_SERVER);
    addServer.actionValue = static_cast<int16_t>(serverCount);
    items.push_back(addServer);

    fui::ListItem folder;
    folder.label = tr(STR_OPDS_DOWNLOAD_FOLDER);
    folder.subtitle = SETTINGS.opdsDownloadFolder[0] ? SETTINGS.opdsDownloadFolder : tr(STR_OPDS_SD_ROOT);
    folder.actionValue = static_cast<int16_t>(serverCount + 1);
    items.push_back(folder);

    fui::ListItem format;
    format.label = tr(STR_OPDS_FILENAME_FORMAT);
    format.subtitle = I18N.get(opdsFormatLabel(SETTINGS.opdsFilenameFormat));
    format.actionValue = static_cast<int16_t>(serverCount + 2);
    items.push_back(format);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  const auto rows = fui::listVisibleRows(screen.body(), screen.theme().rowHeight, screen.theme().listRowGap);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, itemCount);  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void OpdsServerListActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_OPDS_SERVERS));

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
