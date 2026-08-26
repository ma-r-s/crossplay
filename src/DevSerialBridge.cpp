#include "DevSerialBridge.h"

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>

#include "DevInputInjector.h"
#include "network/FirmwareBoardTag.h"

namespace devbridge {
namespace {

auto& transport() { return BoardConfig::serialTransport(); }

char lineBuf[96];
size_t lineLen = 0;

float normX(long px) { return (static_cast<float>(px) + 0.5f) / BoardConfig::ACTIVE.displayWidth; }
float normY(long py) { return (static_cast<float>(py) + 0.5f) / BoardConfig::ACTIVE.displayHeight; }

// Parse up to n longs out of s; returns how many were found.
int parseLongs(const char* s, long* out, int n) {
  int found = 0;
  char* end = nullptr;
  while (found < n) {
    while (*s == ' ') s++;
    if (*s == '\0') break;
    const long v = strtol(s, &end, 10);
    if (end == s) break;
    out[found++] = v;
    s = end;
  }
  return found;
}

int buttonIndexByName(const char* name, size_t len) {
  struct Entry {
    const char* name;
    uint8_t index;
  };
  static constexpr Entry MAP[] = {
      {"BACK", HalGPIO::BTN_BACK}, {"CONFIRM", HalGPIO::BTN_CONFIRM}, {"LEFT", HalGPIO::BTN_LEFT},
      {"RIGHT", HalGPIO::BTN_RIGHT}, {"UP", HalGPIO::BTN_UP},         {"DOWN", HalGPIO::BTN_DOWN},
      {"POWER", HalGPIO::BTN_POWER},
  };
  for (const auto& e : MAP) {
    if (strlen(e.name) == len && strncmp(e.name, name, len) == 0) return e.index;
  }
  return -1;
}

void handleLine(const char* line) {
  if (strncmp(line, "CMD:", 4) != 0) return;
  const char* cmd = line + 4;

  if (strcmp(cmd, "PING") == 0) {
    transport().printf("OK PONG board=%.*s version=%s heap=%u minheap=%u psram=%u\n",
                       static_cast<int>(board_tag::boardNameLen()), board_tag::boardName(), CROSSPOINT_VERSION,
                       ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getPsramSize());
    return;
  }

  if (strcmp(cmd, "HEAP") == 0) {
    transport().printf("OK HEAP free=%u min=%u maxalloc=%u total=%u psramfree=%u psramtotal=%u\n", ESP.getFreeHeap(),
                       ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), ESP.getHeapSize(), ESP.getFreePsram(),
                       ESP.getPsramSize());
    return;
  }

  if (strcmp(cmd, "REBOOT") == 0) {
    transport().printf("OK REBOOT\n");
    delay(50);
    ESP.restart();
    return;
  }

  if (strncmp(cmd, "SD", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
    // Retry the SD mount, optionally at a different SPI clock — a remote probe
    // for the shared-bus bring-up question (the SDK's own diagnostics print to
    // the native-USB Serial, which has no host on the Sticky).
    long hz[1];
    if (cmd[2] == ' ' && parseLongs(cmd + 3, hz, 1) == 1 && hz[0] > 0) {
      BoardConfig::ACTIVE.sd.spiHz = static_cast<uint32_t>(hz[0]);
    }
    const bool up = Storage.begin();
    transport().printf("OK SD mounted=%d spiHz=%u\n", up ? 1 : 0, BoardConfig::ACTIVE.sd.spiHz);
    return;
  }

  if (strcmp(cmd, "SCREENSHOT") == 0) {
    const uint32_t bufferSize = display.getBufferSize();
    transport().printf("SCREENSHOT_START:%u\n", bufferSize);
    transport().write(display.getFrameBuffer(), bufferSize);
    transport().printf("SCREENSHOT_END\n");
    transport().printf("OK SCREENSHOT %u\n", bufferSize);
    return;
  }

  long v[5];
  if (strncmp(cmd, "TAP ", 4) == 0) {
    const int n = parseLongs(cmd + 4, v, 3);
    if (n < 2) {
      transport().printf("ERR TAP wants: x y [holdMs]\n");
      return;
    }
    const unsigned long hold = n >= 3 ? static_cast<unsigned long>(v[2]) : 140;
    if (devinput::tap(normX(v[0]), normY(v[1]), hold)) {
      transport().printf("OK TAP %ld %ld %lu\n", v[0], v[1], hold);
    } else {
      transport().printf("ERR busy\n");
    }
    return;
  }

  if (strncmp(cmd, "LONG ", 5) == 0) {
    if (parseLongs(cmd + 5, v, 2) < 2) {
      transport().printf("ERR LONG wants: x y\n");
      return;
    }
    if (devinput::longPress(normX(v[0]), normY(v[1]))) {
      transport().printf("OK LONG %ld %ld\n", v[0], v[1]);
    } else {
      transport().printf("ERR busy\n");
    }
    return;
  }

  if (strncmp(cmd, "SWIPE ", 6) == 0) {
    const int n = parseLongs(cmd + 6, v, 5);
    if (n < 4) {
      transport().printf("ERR SWIPE wants: x0 y0 x1 y1 [ms]\n");
      return;
    }
    const unsigned long ms = n >= 5 ? static_cast<unsigned long>(v[4]) : 250;
    if (devinput::swipe(normX(v[0]), normY(v[1]), normX(v[2]), normY(v[3]), ms)) {
      transport().printf("OK SWIPE %ld %ld %ld %ld %lu\n", v[0], v[1], v[2], v[3], ms);
    } else {
      transport().printf("ERR busy\n");
    }
    return;
  }

  if (strncmp(cmd, "BTN ", 4) == 0) {
    const char* rest = cmd + 4;
    const char* space = strchr(rest, ' ');
    const size_t nameLen = space ? static_cast<size_t>(space - rest) : strlen(rest);
    const int index = buttonIndexByName(rest, nameLen);
    if (index < 0) {
      transport().printf("ERR BTN wants: UP|DOWN|CONFIRM|BACK|LEFT|RIGHT|POWER [holdMs]\n");
      return;
    }
    unsigned long hold = 80;
    if (space && parseLongs(space, v, 1) == 1) hold = static_cast<unsigned long>(v[0]);
    if (devinput::button(static_cast<uint8_t>(index), hold)) {
      transport().printf("OK BTN %.*s %lu\n", static_cast<int>(nameLen), rest, hold);
    } else {
      transport().printf("ERR busy\n");
    }
    return;
  }

  transport().printf("ERR unknown command\n");
}

}  // namespace

void begin() {
#if FREEINK_DEVICE_STICKY
  // The Sticky's transport is UART0 behind the on-board WCH bridge; nothing
  // else starts its receive side (log output goes through esp_rom_printf).
  transport().begin(115200);
#endif
  transport().printf("OK DEVBRIDGE %.*s %s\n", static_cast<int>(board_tag::boardNameLen()), board_tag::boardName(),
                     CROSSPOINT_VERSION);
}

void update() {
  while (transport().available() > 0) {
    const int c = transport().read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        lineLen = 0;
        handleLine(lineBuf);
      }
      continue;
    }
    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = static_cast<char>(c);
    } else {
      lineLen = 0;  // overlong line: drop it rather than act on a truncation
    }
  }
}

}  // namespace devbridge

#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE
