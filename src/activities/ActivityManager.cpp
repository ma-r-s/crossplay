#include "ActivityManager.h"

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/UsbDriveActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/BmpViewerActivity.h"
#include "util/FrontlightPanelActivity.h"
#include "util/FullScreenMessageActivity.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          8192,               // Stack size
                          this,               // Parameters
                          1,                  // Priority
                          &renderTaskHandle,  // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

// One line, only when a repaint was slow, saying which half of it was slow.
//
// GitHub issue #7 reported a 4.2s page turn and could not be taken further,
// because the only breakdown in the firmware (the reader's own "Page render"
// lines) is LOG_DBG and every release build compiles it out -- and even in a
// dev build it covers renderContents() alone, so a turn that spent its time
// waiting for the RenderLock, loading the section, or building pages reported
// a fast render and a slow device.
//
// This covers every screen instead of the reader only, which is what the
// report actually described: entering and leaving the reader and opening the
// reader menu were all slow the same way, and those share no epub layout, no
// glyph decompression and no section cache with a page turn. All they share is
// build-a-framebuffer-and-show-it.
void ActivityManager::reportRepaint(const char* activityName, const uint32_t requestedAtMs,
                                    const unsigned long repaintStartMs) const {
  const unsigned long now = millis();
  const unsigned long total = now - repaintStartMs;
  // requestedAtMs == 0 means nobody stamped this repaint (an activity that
  // paints from its own loop(), or the very first render after begin()). Say
  // "queued" is unknown rather than reporting the whole uptime as a queue wait.
  const unsigned long queued = requestedAtMs != 0 ? repaintStartMs - requestedAtMs : 0;
  if (total + queued < SLOW_REPAINT_MS) return;
  const unsigned long panel = paintclock::panelBusy().busyMs();
  // Saturating: panel time is billed from a second clock read, so a repaint
  // that ends mid-millisecond can bill one more to the panel than the total.
  const unsigned long compute = total > panel ? total - panel : 0;
  LOG_INF("PERF", "%s repaint %lums: queued %lums, panel %lums, ours %lums", activityName, total + queued, queued,
          panel, compute);
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // The waiter is claimed HERE, before the frame is built, and not at the
    // bottom after it. Card #306: claiming it at the bottom meant a waiter that
    // registered while this task was ALREADY inside render() was woken by the
    // frame that began before it asked -- so requestUpdateAndWait() returned
    // with the PREVIOUS screen on the panel, which is the exact guarantee it
    // exists to give. It is reachable on Get Books' cold start: loop() drops
    // the render lock before onEnter(), so a notification left pending from
    // before the activity swap can have this task painting while
    // onEnter() -> checkAndConnectWifi() -> beginFetch() registers its wait.
    //
    // Worse than a missed frame: that leftover notification made the NEXT
    // render run concurrently with the fetch the caller went on to start, and
    // a fetch mutates the entry list an activity's render() is reading with no
    // RenderLock between them. Claiming at the top cannot mis-pair a waiter
    // with an older frame -- every waiter notifies after registering, so the
    // iteration that claims it is always one that starts afterwards.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);

    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    // Stamped before RenderLock, not after: a background build or an idle task
    // holding the lock is time the user spends staring at the old page, and
    // measuring from inside the lock would report that wait as zero.
    const unsigned long repaintStartMs = millis();
    const uint32_t requestedAtMs = updateRequestedAtMs.exchange(0);
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      // Night mode is a global output polarity applied to every activity.
      // The sleep screen forces normal polarity itself (SleepActivity).
      display.setInverted(SETTINGS.screenInverted != 0);
      // Stamp what a tap on the play surface will mean in the frame about to
      // be built, before render() blocks in displayBuffer(). Taken here rather
      // than inside each render() so a new screen cannot forget it; a no-op
      // for every activity that does not override surfaceMeaning(). See
      // Activity::surfaceMeaning().
      currentActivity->noteSurfaceBuilt();
      paintclock::panelBusy().reset();
      currentActivity->render(std::move(lock));
      reportRepaint(currentActivity->name.c_str(), requestedAtMs, repaintStartMs);
    }
    // The frame claimed at the top of this iteration is now on the panel --
    // render() ends in displayBuffer(), which returns only after the waveform
    // -- so the task blocked in requestUpdateAndWait() can go on to block on
    // its socket.
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  // Before any activity reads input, and before the early returns below, so a
  // home gesture or a light-panel push cannot leave an arm standing. See
  // util/ButtonReleaseGate.h: the arm is what stops one physical press being
  // read by two activities, and this is the only thing that takes it back off.
  mappedInput.settleReleaseGate();

  if (mappedInput.consumeSuppressedRelease()) return;

  if (currentActivity && currentActivity->requiresExclusiveStorageLoop()) {
    currentActivity->loop();
    // An exclusive-storage activity must restart rather than navigate away:
    // processing a pending action here could re-enable filesystem users while
    // the USB host still owns the raw SD card.
    if (requestedUpdate.exchange(false) && renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
    return;
  }

  if (currentActivity) {
    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) {
        return;
      }
      goHome();
      return;
    }

    // Tap-first control-center entry: a tap on the status-bar band of the
    // top-level tab screens opens it, mirroring the top-edge swipe (which some
    // panels' etched glass makes unreliable). The reader keeps its clean page
    // (no status bar there to tap). Touch boards only, like the swipe itself.
    bool statusBarTap = false;
    if (mappedInput.hasTouch() &&
        (currentActivity->name == "Home" || currentActivity->name == "FileBrowser" ||
         currentActivity->name == "Settings" || currentActivity->name == "NetworkModeSelection")) {
      int tx = 0;
      int ty = 0;
      statusBarTap = mappedInput.wasScreenTapped(tx, ty) && ty < 44;
    }
    // Guarded on present() so a board without a frontlight never opens a panel
    // for one. Pushed, so it returns to whatever was underneath -- including
    // mid-book.
    if (Frontlight.present() && currentActivity->name != "FrontlightPanel" &&
        (statusBarTap || mappedInput.wasLightPanelGesture())) {
      pushActivity(std::make_unique<FrontlightPanelActivity>(renderer, mappedInput));
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        setCurrentActivity(std::move(stackActivities.back()));
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction = PendingAction::None;
      setCurrentActivity(std::move(pendingActivity));

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

// The ONE place the activity on top is installed. The screen being replaced
// may have acted on a button that is still down -- WifiSelectionActivity
// cancels on the Back PRESS -- and that button's release is not the incoming
// screen's to read, so the gate is armed here rather than at each caller. See
// util/ButtonReleaseGate.h.
//
// A setter rather than a call at every site because the call sites are what
// failed: the arm was written at the two sites an audit enumerated and missed
// the third (the immediate branch of replaceActivity), which is the one a
// crash report pops back through. host-tests/pickerseam asserts that no
// assignment to currentActivity exists outside this function.
void ActivityManager::setCurrentActivity(std::unique_ptr<Activity>&& next) {
  mappedInput.swallowNextReleaseOfHeldButtons();
  currentActivity = std::move(next);
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately. Reached whenever the
    // stack emptied first: popActivity() -> goHome(), which is how Back leaves
    // the crash report.
    setCurrentActivity(std::move(newActivity));
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() {
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput));
}

void ActivityManager::goToUsbDrive() {
#if FREEINK_CAP_USB_MSC
  auto activity = makeUniqueNoThrow<UsbDriveActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: USB Drive activity");
    return;
  }
  replaceActivity(std::move(activity));
#else
  LOG_ERR("ACT", "USB Drive requested in a build without USB Drive capability");
#endif
}

void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToRecentBooks() {
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
}

void ActivityManager::goToBrowser() {
  // Which catalog to open is the browser's rule, and it is written once there
  // because the APPS row uses the same factory.
  replaceActivity(OpdsBookBrowserActivity::create(renderer, mappedInput));
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  if (path.empty()) {
    goToFileBrowser("/");
    return;
  }

  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path)) {
    auto activity = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, std::move(path));
    if (!activity) {
      LOG_ERR("ACT", "OOM: bitmap viewer activity");
      return;
    }
    replaceActivity(std::move(activity));
    return;
  }

  auto activity = ReaderActivity::create(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  if (activity) {
    replaceActivity(std::move(activity));
  }
}

void ActivityManager::goToSleep(bool fromTimeout) {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem, bool cleanInitialRefresh) {
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "RecentBooks") {
      initialMenuItem = HomeMenuItem::RECENTS;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem, cleanInitialRefresh));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

const char* ActivityManager::currentActivityName() const {
  // A replacement that has been requested but not yet swapped in counts as the
  // current one: replaceActivity() defers the swap to the top of the next loop,
  // and a caller asking right after requesting one means the one it requested.
  if (pendingActivity) return pendingActivity->name.c_str();
  return currentActivity ? currentActivity->name.c_str() : "";
}

bool ActivityManager::requiresExclusiveStorageLoop() const {
  return currentActivity && currentActivity->requiresExclusiveStorageLoop();
}

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

// First request in a burst wins. Several requestUpdate() calls in one loop
// collapse into one render, and the wait the user feels began at the first.
void ActivityManager::noteUpdateRequested() {
  uint32_t expected = 0;
  updateRequestedAtMs.compare_exchange_strong(expected, static_cast<uint32_t>(millis()));
}

void ActivityManager::requestUpdate(bool immediate) {
  noteUpdateRequested();
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }
  noteUpdateRequested();

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  // Recorded inside the critical section, not re-derived from
  // waitingTaskHandle afterwards: the render task claims that field the moment
  // it takes a notification, so a later read of it says nothing about whether
  // THIS call was the one that registered.
  const bool claimed = (!alreadyWaiting && !isRenderTask && !holdingRenderLock);
  if (claimed) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  // Only the caller that actually CLAIMED the wait may block on it. This used
  // to notify and then ulTaskNotifyTake(portMAX_DELAY) whatever the guards
  // said, so with asserts compiled out a caller the guards rejected -- a second
  // waiter, or one holding the RenderLock -- blocked forever on a notification
  // nobody would ever send, with no timeout and no log line. Asserts are live
  // in every env in platformio.ini today, so it panics rather than hangs; this
  // is what happens on the day one is not. Degrading to "no wait" leaves a
  // busy frame racing its socket, which is the bug this file is fixing rather
  // than a dead device.
  if (!claimed) {
    LOG_ERR("ACT", "requestUpdateAndWait() refused (render task: %d, already waiting: %d, holds lock: %d)",
            static_cast<int>(isRenderTask), static_cast<int>(alreadyWaiting), static_cast<int>(holdingRenderLock));
    xTaskNotify(renderTaskHandle, 1, eIncrement);
    return;
  }

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
