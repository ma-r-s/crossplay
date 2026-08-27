#include "OpdsFilterActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsLanguages.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

OpdsFilterActivity::OpdsFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("OpdsFilter", renderer, mappedInput) {}

void OpdsFilterActivity::onEnter() {
  mask = opdsLanguageMaskFromCodes(SETTINGS.opdsLanguages);
  rebuildRows();
  UiListActivity::onEnter();
}

const char* OpdsFilterActivity::headerTitle() const { return tr(STR_OPDS_FILTERS); }

int OpdsFilterActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

void OpdsFilterActivity::persist() {
  opdsLanguageCodesFromMask(mask, SETTINGS.opdsLanguages, sizeof(SETTINGS.opdsLanguages));
  SETTINGS.saveToFile();
}

void OpdsFilterActivity::rebuildRows() {
  rowItems_.clear();
  rowLabels_.clear();
  // Labels are owned here because ListItem holds a bare const char*; reserving
  // up front keeps those pointers stable while the vector fills.
  rowLabels_.reserve(OPDS_LANGUAGE_COUNT + 1);
  rowItems_.reserve(OPDS_LANGUAGE_COUNT + 1);

  // "All languages" first: without it, turning the filter off means toggling
  // every row. It costs one row and nothing moves when toggled -- an
  // arrangement that reordered rows by selection was tried and is wrong here,
  // because a 1-2s e-ink refresh rearranges the list under the user's finger.
  rowLabels_.emplace_back(tr(STR_OPDS_FILTER_ALL));
  fui::ListItem all;
  all.label = rowLabels_.back().c_str();
  all.toggle = true;
  all.toggleChecked = (mask == opdsAllLanguagesMask());
  all.actionValue = -1;
  rowItems_.push_back(all);

  for (size_t i = 0; i < OPDS_LANGUAGE_COUNT; ++i) {
    rowLabels_.emplace_back(OPDS_LANGUAGES[i].label);
    fui::ListItem item;
    item.label = rowLabels_.back().c_str();
    item.toggle = true;
    item.toggleChecked = (mask & (1u << i)) != 0;
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void OpdsFilterActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowItems_.size())) return;
  const int16_t value = rowItems_[index].actionValue;

  if (value < 0) {
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
  screen.setContentMarginAbsolute(
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
