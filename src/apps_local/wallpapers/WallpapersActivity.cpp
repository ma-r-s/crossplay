#include "WallpapersActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../CrossPointSettings.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WallpapersCore.h"

namespace fui = freeink::ui;

namespace {
// A wallpaper library will not be huge, but a runaway directory should not pin
// unbounded heap in the list model. Well past any sane collection.
constexpr int kMaxLibrary = 256;
constexpr size_t kNameMax = 128;
// The pinned copy is small; a page at a time keeps the stack flat.
constexpr size_t kCopyChunk = 4096;
}  // namespace

std::unique_ptr<Activity> WallpapersActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WallpapersActivity>(renderer, mappedInput);
}

void WallpapersActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Make the folder so the browser uploader and File Transfer have somewhere to
  // land. Best-effort: if it fails the scan simply finds nothing and the empty
  // state explains how to add wallpapers.
  Storage.mkdir(wallpapers::kLibraryDir);
  scanLibrary();
  loadActive();
  computeWarning();
  requestUpdate();
}

void WallpapersActivity::scanLibrary() {
  names_.clear();
  auto dir = Storage.open(wallpapers::kLibraryDir);
  if (!dir || !dir.isDirectory()) return;

  auto name = makeUniqueNoThrow<char[]>(kNameMax);
  if (!name) {
    LOG_ERR("WALL", "OOM: wallpaper name buffer");
    return;
  }
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax);
    if (!wallpapers::isSupportedWallpaper(std::string_view{name.get()})) continue;
    names_.emplace_back(name.get());
    if (static_cast<int>(names_.size()) >= kMaxLibrary) break;
  }
  std::sort(names_.begin(), names_.end());
  if (selected_ >= static_cast<int>(names_.size())) selected_ = 0;
}

void WallpapersActivity::loadActive() {
  activeIndex_ = -1;
  // The marker is only trusted when the pin it names actually exists and the
  // sleep mode is CUSTOM -- otherwise the sleep screen is showing something
  // else and marking a row "ON" would be a lie.
  if (SETTINGS.sleepScreen != CrossPointSettings::CUSTOM) return;
  if (!Storage.exists(wallpapers::kPinnedSleep)) return;

  char marker[kNameMax] = {};
  if (Storage.readFileToBuffer(wallpapers::kActiveMarker, marker, sizeof(marker)) == 0) return;
  // The marker is a bare file name; trim any trailing newline the writer left.
  for (char* p = marker; *p; ++p) {
    if (*p == '\n' || *p == '\r') {
      *p = '\0';
      break;
    }
  }
  for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
    if (names_[i] == marker) {
      activeIndex_ = i;
      selected_ = i;
      break;
    }
  }
}

void WallpapersActivity::computeWarning() {
  warning_.clear();
  uint64_t free = 0;
  const bool ok = Storage.freeBytes(free);
  switch (wallpapers::roomFor(ok, free, wallpapers::kCardFloorBytes)) {
    case wallpapers::Room::Ok:
      break;
    case wallpapers::Room::TooFull:
      // Two facts, two sentences: this is FULL, not UNKNOWN. Sized by who pays
      // when the card fills (a book download, a deck's review log), not by this
      // app's own tiny write.
      warning_ = "Card is low on space. New wallpapers or books may not save.";
      break;
    case wallpapers::Room::Unknown:
      // Never claim the card is full when the truth is "could not tell".
      warning_ = "Could not check card space.";
      break;
  }
}

bool WallpapersActivity::setWallpaper(int index) {
  if (index < 0 || index >= static_cast<int>(names_.size())) return false;

  std::string src = std::string(wallpapers::kLibraryDir) + "/" + names_[static_cast<size_t>(index)];
  HalFile in;
  if (!Storage.openFileForRead("WALL", src, in)) {
    LOG_ERR("WALL", "Cannot open wallpaper %s", src.c_str());
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("WALL", wallpapers::kPinnedSleep, out)) {
    LOG_ERR("WALL", "Cannot open %s for write", wallpapers::kPinnedSleep);
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kCopyChunk);
  if (!buffer) {
    LOG_ERR("WALL", "OOM: copy buffer");
    return false;
  }
  for (;;) {
    const int got = in.read(buffer.get(), kCopyChunk);
    if (got < 0) {
      LOG_ERR("WALL", "Read error copying wallpaper");
      return false;
    }
    if (got == 0) break;
    if (out.write(buffer.get(), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
      LOG_ERR("WALL", "Write error pinning wallpaper (card full?)");
      return false;
    }
  }
  out.close();
  in.close();

  // Point the sleep system at the pinned copy and remember which library file
  // it was, so the picker can mark it.
  SETTINGS.sleepScreen = CrossPointSettings::CUSTOM;
  SETTINGS.saveToFile();

  HalFile marker;
  if (Storage.openFileForWrite("WALL", wallpapers::kActiveMarker, marker)) {
    const std::string& n = names_[static_cast<size_t>(index)];
    marker.write(reinterpret_cast<const uint8_t*>(n.c_str()), n.size());
    marker.close();
  }

  activeIndex_ = index;
  selected_ = index;
  LOG_INF("WALL", "Set sleep wallpaper: %s", names_[static_cast<size_t>(index)].c_str());
  return true;
}

void WallpapersActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  // Physical side keys move the highlight, so a library taller than the panel
  // is reachable without the un-tappable overflow track (see shelf.md).
  if (!names_.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selected_ = std::min(selected_ + 1, static_cast<int>(names_.size()) - 1);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selected_ = std::max(selected_ - 1, 0);
      requestUpdate();
      return;
    }
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);

  switch (event.action) {
    case wallpapersui::ActionPick:
      // A row tap sets that wallpaper as the sleep screen at once.
      if (setWallpaper(event.value)) requestUpdate();
      return;
    default:
      return;
  }
}

void WallpapersActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen surface(frame);

  if (names_.empty()) {
    wallpapersui::EmptyModel model;
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    wallpapersui::buildEmpty(surface, model);
  } else {
    char count[24];
    snprintf(count, sizeof(count), "%d SAVED", static_cast<int>(names_.size()));
    rightLabel_ = count;

    std::vector<wallpapersui::Entry> entries;
    entries.reserve(names_.size());
    for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
      wallpapersui::Entry entry;
      entry.name = names_[static_cast<size_t>(i)].c_str();
      entry.active = (i == activeIndex_);
      entries.push_back(entry);
    }

    wallpapersui::PickerModel model;
    model.items = entries.data();
    model.count = static_cast<int>(entries.size());
    model.selected = selected_;
    model.rightLabel = rightLabel_.c_str();
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    wallpapersui::buildPicker(surface, model);
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Wallpapers");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
