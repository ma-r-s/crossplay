#pragma once

// Cards: codes you show at a counter -- a loyalty card, a boarding pass, a link,
// your contact details -- one per screen, at a size a scanner reads.
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card -- so that
// host-tests/wallet can drive every rule on a laptop. WalletStore reads and
// writes the bytes these functions hand it.
//
// A card is one file in /cards, named by a number so the files sort in the
// order they were added:
//
//   Lidl Plus            <- the title, line one
//   Member since 2021    <- the caption, line two, may be empty
//   https://lidl.de/...  <- the code's payload: everything after line two
//
// A barcode card names its kind after the title and a tab, "Lidl Plus\tean13";
// a title with no kind is a QR code, which is every card written before
// barcodes. A title can never hold a tab of its own, because cleanLine() turns
// tabs into spaces, so the tab is unambiguous.
//
// The payload is what the code SAYS, not a picture of it. The phone decodes the
// photo and the reader draws the code again from these bytes, so the panel
// shows sharp modules at whatever size fits rather than a dithered photograph.
// Everything after the second newline is the payload verbatim, because a
// contact card or a Wi-Fi code carries newlines of its own; only trailing line
// ends are dropped, which an editor on a computer adds and no code needs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "WalletBars.h"

namespace wallet {

constexpr size_t kMaxTitle = 40;
constexpr size_t kMaxCaption = 120;
// The most a version 40 code holds in byte mode at the lowest correction level
// is 2953 bytes, at 177 modules: two pixels each on this panel, which no
// scanner at a till will read. 1200 keeps every module at three pixels or more.
constexpr size_t kMaxPayload = 1200;
constexpr int kMaxCards = 60;
// A card file is never larger than this; anything that is, is not a card.
constexpr size_t kMaxFileBytes = kMaxTitle + kMaxCaption + kMaxPayload + 64;

struct Card {
  std::string file;  // "0003.txt": its name in /cards, and its identity
  std::string title;
  std::string caption;
  std::string payload;
  CodeKind kind = CodeKind::Qr;
};

// A file's contents as a card. False when there is no payload, which is the one
// thing a card cannot do without. Title and caption are cleaned as they would
// be on the way in, so a hand-edited file reads like one the phone wrote.
bool parseCard(const std::string& text, Card& out);
std::string formatCard(const Card& card);

// One line of text as the panel can hold it: control characters (tabs, line
// ends) become spaces, runs of spaces collapse, the ends are trimmed, and it is
// cut to `max` bytes without splitting a UTF-8 sequence.
std::string cleanLine(const std::string& text, size_t max);

// "0007.txt" for 7. Card files are the four-digit names this writes.
std::string fileNameFor(int number);
// The number a card file carries, or -1 for any other name.
int numberOf(const std::string& file);
// The name the next card is saved under: one past the highest in use.
std::string nextFileName(const std::vector<std::string>& files);

// The card files among a directory listing, in the order they were added.
std::vector<std::string> cardFiles(const std::vector<std::string>& names);

// What the phone sends: the title, the caption, the payload in hex, and the
// kind of code ("ean13"; missing means QR), each on a line of its own. Hex
// because a code may carry any byte and the request body arrives as text.
// False, with `error` set to a sentence for the phone, when what came is not a
// card or not a barcode of the kind it says. An EAN or UPC sent without its
// check digit is saved with it.
bool parseUpload(const std::string& body, Card& out, std::string& error);

// The list the phone page shows: "file<TAB>title<TAB>caption<TAB>kind" per
// line.
std::string formatListing(const std::vector<Card>& cards);

// The smallest QR version whose byte mode holds `bytes` at error correction
// level M (`medium`) or L, or 0 when even version 40 cannot. Byte mode is the
// roomiest assumption: a payload of digits fits the same version or a smaller
// one, so the version this names always takes it.
int qrVersionFor(size_t bytes, bool medium);

// --- The sleep screen ----------------------------------------------------

// Which card the sleep screen shows, and the sleep settings it replaced so
// taking it off puts back what the person had. Three lines, as Notes keeps its
// own: the card's file name, the old sleep screen mode, the old Quick Resume on
// Timeout (0 off, 1 on). A number that is missing or unreadable is -1, nothing
// to put back. Only a card's own file name parses, so the choice can never
// name a path outside /cards.
struct AsleepChoice {
  std::string file;
  int previousMode = -1;
  int previousQuickResume = -1;
};
std::string formatAsleep(const AsleepChoice& choice);
bool parseAsleep(const std::string& text, AsleepChoice& out);

}  // namespace wallet
