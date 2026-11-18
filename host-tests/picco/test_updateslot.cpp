// updateSlot() never hands a flash path the slot that is running.
//
// esp-idf's esp_ota_get_next_update_partition() falls back to the FIRST OTA
// slot when no other one follows the running slot. On the Picco's table there
// is one (`app`, ota_0, beside the vendor's `safeboot` factory partition), so
// the answer is the slot executing right now, and FirmwareFlasher's raw erase
// would wipe it. The four flash paths ask updateSlot() instead; test_config.py
// checks that they do.
#include "network/UpdateSlot.h"

#include <cstdio>

static int failures = 0;
#define EXPECT(cond, what)                     \
  do {                                         \
    if (!(cond)) {                             \
      std::printf("FAIL picco  %s\n", (what)); \
      failures++;                              \
    }                                          \
  } while (0)

int main() {
  const esp_partition_t app0{0x10000}, app1{0x7F0000}, picco{0xE0000};

  // Two slots, running app0: the other one is the update slot.
  fake_ota::running = &app0;
  fake_ota::next = &app1;
  EXPECT(updateSlot() == &app1, "with two slots the update goes to the one not running");

  // The Picco: esp-idf answers with the running slot itself.
  fake_ota::running = &picco;
  fake_ota::next = &picco;
  EXPECT(updateSlot() == nullptr, "a single-slot table has no update slot: the one it names is running");

  // No OTA partition at all.
  fake_ota::running = &app0;
  fake_ota::next = nullptr;
  EXPECT(updateSlot() == nullptr, "no OTA partition means no update slot");

  if (failures == 0) std::printf("picco updateSlot: ok\n");
  return failures == 0 ? 0 : 1;
}
