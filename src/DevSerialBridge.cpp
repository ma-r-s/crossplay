#include "DevSerialBridge.h"

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SPI.h>
#include <SdFat.h>
#include <driver/gpio.h>

#include <cstdlib>
#include <cstring>

#include "DevInputInjector.h"
#include "network/FirmwareBoardTag.h"

namespace devbridge {
namespace {

auto& transport() { return BoardConfig::serialTransport(); }

// Own SdFs instance for the raw card probe and the formatter, so the bridge
// can see below HalStorage (whose SDK owner prints its diagnostics to the
// native-USB Serial, dead on the Sticky). Never used while the firmware's own
// volume is mounted: probe and format only make sense when Storage.begin()
// has failed, and both end() the instance before returning.
SdFs probeSd;

// Bring up the SD rail and the (possibly display-shared) SPI bus the way
// SDCardManager::begin does for SPI boards.
void sdRailAndBus() {
  const auto& sd = BoardConfig::ACTIVE.sd;
  if (sd.powerEnable >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(sd.powerEnable));
    pinMode(sd.powerEnable, OUTPUT);
    digitalWrite(sd.powerEnable, sd.powerActiveHigh ? HIGH : LOW);
    delay(50);
  }
  const auto& dp = BoardConfig::ACTIVE.display;
  const int8_t sclk = sd.sclk >= 0 ? sd.sclk : dp.sclk;
  const int8_t mosi = sd.mosi >= 0 ? sd.mosi : dp.mosi;
  if (dp.cs >= 0 && dp.sclk == sclk) {
    pinMode(dp.cs, OUTPUT);
    digitalWrite(dp.cs, HIGH);
  }
  if (sclk >= 0 && mosi >= 0 && sd.miso >= 0) {
    SPI.begin(sclk, sd.miso, mosi, sd.cs);
  }
}

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
      {"BACK", HalGPIO::BTN_BACK},   {"CONFIRM", HalGPIO::BTN_CONFIRM}, {"LEFT", HalGPIO::BTN_LEFT},
      {"RIGHT", HalGPIO::BTN_RIGHT}, {"UP", HalGPIO::BTN_UP},           {"DOWN", HalGPIO::BTN_DOWN},
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

  if (strcmp(cmd, "DATE") == 0) {
    // Both clocks side by side: system time (what today()/Study/Connections
    // consume) and the hardware RTC (what the status bar reads).
    const time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char sys[32];
    strftime(sys, sizeof(sys), "%Y-%m-%d %H:%M:%S", &t);
    Rtc::DateTime dt;
    const bool rtcOk = halClock.available() && halClock.raw(dt);
    transport().printf("OK DATE system=%sZ rtc=", sys);
    if (rtcOk) {
      transport().printf("%04u-%02u-%02u %02u:%02u:%02uZ\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
      transport().printf("unavailable\n");
    }
    return;
  }

  if (strcmp(cmd, "REBOOT") == 0) {
    transport().printf("OK REBOOT\n");
    delay(50);
    ESP.restart();
    return;
  }

  if (strncmp(cmd, "LS", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
    const char* path = cmd[2] == ' ' ? cmd + 3 : "/";
    const auto files = Storage.listFiles(path, 200);
    for (const auto& f : files) {
      transport().printf("  %s\n", f.c_str());
    }
    transport().printf("OK LS %s %u entries\n", path, static_cast<unsigned>(files.size()));
    return;
  }

  if (strncmp(cmd, "WRITETEST", 9) == 0 && (cmd[9] == '\0' || cmd[9] == ' ')) {
    // Isolate the storage layer: open exactly the way the games save
    // (openFileForWrite), write, close, stat, read back, reporting every
    // return value. Optional path argument to probe subdirectories.
    const char* p = cmd[9] == ' ' ? cmd + 10 : "/bridge-test.txt";
    const bool parent = Storage.exists("/.crosspoint");
    bool opened = false;
    size_t written = 0;
    {
      HalFile f;
      opened = Storage.openFileForWrite("BRIDGE", p, f);
      if (opened) {
        const char msg[] = "hello from the bridge\n";
        written = f.write(reinterpret_cast<const uint8_t*>(msg), sizeof(msg) - 1);
      }
    }
    const bool there = Storage.exists(p);
    char buf[64] = {};
    const size_t got = there ? Storage.readFileToBuffer(p, buf, sizeof(buf)) : 0;
    transport().printf("OK WRITETEST path=%s dotdir=%d opened=%d written=%u exists=%d read=%u content=%s\n", p,
                       parent ? 1 : 0, opened ? 1 : 0, static_cast<unsigned>(written), there ? 1 : 0,
                       static_cast<unsigned>(got), buf);
    return;
  }

  if (strncmp(cmd, "MKDIR ", 6) == 0) {
    const bool ok = Storage.mkdir(cmd + 6);
    transport().printf("OK MKDIR %s ok=%d exists=%d\n", cmd + 6, ok ? 1 : 0, Storage.exists(cmd + 6) ? 1 : 0);
    return;
  }

  if (strncmp(cmd, "RM ", 3) == 0) {
    const bool ok = Storage.remove(cmd + 3);
    transport().printf("OK RM %s ok=%d\n", cmd + 3, ok ? 1 : 0);
    return;
  }

  if (strncmp(cmd, "RMDIR ", 6) == 0) {
    const bool ok = Storage.rmdir(cmd + 6);
    transport().printf("OK RMDIR %s ok=%d\n", cmd + 6, ok ? 1 : 0);
    return;
  }

  if (strncmp(cmd, "CAT ", 4) == 0) {
    const char* path = cmd + 4;
    if (!Storage.exists(path)) {
      transport().printf("ERR CAT no such file: %s\n", path);
      return;
    }
    transport().printf("CAT_START %s\n", path);
    Storage.readFileToStream(path, transport());
    transport().printf("\nOK CAT %s\n", path);
    return;
  }

  if (strcmp(cmd, "SDPROBE") == 0) {
    // Raw card diagnosis, below the SDK: does the card answer SPI init at
    // all, and if so, does anything mountable live on it?
    sdRailAndBus();
    if (!probeSd.cardBegin(SdSpiConfig(BoardConfig::ACTIVE.sd.cs, SHARED_SPI, SD_SCK_MHZ(4)))) {
      transport().printf("OK SDPROBE card=0 err=0x%02X data=0x%02X\n", probeSd.sdErrorCode(), probeSd.sdErrorData());
      return;
    }
    const uint32_t sectors = probeSd.card()->sectorCount();
    const bool vol = probeSd.volumeBegin();
    transport().printf("OK SDPROBE card=1 sectors=%u mb=%u vol=%d fatType=%d err=0x%02X\n", sectors,
                       static_cast<unsigned>(sectors / 2048u), vol ? 1 : 0, vol ? probeSd.fatType() : 0,
                       probeSd.sdErrorCode());
    probeSd.end();
    return;
  }

  if (strncmp(cmd, "SDFORMAT", 8) == 0) {
    // Destructive: erases whatever is on the card. The literal YES is the
    // arming pin so a mistyped command cannot do it.
    if (strcmp(cmd, "SDFORMAT YES") != 0) {
      transport().printf("ERR SDFORMAT requires the literal argument YES\n");
      return;
    }
    sdRailAndBus();
    if (!probeSd.cardBegin(SdSpiConfig(BoardConfig::ACTIVE.sd.cs, SHARED_SPI, SD_SCK_MHZ(4)))) {
      transport().printf("ERR SDFORMAT no card: err=0x%02X data=0x%02X\n", probeSd.sdErrorCode(),
                         probeSd.sdErrorData());
      return;
    }
    // FsFormatter picks FAT16/32 vs exFAT by card size; progress lines go to
    // this same serial.
    const bool ok = probeSd.format(&transport());
    const bool vol = ok && probeSd.volumeBegin();
    transport().printf("OK SDFORMAT ok=%d vol=%d fatType=%d\n", ok ? 1 : 0, vol ? 1 : 0, vol ? probeSd.fatType() : 0);
    probeSd.end();
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
