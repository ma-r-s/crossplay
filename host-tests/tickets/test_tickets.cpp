// Tests for the ticket file format. Every case here is a file somebody could
// plausibly drop into /tickets/ -- the failures are silent on the device (a
// ticket that is not in the list, with one log line), so each gets a name here.
#include <cstdio>
#include <string>

#include "TicketsCore.h"

static int checks = 0;
static int failures = 0;

static void check(const bool cond, const char* name) {
  ++checks;
  if (!cond) {
    ++failures;
    std::printf("FAIL %s\n", name);
  }
}

static tickets::Ticket parse(const std::string& json, tickets::ParseError expected) {
  tickets::Ticket ticket;
  const tickets::ParseError err = tickets::parseTicket(json, ticket);
  if (err != expected) {
    std::printf("FAIL expected %s, got %s, for: %.60s\n", tickets::nameOf(expected), tickets::nameOf(err),
                json.c_str());
    ++failures;
  }
  ++checks;
  return ticket;
}

int main() {
  // The shape the format documents.
  {
    const tickets::Ticket t =
        parse(R"({"name":"Ryanair FR1234","subtitle":"MXP -> CTA 24 SEP","qr":"ABC123"})", tickets::ParseError::None);
    check(t.name == "Ryanair FR1234", "name is read");
    check(t.subtitle == "MXP -> CTA 24 SEP", "subtitle is read");
    check(t.qr == "ABC123", "qr is read");
  }

  // Subtitle is optional.
  {
    const tickets::Ticket t = parse(R"({"name":"Cinema","qr":"X"})", tickets::ParseError::None);
    check(t.subtitle.empty(), "subtitle defaults to empty");
  }

  // Whitespace, key order and pretty-printing are the writer's business.
  parse("{\n  \"qr\": \"PAYLOAD\",\n  \"subtitle\": \"row 12\",\n  \"name\": \"Museum\"\n}\n",
        tickets::ParseError::None);

  // Unknown keys are skipped, whatever they hold.
  parse(R"({"name":"A","qr":"Q","validUntil":"2026-09-24","gate":12,"used":false,"meta":{"a":[1,2,{"b":null}]}})",
        tickets::ParseError::None);

  // A qr that is only escaped quotes and a unicode escape.
  {
    const tickets::Ticket t = parse(R"({"name":"A","qr":"\"A\" café"})", tickets::ParseError::None);
    check(t.qr == "\"A\" caf\xC3\xA9", "escapes and é survive into the payload byte-exactly");
  }

  // A surrogate pair combines into one codepoint.
  {
    const tickets::Ticket t = parse(R"({"name":"A","qr":"SMILE 😀"})", tickets::ParseError::None);
    check(t.qr == "SMILE \xF0\x9F\x98\x80", "surrogate pair becomes one UTF-8 sequence");
  }

  // Typography in the NAME is folded to ASCII (the Toybox cuts carry nothing
  // else); the payload is not touched. Hex escapes, not literals: host-tests/nodash
  // keeps em-dashes out of source files, test data included.
  {
    const tickets::Ticket t = parse(
        "{\"name\":\"\xE2\x80\x9C"
        "Boarding\xE2\x80\x9D \xE2\x80\x94 Roma\",\"qr\":\"Q\xE2\x80\x94\"}",
        tickets::ParseError::None);
    const bool folded = t.name == "\"Boarding\" -- Roma" || t.name == "\"Boarding\" - Roma";
    if (!folded) std::printf("  name folded to: %s\n", t.name.c_str());
    check(folded, "name typography is folded");
    check(t.qr == "Q\xE2\x80\x94", "payload keeps its em dash byte-exactly");
  }

  // --- The failures --------------------------------------------------------

  parse("", tickets::ParseError::NotJson);
  parse("null", tickets::ParseError::NotJson);
  parse("[1,2]", tickets::ParseError::NotJson);
  parse("{", tickets::ParseError::NotJson);
  parse(R"({"name":"A","qr":"Q")", tickets::ParseError::NotJson);            // truncated
  parse(R"({"name":"A","qr":"Q"} trailing)", tickets::ParseError::NotJson);  // garbage after the object
  parse(R"({"name":"A","qr":"Q",})", tickets::ParseError::NotJson);          // trailing comma
  parse(R"({"name":"A";"qr":"Q"})", tickets::ParseError::NotJson);           // wrong separator
  parse(R"({"name":"A","qr":"bad\q"})", tickets::ParseError::NotJson);       // unknown escape
  parse(R"({"name":"A","qr":"bad"} unterminated)", tickets::ParseError::NotJson);
  parse("{\"name\":\"A\",\"qr\":\"tab\there\"}", tickets::ParseError::NotJson);  // raw control char
  parse(R"({"name":"A","qr":"\uD800"})", tickets::ParseError::NotJson);          // lone surrogate

  // Known keys with the wrong type are errors, not coercions.
  parse(R"({"name":12,"qr":"Q"})", tickets::ParseError::NotJson);
  parse(R"({"name":"A","qr":["Q"]})", tickets::ParseError::NotJson);

  // Missing or empty required fields.
  parse(R"({"qr":"Q"})", tickets::ParseError::NameMissing);
  parse(R"({"name":"","qr":"Q"})", tickets::ParseError::NameMissing);
  parse(R"({"name":"A"})", tickets::ParseError::QrMissing);
  parse(R"({"name":"A","qr":""})", tickets::ParseError::QrMissing);
  parse(R"({})", tickets::ParseError::NameMissing);

  check(tickets::fileStemFor(std::string("A\0B\t/C", 6)) == "ABC", "file name drops control bytes and separators");

  // The saved JSON must fit the same limit used when scanning the card.
  {
    std::string exact = R"({"name":"A","qr":"Q"})";
    exact.append(tickets::kMaxTicketBytes - exact.size(), ' ');
    parse(exact, tickets::ParseError::None);
    exact.push_back(' ');
    parse(exact, tickets::ParseError::TooLarge);
  }

  // A payload past the QR drawer's capacity is refused here, so generation on
  // the device has no failure left to report.
  {
    std::string big = R"({"name":"A","qr":")";
    big.append(tickets::kMaxQrPayload + 1, 'X');
    big.push_back('"');
    big.push_back('}');
    parse(big, tickets::ParseError::QrTooLong);

    std::string fits = R"({"name":"A","qr":")";
    fits.append(tickets::kMaxQrPayload, 'X');
    fits.push_back('"');
    fits.push_back('}');
    parse(fits, tickets::ParseError::None);
  }

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
