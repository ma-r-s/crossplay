#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Language filter for the OPDS browser.
 *
 * Writes SETTINGS.opdsLanguages (a comma-separated code list) as the user
 * toggles rows, so leaving by any route keeps the choice -- there is no
 * confirm step to miss.
 */
class OpdsFilterActivity final : public UiListActivity {
 public:
  OpdsFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  void rebuildRows();
  void persist();

  uint32_t mask = 0;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> rowLabels_;
};
