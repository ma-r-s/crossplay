#pragma once

// Device reporting: the device never makes a request of its own to report.
//
// When the firmware talks to one of CrossPlay's own services for some other
// reason (a Get Books catalog page, an Anki sync, an Instapaper sync), that
// request carries three headers: a hashed device id, the board, and a small
// JSON report (battery, lowest free heap, uptime, and, only while pending, the
// panic the device just recovered from and the install it attempted). The
// service posts the events; the device never brings the radio up for this,
// never opens a connection of its own, and adds nothing to any other host.
//
// Settings > System > "Include anonymous device info when using CrossPlay
// services" turns all of it off, and off records nothing. The rules are pure
// functions in DeviceReportCore.h (host-tests/devreport); this file is the
// card, the clock and the settings. Contract: docs/workflow/events.md.

namespace devreport {

// After the card is mounted, HalSystem::checkPanic() has run and settings are
// loaded. Reads the state file and, on a boot after a panic, records it.
// `panicReasonRecorded` is HalSystem::panicReasonRecorded() as read BEFORE
// checkPanic() cleared the marker: false means the reason in RTC memory is a
// previous crash's, and the record names only the reset and the last logger.
void begin(bool rebootedFromPanic, bool panicReasonRecorded);

// The headers a request to `url` carries: kHeaderCount when the host is one
// of ours and the toggle is on, 0 otherwise. The values point into this
// module's buffers and are valid until the next call; requests run one at a
// time. Nothing in the simulator, which is not a device.
struct Header {
  const char* name;
  const char* value;
};
constexpr int kHeaderCount = 3;
int headersFor(const char* url, Header out[kHeaderCount]);

// The request to `url` came back with `httpStatus` (0 or -1 for no answer).
// A 2xx from one of our hosts means the service has whatever the headers
// carried, so the pending crash and install record are cleared. Anything
// else, or any other host, changes nothing.
void delivered(const char* url, int httpStatus);

// A firmware install is about to run, or failed with this error. `path` is
// "ota" (the update screen) or "sd" (a .bin picked from the card): both land
// in the same partition and the same 6.25MB slots refuse both, but a device
// can fail one and not the other. Success is never recorded: the install
// reboots, and the next report infers it from the version that came up.
void noteOtaAttempt(const char* path);
void noteOtaFailed(const char* error);

// OtaUpdater::OtaUpdaterError as the word the services will show.
const char* otaErrorName(int otaUpdaterError);
// firmware_flash::Result as the word the services will show; the same word
// as otaErrorName() where the meaning is the same (too_large, wrong_device,
// oom).
const char* flashErrorName(int firmwareFlashResult);

}  // namespace devreport
