#pragma once

// The TRMNL screens around the picture.
//
// Freestanding builders in the NotesScreens mould: a model in, a drawn frame
// out, no renderer and no Activity, so host-tests/trmnl can assert what they
// drew and what they made tappable. The picture itself is not here: it is the
// server's, drawn edge to edge by the activity with the panel turned the way
// the setting says.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace trmnlui {

namespace fui = freeink::ui;

// Notes has the 340s and Workouts the 620s; TRMNL takes the 700s.
enum : fui::ActionId {
  ActionShow = 700,
  ActionRefresh = 701,
  ActionUsePhone = 702,
  ActionDismiss = 703,
};

// --- The app's own screen -----------------------------------------------------

struct HomeModel {
  // "trmnl.app", or "192.168.1.20:2300" for a server at home.
  const char* server = "";
  // What the server calls this reader: its friendly id once setup has given
  // one, else the ID it sends.
  const char* device = "";
  // "every 15 min"
  const char* cadence = "";
  // One sentence: when the screen last changed and when it changes next, or
  // why it did not.
  const char* status = "";
  // A picture is on the card, so there is something to preview.
  bool hasImage = false;
};

// Returns the frame the caller draws the last picture into, scaled to fit.
// QR codes and pictures need a renderer, which this layer does not have.
fui::Rect buildHome(toybox::Screen& screen, const HomeModel& model);

// --- The phone ---------------------------------------------------------------

struct PhoneModel {
  // What the QR carries: the device's own address, from the live IP.
  const char* url = "";
  // What a person reads: the mDNS name when it started, else the address.
  const char* readable = "";
  bool saved = false;
};

// Returns the square the caller draws the code into.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

// --- Waiting, and refusals ------------------------------------------------------

// A sentence and a BACK button.
void buildNotice(toybox::Screen& screen, const char* prose);
// A sentence and nothing to press: the reader is busy talking to the server.
void buildBusy(toybox::Screen& screen, const char* prose);

}  // namespace trmnlui
