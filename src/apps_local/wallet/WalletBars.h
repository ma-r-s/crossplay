#pragma once

// The kinds of code a card can hold, and the 1D barcodes drawn from their
// payload.
//
// Freestanding like WalletCore, so host-tests/wallet checks every encoder, and
// the suite round-trips each one through ZXing. A barcode is drawn the way a QR
// code is: the phone reads the picture and sends what it says and which kind it
// is, and the reader draws the bars again from that, at whole pixels per module.

#include <cstdint>
#include <string>
#include <vector>

namespace wallet {

enum class CodeKind : uint8_t { Qr, Code128, Code39, Ean13, Ean8, UpcA, UpcE, Itf, Codabar };

// The name in a card file and on the wire: "qr", "code128", "ean13", ...
const char* kindName(CodeKind kind);
// False for a name this firmware cannot draw.
bool kindFromName(const std::string& name, CodeKind& out);
// What a person reads: "QR", "CODE 128", "EAN-13", ...
const char* kindLabel(CodeKind kind);

// White modules a scanner needs either side of a 1D barcode.
constexpr int kQuietModules = 10;
// The widest barcode, quiet zones included, that still gets two pixels a
// module running down the panel. A phone sending a longer one is told so.
constexpr int kMaxBarModules = 260;

// One entry per module, 1 for a dark bar, without the quiet zones; `text` is
// the line printed under the bars, which for EAN and UPC carries the check
// digit even when the payload left it off. False when the payload cannot be
// that kind of barcode: a letter in an EAN, a wrong check digit, an odd-length
// ITF, a character Code 39 has no bars for.
bool encodeBars(CodeKind kind, const std::string& payload, std::vector<uint8_t>& modules, std::string& text);

}  // namespace wallet
