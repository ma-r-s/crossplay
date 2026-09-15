#pragma once

// Notes, card #516.
//
// RIGHT NOW THIS IS A GALLERY, not the app. It walks the nine screen proposals
// so they can be photographed on the real panel with the real cuts, because a
// mockup in any other medium is a drawing of a screen this device may not be
// able to make. Every proposal that has ever been judged from an SVG in this
// fork was judged wrong.
//
// A tap anywhere advances to the next proposal. The Activity that replaces it
// keeps this file's three-way split: state and storage here, layout in
// NotesScreens, rules in NotesCore.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"

class NotesActivity final : public Activity {
 public:
  NotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Notes", renderer, mappedInput) {}
  ~NotesActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The order the gallery walks. Deck, note, add: three proposals each, then
  // the two states every one of them has to survive.
  enum class Proposal : uint8_t {
    DeckBar,
    DeckRow,
    NoteBar,
    NoteRow,
    Menu,
    MenuNoWifi,
    DeckShort,
    NoteShort,
    DeckEmpty,
    kCount,
  };

  Proposal proposal_ = Proposal::DeckBar;
  toybox::Interactions interactions_;
};
