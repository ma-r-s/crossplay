#pragma once
// Minimal Arduino shim for the fontguard host suite.
//
// FontDecompressor.cpp includes <Arduino.h> for exactly two symbols, both of
// which only feed its stats counters. Nothing under test reads a clock to make
// a decision, so a monotonic counter is enough and keeps the suite free of the
// ESP32 toolchain.
#include <cstdint>

inline uint32_t millis() { return 0; }
inline uint32_t micros() {
  static uint32_t t = 0;
  return t++;
}
