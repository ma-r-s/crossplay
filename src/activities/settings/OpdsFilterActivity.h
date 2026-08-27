#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

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
  void render(RenderLock&& lock) override;

 protected:
  bool handleCustomInput() override;
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  void rebuildRows();
  void persist();

  uint32_t mask = 0;
  std::vector<freeink::ui::ListItem> rowItems_;
  // Parallel to rowItems_: what each row means. See the constants in the .cpp.
  std::vector<int> rowKind_;
  std::vector<std::string> rowLabels_;
  std::vector<std::string> sourceLabels_;
  std::vector<const char*> sourcePointers_;
  OptionPopup optionPopup;
};
