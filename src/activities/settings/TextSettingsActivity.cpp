#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "TextSettingsPreview.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr fui::ActionId ACTION_TAB = 2;

// Tab labels for Font | Size | Layout | Style.
constexpr StrId TAB_NAME_IDS[] = {StrId::STR_FONT, StrId::STR_SIZE, StrId::STR_LAYOUT, StrId::STR_STYLE};

constexpr StrId LAYOUT_ROW_NAME_IDS[] = {StrId::STR_LINE_SPACING, StrId::STR_EXTRA_SPACING, StrId::STR_ALIGNMENT,
                                         StrId::STR_SCREEN_MARGIN};
constexpr StrId STYLE_ROW_NAME_IDS[] = {StrId::STR_FOCUS_READING, StrId::STR_HYPHENATION, StrId::STR_EMBEDDED_STYLE,
                                        StrId::STR_TEXT_AA};

int findCurrentFontIndex(const SdCardFontRegistry* registry, const char* sdFontFamilyName, uint8_t fontFamily) {
  if (sdFontFamilyName[0] != '\0' && registry) {
    const auto& families = registry->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == sdFontFamilyName) {
        return CrossPointSettings::BUILTIN_FONT_COUNT + i;
      }
    }
  }

  return fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? fontFamily : 0;
}

constexpr StrId LINE_SPACING_IDS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr int MARGIN_MIN = CrossPointSettings::SCREEN_MARGIN_MIN;
constexpr int MARGIN_MAX = CrossPointSettings::SCREEN_MARGIN_MAX;
constexpr int MARGIN_STEP = CrossPointSettings::SCREEN_MARGIN_STEP;
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, Tab initialTab)
    : Activity("TextSettings", renderer, mappedInput),
      registry_(registry),
      tab_(initialTab),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void TextSettingsActivity::onEnter() {
  Activity::onEnter();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SERIF), true, static_cast<uint8_t>(CrossPointSettings::NOTOSERIF)});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, static_cast<uint8_t>(CrossPointSettings::NOTOSANS)});
  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  rebuildSizeList();

  currentFamilyIndex_ = findCurrentFontIndex(registry_, SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);
  std::fill(std::begin(selectedIndex_), std::end(selectedIndex_), 1);       // default to the first list row
  selectedIndex_[static_cast<int>(Tab::Family)] = currentFamilyIndex_ + 1;  // Family/Size open on current selection
  selectedIndex_[static_cast<int>(Tab::Size)] = currentSizeIndex_ + 1;
  selectedIndex_[static_cast<int>(tab_)] = 0;  // screen opens with the tab bar focused, not a list row

  uiReady = false;
  visibleRows = 1;
  std::fill(std::begin(topIndex_), std::end(topIndex_), 0);
  followPending_ = true;  // first build shows each Family/Size current item
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &TextSettingsActivity::onRowEvent, this);
  app.on(ACTION_TAB, &TextSettingsActivity::onTabEvent, this);
  app.setScreen(&TextSettingsActivity::textSettingsScreen, this);

  requestUpdate();
}

void TextSettingsActivity::onExit() { Activity::onExit(); }

// The selectable sizes belong to the active family, so this runs on entry and
// again after every family change. A family change goes through ensureLoaded(),
// which snaps SETTINGS.fontPointSize into the new family's set — but entry does
// not, so the highlight is resolved by snapping rather than by exact match.
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);

  // The stored size can still sit outside this family's set — e.g. the family
  // was deleted while selected, or the card was swapped. Highlight the size the
  // reader actually renders, which getReaderFontId() resolves the same way.
  const uint8_t selectedPt = snapToNearestPointSize(points, SETTINGS.fontPointSize);

  sizes_.clear();
  sizes_.reserve(points.size());
  currentSizeIndex_ = 0;
  for (const uint8_t pt : points) {
    // "pt" is deliberately not translated: it is the typographic unit symbol,
    // written the same way in every language CrossPoint ships.
    char label[12];
    snprintf(label, sizeof(label), "%u pt", pt);
    if (pt == selectedPt) currentSizeIndex_ = static_cast<int>(sizes_.size());
    sizes_.push_back({label, pt});
  }
}

void TextSettingsActivity::onTabEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<TextSettingsActivity*>(user);
  if (self->optionPopup_.isActive()) return;
  if (event.value < 0 || event.value >= static_cast<int>(Tab::Count)) return;
  if (self->tab_ != static_cast<Tab>(event.value)) {
    self->tab_ = static_cast<Tab>(event.value);
    self->selectedIndex() = 0;  // tab taps land with the tab bar focused (legacy tap behavior)
    self->followPending_ = true;
    self->requestUpdate();
  }
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  self->app.clearTapFlash();
}

void TextSettingsActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<TextSettingsActivity*>(user);
  if (self->optionPopup_.isActive()) return;
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->currentListSize())) return;
  self->selectedIndex() = event.value + 1;
  // Most rows repaint a different surface (popup, preview, new value);
  // a lingering tap flash would gray an unrelated element.
  self->app.clearTapFlash();
  self->activateRow(event.value);
}

void TextSettingsActivity::loop() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) {  // picker owns input while open
    // The popup acts on button press; if that input closed it, the trailing
    // release must be swallowed below (Back would close this screen, Confirm
    // would re-activate the selected row).
    popupClosing_ = !optionPopup_.isActive();
    return;
  }
  if (popupClosing_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;  // closing press still held
    }
    popupClosing_ = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;  // swallow the release that closed the popup
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex() == 0) {
      switchTab();
      return;
    }

    activateRow(selectedIndex() - 1);
    return;
  }

  // Touch goes through the FreeInkApp: render() registered the tab and row
  // hit rects; route the snapshot and let the handlers dispatch.
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      // No pressed-state repaint here: it would cost a second e-ink refresh
      // per tap and paint a transient double pill on tab switches whose
      // erased black leaves a partial-refresh ghost.
      if (app.invalidated()) requestUpdate();
      if (event) return;  // dispatched to onTabEvent/onRowEvent
    }
  }

  // Swipes scroll the active tab's viewport; the selection stays put (it may
  // scroll off-screen) and button navigation pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex(), delta, visibleRows, currentListSize());
    if (next != topIndex()) {
      topIndex() = next;
      requestUpdate();
    }
    return;
  }

  // Buttons walk the tab band (index 0) plus the rows (1..listCount).
  const int ringSize = currentListSize() + 1;  // +1 for the tab bar at position 0
  const auto moveSelection = [this](const int index) {
    selectedIndex() = index;
    if (selectedIndex() == 0) {
      topIndex() = 0;
    } else {
      topIndex() = followListSelection(selectedIndex() - 1, topIndex(), visibleRows, currentListSize());
    }
    requestUpdate();
  };

  buttonNavigator_.onNextRelease(
      [this, ringSize, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex(), ringSize)); });

  buttonNavigator_.onPreviousRelease(
      [this, ringSize, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex(), ringSize)); });

  buttonNavigator_.onNextContinuous([this] { switchTab(); });
  buttonNavigator_.onPreviousContinuous([this] { switchTab(-1); });
}

void TextSettingsActivity::textSettingsScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<TextSettingsActivity*>(user)->buildTextSettingsScreen(screen);
}

void TextSettingsActivity::buildTextSettingsScreen(UiApp::ScreenType& screen) {
  // Content sits below the preview pane (render() draws header + preview
  // directly) and above the caption band + button hints.
  const int tabTop = afterHeader + previewHeight;
  const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(tabTop), 0, static_cast<int16_t>(bottomReserved + captionHeight), 0});

  // Tab bar. The selected pill dims to a dither when the selection is down in
  // the list (the legacy focused/unfocused tab distinction).
  constexpr int tabCount = static_cast<int>(Tab::Count);
  fui::TabItem tabs[tabCount];
  for (int t = 0; t < tabCount; t++) {
    tabs[t].label = I18N.get(TAB_NAME_IDS[t]);
    tabs[t].value = static_cast<int16_t>(t);
    tabs[t].selected = tab_ == static_cast<Tab>(t);
  }
  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = tabCount;
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  // Pill shape and label size are theme-driven; same treatment as
  // SettingsActivity (label-hugging Lyra pill vs full-slot RoundedRaff pill).
  const bool tabsFocused = selectedIndex() == 0;
  if (metrics_.tabPillFullSlot) {
    tabProps.text = screen.theme().bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = screen.theme().smallText;
    // Unfocused state: no bottom inset, so the pill (and the 2px selected
    // underline drawn along its bottom edge) reaches the band's 1px divider —
    // legacy Lyra drew the underline sitting on that rule, not floating above.
    tabProps.tabInset = tabsFocused ? fui::Insets{2, 2, 4, 2} : fui::Insets{2, 2, 0, 2};
    tabProps.contentInset = fui::Insets{2, 8, 2, 8};
  }
  const int16_t tabLineHeight = screen.target().lineHeight(tabProps.text.font);
  const int16_t tabBand =
      static_cast<int16_t>(metrics_.tabBarHeight > tabLineHeight + 10 ? metrics_.tabBarHeight : tabLineHeight + 10);
  // Legacy Lyra two-state treatment: with the selection on the tab band, the
  // band fills gray and the active tab is a solid pill; with the selection
  // down in the list, the band is plain and the active tab keeps a gray box
  // with an underline. The 1px rule under the band is always there.
  tabProps.divider = true;
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  if (tabsFocused) {
    tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else if (metrics_.tabPillFullSlot) {
    // Legacy RoundedRaff unfocused treatment: same pill, dimmed to dark gray,
    // text stays inverted; no underline.
    tabStyles.selected.background = fui::Paint::dither(fui::Color::DarkGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabProps.selectedUnderline = 2;
  }
  // Focus/flash states keep the pill instead of falling back to an unset
  // (blank) style.
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;
  const fui::Rect tabRect = screen.takeTop(tabBand);
  // Focused band wash is the Lyra treatment; legacy RoundedRaff keeps the
  // band plain in both states.
  if (tabsFocused && !metrics_.tabPillFullSlot) {
    screen.target().fill(tabRect, fui::Paint::dither(fui::Color::LightGray));
  }
  fui::tabBar(screen.frame(), tabRect, tabProps);
  screen.spacer(static_cast<int16_t>(metrics_.verticalSpacing));

  // Active tab's rows. Values are built per render and owned for the draw only.
  const int count = currentListSize();
  std::vector<std::string> values(count);
  std::vector<fui::ListItem> items;
  items.reserve(count);
  for (int i = 0; i < count; i++) {
    fui::ListItem item;
    switch (tab_) {
      case Tab::Family:
        item.label = fonts_[i].name.c_str();
        if (i == currentFamilyIndex_) values[i] = tr(STR_SELECTED);
        break;
      case Tab::Size:
        item.label = sizes_[i].name.c_str();
        if (i == currentSizeIndex_) values[i] = tr(STR_SELECTED);
        break;
      case Tab::Layout:
        item.label = I18N.get(LAYOUT_ROW_NAME_IDS[i]);
        values[i] = layoutValueText(i);
        break;
      case Tab::Style:
        item.label = I18N.get(STYLE_ROW_NAME_IDS[i]);
        values[i] = styleValueText(i);
        break;
      default:
        break;
    }
    if (!values[i].empty()) item.value = values[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex() - 1);  // -1 = tab band focused
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  const auto rows = fui::listVisibleRows(screen.body(), screen.theme().rowHeight, screen.theme().listRowGap);
  visibleRows = rows > 0 ? rows : 1;
  if (followPending_) {
    // Screen entry / tab switch: show the tab's remembered selection (the
    // current family/size), or the top when the tab bar holds the focus.
    topIndex() = selectedIndex() > 0 ? followListSelection(selectedIndex() - 1, topIndex(), visibleRows, count) : 0;
    followPending_ = false;
  }
  topIndex() = scrollListBy(topIndex(), 0, visibleRows, count);  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex());
  screen.list(props);
}

const char* TextSettingsActivity::confirmLabelText() const {
  if (selectedIndex() == 0) {
    // Confirm on the tab bar advances to the next tab.
    return I18N.get(TAB_NAME_IDS[(static_cast<int>(tab_) + 1) % static_cast<int>(Tab::Count)]);
  }
  switch (tab_) {
    case Tab::Layout:
      // Extra Paragraph Spacing toggles; the rest open a picker
      return selectedIndex() - 1 == static_cast<int>(LayoutRow::ParaSpacing) ? tr(STR_TOGGLE) : tr(STR_SELECT);
    case Tab::Style:
      return tr(STR_TOGGLE);
    default:
      return tr(STR_SELECT);
  }
}

void TextSettingsActivity::render(RenderLock&&) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;  // picker draws over everything

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_TEXT_SETTINGS));

  const char* familyName = (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                               ? fonts_[currentFamilyIndex_].name.c_str()
                               : "";
  const char* sizeName = (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                             ? sizes_[currentSizeIndex_].name.c_str()
                             : "";
  textsettings::renderPreview(renderer, previewLayout_, metrics_.previewPadding, metrics_.verticalSpacing, afterHeader,
                              previewHeight, familyName, sizeName);

  // Tab bar + active tab's list draw inside the screen builder.
  uiReady = false;
  app.render();
  uiReady = true;

  if (focusedRowHasNoPreview()) {
    const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
    const int capY = afterHeader + usableHeight - captionHeight + metrics_.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics_.previewPadding, capY, tr(STR_NOT_IN_PREVIEW));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabelText(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// Font switching runs on the main task from loop(), which deliberately holds no
// RenderLock. ensureLoaded() deletes the resident SdCardFont before loading the
// next one, and the render task walks that same object inside the preview's
// prewarmCache() — so without this lock a font switch can free the mini glyph
// arrays out from under prewarmStyle() (crash: null s.miniGlyphs mid-read/sort).
void TextSettingsActivity::applyFamily(int listIndex) {
  RenderLock lock;
  const auto& font = fonts_[listIndex];
  if (font.isBuiltin) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
    sdFontSystem.ensureLoaded(renderer);  // unloads the previously resident SD font
    currentFamilyIndex_ = listIndex;
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      sdFontSystem.ensureLoaded(renderer);
      currentFamilyIndex_ = listIndex;
    }
  }

  if (currentFamilyIndex_ != listIndex) return;  // switch failed — keep the old size list

  // The new family ships its own set of point sizes, and ensureLoaded() may have
  // snapped the selection into it, so the Size tab's list and its nav position
  // both have to be rebuilt.
  rebuildSizeList();
  selectedIndex_[static_cast<int>(Tab::Size)] = currentSizeIndex_ + 1;
}

void TextSettingsActivity::activateRow(int row) {
  switch (tab_) {
    case Tab::Family:
      if (row != currentFamilyIndex_) {
        applyFamily(row);
        // Persist immediately (like SettingsActivity's per-change saves): the
        // parent's result callback only runs on a normal finish(), so relying
        // on it loses the change when this screen is left via the home
        // gesture/key or a sleep. Saved here, not inside applyFamily, so the
        // SD write happens outside its RenderLock.
        if (currentFamilyIndex_ == row) {
          SETTINGS.saveToFile();
        }
        requestUpdate();
      }
      break;
    case Tab::Size:
      if (row != currentSizeIndex_) {
        applySize(row);
        SETTINGS.saveToFile();
        requestUpdate();
      }
      break;
    case Tab::Layout:
      confirmLayoutRow(row);
      break;
    case Tab::Style:
      confirmStyleRow(row);
      break;
    default:
      break;
  }
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font
// file, which frees and replaces the SdCardFont the render task may be reading.
void TextSettingsActivity::applySize(int listIndex) {
  RenderLock lock;

  currentSizeIndex_ = listIndex;
  SETTINGS.fontPointSize = sizes_[listIndex].pointSize;
  sdFontSystem.ensureLoaded(renderer);
}

void TextSettingsActivity::confirmLayoutRow(int row) {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::ParaSpacing:
      SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::LineSpacing:
      optionPopup_.show(StrId::STR_LINE_SPACING, LINE_SPACING_IDS, static_cast<int>(std::size(LINE_SPACING_IDS)),
                        SETTINGS.lineSpacing, [](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      break;
    case LayoutRow::Alignment:
      optionPopup_.show(StrId::STR_ALIGNMENT, ALIGNMENT_IDS, static_cast<int>(std::size(ALIGNMENT_IDS)),
                        SETTINGS.paragraphAlignment, [](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      break;
    case LayoutRow::ScreenMargin: {
      std::vector<std::string> options;
      options.reserve((MARGIN_MAX - MARGIN_MIN) / MARGIN_STEP + 1);
      for (int m = MARGIN_MIN; m <= MARGIN_MAX; m += MARGIN_STEP) options.push_back(std::to_string(m));
      const int cur = (std::clamp<int>(SETTINGS.screenMargin, MARGIN_MIN, MARGIN_MAX) - MARGIN_MIN) / MARGIN_STEP;
      optionPopup_.show(StrId::STR_SCREEN_MARGIN, options, cur, [](int idx) {
        SETTINGS.screenMargin = static_cast<uint8_t>(MARGIN_MIN + idx * MARGIN_STEP);
        SETTINGS.saveToFile();
      });
      requestUpdate();
      break;
    }

    default:
      break;
  }
}

std::string TextSettingsActivity::layoutValueText(int row) const {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::LineSpacing: {
      const uint8_t v = SETTINGS.lineSpacing;
      return v < std::size(LINE_SPACING_IDS) ? I18N.get(LINE_SPACING_IDS[v]) : I18N.get(StrId::STR_NORMAL);
    }
    case LayoutRow::ParaSpacing:
      return SETTINGS.extraParagraphSpacing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LayoutRow::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      return v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
    }
    case LayoutRow::ScreenMargin:
      return std::to_string(SETTINGS.screenMargin);

    default:
      return "";
  }
}

void TextSettingsActivity::confirmStyleRow(int row) {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::FocusReading:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      break;
    case StyleRow::Hyphenation:
      SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
      break;
    case StyleRow::EmbeddedStyle:
      SETTINGS.embeddedStyle = !SETTINGS.embeddedStyle;
      break;
    case StyleRow::AntiAliasing:
      SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
      break;

    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

std::string TextSettingsActivity::styleValueText(int row) const {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::FocusReading:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::Hyphenation:
      return SETTINGS.hyphenationEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::EmbeddedStyle:
      return SETTINGS.embeddedStyle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

    default:
      return "";
  }
}

// Only Focus Reading shows in the preview (bold prefixes); the other Style rows
// have no distinct preview.
bool TextSettingsActivity::focusedRowHasNoPreview() const {
  if (selectedIndex() == 0 || tab_ != Tab::Style) return false;
  const StyleRow row = static_cast<StyleRow>(selectedIndex() - 1);
  return row == StyleRow::Hyphenation || row == StyleRow::EmbeddedStyle || row == StyleRow::AntiAliasing;
}

void TextSettingsActivity::switchTab(int direction) {
  const bool onTabBar = selectedIndex() == 0;
  constexpr int tabCount = static_cast<int>(Tab::Count);
  tab_ = static_cast<Tab>((static_cast<int>(tab_) + direction + tabCount) % tabCount);
  if (onTabBar) selectedIndex() = 0;
  followPending_ = true;  // pull the new tab's viewport to its remembered selection
  requestUpdate();
}

int TextSettingsActivity::currentListSize() const {
  switch (tab_) {
    case Tab::Family:
      return static_cast<int>(fonts_.size());
    case Tab::Size:
      return static_cast<int>(sizes_.size());
    case Tab::Layout:
      return static_cast<int>(LayoutRow::Count);
    case Tab::Style:
      return static_cast<int>(StyleRow::Count);

    default:
      return 0;
  }
}

int& TextSettingsActivity::selectedIndex() { return selectedIndex_[static_cast<int>(tab_)]; }
int TextSettingsActivity::selectedIndex() const { return selectedIndex_[static_cast<int>(tab_)]; }
int& TextSettingsActivity::topIndex() { return topIndex_[static_cast<int>(tab_)]; }
