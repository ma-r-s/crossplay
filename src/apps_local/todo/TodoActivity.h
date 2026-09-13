#pragma once

#include <memory>

#include "../../activities/Activity.h"

class TodoActivity final : public Activity {
 public:
  TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Todo", renderer, mappedInput) {}

  ~TodoActivity() override = default;

  static std::unique_ptr<Activity> create(
      GfxRenderer& renderer,
      MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kMaxTasks = 16;
  static constexpr int kTaskTextBytes = 80;

  struct Task {
    bool done = false;
    char text[kTaskTextBytes] = {};
  };

  Task tasks_[kMaxTasks];
  int taskCount_ = 0;

  void loadTasks();
  void saveTasks();
  void toggleTask(int index);

  int taskRowAt(int x, int y) const;
};
