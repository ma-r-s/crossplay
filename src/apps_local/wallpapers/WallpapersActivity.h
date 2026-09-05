#pragma once

// Wallpapers on the device. The thin layer: renderer, storage, input, shelf.
//
// The app is a face for the sleep system that already exists. It shows the BMPs
// in /wallpapers as a two-column grid of thumbnails, and a tap on one pins it as
// the sleep screen by copying it to /sleep.bmp -- the single image the sleep
// screen already checks first -- and switching the sleep mode to CUSTOM. The
// set wallpaper wears a thick border. SleepActivity is untouched.
//
// A thumbnail is a full 480x800 1-bit BMP box-downscaled into a cell on the
// device (the renderer's 1-bit blit cannot scale, so the app does it), decoded
// once per page and cached in RAM so moving the border does not re-read the SD.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WallpapersScreens.h"

class WallpapersActivity final : public Activity {
 public:
  WallpapersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wallpapers", renderer, mappedInput) {}
  ~WallpapersActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // A wallpaper scaled down to a cell: a 1-bpp bit buffer (set bit = ink) plus
  // where it sits inside the cell once its aspect is fitted.
  struct Thumb {
    int16_t w = 0;
    int16_t h = 0;
    int16_t ox = 0;
    int16_t oy = 0;
    std::vector<uint8_t> bits;  // packed, (w+7)/8 bytes per row
    bool ok = false;
  };

  void scanLibrary();
  void loadActive();
  void computeWarning();
  bool setWallpaper(int index);  // copy /wallpapers/<index> -> /sleep.bmp
  int pageCount() const;         // over the whole library
  void clampPage();
  void ensureThumbsForPage();  // decode this page's cells if not cached
  Thumb decodeThumb(const std::string& path, int16_t cellW, int16_t cellH) const;
  void drawGrid(const wallpapersui::GridGeom& geom);

  std::vector<std::string> names_;  // library file names, sorted
  int activeIndex_ = -1;            // which name is pinned, or -1
  int page_ = 0;
  std::string rightLabel_;
  std::string warning_;

  // The current page's thumbnails, one per on-page slot (perPage entries;
  // trailing empty slots have ok = false).
  std::vector<Thumb> thumbs_;
  int cachedPage_ = -1;
  int cachedPerPage_ = -1;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;

  // The grid is hit-tested against geometry and never reaches route(), so its
  // taps are gated on the page and the pinned wallpaper -- state loop() settles
  // before render() runs. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;
};
