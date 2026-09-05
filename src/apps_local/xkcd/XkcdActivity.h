#pragma once

// The xkcd reader. The only part of the app that needs hardware: the pack on
// the card, the renderer, the touch panel and the radio. Everything it can
// answer without those lives in XkcdCore (freestanding, host-tested) or
// XkcdScreens (freestanding, host-tested).
//
// **Portrait, always.** The panel is never rotated. Comics that want to be
// read sideways are stored already turned, so the reader turns the device and
// the screen layout stays put -- the bar and the controls never move. An
// earlier version rotated the panel per comic and shuffled the whole UI around
// underneath the reader.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxFormat.h"
#include "XkcdCore.h"
#include "XkcdScreens.h"

class HalFile;

class XkcdActivity final : public Activity {
 public:
  XkcdActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Xkcd", renderer, mappedInput) {}
  ~XkcdActivity() override;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, List, Reader, Alt, Notice, Updating, Number };

  // --- the pack -----------------------------------------------------------
  bool openArchive();
  void closeArchive();
  bool loadComic(int position);

  // --- drawing ------------------------------------------------------------
  void drawComic();
  void drawMosaic(const freeink::ui::Rect& band);

  // --- panning ------------------------------------------------------------
  // Streams the rows xkcd::gapWindowFor asks for past xkcd::rowIsGap and hands
  // the flags back to the core. The window is read here rather than held: one
  // byte a row instead of ninety-three.
  void pan(bool down);

  // --- state --------------------------------------------------------------
  void markRead(uint16_t num);
  bool isRead(uint16_t num) const;
  void loadReadState();
  void saveReadState();

  void openList(int firstPosition);
  void typeDigit(int digit);
  void backspace();
  bool typedIsOnCard() const;
  void goToTyped();
  void fillListRows();
  void openComicAt(int position);
  void showNotice(const char* headline, const char* detail, const char* actionLabel = nullptr,
                  freeink::ui::ActionId action = freeink::ui::NO_ACTION);

  // --- the internet -------------------------------------------------------
  void beginUpdate();
  void onWifiChosen(bool connected);
  bool fetchOne(uint16_t num, char* whyNot, int whyNotCap);
  void runUpdate();
  void runPackDownload();

  void handleAction(freeink::ui::ActionId action, int16_t value);

  // The three files, kept open for the life of the activity: the list screen
  // touches the index on every scroll and the reader touches the artwork on
  // every step, so opening per read would cost a FAT traversal each time.
  std::unique_ptr<HalFile> indexFile_;
  std::unique_ptr<HalFile> imageFile_;
  std::unique_ptr<HalFile> textFile_;
  std::unique_ptr<xkcd::ByteSource> indexSrc_;
  std::unique_ptr<xkcd::ByteSource> imageSrc_;
  std::unique_ptr<xkcd::ByteSource> textSrc_;

  xkcd::Archive archive_;
  bool archiveOpen_ = false;

  View view_ = View::Menu;

  // The comic on screen.
  xkcd::Comic comic_{};
  int position_ = -1;
  xkcd::Position at_{};
  char title_[64] = {0};
  char alt_[512] = {0};

  // The browse list. A page at a time rather than the whole archive: 3281
  // records is 105KB and the screen shows eight.
  static constexpr int kPageRows = 8;
  static constexpr int kRowTextCap = 56;
  int listFirst_ = 0;
  int listSelected_ = 0;
  // "%u  %s": the comic number and the WHOLE of a kRowTextCap title. Sized at
  // kRowTextCap the number ate the title's tail, and this is the one place in
  // the fork where that cut was reachable -- xkcd titles run past fifty
  // characters and the row lost them with no ellipsis to say so. The title is
  // still capped at kRowTextCap on the way in; that cap is deliberate, this
  // second one was not. 96 bytes across the eight rows.
  static constexpr int kRowLabelCap = toybox::kUIntChars + toybox::literalChars("  ") + kRowTextCap;
  char rowLabels_[kPageRows][kRowLabelCap] = {};
  char rowValues_[kPageRows][16] = {};
  freeink::ui::ListItem rowItems_[kPageRows] = {};
  int rowCount_ = 0;
  char listRight_[24] = {0};

  // One bit per comic number. 3300 comics is 413 bytes, so this is held whole
  // rather than paged: the menu's mosaic asks about sixty of them at once and
  // a seek per bit would make the front door slower than the reader.
  static constexpr int kReadBitsBytes = 1024;  // covers comic numbers up to 8191
  uint8_t readBits_[kReadBitsBytes] = {};
  bool readDirty_ = false;
  int readCount_ = 0;

  // The number being typed on the go-to pad. Five digits is past any comic
  // number xkcd will reach in a lifetime, and the buffer refuses a sixth
  // rather than wrapping.
  char typed_[6] = {0};
  int typedLen_ = 0;

  // The notice screen's text, owned here because the model only borrows it.
  char noticeHead_[48] = {0};
  char noticeBody_[256] = {0};
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = freeink::ui::NO_ACTION;

  // The update.
  bool wifiConnected_ = false;
  bool updateQueued_ = false;
  bool downloadQueued_ = false;
  bool downloadCancel_ = false;
  int fetched_ = 0;
  int waiting_ = -1;

  toybox::Interactions interactions_{};
  bool interactionsReady_ = false;
};
