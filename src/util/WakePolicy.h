#pragma once

// Whether a boot may turn the frontlight on, decided by WHY the boot happened.
//
// The frontlight is the largest single draw on this device, and until Live
// existed every boot had a person behind it, so "restore the light the way the
// user left it" was the whole rule and the wake reason never entered into it.
//
// Live broke that. A scheduled refresh is a full chip reset (deep sleep always
// is), so it runs setup() exactly like a person's wake does, and setup() lit
// the panel for the radio's benefit: on a fridge, at whatever hour the schedule
// landed on, in a dark kitchen, for every check including the ones that find
// nothing. Mario saw it on hardware -- "the backlight is turning on on refresh".
//
// So the wake reason has to reach this decision. It is a pure function in a
// header because the value of writing it down is that it can be asserted on a
// laptop: the failure it prevents is a light in a dark room at 4am and a
// battery gone in weeks, neither of which any build, suite or screenshot can
// see. host-tests/wakepolicy walks every case.
//
// No Arduino, HAL or SDK include belongs here, and none is needed: the inputs
// are three booleans and a reason.

namespace wakepolicy {

// Why setup() is running, as far as the light is concerned. Not the HAL's
// WakeupReason: that enum names the electrical cause, and the simulator's own
// HalGPIO does not even carry Timer. This names the only distinction the light
// cares about, which is whether a person is there.
enum class Boot : unsigned char {
  // A person did this: a power-button wake, a cold boot, a flash, a reset.
  // Whatever they left the light doing is what they expect to find.
  User,
  // An internal heap-defrag restart the user never asked for (leaving a WiFi
  // activity, say). They are still holding the device and still looking at it,
  // so the light must come back exactly as it was -- which is the LIVE state
  // carried across the reboot, not the saved preference. Those legitimately
  // diverge: a wake with Restore Light on Wake off leaves the light off while
  // the saved "was on" preference is kept.
  Silent,
  // The device woke ITSELF on its own RTC timer and nobody is there. It will go
  // straight back to sleep whether or not it finds anything, and the panel
  // keeps whatever image it was already showing. Nothing is to be looked at, so
  // nothing may be lit.
  Unattended,
};

struct Saved {
  // SETTINGS.frontlightOn: the user's saved preference, which survives a wake
  // that deliberately did not honour it.
  bool lightOn = false;
  // SETTINGS.frontlightRestoreOnWake: "Restore Light on Wake" in Display.
  bool restoreOnWake = false;
  // The live on/off captured at a silent restart and carried through it.
  bool silentRebootLightOn = false;
};

// The whole policy. Unattended is checked FIRST and unconditionally: it is the
// one case where the answer cannot depend on a setting, because a setting the
// user chose for their own wakes is not consent for a wake they never asked
// for. Ordering it first also makes a hypothetical Unattended-and-Silent boot
// (the RTC_NOINIT magic is cleared on every boot, so it cannot happen today)
// fail safe rather than fail lit.
constexpr bool restoreFrontlight(const Boot boot, const Saved& saved) {
  switch (boot) {
    case Boot::Unattended:
      return false;
    case Boot::Silent:
      return saved.silentRebootLightOn;
    case Boot::User:
      return saved.lightOn && saved.restoreOnWake;
  }
  return false;
}

// May this boot put a user interface on the glass: the splash, the home screen,
// the book that was open.
//
// The same rule as the light, and it is the same complaint. The light was only
// the half Mario could name. A refresh that found a drawing used to boot the
// whole reader to draw it -- splash, then home or whatever book was open, then
// the drawing -- and on e-ink each of those is a full visible repaint. A
// picture arriving at 3am announced itself with a startup screen first. Both
// halves are setup() doing something because a person is presumably there,
// when for this one caller there is not.
//
// What an unattended boot may still do is bring up the display and the fonts
// and repaint the sleep screen, because that is the image the panel is meant
// to be showing anyway. What it may not do is answer questions nobody asked.
constexpr bool presentsUi(const Boot boot) { return boot != Boot::Unattended; }

}  // namespace wakepolicy
