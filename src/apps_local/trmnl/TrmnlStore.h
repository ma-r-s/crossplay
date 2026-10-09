#pragma once

// TRMNL's files on the card. Hidden under /.crosspoint, because one of them
// holds the server's API key and none of them is anything to open by hand.

#include <cstddef>
#include <string>

namespace trmnl {
namespace store {

constexpr const char* kDir = "/.crosspoint/trmnl";
constexpr const char* kConfigPath = "/.crosspoint/trmnl/config.txt";
constexpr const char* kStatePath = "/.crosspoint/trmnl/state.txt";
// The picture on the panel, always a BMP the renderer can draw.
constexpr const char* kImagePath = "/.crosspoint/trmnl/screen.bmp";
// Where a picture lands before it is known to be one.
constexpr const char* kDownloadPath = "/.crosspoint/trmnl/download.bin";
constexpr const char* kConvertPath = "/.crosspoint/trmnl/convert.bmp";
// The last JSON answer.
constexpr const char* kReplyPath = "/.crosspoint/trmnl/reply.json";

bool begin();
bool exists(const char* path);
// Empty when the file is missing or past `cap`.
std::string read(const char* path, size_t cap);
// Through a .part file, so a cut-off write never replaces a good one.
bool write(const char* path, const std::string& text);

}  // namespace store
}  // namespace trmnl
