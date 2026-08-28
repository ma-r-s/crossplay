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
#include <esp_mac.h>

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
  uint32_t worstStallMs = 0;  // longest single wait, LIFETIME
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
// So the fix is not to retry harder, it is to stop making the core wait:
// availableForWrite() says how much fits right now, and asking for exactly that
// removes the DOMINANT path into its wait. It does not remove every path, and
// an earlier version of this comment claimed it did -- availableForWrite takes
// and releases tx_lock, so another writer (a LOG_ from the render task, a web
// handler) can take the space in between and write() falls into the blocking
// loop anyway. Much less likely, not never.
// The waiting we do control happens HERE, bounded, counted and reportable.
bool writeAll(const uint8_t* data, const size_t len, const unsigned long timeoutMs) {
  size_t sent = 0;
  unsigned long stallStart = 0;
  // Two deadlines. The per-stall one catches a transport that has stopped; the
  // overall one catches a transport that dribbles -- a few bytes, a long pause,
  // repeat -- which resets the per-stall timer forever and would hold the loop
  // task without bound. That matters more than it sounds: the loop task also
  // serves Developer Mode's HTTP, so an unbounded stall here would take out
  // /api/dev/serial, the very channel this instrumentation exists to reach.
  const unsigned long overallDeadline = millis() + timeoutMs * 4;
  while (sent < len) {
    // Checked at the TOP, unconditionally. Tucked inside the stall branch it was
    // unreachable for the pattern it was added for: one byte accepted, one
    // stalled iteration, repeat -- stallStart resets on every scrap of progress,
    // so neither deadline fired and a 48KB payload could hold the loop task for
    // minutes.
    if (static_cast<long>(millis() - overallDeadline) >= 0) {
      txStats.timeouts++;
      LOG_ERR("DEVBRIDGE", "gave up after %lums overall with %u of %u bytes left", timeoutMs * 4,
              static_cast<unsigned>(len - sent), static_cast<unsigned>(len));
      return false;
    }
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

// A short line, paced the same way. printf() goes straight to HWCDC::write, and
// the ring is 256 bytes -- so a line written immediately AFTER a 48KB payload
// lands when it is at its fullest, which is exactly the condition the removed
// flush() was dangerous in. The worst case was the truncation notice itself:
// the line reporting the jam was the most likely thing to flip the flag.
bool writeLine(const char* text, const unsigned long timeoutMs) {
  return writeAll(reinterpret_cast<const uint8_t*>(text), strlen(text), timeoutMs);
}

void formatTxStats(char* out, const size_t len) {
  if (out == nullptr || len == 0) return;
  // plugged and connected FIRST, because without them the rest is ambiguous:
  // HWCDC::write returns the FULL size when isCDC_Connected() is false -- it
  // runs a drop policy and reports complete acceptance -- so a device
  // discarding every byte reports short=0 zero=0 timeouts=0, byte-identical to
  // a healthy idle one. An all-zero line was once quoted as proof this route
  // worked; it proved only that the route answered.
  // The MAC, from efuse rather than from WiFi: it has to answer while the
  // radio is off, which is exactly the case a device in a link match is in.
  // Over the cable this is redundant with ioreg, but over Wi-Fi it is the only
  // way to tell two identical desk units apart -- and identifying the unit
  // immediately before writing to it is the standing rule here.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(out, len,
           "mac=%02x:%02x:%02x:%02x:%02x:%02x plugged=%d connected=%d short=%u zero=%u retryMs=%u "
           "worstStallMsLifetime=%u timeouts=%u logDrops=%u",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], HWCDC::isPlugged() ? 1 : 0,
           static_cast<bool>(transport()) ? 1 : 0, txStats.shortWrites, txStats.zeroWrites, txStats.retryMs,
           txStats.worstStallMs, txStats.timeouts, getDroppedLogLines());
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
    // Paced: this is the command you run BECAUSE the transport is
    // misbehaving, so it is the last one that should be shoved at it unpaced.
    char stats[256];
    formatTxStats(stats, sizeof(stats));
    char reply[288];
    snprintf(reply, sizeof(reply), "OK CDCSTAT %s\n", stats);
    writeLine(reply, 1000);
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
    // Paced. 200 names is several KB through a 256-byte ring, and an unpaced
    // run of it is a larger overrun than the screenshot ever was.
    for (const auto& f : files) {
      char row[288];
      snprintf(row, sizeof(row), "  %s\n", f.c_str());
      if (!writeLine(row, 1000)) {
        writeLine("\nERR LS truncated\n", 1000);
        return;
      }
    }
    char tail[160];
    snprintf(tail, sizeof(tail), "OK LS %s %u entries\n", path, static_cast<unsigned>(files.size()));
    writeLine(tail, 1000);
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
    HalFile file;
    if (!Storage.openFileForRead("DEVBRIDGE", path, file)) {
      transport().printf("ERR CAT cannot open: %s\n", path);
      return;
    }
    char head[160];
    snprintf(head, sizeof(head), "CAT_START %s\n", path);
    writeLine(head, 1000);
    // Paced, rather than SDCardManager::readFileToStream. That helper pushes
    // 256-byte chunks into a 256-byte ring in a tight loop and ignores every
    // return value, so any file past the first chunk drives HWCDC::write into
    // its blocking path with tries = tx_timeout_ms = 1. One millisecond without
    // drain progress and the connection flag goes down and the ring is thrown
    // away -- the wedge, reached by a single CAT of an ordinary file.
    uint8_t chunk[512];
    const char* failure = nullptr;
    for (;;) {
      // 0 is EOF, -1 is a read error, and they must not share an exit: the CAT
      // protocol carries no length, so a card that fails 4KB into a 40KB file
      // otherwise ends with "OK CAT" over a truncated body and the host has no
      // way to know.
      const int n = file.read(chunk, sizeof(chunk));
      if (n == 0) break;
      if (n < 0) {
        failure = "read error";
        break;
      }
      if (!writeAll(chunk, static_cast<size_t>(n), 5000)) {
        failure = "transport";
        break;
      }
    }
    if (failure != nullptr) {
      LOG_ERR("DEVBRIDGE", "cat truncated (%s): %s", failure, path);
      char err[96];
      snprintf(err, sizeof(err), "\nERR CAT truncated (%s)\n", failure);
      writeLine(err, 1000);
      return;
    }
    char tail[160];
    snprintf(tail, sizeof(tail), "\nOK CAT %s\n", path);
    writeLine(tail, 1000);
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
    // Paced like everything else. Print::vprintf does ONE write() and throws
    // the result away, so a header written while the boot log still shares the
    // ring goes out short -- and the host then parses a truncated digit string
    // as the frame length.
    char head[48];
    snprintf(head, sizeof(head), "SCREENSHOT_START:%u\n", bufferSize);
    writeLine(head, 1000);
    // One call. The old 512-byte chunking with a blind delay(3) was standing in
    // for flow control; writeAll paces itself off availableForWrite(), which is
    // the real thing, and is both faster when the ring is empty and correct
    // when it is not.
    // 5s, not 2. The first screenshot after a reset shares the ring with the
    // tail of the boot log, and 2s was short enough to lose that one and only
    // that one -- shots two through five went through untouched.
    const bool complete = writeAll(fb, bufferSize, 5000);
    // NO flush() HERE, and that is the point. HWCDC::flush starts
    // tries = tx_timeout_ms, and this firmware sets that to 1
    // (main.cpp: "This is a load-bearing 1. Do not modify."). One millisecond
    // without drain progress and flush sets connected = false AND calls
    // flushTXBuffer(NULL, 0), throwing the entire pending ring away. Calling it
    // straight after a 48KB payload means calling it exactly when the ring is
    // fullest, on every single screenshot -- the old code called it every 512
    // bytes, which is worse still.
    //
    // Nothing needs it. writeAll has already handed every byte to the ring and
    // the IN_EMPTY ISR drains it; the OK line below enters the same ring behind
    // the payload, so ordering holds. flush() only answers "has it left yet",
    // which no caller asks, at the price of the connection flag.
    if (!complete) {
      // Log FIRST, then the wire. LOG_ERR goes out on this same transport, so
      // logging after the notice buries the notice behind a log line. The host
      // no longer depends on that ordering -- it reads the terminator's
      // position, not what came last -- but a reader tailing the cable does,
      // and the notice is the line they are looking for.
      LOG_ERR("DEVBRIDGE", "screenshot truncated: short=%u zero=%u worstStallMsLifetime=%ums",
              static_cast<unsigned>(txStats.shortWrites - before.shortWrites),
              static_cast<unsigned>(txStats.zeroWrites - before.zeroWrites),
              static_cast<unsigned>(txStats.worstStallMs));
      writeLine("\nERR SCREENSHOT truncated\n", 1000);
      return;
    }
    writeLine("SCREENSHOT_END\n", 1000);
    // Paced too. It follows 48KB and the terminator, so of every line in this
    // file it meets the fullest ring -- which is the condition the whole
    // no-flush argument above is about.
    char summary[128];
    snprintf(summary, sizeof(summary), "OK SCREENSHOT %u short=%u zero=%u worstStallMsLifetime=%ums\n", bufferSize,
             static_cast<unsigned>(txStats.shortWrites - before.shortWrites),
             static_cast<unsigned>(txStats.zeroWrites - before.zeroWrites),
             static_cast<unsigned>(txStats.worstStallMs));
    writeLine(summary, 1000);
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

void txStatsLine(char* out, const size_t len) { formatTxStats(out, len); }

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
