#pragma once

// Finish a WiFi session. Non-touch devices retain the existing ESP.restart()
// behavior, with an RTC_NOINIT flag so setup() skips the splash and routes to
// the destination. Touch-capable devices tear WiFi down normally to preserve
// external touch/frontlight peripheral state.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
