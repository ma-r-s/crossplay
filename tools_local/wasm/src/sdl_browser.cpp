// The browser side of the SDL stub declared in stubs/SDL.h.
//
// Two shared things live here, both written by the firmware worker thread and
// read by the browser's main thread:
//
//   the present buffer -- one frame of ARGB, panel-sized, plus the rotation
//     the panel asked for and a dirty flag. HalDisplay::presentIfNeeded()
//     fills it through SDL_UpdateTexture + SDL_RenderCopyEx exactly as it
//     fills a real texture, so the grayscale preview and the inversion setting
//     come along for free.
//
//   the input queue -- events pushed by JavaScript through the exported
//     functions in wasm_main.cpp and drained by HalGPIO::update(). It is a
//     ring rather than a vector so an over-eager finger cannot make the
//     firmware thread allocate.
//
// Both are guarded by a mutex rather than made lock-free: the firmware touches
// them once a frame, and a browser build has no realtime deadline to miss.

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

#include <atomic>
#include <cstring>
#include <ctime>
#include <mutex>

#include "SDL.h"

namespace {

// 800x480 ARGB. Sized from the panel rather than a constant so a device with
// different geometry cannot silently truncate.
constexpr int kMaxPixels = 800 * 480;

std::mutex& frameMutex() {
  static std::mutex m;
  return m;
}

uint32_t g_present[kMaxPixels];
std::atomic<int> g_dirty{1};  // start dirty so the first frame paints
std::atomic<int> g_rotation{0};
std::atomic<int> g_frameW{800};
std::atomic<int> g_frameH{480};

// The staging frame SDL_UpdateTexture writes. It only becomes the present
// buffer at SDL_RenderPresent, so the page can never sample a frame the
// firmware is halfway through compositing.
uint32_t g_staging[kMaxPixels];
int g_stagingW = 800;
int g_stagingH = 480;
int g_stagingRotation = 0;

constexpr size_t kQueueSize = 256;

std::mutex& inputMutex() {
  static std::mutex m;
  return m;
}

SDL_Event g_queue[kQueueSize];
size_t g_head = 0;
size_t g_tail = 0;
uint8_t g_keys[SDL_NUM_SCANCODES] = {};

uint32_t nowMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

}  // namespace

namespace simbrowser {

void pushEvent(const SDL_Event& event) {
  std::lock_guard<std::mutex> lock(inputMutex());
  const size_t next = (g_tail + 1) % kQueueSize;
  // Drop rather than overwrite: losing the newest event of a burst is a missed
  // tap, losing the oldest is a stuck button.
  if (next == g_head) return;
  g_queue[g_tail] = event;
  g_tail = next;
}

void setKey(int scancode, bool down) {
  if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) return;
  std::lock_guard<std::mutex> lock(inputMutex());
  g_keys[scancode] = down ? 1 : 0;
}

const uint32_t* presentPixels() { return g_present; }
int presentWidth() { return g_frameW.load(std::memory_order_acquire); }
int presentHeight() { return g_frameH.load(std::memory_order_acquire); }
int presentRotation() { return g_rotation.load(std::memory_order_acquire); }
int consumeDirty() { return g_dirty.exchange(0, std::memory_order_acquire); }

}  // namespace simbrowser

extern "C" {

int SDL_Init(uint32_t) { return 0; }
void SDL_Quit() {}
const char* SDL_GetError() { return ""; }

// Handles the simulator only null-checks. Returning a fixed non-null address
// keeps every `if (!window)` guard behaving as it does on the desktop without
// allocating anything to leak.
SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, uint32_t) { return reinterpret_cast<SDL_Window*>(1); }
SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, uint32_t) { return reinterpret_cast<SDL_Renderer*>(2); }
SDL_Texture* SDL_CreateTexture(SDL_Renderer*, uint32_t, int, int, int) { return reinterpret_cast<SDL_Texture*>(3); }
int SDL_SetHint(const char*, const char*) { return 1; }
void SDL_SetWindowSize(SDL_Window*, int, int) {}
int SDL_RenderSetLogicalSize(SDL_Renderer*, int, int) { return 0; }
int SDL_GetRendererOutputSize(SDL_Renderer*, int* w, int* h) {
  if (w) *w = g_stagingW;
  if (h) *h = g_stagingH;
  return 0;
}

int SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void* pixels, int pitch) {
  if (!pixels || pitch <= 0) return -1;
  const int width = pitch / static_cast<int>(sizeof(uint32_t));
  const int height = kMaxPixels / (width > 0 ? width : 1);
  g_stagingW = width;
  g_stagingH = height;
  std::memcpy(g_staging, pixels, static_cast<size_t>(width) * height * sizeof(uint32_t));
  return 0;
}

int SDL_RenderClear(SDL_Renderer*) { return 0; }

int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) {
  g_stagingRotation = 0;
  return 0;
}

int SDL_RenderCopyEx(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*, const double angle,
                     const SDL_Point*, SDL_RendererFlip) {
  // The panel is natively landscape; HalDisplay rotates the texture to undo
  // whatever rotation GfxRenderer baked into the buffer. That angle is the one
  // fact the page needs in order to draw the frame the right way up, so record
  // it here rather than re-deriving the orientation on the JavaScript side.
  int normalized = static_cast<int>(angle) % 360;
  if (normalized < 0) normalized += 360;
  g_stagingRotation = normalized;
  return 0;
}

void SDL_RenderPresent(SDL_Renderer*) {
  {
    std::lock_guard<std::mutex> lock(frameMutex());
    std::memcpy(g_present, g_staging, static_cast<size_t>(g_stagingW) * g_stagingH * sizeof(uint32_t));
    g_frameW.store(g_stagingW, std::memory_order_release);
    g_frameH.store(g_stagingH, std::memory_order_release);
    g_rotation.store(g_stagingRotation, std::memory_order_release);
  }
  g_dirty.store(1, std::memory_order_release);
}

// No filesystem worth writing a BMP to, and the harness that wants one runs
// the desktop build. Fail rather than report a screenshot that does not exist.
int SDL_RenderReadPixels(SDL_Renderer*, const SDL_Rect*, uint32_t, void*, int) { return -1; }
SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void*, int, int, int, int, uint32_t) { return nullptr; }
int SDL_SaveBMP(SDL_Surface*, const char*) { return -1; }
void SDL_FreeSurface(SDL_Surface*) {}

uint32_t SDL_GetTicks() { return nowMs(); }

void SDL_Delay(const uint32_t ms) {
  // The firmware loop runs on a Web Worker, which may block. This is the yield
  // that keeps that worker from spinning a core flat.
  emscripten_thread_sleep(static_cast<double>(ms));
}

int SDL_PollEvent(SDL_Event* event) {
  std::lock_guard<std::mutex> lock(inputMutex());
  if (g_head == g_tail) return 0;
  if (event) *event = g_queue[g_head];
  g_head = (g_head + 1) % kQueueSize;
  return 1;
}

const uint8_t* SDL_GetKeyboardState(int* numkeys) {
  if (numkeys) *numkeys = SDL_NUM_SCANCODES;
  return g_keys;
}

}  // extern "C"
