#pragma once

#include <esp_ota_ops.h>

// The app partition a firmware update may be written to, or nullptr when the
// partition table has none. Every flash path asks this instead of
// esp_ota_get_next_update_partition() directly.
//
// esp-idf falls back to the FIRST OTA slot when no other one follows the
// running slot, so on a single-slot table (the Picco's stock layout keeps one
// `app` slot beside the vendor's `safeboot` recovery partition) it answers
// with the slot that is RUNNING. FirmwareFlasher erases and writes raw, so
// taking that answer would erase the code being executed. esp_ota_begin()
// refuses the running slot on its own; the raw paths need this.
inline const esp_partition_t* updateSlot() {
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
#ifndef SIMULATOR
  if (next != nullptr && next == esp_ota_get_running_partition()) return nullptr;
#endif
  return next;
}
