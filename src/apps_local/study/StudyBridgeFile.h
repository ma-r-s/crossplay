#pragma once
// Where the reader keeps its pairing with the sync service, and the one write
// that puts it there. Apart from StudySync so a host suite can run it against a
// card that behaves like the device's (host-tests/study/test_bridge_file.cpp):
// the simulator creates missing folders on every write and SdFat does not, so
// a write into a folder nothing made passes in the simulator and fails on
// every real reader.

#include <string>

namespace study {

constexpr char kStudyDir[] = "/study";
constexpr char kBridgeStatePath[] = "/study/.bridge";

// Writes `raw` to kBridgeStatePath through a temp file and a rename, creating
// /study first: a reader that has never held a deck has no /study, and pairing
// is the first thing it writes there.
bool writeBridgeStateFile(const std::string& raw);

}  // namespace study
