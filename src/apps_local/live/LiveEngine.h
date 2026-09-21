#pragma once

#include <cstdint>
#include <string>

#include "LiveStore.h"

// The part of Live that touches the radio, the card and the clock.
//
// Two entry points, and they are the same operation seen from two sides:
//
//   checkNow()  -- somebody is looking at the screen and pressed Check now.
//   onSleep()   -- nobody is looking, the panel already shows what it will show,
//                  and the device is on its way down.
//
// Both end in the same place: what the service said, on the card, and a number
// of seconds for the RTC timer.

namespace live {
namespace engine {

// How long a headless join is allowed to spend before it is called a failure.
//
// DevMode's own join allows 20 seconds. That is the right number for a device
// somebody is holding and the wrong one for a wake nobody sees: 20 seconds of
// radio is most of what a successful check costs in the first place, so a
// device out of range pays nearly a full check to learn nothing. 12 seconds
// joins every network that is going to join -- an ESP32 associating with a
// known AP is a two-to-five second affair -- and the ones it gives up on are
// the ones that were not going to answer.
constexpr uint32_t kJoinTimeoutMs = 12000;

// Ask the service now, over a radio this brings up and puts back down.
//
// `imageArrived` distinguishes "there is something new on the glass" from "the
// check worked and nothing had changed", which is the difference between a
// repaint and no repaint. It is false on 304 and on 204, and it is false on
// every failure.
//
// A 401 clears the pairing (LiveStore::forgetPairing) and returns false with a
// sentence: the phone let this reader go, and the screen has to be able to say
// so rather than retry a token that will never work again.
//
// Saves the state before it returns, always -- including on failure, because
// the failure count IS the backoff and a count that only survives a success is
// not a backoff at all.
bool checkNow(State& state, bool& imageArrived, std::string& message);

// Holds the radio for one network call, and puts it down exactly as it found
// it. Every Live request needs this; checkNow does it internally.
//
// It exists because the pairing, join, poll, sender-list and revoke calls did
// NOT. They went straight to the transport, so pressing the tile on a reader
// whose radio was down made a TLS request with no network under it and the
// device panicked on a null semaphore -- reproducibly, from a button, in
// v1.13.10. A transport does not bring a radio up; that is policy and it lives
// here, beside the rule that Live loses every argument about the radio.
//
// Returns false with a sentence when the radio cannot be had. Call release()
// on EVERY path out, including the failures, or a reader that could not reach
// the service leaves the radio up and spends the night at 80mA.
class RadioLease {
 public:
  explicit RadioLease(std::string& message);
  ~RadioLease();
  bool held() const { return held_; }

  RadioLease(const RadioLease&) = delete;
  RadioLease& operator=(const RadioLease&) = delete;

 private:
  bool held_ = false;
};

// The one rule, run on the way into deep sleep, AFTER the sleep screen is on
// the glass.
//
// Returns the seconds to arm the RTC timer for; 0 means arm nothing, which is
// what every build did before Live and what a device with Live off still does.
//
// `repaintNeeded` comes back true only when an image actually arrived, which is
// the only case where it is worth spending a panel refresh on the way down. A
// failed or empty check NEVER blanks the panel: yesterday's message staying up
// is the correct failure state, and it is also the only one that looks like a
// working device from across a kitchen.
// `timerFired` says our own RTC timer ended the sleep that is now over, which
// main.cpp knows from the wake reason and nothing down here can work out. It is
// what lets a device with no wall clock keep a schedule: see LiveCore's decide.
uint32_t onSleep(bool& repaintNeeded, bool timerFired = false);

}  // namespace engine
}  // namespace live
