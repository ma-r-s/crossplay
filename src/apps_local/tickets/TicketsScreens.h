#pragma once

// The Tickets screens.
//
// Freestanding builders in the NotesScreens mould: a model in, a drawn frame
// out, no renderer and no Activity, so host tests can drive them without
// hardware. The QR square is RETURNED rather than drawn: QrUtils needs the
// renderer, which this layer does not have.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace ticketsui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer the 200s, Hacker News the 300s, Instapaper the
// 320s, Notes the 340s. Tickets takes the 360s.
enum : fui::ActionId {
  ActionOpenTicket = 360,
  ActionOpenUpload = 361,
  ActionDismiss = 363,
  ActionDeleteTicket = 364,
};

// --- The wallet ------------------------------------------------------------

struct ListModel {
  // Rows are ListItems already, built and owned by the Activity (subtitle is
  // the ticket's optional second line; the component draws label + subtitle,
  // one line each).
  const fui::ListItem* items = nullptr;
  int count = 0;
  int firstVisible = 0;
  // "1 / 2" when the wallet does not fit one page; null otherwise.
  const char* pageLabel = nullptr;
  // Set when there is nothing to list: a missing or empty /tickets/, a card
  // that would not open, or a directory whose every file failed to parse. The
  // two lines say which, because "NO TICKETS" over a directory of unreadable
  // files would read as the card being empty, and it is not.
  const char* emptyHeadline = nullptr;
  const char* emptyMessage = nullptr;
};

void buildList(toybox::Screen& screen, const ListModel& model);

// How many rows a page of the wallet holds, asked of the same geometry the
// drawing uses, so the page label, the physical keys and the drawn rows cannot
// disagree.
int listCapacity(const fui::DeviceContext& device);

// --- A ticket, open --------------------------------------------------------

struct TicketModel {
  const char* name = "";
  const char* subtitle = nullptr;  // null when the file carried none
  // Optional: callback for delete button in header (top-right)
  void (*onDelete)() = nullptr;
};

// The band carries the ticket's name (headerBand fits it); the QR takes every
// pixel the body can give a square, centred; the subtitle sits under it. The
// returned square is where the caller draws the code itself.
fui::Rect buildTicket(toybox::Screen& screen, const TicketModel& model);

// --- The upload page -------------------------------------------------------

struct UploadModel {
  const char* title = "";
  const char* url = "";           // address the QR encodes
  const char* readable = "";      // address a human reads/bookmarks
  const char* status_ = nullptr;  // "Saved: ..." or refusal text (underscore to avoid modfl conflict)
  bool wifiConnected = true;      // false when WiFi is off/disconnected
};

fui::Rect buildUpload(toybox::Screen& screen, const UploadModel& model);

}  // namespace ticketsui
