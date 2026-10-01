#pragma once

// A ticket is one JSON file on the card: `/tickets/<anything>.json`, holding a
// flat object with a name, an optional subtitle, and the QR payload:
//
//   {"name": "Ryanair FR1234", "subtitle": "MXP -> CTA 24 SEP", "qr": "..."}
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card -- so that
// host-tests/tickets can drive every rule on a laptop. The device half
// (TicketsLibrary) does nothing but read the bytes these functions are handed.
//
// The parser is deliberately NOT a general JSON library. The format is ours and
// flat, so a small strict reader beats pulling ArduinoJson into a layer the
// host suites cannot compile (see host-tests/instapaper/run.sh for why the
// device JSON library is kept out of freestanding code):
//
//   * the top level must be a single object, with nothing after its `}`;
//   * `name`, `subtitle` and `qr` must be strings when present; any other type
//     for those three is an error, not a coercion;
//   * unknown keys are skipped, whatever they hold (strings, numbers,
//     literals, nested objects or arrays), so the format can grow;
//   * strings carry the JSON escapes, `\uXXXX` included (surrogate pairs are
//     combined); anything else is malformed.
//
// A ticket is valid when `name` and `qr` are both present and non-empty, and
// the payload fits what the QR drawer can encode. Everything else is one of
// the errors below, and the library skips the file with a log line.

#include <cstdint>
#include <string>

namespace tickets {

// QrUtils truncates payloads past this (version 40, ECC_LOW, byte mode). A
// ticket that long is rejected here instead, so QR generation on the device
// cannot fail silently for a ticket the list offered.
constexpr size_t kMaxQrPayload = 2953;
constexpr size_t kMaxTicketBytes = 8 * 1024;

struct Ticket {
  std::string name;
  std::string subtitle;  // empty when absent
  std::string qr;
  std::string fileName;  // exact SD entry name, set by Library::scan
};

enum class ParseError : uint8_t {
  None,
  NotJson,      // malformed JSON, trailing garbage, or a non-string where
                // name/subtitle/qr belong
  NameMissing,  // no `name`, an empty one, or one that is not a string
  QrMissing,    // no `qr`, an empty one, or one that is not a string
  QrTooLong,    // past kMaxQrPayload
  TooLarge,     // past kMaxTicketBytes
};

// Fills `out` and returns ParseError::None on success. On failure `out` is
// untouched. Name and subtitle are run through utf8FoldTypography: they are
// drawn in Toybox's ASCII-only cuts, and folding at the door means nothing
// downstream has to remember. The QR payload is data and is kept byte-exact.
ParseError parseTicket(const std::string& json, Ticket& out);

// The ticket's name as a FAT-safe file stem, for the one writer that names a
// file from content: the browser upload. No separators, no wildcards, no
// surrounding whitespace (invisible in a list), capped well under the
// directory's name cap. Empty when nothing usable survives, which the caller
// turns into a fallback name.
std::string fileStemFor(const std::string& name);

// For log lines: a stable word per error, never shown to the user.
const char* nameOf(ParseError error);

}  // namespace tickets
