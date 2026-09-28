#include "NotesSleep.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <vector>

#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxTheme.h"
#include "NotesLibrary.h"
#include "NotesScreens.h"

namespace notes {

bool readAsleep(AsleepChoice& out) {
  HalFile file;
  if (!Storage.openFileForRead("NOTES", kAsleepFile, file)) return false;
  char buf[160];
  const int read = file.read(buf, sizeof(buf) - 1);
  file.close();
  if (read <= 0) return false;
  buf[read] = '\0';
  return parseAsleep(std::string(buf, static_cast<size_t>(read)), out);
}

bool writeAsleep(const AsleepChoice& choice) {
  HalFile file;
  if (!Storage.openFileForWrite("NOTES", kAsleepFile, file)) {
    LOG_ERR("NOTES", "could not write %s", kAsleepFile);
    return false;
  }
  const std::string text = formatAsleep(choice);
  const size_t wrote = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  file.close();
  return wrote == text.size();
}

void clearAsleep() { Storage.remove(kAsleepFile); }

bool drawAsleep(GfxRenderer& renderer) {
  AsleepChoice choice;
  if (!readAsleep(choice)) {
    LOG_INF("NOTES", "sleep screen: no note chosen");
    return false;
  }
  Library library;
  std::string doc;
  if (!library.load(choice.name, doc)) {
    LOG_INF("NOTES", "sleep screen: '%s' is gone", choice.name.c_str());
    return false;
  }

  // The same rows the open note draws, from the same rule.
  const std::vector<Line> lines = parse(doc);
  const std::vector<size_t> drawn = drawnLines(doc, lines);
  std::vector<std::string> texts;
  texts.reserve(drawn.size());
  for (const size_t i : drawn) texts.push_back(textOf(doc, lines[i]));
  // Pointers in a second pass: push_back may move the strings.
  std::vector<notesui::Task> tasks;
  tasks.reserve(drawn.size());
  bool anyDone = false;
  for (size_t k = 0; k < drawn.size(); k++) {
    notesui::Task task;
    task.text = texts[k].c_str();
    task.checked = lines[drawn[k]].checked;
    anyDone = anyDone || task.checked;
    tasks.push_back(task);
  }

  notesui::NoteModel model;
  model.title = choice.name.c_str();
  model.tasks = tasks.data();
  model.count = static_cast<int>(tasks.size());
  model.page = kindOf(lines) == Kind::Page;
  model.anyDone = anyDone;
  if (!model.page) {
    const Counts c = counts(lines);
    model.done = c.done;
    model.total = c.marked;
  }
  model.asleep = true;

  toybox::ensureFonts(renderer);
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  // A sleep screen cannot turn a page, so what does not fit is counted, in
  // the page label's place: "1 / 3" says there is more than this.
  const std::vector<int> starts = notesui::notePageStarts(target, target.deviceContext(), model);
  char label[32] = "";
  if (starts.size() > 1) {
    std::snprintf(label, sizeof(label), "1 / %d", static_cast<int>(starts.size()));
    model.pageLabel = label;
  }
  const fui::InputSnapshot noInput{};
  toybox::Interactions interactions;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  notesui::buildNote(screen, model);
  LOG_INF("NOTES", "sleep screen: '%s', %d row(s), %d page(s)", choice.name.c_str(), model.count,
          static_cast<int>(starts.size()));
  return true;
}

}  // namespace notes
