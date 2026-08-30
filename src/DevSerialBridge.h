#pragma once

// Line-based serial command channel for driving a desk device from the host:
// synthetic taps, swipes and buttons (through DevInputInjector), framebuffer
// screenshots, and heap probes. Dev builds only
// (-DCROSSPOINT_DEV_SERIAL_BRIDGE=1 in [env:x4pro] / [env:sticky]); it reads
// the board's own serial transport (BoardConfig::serialTransport()), which is
// UART0 behind the WCH bridge on the Sticky and the native USB CDC on the X4
// Pro. tools_local/device/drive.py is the host side.
//
// Commands (one per line, coordinates in panel-native pixels):
//   CMD:PING
//   CMD:HEAP
//   CMD:SCREENSHOT
//   CMD:TAP x y [holdMs]
//   CMD:LONG x y
//   CMD:SWIPE x0 y0 x1 y1 [ms]
//   CMD:BTN UP|DOWN|CONFIRM|BACK|LEFT|RIGHT|POWER [holdMs]
// Every command answers with one "OK ..." or "ERR ..." line; SCREENSHOT
// streams SCREENSHOT_START:<size>\n<raw framebuffer>SCREENSHOT_END first.

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <cstddef>

namespace devbridge {

// Call once after the display HAL is up; on boards whose transport is a real
// UART this also starts that UART's receive side.
void begin();

// Poll for complete command lines. Call every loop() iteration.
void update();

// The transport's TX counters, as one line, for callers that are not the
// transport. This exists because the failure it measures KILLS the cable: a
// device whose serial has gone answers nothing on CMD:CDCSTAT, and the numbers
// that would explain why are only reachable over Wi-Fi. Writes at most `len`
// bytes including the terminator.
void txStatsLine(char* out, size_t len);

}  // namespace devbridge

#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE
