#include "TodoActivity.h"

#include <HalStorage.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "../../components/UITheme.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../activities/util/ConfirmationActivity.h"
#include "../Shelf.h"
#include "fontIds.h"

namespace {

constexpr char kTodoPath[] = "/.crosspoint/todo.txt";
constexpr char kTodoSettingsPath[] = "/.crosspoint/todo.cfg";

constexpr int kRowHeight = 48;
constexpr int kLeftMargin = 24;
constexpr int kCheckboxSize = 22;

}  // namespace

std::unique_ptr<Activity> TodoActivity::create(
    GfxRenderer& renderer,
    MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TodoActivity>(renderer, mappedInput);
}

void TodoActivity::onEnter() {
  Activity::onEnter();

  loadSettings();
  loadTasks();

  requestUpdate();
}


void TodoActivity::loadSettings() {
  hideCompleted_ = false;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kTodoSettingsPath)) {
    return;
  }

  char buffer[64] = {};

  if (Storage.readFileToBuffer(
          kTodoSettingsPath,
          buffer,
          sizeof(buffer)) == 0) {
    return;
  }

  hideCompleted_ =
      std::strstr(buffer, "hide_completed=1") != nullptr;

  sleepScreenEnabled_ =
      std::strstr(buffer, "sleep_screen=1") != nullptr;
#endif
}

void TodoActivity::saveSettings() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char buffer[64];

  std::snprintf(
      buffer,
      sizeof(buffer),
      "hide_completed=%d\nsleep_screen=%d\n",
      hideCompleted_ ? 1 : 0,
      sleepScreenEnabled_ ? 1 : 0);

  Storage.writeFile(
      kTodoSettingsPath,
      String(buffer));
#endif
}

void TodoActivity::loadTasks() {
  taskCount_ = 0;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kTodoPath)) {
    // First-launch sample data.
    tasks_[0].done = false;
    std::snprintf(tasks_[0].text, sizeof(tasks_[0].text), "Try the Todo app");

    tasks_[1].done = false;
    std::snprintf(tasks_[1].text, sizeof(tasks_[1].text), "Tap this task to complete it");

    taskCount_ = 2;
    saveTasks();
    return;
  }

  char buffer[2048] = {};

  if (Storage.readFileToBuffer(
          kTodoPath,
          buffer,
          sizeof(buffer)) == 0) {
    return;
  }

  char* line = std::strtok(buffer, "\n");

  while (line != nullptr && taskCount_ < kMaxTasks) {
    bool done = false;
    const char* text = line;

    if (std::strncmp(line, "[x] ", 4) == 0 ||
        std::strncmp(line, "[X] ", 4) == 0) {
      done = true;
      text += 4;
    } else if (std::strncmp(line, "[ ] ", 4) == 0) {
      text += 4;
    }

    if (*text != '\0') {
      Task& task = tasks_[taskCount_];

      task.done = done;

      std::snprintf(
          task.text,
          sizeof(task.text),
          "%s",
          text);

      ++taskCount_;
    }

    line = std::strtok(nullptr, "\n");
  }
#endif
}

void TodoActivity::saveTasks() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char buffer[2048] = {};
  int used = 0;

  for (int i = 0; i < taskCount_; ++i) {
    const Task& task = tasks_[i];

    const int written = std::snprintf(
        buffer + used,
        sizeof(buffer) - used,
        "%s%s\n",
        task.done ? "[x] " : "[ ] ",
        task.text);

    if (written <= 0) {
      return;
    }

    if (written >= static_cast<int>(sizeof(buffer) - used)) {
      return;
    }

    used += written;
  }

  Storage.writeFile(
      kTodoPath,
      String(buffer));
#endif
}

void TodoActivity::toggleTask(int index) {
  if (index < 0 || index >= taskCount_) {
    return;
  }

  tasks_[index].done = !tasks_[index].done;

  saveTasks();
  requestUpdate();
}


void TodoActivity::openAddTask() {
  if (taskCount_ >= kMaxTasks) {
    return;
  }

  auto handler = [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      const auto& kb = std::get<KeyboardResult>(result.data);

      if (!kb.text.empty()) {
        Task& task = tasks_[taskCount_];

        task.done = false;

        std::snprintf(
            task.text,
            sizeof(task.text),
            "%s",
            kb.text.c_str());

        ++taskCount_;

        saveTasks();
      }
    }

    requestUpdate();
  };

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(
          renderer,
          mappedInput,
          "Add task",
          "",
          kTaskTextBytes - 1,
          InputType::Text),
      handler);
}


void TodoActivity::openTaskMenu(int index) {
  if (index < 0 || index >= taskCount_) {
    return;
  }

  const bool canMoveUp = index > 0;
  const bool canMoveDown = index < taskCount_ - 1;

  const char* options[4];
  int actions[4];
  int count = 0;

  options[count] = "Edit";
  actions[count++] = 0;

  if (canMoveUp) {
    options[count] = "Move up";
    actions[count++] = 1;
  }

  if (canMoveDown) {
    options[count] = "Move down";
    actions[count++] = 2;
  }

  options[count] = "Delete";
  actions[count++] = 3;

  taskMenu_.show(
      tasks_[index].text,
      options,
      count,
      0,
      [this, index, actions](int choice) {
        if (choice < 0) {
          return;
        }

        const int action = actions[choice];

        if (action == 0) {
          editTask(index);
        } else if (action == 1) {
          moveTaskUp(index);
        } else if (action == 2) {
          moveTaskDown(index);
        } else if (action == 3) {
          confirmDeleteTask(index);
        }
      });

  requestUpdate();
}

void TodoActivity::editTask(int index) {
  if (index < 0 || index >= taskCount_) {
    return;
  }

  const std::string currentText = tasks_[index].text;

  auto handler = [this, index](const ActivityResult& result) {
    if (!result.isCancelled &&
        index >= 0 &&
        index < taskCount_) {
      const auto& kb =
          std::get<KeyboardResult>(result.data);

      if (!kb.text.empty()) {
        std::snprintf(
            tasks_[index].text,
            sizeof(tasks_[index].text),
            "%s",
            kb.text.c_str());

        saveTasks();
      }
    }

    requestUpdate();
  };

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(
          renderer,
          mappedInput,
          "Edit task",
          currentText,
          kTaskTextBytes - 1,
          InputType::Text),
      handler);
}

void TodoActivity::confirmDeleteTask(int index) {
  if (index < 0 || index >= taskCount_) {
    return;
  }

  const std::string taskText = tasks_[index].text;

  auto handler = [this, index](const ActivityResult& result) {
    if (!result.isCancelled) {
      deleteTask(index);
    }

    requestUpdate();
  };

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(
          renderer,
          mappedInput,
          "Delete task?",
          taskText),
      handler);
}

void TodoActivity::deleteTask(int index) {
  if (index < 0 || index >= taskCount_) {
    return;
  }

  for (int i = index; i < taskCount_ - 1; ++i) {
    tasks_[i] = tasks_[i + 1];
  }

  --taskCount_;

  if (taskCount_ >= 0 && taskCount_ < kMaxTasks) {
    tasks_[taskCount_] = Task{};
  }

  saveTasks();
  requestUpdate();
}


int TodoActivity::visibleTaskCount() const {
  if (!hideCompleted_) {
    return taskCount_;
  }

  int count = 0;

  for (int i = 0; i < taskCount_; ++i) {
    if (!tasks_[i].done) {
      ++count;
    }
  }

  return count;
}

int TodoActivity::taskIndexForVisibleRow(int row) const {
  if (row < 0) {
    return -1;
  }

  int visibleRow = 0;

  for (int i = 0; i < taskCount_; ++i) {
    if (hideCompleted_ && tasks_[i].done) {
      continue;
    }

    if (visibleRow == row) {
      return i;
    }

    ++visibleRow;
  }

  return -1;
}

void TodoActivity::sortCompletedLast() {
  // Stable partition without heap allocation:
  // unfinished tasks retain their relative order,
  // completed tasks retain theirs too.
  Task sorted[kMaxTasks];
  int out = 0;

  for (int i = 0; i < taskCount_; ++i) {
    if (!tasks_[i].done) {
      sorted[out++] = tasks_[i];
    }
  }

  for (int i = 0; i < taskCount_; ++i) {
    if (tasks_[i].done) {
      sorted[out++] = tasks_[i];
    }
  }

  for (int i = 0; i < taskCount_; ++i) {
    tasks_[i] = sorted[i];
  }
}


void TodoActivity::moveTaskUp(int index) {
  if (index <= 0 || index >= taskCount_) {
    return;
  }

  Task tmp = tasks_[index - 1];
  tasks_[index - 1] = tasks_[index];
  tasks_[index] = tmp;

  saveTasks();
  requestUpdate();
}

void TodoActivity::moveTaskDown(int index) {
  if (index < 0 || index >= taskCount_ - 1) {
    return;
  }

  Task tmp = tasks_[index + 1];
  tasks_[index + 1] = tasks_[index];
  tasks_[index] = tmp;

  saveTasks();
  requestUpdate();
}

int TodoActivity::taskRowAt(int x, int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();

  const int bodyTop =
      metrics.topPadding +
      metrics.headerHeight +
      metrics.verticalSpacing;

  if (x < 0 || x >= renderer.getScreenWidth()) {
    return -1;
  }

  if (y < bodyTop) {
    return -1;
  }

  const int visibleRow =
      (y - bodyTop) / kRowHeight;

  if (visibleRow < 0 ||
      visibleRow >= visibleTaskCount()) {
    return -1;
  }

  return taskIndexForVisibleRow(visibleRow);
}

void TodoActivity::loop() {
  if (taskMenu_.isActive()) {
    taskMenu_.handleInput(
        mappedInput,
        [this]() {
          requestUpdate();
        });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openAddTask();
    return;
  }

  int holdX = 0;
  int holdY = 0;

  if (mappedInput.wasScreenLongPress(holdX, holdY)) {
    const int taskIndex =
        taskRowAt(holdX, holdY);

    if (taskIndex >= 0) {
      openTaskMenu(taskIndex);
    }

    return;
  }

  int tapX = 0;
  int tapY = 0;

  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    const auto& metrics =
        UITheme::getInstance().getMetrics();

    const int bodyTop =
        metrics.topPadding +
        metrics.headerHeight +
        metrics.verticalSpacing;

    const int visibleCount =
        visibleTaskCount();

    const int taskIndex =
        taskRowAt(tapX, tapY);

    if (taskIndex >= 0) {
      if (mappedInput.tapWasHeldLong()) {
        openTaskMenu(taskIndex);
      } else {
        toggleTask(taskIndex);
      }

      return;
    }

    const int addY =
        bodyTop +
        visibleCount * kRowHeight;

    if (taskCount_ < kMaxTasks &&
        tapY >= addY &&
        tapY < addY + kRowHeight) {
      openAddTask();
      return;
    }

    const int filterY =
        addY +
        (taskCount_ < kMaxTasks ? kRowHeight : 0);

    if (tapY >= filterY &&
        tapY < filterY + kRowHeight) {
      hideCompleted_ = !hideCompleted_;
      saveSettings();
      requestUpdate();
      return;
    }

    const int sleepY =
        filterY + kRowHeight;

    if (tapY >= sleepY &&
        tapY < sleepY + kRowHeight) {
      sleepScreenEnabled_ = !sleepScreenEnabled_;
      saveSettings();
      requestUpdate();
      return;
    }
  }
}

void TodoActivity::render(RenderLock&&) {
  const auto& metrics =
      UITheme::getInstance().getMetrics();

  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(
      renderer,
      Rect{
          0,
          metrics.topPadding,
          sw,
          metrics.headerHeight},
      "TO DO");

  const int bodyTop =
      metrics.topPadding +
      metrics.headerHeight +
      metrics.verticalSpacing;

  if (taskCount_ == 0) {
    UITheme::drawCenteredWrappedText(
        renderer,
        Rect{
            0,
            bodyTop,
            sw,
            sh - bodyTop -
                metrics.buttonHintsHeight},
        UI_12_FONT_ID,
        "No tasks",
        4);
  } else {
    int visibleRow = 0;

    for (int i = 0; i < taskCount_; ++i) {
      Task& task = tasks_[i];

      if (hideCompleted_ && task.done) {
        continue;
      }

      const int y =
          bodyTop +
          visibleRow * kRowHeight;

      if (y + kRowHeight >
          sh - metrics.buttonHintsHeight) {
        break;
      }

      const int boxX = kLeftMargin;
      const int boxY =
          y +
          (kRowHeight - kCheckboxSize) / 2;

      renderer.drawRect(
          boxX,
          boxY,
          kCheckboxSize,
          kCheckboxSize,
          true);

      if (task.done) {
        renderer.fillRect(
            boxX + 5,
            boxY + 5,
            kCheckboxSize - 10,
            kCheckboxSize - 10,
            true);
      }

      const int textX =
          boxX + kCheckboxSize + 16;

      renderer.drawText(
          UI_12_FONT_ID,
          textX,
          y + 14,
          task.text);

      renderer.drawLine(
          kLeftMargin,
          y + kRowHeight - 1,
          sw - kLeftMargin,
          y + kRowHeight - 1,
          true);

      ++visibleRow;
    }
  }

  const int visibleCount =
      visibleTaskCount();

  int controlsY =
      bodyTop +
      visibleCount * kRowHeight;

  if (taskCount_ < kMaxTasks) {
    if (controlsY + kRowHeight <
        sh - metrics.buttonHintsHeight) {
      renderer.drawText(
          UI_12_FONT_ID,
          kLeftMargin,
          controlsY + 14,
          "+ ADD TASK");

      renderer.drawLine(
          kLeftMargin,
          controlsY + kRowHeight - 1,
          sw - kLeftMargin,
          controlsY + kRowHeight - 1,
          true);
    }

    controlsY += kRowHeight;
  }

  if (controlsY + kRowHeight <
      sh - metrics.buttonHintsHeight) {
    renderer.drawText(
        UI_12_FONT_ID,
        kLeftMargin,
        controlsY + 14,
        hideCompleted_ ? "SHOW: OPEN" : "SHOW: ALL");

    renderer.drawLine(
        kLeftMargin,
        controlsY + kRowHeight - 1,
        sw - kLeftMargin,
        controlsY + kRowHeight - 1,
        true);

    controlsY += kRowHeight;
  }

  if (controlsY + kRowHeight <
      sh - metrics.buttonHintsHeight) {
    renderer.drawText(
        UI_12_FONT_ID,
        kLeftMargin,
        controlsY + 14,
        sleepScreenEnabled_
            ? "SLEEP SCREEN: ON"
            : "SLEEP SCREEN: OFF");

    renderer.drawLine(
        kLeftMargin,
        controlsY + kRowHeight - 1,
        sw - kLeftMargin,
        controlsY + kRowHeight - 1,
        true);
  }

  const auto labels =
      mappedInput.mapLabels(
          "Back",
          "Add",
          "",
          "");

  GUI.drawButtonHints(
      renderer,
      labels.btn1,
      labels.btn2,
      labels.btn3,
      labels.btn4);

  if (taskMenu_.processRender(renderer, mappedInput)) {
    return;
  }

  renderer.displayBuffer();
}
