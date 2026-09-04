#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
// True while the capture marker is set: the panic before this boot went
// through __wrap_panic_abort (an assert or abort) and getPanicInfo(false) is
// its reason. checkPanic() clears the marker once the report is on the card,
// so ask before that. On Xtensa nothing else sets it, and a CPU exception
// leaves panicMessage holding whatever the last abort wrote: without this a
// crash after an assert, with no clean boot between, reads as that assert.
// (On RISC-V the backtrace wrap sets it too; this fork builds Xtensa only.)
bool panicReasonRecorded();
}  // namespace HalSystem
