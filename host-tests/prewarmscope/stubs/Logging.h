#pragma once
// Minimal logging shim for the fontguard host suite.
//
// The real lib/Logging/Logging.h reaches for HardwareSerial and HWCDC, which
// would drag the whole Arduino core in. The decoder only ever writes log lines,
// never reads one back, so a sink is enough.
//
// The ARGUMENTS ARE DISCARDED on purpose. uint32_t is `unsigned long` on the
// ESP32 toolchain and `unsigned int` here, so the firmware's %lu specifiers are
// correct on the device and wrong on the host -- and forwarding them through
// varargs anyway would be undefined behaviour that UBSan would rightly flag.
// Rewriting the firmware's format strings to suit a test stub would be churn on
// the device for no gain, so the stub gives way instead. The format string and
// origin still print, which is enough to see WHICH line fired while debugging.
#include <cstdio>

template <typename... Args>
inline void hostLogSink(const char* level, const char* origin, const char* format, Args...) {
  fprintf(stderr, "[%s %s] %s\n", level, origin, format);
}

#define LOG_ERR(origin, format, ...) hostLogSink("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) hostLogSink("INF", origin, format, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) hostLogSink("DBG", origin, format, ##__VA_ARGS__)
