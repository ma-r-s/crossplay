#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
class RenderLock;  // forward declaration

enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU };

/**
 * ActivityManager
 *
 * This mirrors the same concept of Activity in Android, where an activity represents a single screen of the UI. The
 * manager is responsible for launching activities, and ensuring that only one activity is active at a time.
 *
 * It also provides a stack mechanism to allow activities to launch sub-activities and get back the results when the
 * sub-activity is done. For example, the WebServer activity can launch a WifiSelect activity to let the user choose a
 * wifi network, and get back the selected network when the user is done.
 *
 * Main differences from Android's ActivityManager:
 * - No onPause/onResume, since we don't have a concept of background activities
 * - onActivityResult is implemented via a callback instead of a separate method, for simplicity
 */
class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;

  void exitActivity(const RenderLock& lock);
  // The only way currentActivity is assigned. Arms the release gate first --
  // see the definition, and util/ButtonReleaseGate.h for the seam.
  void setCurrentActivity(std::unique_ptr<Activity>&& next);

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();
  void reportRepaint(const char* activityName, uint32_t requestedAtMs, unsigned long repaintStartMs) const;
  void reportStackHeadroom(const char* activityName);
  void noteUpdateRequested();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;

  // Mutex to protect rendering operations from race conditions
  // Must only be used via RenderLock
  SemaphoreHandle_t renderingMutex = nullptr;

  // Whether to trigger a render after the current loop()
  // This variable must only be set by the main loop, to avoid race conditions
  std::atomic<bool> requestedUpdate{false};

  // millis() at the moment a repaint was first asked for, or 0 when none is
  // outstanding. Only the FIRST request in a burst is stamped: several
  // requestUpdate() calls in one loop collapse into a single render, and the
  // wait the user feels started at the first of them.
  //
  // Read by the render task to say how long a repaint sat queued before it
  // began. That gap is invisible to every timer inside render(), and it is one
  // of the two places a page turn can lose time without anything logging it --
  // the other being the RenderLock a background task may be holding.
  std::atomic<uint32_t> updateRequestedAtMs{0};

  // Repaints slower than this get one INF line naming where the time went.
  //
  // A threshold rather than a line per repaint, because LOG_INF also writes the
  // sixteen-line RTC ring that /api/dev/crash reads: an unconditional line per
  // screen update would erase every crash tail within seconds. 400ms is above
  // any healthy repaint on any panel in the fork (the X4 Pro's own FAST
  // waveform is ~260ms) and well below the 4.2s of GitHub issue #7, so a
  // device that is behaving stays silent and a device that is not explains
  // itself on the cable, in a release build, without a rebuild.
  static constexpr unsigned long SLOW_REPAINT_MS = 400;

  // Fewest bytes ever left unused on the render task's stack, and the screen
  // that reached it. The stack's size is set by CROSSPOINT_RENDER_TASK_STACK,
  // which two crashes were spent arguing about without anyone measuring this.
  std::atomic<uint32_t> worstStackFreeBytes{UINT32_MAX};

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
    assert(renderingMutex != nullptr && "Failed to create rendering mutex");
    stackActivities.reserve(10);
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  void begin();

  // Fewest bytes ever free on the render task's stack, or UINT32_MAX before
  // the first render. Size CROSSPOINT_RENDER_TASK_STACK from this rather than
  // from an estimate; scripts_local/stack_budget.py only ever gives a lower
  // bound on the true peak.
  uint32_t stackFreeLowWater() const { return worstStackFreeBytes.load(); }
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // goTo... functions are convenient wrapper for replaceActivity()
  void goToFileTransfer();
  void goToUsbDrive();
  void goToSettings();
  void goToFileBrowser(std::string path = {});
  void goToRecentBooks();
  void goToBrowser();
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  void goToSleep(bool fromTimeout = false);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE, bool cleanInitialRefresh = false);

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();

  bool preventAutoSleep() const;
  bool requiresExclusiveStorageLoop() const;
  bool isReaderActivity() const;

  // The name of the activity on screen, or "" when there is none. A
  // replacement requested but not yet swapped in counts as the current one, so
  // a caller that just asked for one reads back what it asked for. Pushed
  // sub-screens report their own name, not the one underneath.
  const char* currentActivityName() const;

  bool handleForcedRefresh();
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
