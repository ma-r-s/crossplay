// Entry point for the browser build, replacing the simulator's SDL main.
//
// The desktop simulator owns a window, so its main() must stay on the process's
// main thread and spin `while (!display.shouldQuit())`. A browser main thread
// that spins never returns to the event loop, so nothing paints and nothing
// clicks. Here main() instead starts the firmware on a FreeRTOS task -- a Web
// Worker, under the simulator's std::thread shim -- and returns immediately.
// With -sEXIT_RUNTIME=0 the module stays alive, the worker keeps running the
// firmware's setup()/loop(), and the exported functions below stay callable
// from the page.
//
// Everything the page needs crosses here and nowhere else:
//
//   crossplay_frame_ptr / _width / _height  the composited ARGB frame
//   crossplay_frame_rotation                the angle the panel wants it at
//   crossplay_consume_dirty                 has it changed since you last asked
//   crossplay_touch / _button / _key        input, in logical screen pixels
//
// Touch is the addition. CrossMux's browser simulator exposes buttons only,
// which is right for a device you read on; every app in this fork is driven by
// tapping the panel, so a button-only emulator would demonstrate none of them.
// The X4 Pro's touch is already emulated on the SDL mouse path, so injecting
// mouse events is enough to get taps, holds and swipes at once.

#include <emscripten/emscripten.h>

#include <cstdint>

#include "HalDisplay.h"
#include "HalGPIO.h"
#include "SDL.h"
#include "SimulatorLifecycle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern void setup();
extern void loop();
extern HalDisplay display;
extern HalGPIO gpio;

namespace simbrowser {
void pushEvent(const SDL_Event& event);
void setKey(int scancode, bool down);
const uint32_t* presentPixels();
int presentWidth();
int presentHeight();
int presentRotation();
int consumeDirty();
}  // namespace simbrowser

namespace {

void firmwareTask(void*) {
  setup();
  while (true) {
    // Same frame shape as simulator_main.cpp: clear the input edge latches
    // once, run one firmware loop, then flush whatever it drew. Keeping the
    // order identical is what makes a bug reproducible in both builds.
    gpio.beginFrame();
    loop();
    display.presentIfNeeded();
    SDL_Delay(1);
  }
}

}  // namespace

int main(int /*argc*/, char** argv) {
  SimulatorLifecycle::initProcessArgs(argv);
  // Through xTaskCreate rather than a bare thread: the shim registers a task
  // handle in thread-local storage, and ActivityManager asserts on
  // xTaskGetCurrentTaskHandle() being non-null while it holds the render lock.
  xTaskCreate(&firmwareTask, "firmware", 8192, nullptr, 1, nullptr);
  return 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE uintptr_t crossplay_frame_ptr() {
  return reinterpret_cast<uintptr_t>(simbrowser::presentPixels());
}
EMSCRIPTEN_KEEPALIVE int crossplay_frame_width() { return simbrowser::presentWidth(); }
EMSCRIPTEN_KEEPALIVE int crossplay_frame_height() { return simbrowser::presentHeight(); }
EMSCRIPTEN_KEEPALIVE int crossplay_frame_rotation() { return simbrowser::presentRotation(); }
EMSCRIPTEN_KEEPALIVE int crossplay_consume_dirty() { return simbrowser::consumeDirty(); }

// phase: 0 down, 1 move, 2 up. Coordinates are logical screen pixels, the same
// space GfxRenderer draws in, so the page converts from canvas pixels once and
// the firmware never learns there was a canvas.
EMSCRIPTEN_KEEPALIVE void crossplay_touch(const int phase, const int x, const int y) {
  SDL_Event event{};
  if (phase == 1) {
    event.motion.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
  } else {
    event.button.type = phase == 0 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
  }
  simbrowser::pushEvent(event);
}

// Physical buttons, by SDL scancode so HalGPIO's own remap table stays the one
// place the mapping lives.
EMSCRIPTEN_KEEPALIVE void crossplay_key(const int scancode, const int down) {
  SDL_Event event{};
  event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  event.key.repeat = 0;
  event.key.keysym.scancode = scancode;
  simbrowser::pushEvent(event);
  // isPressed() reads the keyboard state array rather than the queue, so both
  // have to move together or a held button reads as released.
  simbrowser::setKey(scancode, down != 0);
}

}  // extern "C"
