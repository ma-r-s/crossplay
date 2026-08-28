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

#include "DevInputCommands.h"
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

// How many bytes the transport refused, and how long we waited for it.
//
// These exist because the failure they measure is invisible from the device:
// the host sees a short read, the device sees nothing at all. Reported by
// CMD:CDCSTAT and, more usefully, LOGGED -- so on a unit whose cable has
// wedged they can still be read over Wi-Fi with GET /api/dev/log. The
// debugging channel and the broken channel finally being different things is
// the only reason the wedge is investigable at all.
struct TxStats {
  uint32_t shortWrites = 0;   // write() accepted less than it was asked for
  uint32_t zeroWrites = 0;    // write() accepted nothing at all
  uint32_t retryMs = 0;       // total time spent waiting on the ring
  uint32_t worstStallMs = 0;  // longest single wait
  uint32_t timeouts = 0;      // gave up on a payload
};
TxStats txStats;

// Write every byte, and NEVER ask for more than the transport can take.
//
// This is two bugs with one root, and the root is in the core rather than here.
// HWCDC::write returns how many bytes it ACCEPTED and returns 0 when the TX ring
// is full or it cannot take tx_lock inside tx_timeout_ms. The bridge discarded
// that number, so short writes vanished silently -- desk reads died at 26254,
// 712 and ~600 bytes of 48000, and the host only learned by counting.
//
// The worse half: when its internal wait expires, HWCDC gives up and sets
// `connected = false` (HWCDC.cpp, "write failed due to waiting USB Host -
// timeout"). Everything downstream reads that flag. `logSerial` is falsy, so
// logs stop; writes fall into the drop policy; and the device answers nothing
// on the cable while Wi-Fi carries on perfectly. That is exactly the "wedge"
// that cost two desk units a night and needed a physical button press: not a
// dead peripheral, a device that has concluded nobody is listening.
//
// So the fix is not to retry harder, it is to never make the core wait:
// availableForWrite() says how much fits right now, and asking for exactly that
// means write() cannot time out, cannot flip the flag, and cannot truncate.
// Waiting happens HERE, where it is bounded, counted, and reportable.
bool writeAll(const uint8_t* data, const size_t len, const unsigned long timeoutMs) {
  size_t sent = 0;
  unsigned long stallStart = 0;
  while (sent < len) {
    const int space = transport().availableForWrite();
    if (space > 0) {
      const size_t want = static_cast<size_t>(space) < len - sent ? static_cast<size_t>(space) : len - sent;
      const size_t n = transport().write(data + sent, want);
      if (n > 0) {
        if (stallStart != 0) {
          const uint32_t waited = static_cast<uint32_t>(millis() - stallStart);
          txStats.retryMs += waited;
          if (waited > txStats.worstStallMs) txStats.worstStallMs = waited;
          stallStart = 0;
        }
        if (n < want) txStats.shortWrites++;
        sent += n;
        continue;
      }
      txStats.zeroWrites++;
    }
    if (stallStart == 0) {
      stallStart = millis();
    } else if (millis() - stallStart >= timeoutMs) {
      // Record the stall BEFORE bailing. Only the success path updated
      // worstStallMs, so a run that timed out reported "timeouts=1
      // worstStallMs=2" -- two numbers that cannot both be true, and the
      // contradiction is what showed the accounting was wrong rather than the
      // transport.
      const uint32_t waited = static_cast<uint32_t>(millis() - stallStart);
      txStats.retryMs += waited;
      if (waited > txStats.worstStallMs) txStats.worstStallMs = waited;
      txStats.timeouts++;
      LOG_ERR("DEVBRIDGE", "transport took no bytes for %lums with %u of %u left; giving up", timeoutMs,
              static_cast<unsigned>(len - sent), static_cast<unsigned>(len));
      return false;
    }
    delay(2);  // vTaskDelay: let the CDC task drain the ring
  }
  return true;
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

  if (strcmp(cmd, "CDCSTAT") == 0) {
    transport().printf("OK CDCSTAT short=%u zero=%u retryMs=%u worstStallMs=%u timeouts=%u\n", txStats.shortWrites,
                       txStats.zeroWrites, txStats.retryMs, txStats.worstStallMs, txStats.timeouts);
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
    const uint8_t* fb = display.getFrameBuffer();
    if (fb == nullptr) {
      transport().printf("ERR SCREENSHOT no framebuffer\n");
      return;
    }
    const TxStats before = txStats;
    transport().printf("SCREENSHOT_START:%u\n", bufferSize);
    // One call. The old 512-byte chunking with a blind delay(3) was standing in
    // for flow control; writeAll paces itself off availableForWrite(), which is
    // the real thing, and is both faster when the ring is empty and correct
    // when it is not.
    // 5s, not 2. The first screenshot after a reset shares the ring with the
    // tail of the boot log, and 2s was short enough to lose that one and only
    // that one -- shots two through five went through untouched.
    const bool complete = writeAll(fb, bufferSize, 5000);
    transport().flush();
    if (!complete) {
      // Say it on the wire AND in the log. The host is about to see a short
      // read; without this it cannot tell a wedged cable from a crashed device,
      // and that ambiguity cost two sessions a night.
      transport().printf("\nERR SCREENSHOT truncated\n");
      LOG_ERR("DEVBRIDGE", "screenshot truncated: short=%u zero=%u worstStall=%ums",
              static_cast<unsigned>(txStats.shortWrites - before.shortWrites),
              static_cast<unsigned>(txStats.zeroWrites - before.zeroWrites),
              static_cast<unsigned>(txStats.worstStallMs));
      return;
    }
    transport().printf("SCREENSHOT_END\n");
    transport().printf("OK SCREENSHOT %u short=%u zero=%u stall=%ums\n", bufferSize,
                       static_cast<unsigned>(txStats.shortWrites - before.shortWrites),
                       static_cast<unsigned>(txStats.zeroWrites - before.zeroWrites),
                       static_cast<unsigned>(txStats.worstStallMs));
    return;
  }

  if (devinput::isCommand(cmd)) {
    // One vocabulary, two transports. See lib/DevInput/DevInputCommands.h.
    char reply[96];
    devinput::runCommand(cmd, reply, sizeof(reply));
    transport().printf("%s\n", reply);
    return;
  }

  transport().printf("ERR unknown command\n");
}

}  // namespace

void txStatsLine(char* out, const size_t len) {
  if (out == nullptr || len == 0) return;
  snprintf(out, len, "short=%u zero=%u retryMs=%u worstStallMs=%u timeouts=%u", txStats.shortWrites, txStats.zeroWrites,
           txStats.retryMs, txStats.worstStallMs, txStats.timeouts);
}

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
