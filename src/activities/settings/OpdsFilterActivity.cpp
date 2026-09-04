#include "OpdsFilterActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsLanguages.h"
#include "OpdsServerStore.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
// Distinct from the language indices, which are 0-based.
// Row meanings live beside the rows, not in actionValue: the list drops any
// negative actionValue before dispatch, so encoding meaning there made the
// rows silently dead to touch while still working under button navigation.
constexpr int SOURCE_ROW = -50;
constexpr int ALL_LANGUAGES_ROW = -1;
constexpr int HEADER_ROW = -999;
}  // namespace

OpdsFilterActivity::OpdsFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("OpdsFilter", renderer, mappedInput) {}

void OpdsFilterActivity::onEnter() {
  mask = opdsLanguageMaskFromCodes(SETTINGS.opdsLanguages);
  rebuildRows();
  UiListActivity::onEnter();
}

bool OpdsFilterActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void OpdsFilterActivity::render(RenderLock&& lock) {
  // The popup owns the screen while it is up, exactly as the other
  // single-choice settings do.
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

const char* OpdsFilterActivity::headerTitle() const { return tr(STR_OPDS_BROWSER); }

int OpdsFilterActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

void OpdsFilterActivity::persist() {
  opdsLanguageCodesFromMask(mask, SETTINGS.opdsLanguages, sizeof(SETTINGS.opdsLanguages));
  SETTINGS.saveToFile();
}

void OpdsFilterActivity::rebuildRows() {
  rowItems_.clear();
  rowLabels_.clear();
  rowKind_.clear();
  // Labels are owned here because ListItem holds a bare const char*; reserving
  // up front keeps those pointers stable while the vector fills.
  const auto& servers = OPDS_STORE.getServers();
  rowLabels_.reserve(OPDS_LANGUAGE_COUNT + servers.size() + 3);
  rowItems_.reserve(OPDS_LANGUAGE_COUNT + servers.size() + 3);

  // Source is a single choice, so it is one row showing the current value that
  // opens the chooser popup -- the same idiom as Filename format elsewhere. A
  // column of switches invited turning several on, which this cannot mean.
  if (!servers.empty()) {
    const size_t current = SETTINGS.opdsLastServer < servers.size() ? SETTINGS.opdsLastServer : 0;
    rowLabels_.emplace_back(servers[current].name.empty() ? servers[current].url : servers[current].name);
    fui::ListItem source;
    source.label = tr(STR_OPDS_SOURCE);
    source.subtitle = rowLabels_.back().c_str();
    source.actionValue = static_cast<int16_t>(rowItems_.size());
    rowKind_.push_back(SOURCE_ROW);
    rowItems_.push_back(source);
  }

  fui::ListItem languageHeading;
  languageHeading.label = tr(STR_OPDS_LANGUAGES);
  languageHeading.isHeader = true;
  languageHeading.actionValue = static_cast<int16_t>(rowItems_.size());
  rowItems_.push_back(languageHeading);
  rowKind_.push_back(HEADER_ROW);

  // "All languages" first: without it, turning the filter off means toggling
  // every row. It costs one row and nothing moves when toggled -- an
  // arrangement that reordered rows by selection was tried and is wrong here,
  // because a 1-2s e-ink refresh rearranges the list under the user's finger.
  rowLabels_.emplace_back(tr(STR_OPDS_FILTER_ALL));
  fui::ListItem all;
  all.label = rowLabels_.back().c_str();
  all.toggle = true;
  all.toggleChecked = (mask == opdsAllLanguagesMask());
  all.actionValue = static_cast<int16_t>(rowItems_.size());
  rowItems_.push_back(all);
  rowKind_.push_back(ALL_LANGUAGES_ROW);

  for (size_t i = 0; i < OPDS_LANGUAGE_COUNT; ++i) {
    rowLabels_.emplace_back(OPDS_LANGUAGES[i].label);
    fui::ListItem item;
    item.label = rowLabels_.back().c_str();
    item.toggle = true;
    item.toggleChecked = (mask & (1u << i)) != 0;
    item.actionValue = static_cast<int16_t>(rowItems_.size());
    rowItems_.push_back(item);
    rowKind_.push_back(static_cast<int>(i));
  }
}

void OpdsFilterActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowKind_.size())) return;
  const int value = rowKind_[index];
  if (value == HEADER_ROW) return;

  if (value == SOURCE_ROW) {
    const auto& servers = OPDS_STORE.getServers();
    if (servers.empty()) return;
    sourceLabels_.clear();
    sourceLabels_.reserve(servers.size());
    for (const auto& s : servers) sourceLabels_.push_back(s.name.empty() ? s.url : s.name);
    const size_t current = SETTINGS.opdsLastServer < servers.size() ? SETTINGS.opdsLastServer : 0;
    sourcePointers_.clear();
    sourcePointers_.reserve(sourceLabels_.size());
    for (const auto& label : sourceLabels_) sourcePointers_.push_back(label.c_str());
    // The const char* overload: catalog names are runtime strings, not StrIds.
    optionPopup.show(tr(STR_OPDS_SOURCE), sourcePointers_.data(), static_cast<int>(sourcePointers_.size()),
                     static_cast<uint8_t>(current), [this](int idx) {
                       SETTINGS.opdsLastServer = static_cast<uint8_t>(idx);
                       SETTINGS.saveToFile();
                       rebuildRows();
                     });
    requestUpdate();
    return;
  }

  if (value == ALL_LANGUAGES_ROW) {
    // The "all languages" row: on means no filtering, off falls back to the
    // default rather than to nothing, since an empty list filters nothing
    // either and would leave the screen looking broken.
    mask =
        (mask == opdsAllLanguagesMask()) ? opdsLanguageMaskFromCodes(OPDS_LANGUAGES_DEFAULT) : opdsAllLanguagesMask();
  } else {
    mask ^= (1u << static_cast<uint32_t>(value));
  }

  persist();
  rebuildRows();
  requestUpdate();
}

void OpdsFilterActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content sits below the header band and above the button hints, derived
  // from the safe area so bezel insets apply -- same as the server list.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);
}
