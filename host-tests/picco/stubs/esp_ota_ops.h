// What UpdateSlot.h needs from esp-idf, answering from test-owned state.
#pragma once

struct esp_partition_t {
  unsigned address;
};

namespace fake_ota {
inline const esp_partition_t* next = nullptr;
inline const esp_partition_t* running = nullptr;
}  // namespace fake_ota

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return fake_ota::next; }
inline const esp_partition_t* esp_ota_get_running_partition() { return fake_ota::running; }
