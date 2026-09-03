#pragma once

// The daily heartbeat, and the crash report after a panic.
//
// Once a day, when the radio is already up for somebody else (Developer
// Mode, an app that went online), the firmware posts one event to the board:
// a hashed device id, the version, the board, and the apps opened since the
// last one. It never brings the radio up for this and never takes it down.
// A boot after a panic also posts one level=error event carrying the panic
// reason, so a crash in the field opens a card by itself.
//
// Settings > System > "Send a daily heartbeat" turns all of it off. The rules
// are pure functions in HeartbeatCore.h (host-tests/heartbeat); this file is
// the clock, the card, the radio and the TLS. Contract: docs/workflow/events.md.

namespace heartbeat {

// After the card is mounted, HalSystem::checkPanic() has run and settings are
// loaded. Reads the state file and, on a boot after a panic, records it.
void begin(bool rebootedFromPanic);

// Once per loop(). At most one request per call, and only while
// WiFi.status() says connected; cheap otherwise.
void update();

// A shelf item was opened. One card write the first time since the last
// heartbeat, nothing afterwards.
void noteAppOpened(const char* title);

// An OTA install is about to run, or failed with this error. Success is
// never recorded: the install reboots, and the next heartbeat infers it from
// the version that came up.
void noteOtaAttempt();
void noteOtaFailed(const char* error);

// OtaUpdater::OtaUpdaterError as the word the board will show.
const char* otaErrorName(int otaUpdaterError);

}  // namespace heartbeat
