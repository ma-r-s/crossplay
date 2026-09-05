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

  // What the screen is showing. Derived from what is on the card, never from a
  // remembered "have I offered already" flag: a stored value that decides what
  // you see turns a reproducible screen into a nondeterministic one
  // (invisible-saved-state-reads-as-nondeterminism).
  enum class View : uint8_t { Grid, Offer, Fetching, Notice, Help };
  View view_ = View::Grid;

  void scanLibrary();
  int builtInsPresent() const;  // how many of the built-in set are on the card
  void pickView();              // Grid or Offer, from the card alone
  void sweepPartFiles();        // drop incomplete unpacks left by a power cut
  void startSetDownload();      // ask for WiFi, then queue the fetch
  void onWifiChosen(bool connected);
  void runSetDownload();  // blocking: fetch the pack, then unpack it
  bool unpackSet();       // pack -> individual .bmp files, resumable
  void showNotice(const char* headline, const char* body, const char* actionLabel, freeink::ui::ActionId action);
  void loadActive();
  void computeWarning();
  bool setWallpaper(int index);  // copy /wallpapers/<index> -> /sleep.bmp
  int pageCount() const;         // over the whole library
  void clampPage();
  void ensureThumbsForPage();  // decode this page's cells if not cached
  Thumb decodeThumb(const std::string& path, int16_t cellW, int16_t cellH) const;
  void drawGrid(const wallpapersui::GridGeom& geom);
  void drawAddTile(const wallpapersui::GridGeom& geom, const freeink::ui::Rect& th);
  void drawGetSetTile(const wallpapersui::GridGeom& geom, const freeink::ui::Rect& th) const;
  int specialTiles() const;  // chrome tiles in front of the wallpapers
  void drawMarker(const freeink::ui::Rect& th) const;

  std::vector<std::string> names_;  // library file names, sorted
  int activeIndex_ = -1;            // which name is pinned, or -1
  int builtInsMissing_ = 0;         // how many of the built-in set are not on the card
  bool warningPending_ = false;     // the free-space walk, deferred until after the first paint
  bool painted_ = false;            // the panel has shown something at least once
  int page_ = 0;
  int fetchDone_ = 0;
  int fetchTotal_ = 0;
  bool fetchCancel_ = false;
  bool fetchUnpacking_ = false;  // second phase: the bar's second half
  bool fetchQueued_ = false;
  std::string noticeHead_;
  std::string noticeBody_;
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;
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
