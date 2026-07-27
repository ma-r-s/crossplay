#pragma once

// Finish a WiFi session. Non-touch devices retain the existing ESP.restart()
// behavior, with an RTC_NOINIT flag so setup() skips the splash and routes to
// the destination. Touch-capable devices tear WiFi down normally to preserve
// external touch/frontlight peripheral state.

void silentRestart();          // restart destination: home
void silentRestartToReader();  // restart destination: currently-open EPUB
