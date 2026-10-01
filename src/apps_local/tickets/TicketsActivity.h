#pragma once

// Tickets: the cards, codes and boarding passes on the SD, one tap from the
// shelf and full-panel when shown.
//
// The three-way split every app in this fork uses. TicketsCore holds the file
// format (and has a host suite), TicketsScreens lays out, this file keeps what
// genuinely needs hardware: the card, and which screen is on the panel.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../../network/CrossPointWebServer.h"
#include "../ui/ToyboxScreen.h"
#include "TicketsLibrary.h"
#include "TicketsScreens.h"

class TicketsActivity final : public Activity {
 public:
  TicketsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Tickets", renderer, mappedInput) {}
  ~TicketsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { List, Ticket, Upload, Notice };

  void openList();
  void openTicket(int index);
  void rebuildRows();
  void relabelList();
  void startUploadServer();
  void stopUploadServer();
  void showNotice(const char* message);
  void deleteTicket(int index);
  void onExit() override;
  // Rows per page, asked of the same layout that draws them.
  int listPageSize();

  tickets::Library library_;
  View view_ = View::List;
  // False only when the card itself refused at entry; the list screen then
  // shows it instead of an empty wallet.
  bool cardOk_ = true;
  int openIndex_ = -1;
  int listTop_ = 0;
  std::string listPage_;
  std::string emptyMessage_;

  // Row storage for the list screen, owned here and rebuilt when the scan
  // changes: a std::vector built inside a screen builder allocates on every
  // paint, and the ListItems point into the library's entries, valid until
  // the next scan.
  std::vector<freeink::ui::ListItem> rows_;

  // The upload server. Owned here and torn down in exactly one place, because
  // a yield taken and not returned leaves Developer Mode without its ports
  // for the rest of the session.
  std::unique_ptr<CrossPointWebServer> uploadServer_;
  bool devPaused_ = false;
  std::string uploadUrl_;
  std::string uploadReadable_;
  std::string uploadStatus_;
  std::string notice_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;

  // Static callback for delete button (workaround for function pointer limitation)
  static int s_deleteTicketIndex;
  static void s_deleteTicketCallback();
};
