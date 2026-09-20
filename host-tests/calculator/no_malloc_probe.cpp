// decNumber must never allocate at this calculator's working precision.
//
// It has an allocation path: decDivideOp's working buffer is sized from
// DECBUFFER, and past roughly DECBUFFER - 4 digits of context precision it falls
// back to malloc. The working precision is sixteen against a buffer of
// thirty-six, so it cannot fire -- and on a device a failed allocation halfway
// through a sum has nowhere to go, so "cannot" is worth proving rather than
// reasoning about. This is the same library, rebuilt with its allocator
// redirected to a counter, driven through the same arithmetic.

#include "no_malloc_probe.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
int calc_probe_calls = 0;

void* calc_probe_malloc(const size_t n) {
  ++calc_probe_calls;
  return std::malloc(n);
}
void calc_probe_free(void* p) { std::free(p); }
}

#include "CalcEngine.h"

using namespace calc;

namespace {
void type(Engine& e, const char* keys) {
  for (const char* p = keys; *p; ++p) {
    switch (*p) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        e.press(static_cast<Key>(static_cast<int>(Key::D0) + (*p - '0')));
        break;
      case '.':
        e.press(Key::Dot);
        break;
      case '+':
        e.press(Key::Add);
        break;
      case '-':
        e.press(Key::Sub);
        break;
      case 'x':
        e.press(Key::Mul);
        break;
      case '/':
        e.press(Key::Div);
        break;
      case '=':
        e.press(Key::Equals);
        break;
      case '%':
        e.press(Key::Percent);
        break;
      case 'n':
        e.press(Key::Negate);
        break;
      case 'C':
        e.press(Key::ClearAll);
        break;
      default:
        break;
    }
  }
}
}  // namespace

int main() {
  Engine e;
  // Division is the operation with the allocation path, so most of this is
  // division, at every magnitude the engine can reach.
  static const char* kWorkout[] = {
      "1/3=",
      "2/7=",
      "1/9999999999=",
      "9999999999/3=",
      "0.0000000001/7=",
      "1/3x3=",
      "22/7x7/22=",
      "9999999999x9999999999=",
      "1/7+1/7+1/7=",
      "123456789/987654321=",
      "0.1+0.2-0.3=",
      "50+10%=",
      "500x5%=",
      "1/0=",
  };
  for (const char* keys : kWorkout) {
    type(e, "C");
    type(e, keys);
    for (int i = 0; i < 8; ++i) type(e, "=");  // repeated equals, over and over
  }
  std::printf("%s: %d checks, %d failed  (%d malloc calls from decNumber)\n", calc_probe_calls ? "FAILED" : "ok", 1,
              calc_probe_calls ? 1 : 0, calc_probe_calls);
  return calc_probe_calls ? 1 : 0;
}
