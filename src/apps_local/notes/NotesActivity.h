#pragma once

// Notes, card #516: a deck of cards you tick with one hand.
//
// The three-way split every app in this fork uses. NotesCore holds the rules
// (what a task line is, what a tick does, what clearing removes) and has a host
// suite. NotesScreens lays out, and can be driven by host-tests/ui. This file
// keeps what genuinely needs hardware: the card, the keyboard, and which screen
// is on the panel.
//
// There is no settings screen. Nothing here has two defensible values: the
// order is alphabetical because that is the order a person can predict, a note
// is as long as it is, and the one thing that looks like a setting -- "type on
// your phone" -- is a per-note action that must never become a mode. The rare
// and destructive things are on a menu sheet over the note they belong to,
// which is where Wallpapers put its own.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../../network/CrossPointWebServer.h"
#include "../ui/ToyboxScreen.h"
#include "NotesCore.h"
#include "NotesLibrary.h"
#include "NotesScreens.h"

class NotesActivity final : public Activity {
 public:
  NotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Notes", renderer, mappedInput) {}
  ~NotesActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // The reader must not fall asleep with its own web server up: the page would
  // stop answering mid-sentence and the phone would blame the network.
  bool preventAutoSleep() override { return server_ && server_->isRunning(); }
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class View : uint8_t { Deck, Note, Menu, Confirm, Notice, Phone };

  void openNote(int index);
  void openDeck();
  void toggleTask(int index);
  void clearDone();
  void askNewName();
  void askRename();
  void askLine();
  void showNotice(const std::string& text);
  void startPhone();
  void stopPhone();
  void reloadNote();
  void rebuildRows();
  bool anyDone() const;
  bool nameFitsBand(const std::string& name);
  void relabelDeck();
  void relabelNote();
  // How many rows fit one page, asked of the same layout that draws them, so
  // the label, the physical keys and the rows on the glass cannot disagree.
  int notePageSize();
  int deckPageSize();

  notes::Library library_;
  View view_ = View::Deck;

  // The open note: its name, its bytes, and the parsed lines that index into
  // those bytes. The lines are rebuilt whenever doc_ changes, because a Line is
  // a byte range and a stale one points into a string that has moved.
  std::string openName_;
  std::string doc_;
  std::vector<notes::Line> lines_;

  // Row storage for the screens, owned here and rebuilt when the data changes.
  // A std::vector built inside a screen builder allocates on every paint.
  std::vector<notesui::DeckItem> deckRows_;
  std::vector<notesui::Task> taskRows_;
  std::vector<std::string> deckTallies_;
  std::vector<std::string> taskTexts_;

  int deckTop_ = 0;
  int noteTop_ = 0;
  std::string deckPage_;
  std::string notePage_;
  std::string notice_;

  // The phone page. Owned here and torn down in exactly one place, because a
  // yield taken and not returned leaves Developer Mode without its ports for
  // the rest of the session.
  std::unique_ptr<CrossPointWebServer> server_;
  bool devPaused_ = false;
  bool phoneSaved_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
