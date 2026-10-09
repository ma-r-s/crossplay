#pragma once

// The workout files on the card. Device-only: SD I/O and nothing else, so
// reading it is the review. Every rule about what the files MEAN lives in
// WorkoutsCore, which has a host suite.
//
// All three live in /workouts, beside each other and outside /.crosspoint, so
// a plan can be read and fixed on a computer the way a note can.

#include <string>

namespace workouts {

namespace store {

constexpr const char* kDir = "/workouts";
constexpr const char* kPlanPath = "/workouts/plan.txt";
constexpr const char* kTodayPath = "/workouts/today.txt";
constexpr const char* kLogPath = "/workouts/log.txt";

// Creates /workouts on first use. False only when the card refuses.
bool begin();

// A missing file reads as empty, which every parser takes as "nothing yet".
std::string read(const char* path, size_t cap);

// Written beside the file and renamed over it, so a power cut mid-write leaves
// the old file rather than an empty one -- which would read exactly like a plan
// somebody deleted.
bool write(const char* path, const std::string& text);

}  // namespace store

}  // namespace workouts
