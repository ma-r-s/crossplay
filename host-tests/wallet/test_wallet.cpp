// The card files, checked without a panel.
//
// What matters most is that a code arrives on the card byte for byte as the
// phone read it -- newlines, high bytes and all -- and that nothing a phone or
// a hand-edited file can send stops the list from opening.

#include <cstdio>
#include <string>
#include <vector>

#include "WalletCore.h"

using namespace wallet;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

std::string hexOf(const std::string& bytes) {
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  for (const char c : bytes) {
    out.push_back(kDigits[(static_cast<unsigned char>(c) >> 4) & 0xF]);
    out.push_back(kDigits[static_cast<unsigned char>(c) & 0xF]);
  }
  return out;
}

void testRoundTrip() {
  Card card;
  card.title = "Contact";
  card.caption = "Scan to save my number";
  card.payload = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Gaurav\r\nEND:VCARD";
  Card back;
  CHECK(parseCard(formatCard(card), back));
  CHECK(back.title == card.title);
  CHECK(back.caption == card.caption);
  // The payload's own line ends survive; only the file's trailing one goes.
  CHECK(back.payload == card.payload);

  // An empty caption is an empty second line, not a missing one.
  card.caption.clear();
  card.payload = "4006381333931";
  CHECK(formatCard(card) == "Contact\n\n4006381333931\n");
  CHECK(parseCard(formatCard(card), back));
  CHECK(back.caption.empty());
  CHECK(back.payload == "4006381333931");

  // Every byte value but the trailing line ends.
  std::string all;
  for (int i = 0; i < 256; i++) all.push_back(static_cast<char>(i));
  card.payload = all;
  CHECK(parseCard(formatCard(card), back));
  CHECK(back.payload == all);
}

void testHandEditedFiles() {
  Card card;
  // Windows line ends, a tab in the title, a trailing newline an editor added.
  CHECK(parseCard("  Lidl\tPlus \r\nMember\r\nhttps://example.com/x\r\n\r\n", card));
  CHECK(card.title == "Lidl Plus");
  CHECK(card.caption == "Member");
  CHECK(card.payload == "https://example.com/x");
  // No payload is not a card.
  CHECK(!parseCard("Title\nCaption\n", card));
  CHECK(!parseCard("Title only", card));
  CHECK(!parseCard("", card));
  // A long title is cut on a character, not inside one.
  std::string title;
  for (int i = 0; i < 30; i++) title += "\xC3\xA9";  // e-acute, two bytes each
  CHECK(parseCard(title + "\n\ncode", card));
  CHECK(card.title.size() <= kMaxTitle);
  CHECK(card.title.size() % 2 == 0);
  // An oversized payload is cut to what the panel can draw.
  CHECK(parseCard("T\n\n" + std::string(kMaxPayload + 50, 'a'), card));
  CHECK(card.payload.size() == kMaxPayload);
}

void testCleanLine() {
  CHECK(cleanLine("  a \t\n b  ", 40) == "a b");
  CHECK(cleanLine("", 40).empty());
  CHECK(cleanLine("abcdef", 3) == "abc");
  CHECK(cleanLine("ab \xE2\x82\xAC", 5) == "ab");  // the euro sign would be split
}

void testFileNames() {
  CHECK(fileNameFor(7) == "0007.txt");
  CHECK(numberOf("0007.txt") == 7);
  CHECK(numberOf("0007.txt.part") == -1);
  CHECK(numberOf("notes.txt") == -1);
  CHECK(numberOf("007.txt") == -1);
  CHECK(nextFileName({}) == "0001.txt");
  CHECK(nextFileName({"0002.txt", "0010.txt", "readme.txt"}) == "0011.txt");
  // A deleted card's number is not handed out again while a later one exists.
  CHECK(nextFileName({"0001.txt", "0003.txt"}) == "0004.txt");
  const std::vector<std::string> files = cardFiles({"0010.txt", "README.md", "0002.txt", "0003.txt.part", "0001.txt"});
  CHECK(files.size() == 3);
  CHECK(files[0] == "0001.txt");
  CHECK(files[1] == "0002.txt");
  CHECK(files[2] == "0010.txt");
}

void testUpload() {
  Card card;
  std::string error;
  const std::string payload = "WIFI:T:WPA;S:home;P:pa\nss;;\xff";
  CHECK(parseUpload("Wi-Fi\nGuest network\n" + hexOf(payload), card, error));
  CHECK(card.title == "Wi-Fi");
  CHECK(card.caption == "Guest network");
  CHECK(card.payload == payload);
  // Upper-case hex too.
  CHECK(parseUpload("A\n\n4142", card, error));
  CHECK(card.payload == "AB");
  CHECK(parseUpload("A\n\n4A4b", card, error));
  CHECK(card.payload == "JK");

  CHECK(!parseUpload("\nCaption\n4142", card, error));
  CHECK(error == "Give the card a title.");
  CHECK(!parseUpload("A\n\n", card, error));
  CHECK(error == "There is no code to save.");
  CHECK(!parseUpload("A\n\n414", card, error));
  CHECK(!parseUpload("A\n\n41zz", card, error));
  CHECK(error == "The code did not arrive whole.");
  CHECK(!parseUpload("A\n\n" + std::string((kMaxPayload + 1) * 2, '4'), card, error));
  CHECK(error == "That code holds too much to draw on the reader.");
  // Only line ends is no code at all, once the file would drop them.
  CHECK(!parseUpload("A\n\n0d0a", card, error));
  // What is accepted is exactly what the file gives back.
  CHECK(parseUpload("A\n\n" + hexOf("x\r\n"), card, error));
  Card back;
  CHECK(parseCard(formatCard(card), back));
  CHECK(back.payload == card.payload);
}

void testListing() {
  std::vector<Card> cards(2);
  cards[0].file = "0001.txt";
  cards[0].title = "Lidl";
  cards[1].file = "0002.txt";
  cards[1].title = "Flight";
  cards[1].caption = "Seat 14C";
  CHECK(formatListing(cards) == "0001.txt\tLidl\t\tqr\n0002.txt\tFlight\tSeat 14C\tqr\n");
}

void testQrVersion() {
  CHECK(qrVersionFor(1, true) == 1);
  CHECK(qrVersionFor(14, true) == 1);
  CHECK(qrVersionFor(15, true) == 2);
  CHECK(qrVersionFor(17, false) == 1);
  CHECK(qrVersionFor(100, true) == 6);
  CHECK(qrVersionFor(100, false) == 5);
  CHECK(qrVersionFor(kMaxPayload, true) == 29);
  CHECK(qrVersionFor(2331, true) == 40);
  CHECK(qrVersionFor(2332, true) == 0);
  CHECK(qrVersionFor(2953, false) == 40);
  CHECK(qrVersionFor(2954, false) == 0);
}

// The sleep screen's choice: a round trip, what an old or damaged file reads
// as, and that it can only ever name a card.
void testAsleep() {
  AsleepChoice choice;
  choice.file = "0007.txt";
  choice.previousMode = 3;
  choice.previousQuickResume = 1;
  AsleepChoice back;
  CHECK(parseAsleep(formatAsleep(choice), back));
  CHECK(back.file == "0007.txt" && back.previousMode == 3 && back.previousQuickResume == 1);

  CHECK(parseAsleep("0002.txt\r\n", back));
  CHECK(back.file == "0002.txt" && back.previousMode == -1 && back.previousQuickResume == -1);
  CHECK(parseAsleep("0002.txt\n-1\n-1\n", back));
  CHECK(back.previousMode == -1 && back.previousQuickResume == -1);
  CHECK(parseAsleep("0002.txt\n999\n7\n", back));
  CHECK(back.previousMode == -1 && back.previousQuickResume == -1);

  CHECK(!parseAsleep("", back));
  CHECK(!parseAsleep("\n3\n0\n", back));
  CHECK(!parseAsleep("../sleep.bmp\n3\n0\n", back));
  CHECK(!parseAsleep("notes.txt\n", back));
}

std::string barsOf(CodeKind kind, const std::string& payload, std::string* text = nullptr) {
  std::vector<uint8_t> modules;
  std::string printed;
  if (!encodeBars(kind, payload, modules, printed)) return "FAIL";
  if (text) *text = printed;
  std::string out;
  for (const uint8_t m : modules) out.push_back(m ? '1' : '0');
  return out;
}

// The encoders, against bars worked out by hand and from the standards. The
// round trip through real decoders lives in tools_local/wallet/bars_roundtrip.sh;
// this is what can be checked with nothing installed.
void testBars() {
  std::string text;
  // EAN-13: 95 modules, guards in place, check digit filled in.
  const std::string ean = barsOf(CodeKind::Ean13, "400638133393", &text);
  CHECK(text == "4006381333931");
  CHECK(ean.size() == 95);
  CHECK(ean.compare(0, 3, "101") == 0 && ean.compare(45, 5, "01010") == 0 && ean.compare(92, 3, "101") == 0);
  // 4 sets the parity LGLLGG...: the first left digit (0) in L is 0001101.
  CHECK(ean.compare(3, 7, "0001101") == 0);
  CHECK(barsOf(CodeKind::Ean13, "4006381333932") == "FAIL");  // wrong check digit
  CHECK(barsOf(CodeKind::Ean13, "40063813339") == "FAIL");    // too short
  CHECK(barsOf(CodeKind::Ean13, "40063813339X") == "FAIL");
  CHECK(barsOf(CodeKind::Ean8, "9638507", &text).size() == 67 && text == "96385074");
  CHECK(barsOf(CodeKind::UpcA, "03600029145", &text).size() == 95 && text == "036000291452");
  // UPC-A is EAN-13 with a leading zero, bar for bar.
  CHECK(barsOf(CodeKind::UpcA, "036000291452") == barsOf(CodeKind::Ean13, "0036000291452"));
  CHECK(barsOf(CodeKind::UpcE, "0123456", &text).size() == 51 && text == "01234565");
  CHECK(barsOf(CodeKind::UpcE, "01234566") == "FAIL");
  CHECK(barsOf(CodeKind::UpcE, "2123456") == "FAIL");  // number system 0 or 1 only

  // Code 128: START B "A" = 104 + 33, check (104 + 33) % 103 = 34, then stop.
  const std::string a = barsOf(CodeKind::Code128, "A");
  CHECK(a ==
        "11010010000"
        "10100011000"
        "10001011000"
        "1100011101011");
  // Digits pair up in set C: 16 digits are 8 symbols, not 16.
  CHECK(barsOf(CodeKind::Code128, "1234567890123456").size() == (1 + 8 + 1) * 11 + 13);
  // A control character switches to set A and back.
  CHECK(barsOf(CodeKind::Code128,
               "ab\x1f"
               "cd") != "FAIL");
  CHECK(barsOf(CodeKind::Code128, "caf\xc3\xa9") == "FAIL");
  CHECK(barsOf(CodeKind::Code128, "") == "FAIL");

  CHECK(barsOf(CodeKind::Code39, "CODE39 TEST") != "FAIL");
  CHECK(barsOf(CodeKind::Code39, "lower") == "FAIL");
  CHECK(barsOf(CodeKind::Itf, "123456") != "FAIL");
  CHECK(barsOf(CodeKind::Itf, "12345") == "FAIL");
  CHECK(barsOf(CodeKind::Codabar, "A40156B", &text) != "FAIL" && text == "40156");
  CHECK(barsOf(CodeKind::Codabar, "40156", &text) == barsOf(CodeKind::Codabar, "A40156A"));
  CHECK(barsOf(CodeKind::Codabar, "4A0156") == "FAIL");
  CHECK(barsOf(CodeKind::Qr, "anything") == "FAIL");

  CodeKind kind;
  CHECK(kindFromName("ean13", kind) && kind == CodeKind::Ean13);
  CHECK(!kindFromName("aztec", kind));
  CHECK(std::string(kindName(CodeKind::UpcE)) == "upce" && std::string(kindLabel(CodeKind::UpcE)) == "UPC-E");
}

// A barcode card's kind rides on its title line, and every card written before
// barcodes still reads as QR.
void testBarcodeCards() {
  Card card;
  card.title = "Lidl Plus";
  card.caption = "Member";
  card.payload = "4006381333931";
  card.kind = CodeKind::Ean13;
  CHECK(formatCard(card) == "Lidl Plus\tean13\nMember\n4006381333931\n");
  Card back;
  CHECK(parseCard(formatCard(card), back));
  CHECK(back.title == "Lidl Plus" && back.kind == CodeKind::Ean13 && back.payload == "4006381333931");
  CHECK(parseCard("Old card\n\nhttps://example.com\n", back) && back.kind == CodeKind::Qr);
  // A tab before something that is not a kind is part of the title.
  CHECK(parseCard("Gym\tpass\n\n123\n", back) && back.kind == CodeKind::Qr && back.title == "Gym pass");

  std::string error;
  CHECK(parseUpload("Lidl\n\n343030363338313333333933\nean13\n", back, error));
  CHECK(back.kind == CodeKind::Ean13 && back.payload == "4006381333931");
  CHECK(!parseUpload("Lidl\n\n3430\nean13\n", back, error));
  CHECK(error == "That is not a valid EAN-13 code.");
  CHECK(!parseUpload("Flight\n\n3430\naztec\n", back, error));
  CHECK(error == "The reader cannot draw that kind of code yet.");
  CHECK(parseUpload("Site\n\n3430\n", back, error) && back.kind == CodeKind::Qr);
}

}  // namespace

int main() {
  testRoundTrip();
  testHandEditedFiles();
  testCleanLine();
  testFileNames();
  testUpload();
  testListing();
  testQrVersion();
  testAsleep();
  testBars();
  testBarcodeCards();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
