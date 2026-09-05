#pragma once

// The one place input is readable while a transfer holds the activity loop.
//
// HttpDownloader runs synchronously inside loop(), so for the whole of a
// transfer loop() cannot run again and its input checks are dead. The progress
// callback is the only code that still executes, which makes it the only place
// a Back or a home gesture can be seen. A flow that does not read them there
// cannot be left until the transfer ends, and a screen that cannot be left is
// what a reader reports as a frozen device.
//
// Both OPDS transfers pump through here so the two cannot drift: the book
// download (OpdsBookBrowserActivity) and the cover fetch that precedes it
// (OpdsDetailActivity).
//
// `cancelled` is what HttpDownloader::downloadToFile reads through its
// cancelFlag parameter -- it aborts the transfer and removes the partial file,
// so nothing half-written is left on the card. `goHome` distinguishes the home
// gesture from Back: the reader asked to leave the app, not to step back one
// screen.
//
// Templated on the input manager so the policy is testable on the host, where
// MappedInputManager's HalGPIO does not exist.
template <typename Input>
void pumpBlockingFetch(const Input& input, bool& cancelled, bool& goHome) {
  // The pump an activity may run only because it blocked the loop.
  input.update();
  if (input.wasReleased(Input::Button::Back)) cancelled = true;
  // That update() consumes the one-shot home event before the central
  // ActivityManager dispatch can see it, so a home gesture arriving mid
  // transfer is lost unless it is honoured right here.
  if (input.wasHomeGesture()) {
    cancelled = true;
    goHome = true;
  }
}
