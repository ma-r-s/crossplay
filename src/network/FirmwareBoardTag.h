#pragma once

#include <cstddef>
#include <cstdint>

// Board-identity tag embedded in every CrossPoint image, plus a streaming
// scanner the firmware update paths use to reject an image built for a
// different board before it can boot and drive another device's pins. All the
// S3 boards (sticky, x4pro, papermono, ...) share a chip_id, so the existing
// esp_image_header chip check cannot tell them apart.
//
// The tag is "CROSSPOINT-BOARD-V1:<board>;" stored once in .rodata — the
// scanner's needle references the same array, so a CrossPoint image contains
// exactly one occurrence. Images without a tag (other projects, forks, older
// releases) are allowed: the guard only rejects a tag naming a DIFFERENT
// board.

// The board name derives from the FREEINK_DEVICE_* build flags so every env
// (and any fork built from this source) is tagged automatically. The combined
// X3/X4 ESP32-C3 binary is one compatibility class, tagged "x4". Names match
// the release asset suffixes (firmware-<name>.bin; see below for x4pro).
#if FREEINK_DEVICE_X4PRO
#define CROSSPOINT_BOARD_NAME "x4pro"
#elif FREEINK_DEVICE_X4CLASSIC
#define CROSSPOINT_BOARD_NAME "x4c"
#elif FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
#define CROSSPOINT_BOARD_NAME "x4"
#elif FREEINK_DEVICE_PAPERMONO
#define CROSSPOINT_BOARD_NAME "papermono"
#elif FREEINK_DEVICE_STICKY
#define CROSSPOINT_BOARD_NAME "sticky"
#elif FREEINK_DEVICE_M5PAPER
#define CROSSPOINT_BOARD_NAME "m5paper"
#elif FREEINK_DEVICE_LILYGO
#define CROSSPOINT_BOARD_NAME "lilygo"
#elif FREEINK_DEVICE_M5
#define CROSSPOINT_BOARD_NAME "m5"
#elif FREEINK_DEVICE_MURPHY
#define CROSSPOINT_BOARD_NAME "murphy"
#elif FREEINK_DEVICE_DELINK
#define CROSSPOINT_BOARD_NAME "delink"
#else
#error "FirmwareBoardTag: no FREEINK_DEVICE_* flag set; cannot derive board name"
#endif

// Release asset this board's OTA updater requests. The x4pro keeps the plain
// name: every unit in the field since v1.0.0 asks for the literal
// "firmware.bin", so renaming its asset would strand them all (see
// OtaUpdater.cpp). Every board added after that ships with the per-board
// suffix from day one, so one release can carry one asset per device.
#if FREEINK_DEVICE_X4PRO
#define CROSSPOINT_RELEASE_ASSET "firmware.bin"
#else
#define CROSSPOINT_RELEASE_ASSET "firmware-" CROSSPOINT_BOARD_NAME ".bin"
#endif

namespace board_tag {

// Full tag: magic prefix + board name + ';'.
extern const char TAG[];

// Board name of the running firmware (pointer into TAG; not null-terminated at
// the name boundary — always pair with boardNameLen()).
const char* boardName();
size_t boardNameLen();

// Incremental scanner: feed every byte of a candidate image in stream order,
// then check mismatch(). State persists across feed() calls, so chunk
// boundaries splitting the tag are handled.
class Scanner {
 public:
  void feed(const uint8_t* data, size_t len);
  // True once a tag naming a different board has been seen. Valid mid-stream:
  // callers may abort a download as soon as this turns true.
  bool mismatch() const { return mismatchFound; }
  // Board name from the offending tag, for logging (empty until mismatch()).
  const char* foundName() const { return mismatchFound ? captured : ""; }

 private:
  static constexpr size_t MAX_NAME = 23;
  char captured[MAX_NAME + 1] = {0};
  size_t nameLen = 0;
  size_t magicMatched = 0;
  bool capturing = false;
  bool mismatchFound = false;
};

}  // namespace board_tag
