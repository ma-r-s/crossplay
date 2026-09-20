#pragma once

// Finish a WiFi session. ESP.restart() with an RTC_NOINIT flag that survives
// the reboot, so setup() skips the boot splash and routes straight to a
// destination. Used to clear heap fragmentation accumulated during a wifi
// session. The live frontlight state rides along in the same flag so the
// reboot is invisible: the light comes back exactly as it was, regardless of
// the Restore Light on Wake preference.
//
// FORK: only non-touch devices take that restart path. Touch-capable devices
// tear WiFi down normally instead, to preserve external touch/frontlight
// peripheral state.

void silentRestart();            // home screen
void silentRestartToReader();    // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToSettings();  // settings screen

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
// Same handoff reboot, landing in the shelf app that was open (the title the
// shelf recorded when it opened it), or on Home if nothing was.
void restartToAppAfterStorageHandoff();
