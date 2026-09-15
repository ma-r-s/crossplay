#include "NotesActivity.h"

#include <ESPmDNS.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "../../DevMode.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"
#include "NotesCore.h"

namespace fui = freeink::ui;

namespace {

// A name is a filename, capped where the library caps one. KeyboardEntry's
// maxLength counts BYTES, which is what this is.
constexpr size_t kNameMax = 64;
constexpr size_t kLineMax = 200;

}  // namespace

std::unique_ptr<Activity> NotesActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<NotesActivity>(renderer, mappedInput);
}

void NotesActivity::onEnter() {
  Activity::onEnter();
  // The Toybox cuts are registered by the app that wants them rather than by
  // main.cpp, so they cost no upstream surface. Without this every string on
  // every screen asks for a face the renderer does not have and draws nothing
  // at all -- not a box, not a fallback: nothing.
  toybox::ensureFonts(renderer);

  if (!library_.begin()) {
    showNotice("The card would not open, so nothing was changed.");
    return;
  }
  openDeck();
}

// --- Rows ----------------------------------------------------------------

void NotesActivity::rebuildRows() {
  const std::vector<notes::Entry>& entries = library_.entries();
  deckTallies_.clear();
  deckTallies_.reserve(entries.size());
  for (const notes::Entry& entry : entries) {
    char tally[28];
    std::snprintf(tally, sizeof(tally), "%d/%d", entry.done, entry.total);
    deckTallies_.emplace_back(entry.hasTasks ? tally : "");
  }
  // Pointers are taken in a SECOND pass. emplace_back can move every string it
  // has already stored, so a c_str() taken during the first loop points into a
  // buffer the vector has since freed.
  deckRows_.clear();
  deckRows_.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); i++) {
    notesui::DeckItem row;
    row.title = entries[i].name.c_str();
    row.tally = deckTallies_[i].empty() ? nullptr : deckTallies_[i].c_str();
    deckRows_.push_back(row);
  }

  taskTexts_.clear();
  taskTexts_.reserve(lines_.size());
  for (size_t i = 0; i < lines_.size(); i++) {
    const notes::Line& line = lines_[i];
    // A note ending in a newline parses one trailing empty line. It is a real
    // line to somebody editing the file, and a blank row on a panel that holds
    // still is a hole, so the last empty one is not drawn.
    if (line.begin >= line.end && i + 1 == lines_.size()) break;
    const std::string text = notes::textOf(doc_, line);
    taskTexts_.push_back(line.isTask ? text : notes::stripHeading(text));
  }
  taskRows_.clear();
  taskRows_.reserve(taskTexts_.size());
  for (size_t i = 0; i < taskTexts_.size(); i++) {
    notesui::Task row;
    row.text = taskTexts_[i].c_str();
    row.checked = lines_[i].checked;
    row.isTask = lines_[i].isTask;
    taskRows_.push_back(row);
  }
}

bool NotesActivity::anyDone() const {
  for (const notesui::Task& task : taskRows_) {
    if (task.isTask && task.checked) return true;
  }
  return false;
}

int NotesActivity::deckPageSize() {
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  notesui::DeckModel probe;
  probe.items = deckRows_.data();
  probe.count = static_cast<int>(deckRows_.size());
  return notesui::deckCapacity(target, target.deviceContext(), probe);
}

int NotesActivity::notePageSize() {
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  notesui::NoteModel probe;
  probe.tasks = taskRows_.data();
  probe.count = static_cast<int>(taskRows_.size());
  return notesui::noteCapacity(target, target.deviceContext(), probe);
}

void NotesActivity::relabelDeck() {
  const int page = deckPageSize();
  const int count = static_cast<int>(deckRows_.size());
  deckPage_.clear();
  if (page <= 0 || count <= page) return;
  char label[32];
  std::snprintf(label, sizeof(label), "%d / %d", deckTop_ / page + 1, (count + page - 1) / page);
  deckPage_ = label;
}

void NotesActivity::relabelNote() {
  const int page = notePageSize();
  const int count = static_cast<int>(taskRows_.size());
  notePage_.clear();
  if (page <= 0 || count <= page) return;
  char label[32];
  std::snprintf(label, sizeof(label), "%d / %d", noteTop_ / page + 1, (count + page - 1) / page);
  notePage_ = label;
}

// --- Navigation ----------------------------------------------------------

void NotesActivity::openDeck() {
  view_ = View::Deck;
  openName_.clear();
  doc_.clear();
  lines_.clear();
  deckTop_ = 0;
  // Re-read on every entry. The card can be edited from a computer between
  // sessions, and an app that trusts a cached list offers notes that are not
  // there any more.
  library_.scan();
  rebuildRows();
  relabelDeck();
  interactionsReady_ = false;
  requestUpdate();
}

void NotesActivity::reloadNote() {
  doc_.clear();
  library_.load(openName_, doc_);
  lines_ = notes::parse(doc_);
  rebuildRows();
  const int count = static_cast<int>(taskRows_.size());
  if (noteTop_ >= count) noteTop_ = 0;
  relabelNote();
}

void NotesActivity::openNote(const int index) {
  if (index < 0 || index >= library_.count()) return;
  openName_ = library_.entries()[index].name;
  noteTop_ = 0;
  reloadNote();
  view_ = View::Note;
  interactionsReady_ = false;
  requestUpdate();
}

void NotesActivity::showNotice(const std::string& text) {
  notice_ = text;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

// --- Edits ---------------------------------------------------------------

void NotesActivity::toggleTask(const int index) {
  if (index < 0 || index >= static_cast<int>(taskRows_.size())) return;
  if (!notes::toggle(doc_, lines_[index])) return;

  // Written NOW, not on the way out. A tick a person saw and the card did not
  // is the failure mode of every app that saves on exit, and this one is used
  // one-handed in a shop with the power button under a thumb.
  std::string message;
  if (!library_.save(openName_, doc_, message)) {
    notes::toggle(doc_, lines_[index]);  // put the model back beside the file
    showNotice(message);
    return;
  }
  rebuildRows();
  requestUpdate();
}

void NotesActivity::clearDone() {
  if (!anyDone()) return;
  const std::string before = doc_;
  notes::clearChecked(doc_);
  std::string message;
  if (!library_.save(openName_, doc_, message)) {
    doc_ = before;  // the file is the truth; take back what RAM claimed
    lines_ = notes::parse(doc_);
    rebuildRows();
    showNotice(message);
    return;
  }
  noteTop_ = 0;
  reloadNote();
  view_ = View::Note;
  interactionsReady_ = false;
  requestUpdate();
}

void NotesActivity::askNewName() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "NAME THIS NOTE", "", kNameMax);
  if (!keyboard) {
    showNotice("There was not enough memory to open the keyboard.");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    interactionsReady_ = false;
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto& entered = std::get<KeyboardResult>(result.data);
    std::string message;
    if (!library_.create(entered.text, message)) {
      showNotice(message);
      return;
    }
    // Straight into the note that was just made. Naming one and then having to
    // find it in the deck is a step nobody asked for.
    const std::string made = notes::Library::sanitise(entered.text);
    rebuildRows();
    for (int i = 0; i < library_.count(); i++) {
      if (library_.entries()[i].name == made) {
        openNote(i);
        return;
      }
    }
    openDeck();
  });
}

void NotesActivity::askRename() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "RENAME", openName_, kNameMax);
  if (!keyboard) {
    showNotice("There was not enough memory to open the keyboard.");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    interactionsReady_ = false;
    if (result.isCancelled) {
      view_ = View::Note;
      requestUpdate();
      return;
    }
    const auto& entered = std::get<KeyboardResult>(result.data);
    std::string message;
    if (!library_.rename(openName_, entered.text, message)) {
      showNotice(message);
      return;
    }
    openName_ = notes::Library::sanitise(entered.text);
    reloadNote();
    view_ = View::Note;
    requestUpdate();
  });
}

void NotesActivity::askLine() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "ADD A LINE", "", kLineMax);
  if (!keyboard) {
    showNotice("There was not enough memory to open the keyboard.");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    interactionsReady_ = false;
    if (result.isCancelled) {
      view_ = View::Note;
      requestUpdate();
      return;
    }
    const auto& entered = std::get<KeyboardResult>(result.data);
    if (entered.text.empty()) {
      view_ = View::Note;
      requestUpdate();
      return;
    }

    // Added as a TASK, always. The keyboard cannot type a newline, so a line
    // added here is one line; and this app's reason to exist is a list you
    // tick. Somebody who wanted prose writes it from a computer, while
    // somebody who wanted a task and got prose has no way to make it tickable
    // on the device at all.
    const std::string before = doc_;
    if (!doc_.empty() && doc_.back() != '\n') doc_.push_back('\n');
    doc_ += "- [ ] ";
    doc_ += entered.text;
    doc_.push_back('\n');

    std::string message;
    if (!library_.save(openName_, doc_, message)) {
      doc_ = before;
      lines_ = notes::parse(doc_);
      rebuildRows();
      showNotice(message);
      return;
    }
    reloadNote();
    // Onto the page the new line landed on, so a line added to a long list is
    // visibly there rather than two pages away.
    const int page = notePageSize();
    const int count = static_cast<int>(taskRows_.size());
    if (page > 0 && count > 0) noteTop_ = ((count - 1) / page) * page;
    relabelNote();
    view_ = View::Note;
    requestUpdate();
  });
}

// --- Typing from a phone -------------------------------------------------

void NotesActivity::startPhone() {
#ifndef SIMULATOR
  // NEVER launch the picker unconditionally: WifiSelectionActivity::startWifiScan
  // calls WiFi.disconnect() on every path, so an unguarded launch drops a
  // working association and shows a redundant chooser. Four other apps guard it
  // exactly this way.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                               showNotice("Typing on your phone needs Wi-Fi. Nothing changed.");
                               return;
                             }
                             startPhone();
                           });
    return;
  }
#endif

  // Developer Mode holds 80, 81 and UDP 8134 for as long as its toggle is on,
  // and Mario keeps a device on it. Two binds on one port fail in a way that
  // reads as "the screen is broken", so dev mode yields while this screen is up.
  // Every failure below leaves through stopPhone(), so the yield is released in
  // exactly ONE place no matter which way this goes wrong.
  devmode::pause();
  devPaused_ = true;

  server_ = makeUniqueNoThrow<CrossPointWebServer>(CrossPointWebServer::Surface::NotesOnly);
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
  server_->setNotesFile(std::string("/notes/") + openName_ + ".md", openName_);
  server_->begin();
  // The simulator has no networking shim, so begin() never leaves the server
  // running there. The SCREEN is still drawn, because its layout is the half
  // that can be checked without hardware; what cannot be checked on a laptop is
  // said out loud in the commit rather than assumed.
#ifndef SIMULATOR
  if (!server_->isRunning()) {
    stopPhone();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
#endif

#ifdef SIMULATOR
  // No radio here, so no name and no address to read off one. The screen is
  // still worth drawing: its layout is the half that can be checked without
  // hardware, and the server underneath it really does serve on the host.
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  // THE CODE CARRIES THE ADDRESS, ALWAYS. It is generated from WiFi.localIP()
  // at the moment of drawing and depends on no service, so the only way it can
  // be wrong is DHCP moving this reader between the paint and the scan. The
  // NAME depends on a responder that can fail to start -- and this function
  // already knows when it has -- so encoding that would put a detected fault
  // into the one element a person cannot read.
  phoneUrl_ = "http://" + dotted + "/n";
#ifdef SIMULATOR
  phoneReadable_ = phoneUrl_;
  (void)mdnsUp;
#else
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/n" : phoneUrl_;
#endif
  phoneSaved_ = false;
  view_ = View::Phone;
  interactionsReady_ = false;
  requestUpdate();
}

void NotesActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  // Guarded by the flag rather than by whether a server exists: the
  // out-of-memory path never got one, and resuming a yield this screen does not
  // hold drops the count out from under whoever does.
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
}

void NotesActivity::onExit() {
  stopPhone();
  Activity::onExit();
}

// --- Input ---------------------------------------------------------------

void NotesActivity::loop() {
  // Back is read on the per-frame path, above any "return unless a tap
  // arrived" guard, because the global back-swipe arrives as Button::Back and a
  // swipe is not a tap. host-tests/backgesture enforces this fork-wide.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Deck:
        // An app never names where Back goes; the shelf puts it back in
        // whichever folder opened it.
        shelf::leave(renderer, mappedInput);
        return;
      case View::Note:
        openDeck();
        return;
      case View::Phone:
        stopPhone();
        view_ = View::Note;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case View::Menu:
      case View::Confirm:
      case View::Notice:
        view_ = openName_.empty() ? View::Deck : View::Note;
        interactionsReady_ = false;
        requestUpdate();
        return;
    }
  }

  // Paging is the two physical keys. They are the only buttons this device has
  // and vertical paging is what they do everywhere else in the fork.
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (down || up) {
    if (view_ == View::Deck) {
      const int page = deckPageSize();
      const int count = static_cast<int>(deckRows_.size());
      const int next = down ? deckTop_ + page : deckTop_ - page;
      if (page > 0 && count > page && next >= 0 && next < count) {
        deckTop_ = next;
        relabelDeck();
        interactionsReady_ = false;
        requestUpdate();
      }
      return;
    }
    if (view_ == View::Note) {
      const int page = notePageSize();
      const int count = static_cast<int>(taskRows_.size());
      const int next = down ? noteTop_ + page : noteTop_ - page;
      if (page > 0 && count > page && next >= 0 && next < count) {
        noteTop_ = next;
        relabelNote();
        interactionsReady_ = false;
        requestUpdate();
      }
      return;
    }
  }

  if (server_ && server_->isRunning()) {
    // Pumped from loop() rather than a task: there are no background threads in
    // this firmware, and a blocking handler on the render path is what makes a
    // screen look frozen.
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    if (server_->takeNotesChanged()) {
      reloadNote();
      phoneSaved_ = true;
      interactionsReady_ = false;
      requestUpdate();
    }
  }

  int x = 0;
  int y = 0;
  // Interactions::route() refuses a tap routed against a table the panel has
  // not shown yet, which is what stops a tap aimed at the screen underneath
  // from landing on the one that replaced it during a 0.3-2s repaint.
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input{};
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent action = interactions_.route(input);

  switch (action.action) {
    case notesui::ActionOpenNote:
      openNote(action.value);
      return;
    case notesui::ActionNewNote:
      askNewName();
      return;
    case notesui::ActionToggleTask:
      toggleTask(action.value);
      return;
    case notesui::ActionAddLine:
      askLine();
      return;
    case notesui::ActionMenu:
      if (openName_.empty()) return;
      view_ = View::Menu;
      interactionsReady_ = false;
      requestUpdate();
      return;
    case notesui::ActionClearDone:
      clearDone();
      return;
    case notesui::ActionRename:
      askRename();
      return;
    case notesui::ActionUsePhone:
      startPhone();
      return;
    case notesui::ActionDelete:
      // On the menu this OPENS the confirm; on the confirm it does the thing.
      // One action id for both, because the confirm's own DELETE IT is the
      // only control that may destroy a note and it lives on a screen the
      // person had to arrive at deliberately.
      if (view_ == View::Menu) {
        view_ = View::Confirm;
        interactionsReady_ = false;
        requestUpdate();
        return;
      }
      library_.remove(openName_);
      openDeck();
      return;
    case notesui::ActionDismiss:
      stopPhone();
      view_ = openName_.empty() ? View::Deck : View::Note;
      interactionsReady_ = false;
      requestUpdate();
      return;
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------

void NotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Deck: {
      notesui::DeckModel model;
      model.items = deckRows_.data();
      model.count = static_cast<int>(deckRows_.size());
      model.firstVisible = deckTop_;
      model.pageLabel = deckPage_.empty() ? nullptr : deckPage_.c_str();
      notesui::buildDeck(screen, model);
      break;
    }
    case View::Note: {
      notesui::NoteModel model;
      model.title = openName_.c_str();
      model.tasks = taskRows_.data();
      model.count = static_cast<int>(taskRows_.size());
      model.firstVisible = noteTop_;
      model.pageLabel = notePage_.empty() ? nullptr : notePage_.c_str();
      model.menuIcon = &icon_go_settings_32;
      model.anyDone = anyDone();
      notesui::buildNote(screen, model);
      break;
    }
    case View::Menu: {
      notesui::MenuModel model;
      model.title = openName_.c_str();
      model.menuIcon = &icon_go_settings_32;
      model.anyDone = anyDone();
      notesui::buildMenu(screen, model);
      break;
    }
    case View::Confirm: {
      notesui::ConfirmModel model;
      model.title = openName_.c_str();
      model.menuIcon = &icon_go_settings_32;
      model.prose = "Delete this note and everything written in it? There is no way back.";
      notesui::buildConfirm(screen, model);
      break;
    }
    case View::Phone: {
      notesui::PhoneModel model;
      model.title = openName_.c_str();
      model.menuIcon = &icon_go_settings_32;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.saved = phoneSaved_;
      const fui::Rect qr = notesui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice: {
      notesui::ConfirmModel model;
      model.title = openName_.empty() ? "NOTES" : openName_.c_str();
      model.prose = notice_.c_str();
      notesui::buildNotice(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  // The buffer records overflow and this reports it; a screen that silently
  // drops its last three controls is how Connections lost all its buttons.
  toybox::reportOverflow(interactions_, "Notes");
  renderer.displayBuffer();
}
