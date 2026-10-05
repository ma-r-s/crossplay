#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace studyui {

// The PART WAY verdict's body: which deck could not be built, and why when the
// bridge said. Pure, so the preview harness and the host test run the same
// formatting the sync does.
//
// The deck's own name, not its path: "United States Immigration Exam
// (2025)::Famous Americans" alone fills three of the body's four lines, and the
// first version of this pushed the reason off the screen.
inline void partWayDetail(char* out, size_t size, const std::vector<std::string>& failed,
                          const std::string& firstWhy, bool othersUpToDate) {
  if (failed.empty()) {
    if (size) out[0] = '\0';
    return;
  }
  const std::string& name = failed.front();
  const size_t sep = name.rfind("::");
  const char* first = name.c_str() + (sep == std::string::npos ? 0 : sep + 2);
  const char* others = othersUpToDate ? " Everything else is up to date." : "";
  if (failed.size() == 1 && !firstWhy.empty()) {
    std::snprintf(out, size, "%s could not be built: %s.", first, firstWhy.c_str());
  } else if (failed.size() == 1) {
    std::snprintf(out, size, "%s could not be built.%s", first, others);
  } else {
    std::snprintf(out, size, "%u decks could not be built, starting with %s.%s",
                  static_cast<unsigned>(failed.size()), first, others);
  }
}

}  // namespace studyui
