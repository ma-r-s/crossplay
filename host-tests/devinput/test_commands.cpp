// The input vocabulary both transports speak. See run.sh for why it is tested.

#include <HalGPIO.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "DevInputCommands.h"
#include "DevInputInjector.h"

namespace {

int checks = 0;
int failed = 0;

void ok() { checks++; }
void bad(const std::string& what) {
  checks++;
  failed++;
  std::printf("FAIL devinput  %s\n", what.c_str());
}

// What the stub injector last saw, and what it should answer next.
struct Seen {
  std::string verb;
  float a = 0, b = 0, c = 0, d = 0;
  unsigned long ms = 0;
  int button = -1;
  int calls = 0;
} seen;
bool injectorAccepts = true;

void expect(const char* cmd, const char* wantPrefix) {
  char reply[96] = {};
  const bool okay = devinput::runCommand(cmd, reply, sizeof(reply));
  if (std::strncmp(reply, wantPrefix, std::strlen(wantPrefix)) != 0) {
    bad(std::string("\"") + cmd + "\" -> \"" + reply + "\", wanted \"" + wantPrefix + "...\"");
    return;
  }
  // The bool return and the reply text must never disagree; both transports
  // use one or the other and a caller should not have to know which.
  if (okay != (std::strncmp(reply, "OK", 2) == 0)) {
    bad(std::string("\"") + cmd + "\" returned " + (okay ? "true" : "false") + " but replied \"" + reply + "\"");
    return;
  }
  ok();
}

void near(float got, float want, const char* what) {
  if (std::fabs(got - want) > 0.002f) {
    bad(std::string(what) + ": got " + std::to_string(got) + ", wanted " + std::to_string(want));
    return;
  }
  ok();
}

}  // namespace

// -- the stub injector -------------------------------------------------------
namespace devinput {
bool tap(float nx, float ny, unsigned long holdMs) {
  seen = {"tap", nx, ny, 0, 0, holdMs, -1, seen.calls + 1};
  return injectorAccepts;
}
bool longPress(float nx, float ny) {
  seen = {"long", nx, ny, 0, 0, 0, -1, seen.calls + 1};
  return injectorAccepts;
}
bool swipe(float nx0, float ny0, float nx1, float ny1, unsigned long ms) {
  seen = {"swipe", nx0, ny0, nx1, ny1, ms, -1, seen.calls + 1};
  return injectorAccepts;
}
bool button(uint8_t index, unsigned long holdMs) {
  seen = {"btn", 0, 0, 0, 0, holdMs, index, seen.calls + 1};
  return injectorAccepts;
}
}  // namespace devinput

int main() {
  // -- isCommand only claims the four verbs --------------------------------
  for (const char* yes : {"TAP 1 1", "LONG 1 1", "SWIPE 1 1 2 2", "BTN UP"}) {
    if (devinput::isCommand(yes))
      ok();
    else
      bad(std::string("isCommand rejected ") + yes);
  }
  // "TAP" with no space is not a TAP: the verbs are prefixes WITH arguments,
  // and a transport that hands over "TAPDANCE" must get its own fall-through.
  for (const char* no : {"PING", "TAP", "SCREENSHOT", "", "BTNX UP", "tap 1 1"}) {
    if (!devinput::isCommand(no))
      ok();
    else
      bad(std::string("isCommand claimed ") + no);
  }

  // -- TAP ------------------------------------------------------------------
  expect("TAP 400 240", "OK TAP 400 240 140");  // default hold
  near(seen.a, (400 + 0.5f) / 800, "tap nx");
  near(seen.b, (240 + 0.5f) / 480, "tap ny");
  if (seen.ms == 140)
    ok();
  else
    bad("tap default hold");

  expect("TAP 0 0", "OK");  // the corners are on the panel
  expect("TAP 799 479", "OK");
  expect("TAP 400 240 300", "OK TAP 400 240 300");
  expect("TAP 400", "ERR TAP wants");
  expect("TAP", "ERR");  // isCommand says no; runCommand still must not crash

  // Off the panel, both directions. Unclamped these reached a float->int
  // conversion out of range, from the network.
  expect("TAP -1 240", "ERR TAP off panel");
  expect("TAP 400 -1", "ERR TAP off panel");
  expect("TAP 800 240", "ERR TAP off panel");
  expect("TAP 400 480", "ERR TAP off panel");
  expect("TAP 99999999999999999999 1", "ERR TAP off panel");

  // THE WEDGE: a negative hold became an enormous unsigned one, the contact
  // never released, and every later command answered ERR busy until a power
  // cycle. There is no CANCEL verb, so this must be refused at the door.
  expect("TAP 400 240 -1", "ERR holdMs");
  expect("TAP 400 240 999999", "ERR holdMs");

  // -- LONG -----------------------------------------------------------------
  expect("LONG 100 100", "OK LONG 100 100");
  expect("LONG 100", "ERR LONG wants");
  expect("LONG -5 100", "ERR LONG off panel");
  expect("LONG 100 -5", "ERR LONG off panel");
  expect("LONG 800 100", "ERR LONG off panel");

  // -- SWIPE ----------------------------------------------------------------
  expect("SWIPE 10 240 300 240", "OK SWIPE 10 240 300 240 250");
  if (seen.ms == 250)
    ok();
  else
    bad("swipe default ms");
  near(seen.c, (300 + 0.5f) / 800, "swipe nx1");
  expect("SWIPE 10 240 300 240 500", "OK SWIPE 10 240 300 240 500");
  expect("SWIPE 10 240 300", "ERR SWIPE wants");
  // BOTH ends, and BOTH bounds on the duration. Testing one representative per
  // verb is how three of these clamps came to be removable with the suite still
  // green: the mutation that deleted them was the mutation nobody ran.
  expect("SWIPE 10 240 900 240", "ERR SWIPE off panel");
  expect("SWIPE 900 240 300 240", "ERR SWIPE off panel");
  expect("SWIPE 10 -1 300 240", "ERR SWIPE off panel");
  expect("SWIPE 10 240 300 480", "ERR SWIPE off panel");
  expect("SWIPE 10 240 300 240 -1", "ERR ms");
  expect("SWIPE 10 240 300 240 999999", "ERR ms");

  // -- BTN ------------------------------------------------------------------
  struct {
    const char* name;
    int index;
  } buttons[] = {
      {"BACK", HalGPIO::BTN_BACK},   {"CONFIRM", HalGPIO::BTN_CONFIRM}, {"LEFT", HalGPIO::BTN_LEFT},
      {"RIGHT", HalGPIO::BTN_RIGHT}, {"UP", HalGPIO::BTN_UP},           {"DOWN", HalGPIO::BTN_DOWN},
      {"POWER", HalGPIO::BTN_POWER},
  };
  for (const auto& b : buttons) {
    expect((std::string("BTN ") + b.name).c_str(), "OK BTN");
    if (seen.button == b.index)
      ok();
    else
      bad(std::string("BTN ") + b.name + " mapped to " + std::to_string(seen.button));
  }
  expect("BTN NOPE", "ERR BTN wants");
  expect("BTN UP 500", "OK BTN UP 500");
  if (seen.ms == 500)
    ok();
  else
    bad("btn explicit hold");
  expect("BTN UP -1", "ERR holdMs");
  expect("BTN UP 999999", "ERR holdMs");

  // -- busy is a retry, not a rejection -------------------------------------
  injectorAccepts = false;
  expect("TAP 400 240", "ERR busy");
  expect("LONG 400 240", "ERR busy");
  expect("SWIPE 1 1 2 2", "ERR busy");
  expect("BTN UP", "ERR busy");
  injectorAccepts = true;

  // -- nothing is scheduled by a refusal ------------------------------------
  const int before = seen.calls;
  expect("TAP 400 240 -1", "ERR holdMs");
  expect("TAP -1 -1", "ERR TAP off panel");
  expect("NONSENSE", "ERR not an input command");
  if (seen.calls == before)
    ok();
  else
    bad("a refused command still reached the injector");

  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
