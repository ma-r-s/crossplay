#include "TicketsCore.h"

#include <Utf8.h>

#include <cstdint>

namespace tickets {
namespace {

struct Parser {
  const char* p;
  const char* end;

  bool atEnd() const { return p >= end; }
  char peek() const { return atEnd() ? '\0' : *p; }

  void ws() {
    while (!atEnd() && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }

  bool expect(const char c) {
    ws();
    if (peek() != c) return false;
    ++p;
    return true;
  }

  // Four hex digits after \u, or nothing.
  bool hex4(uint32_t& out) {
    out = 0;
    for (int i = 0; i < 4; ++i) {
      if (atEnd()) return false;
      const char c = *p++;
      out <<= 4;
      if (c >= '0' && c <= '9') {
        out |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        out |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        out |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  static void utf8Append(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  // Parses a string literal, unescaped into `out`. The opening quote is next.
  bool string(std::string& out) {
    if (peek() != '"') return false;
    ++p;
    out.clear();
    while (!atEnd()) {
      const char c = *p++;
      if (c == '"') return true;
      if (c == '\\') {
        if (atEnd()) return false;
        switch (*p++) {
          case '"':
            out.push_back('"');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '/':
            out.push_back('/');
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u': {
            uint32_t cp;
            if (!hex4(cp)) return false;
            // A high surrogate must be followed by its low half; a lone one is
            // malformed rather than half a character.
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (atEnd() || *p != '\\') return false;
              ++p;
              if (atEnd() || *p != 'u') return false;
              ++p;
              uint32_t low;
              if (!hex4(low)) return false;
              if (low < 0xDC00 || low > 0xDFFF) return false;
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              return false;
            }
            utf8Append(out, cp);
            break;
          }
          default:
            return false;
        }
        continue;
      }
      // Raw control characters are not legal inside a JSON string.
      if (static_cast<unsigned char>(c) < 0x20) return false;
      out.push_back(c);
    }
    return false;  // unterminated
  }

  bool literal(const char* word) {
    while (*word) {
      if (atEnd() || *p != *word) return false;
      ++p;
      ++word;
    }
    return true;
  }

  bool number() {
    if (peek() == '-') ++p;
    if (atEnd()) return false;
    if (peek() == '0') {
      ++p;
    } else if (peek() >= '1' && peek() <= '9') {
      while (!atEnd() && peek() >= '0' && peek() <= '9') ++p;
    } else {
      return false;
    }
    if (peek() == '.') {
      ++p;
      if (atEnd() || peek() < '0' || peek() > '9') return false;
      while (!atEnd() && peek() >= '0' && peek() <= '9') ++p;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++p;
      if (peek() == '+' || peek() == '-') ++p;
      if (atEnd() || peek() < '0' || peek() > '9') return false;
      while (!atEnd() && peek() >= '0' && peek() <= '9') ++p;
    }
    return true;
  }

  // Any value, consumed and discarded. Depth-capped: the files are capped on
  // the card, but a hostile one is still a file.
  bool skipValue(const int depth = 0) {
    if (depth > 16) return false;
    ws();
    const char c = peek();
    if (c == '"') {
      std::string discarded;
      return string(discarded);
    }
    if (c == 't') return literal("true");
    if (c == 'f') return literal("false");
    if (c == 'n') return literal("null");
    if (c == '{') {
      ++p;
      if (expect('}')) return true;
      for (;;) {
        std::string key;
        ws();
        if (!string(key)) return false;
        if (!expect(':')) return false;
        if (!skipValue(depth + 1)) return false;
        ws();
        if (peek() == ',') {
          ++p;
          continue;
        }
        return expect('}');
      }
    }
    if (c == '[') {
      ++p;
      if (expect(']')) return true;
      for (;;) {
        if (!skipValue(depth + 1)) return false;
        ws();
        if (peek() == ',') {
          ++p;
          continue;
        }
        return expect(']');
      }
    }
    return number();
  }
};

}  // namespace

ParseError parseTicket(const std::string& json, Ticket& out) {
  if (json.size() > kMaxTicketBytes) return ParseError::TooLarge;
  Parser parser{json.data(), json.data() + json.size()};
  Ticket ticket;
  bool haveName = false;
  bool haveQr = false;

  if (!parser.expect('{')) return ParseError::NotJson;
  if (!parser.expect('}')) {
    for (;;) {
      std::string key;
      parser.ws();
      if (!parser.string(key)) return ParseError::NotJson;
      if (!parser.expect(':')) return ParseError::NotJson;
      parser.ws();
      const bool known = key == "name" || key == "subtitle" || key == "qr";
      if (known) {
        // A known key holding a non-string is an error, not something to skip:
        // guessing at a payload is how the wrong code ends up on the glass.
        std::string value;
        if (!parser.string(value)) return ParseError::NotJson;
        if (key == "name") {
          ticket.name = std::move(value);
          haveName = true;
        } else if (key == "subtitle") {
          ticket.subtitle = std::move(value);
        } else {
          ticket.qr = std::move(value);
          haveQr = true;
        }
      } else if (!parser.skipValue()) {
        return ParseError::NotJson;
      }
      parser.ws();
      if (parser.peek() == ',') {
        ++parser.p;
        continue;
      }
      if (!parser.expect('}')) return ParseError::NotJson;
      break;
    }
  }
  parser.ws();
  if (!parser.atEnd()) return ParseError::NotJson;

  if (!haveName || ticket.name.empty()) return ParseError::NameMissing;
  if (!haveQr || ticket.qr.empty()) return ParseError::QrMissing;
  if (ticket.qr.size() > kMaxQrPayload) return ParseError::QrTooLong;

  ticket.name = utf8FoldTypography(ticket.name);
  ticket.subtitle = utf8FoldTypography(ticket.subtitle);
  out = std::move(ticket);
  return ParseError::None;
}

std::string fileStemFor(const std::string& name) {
  constexpr size_t kStemMax = 48;
  size_t start = 0;
  while (start < name.size() && (name[start] == ' ' || name[start] == '\t')) start++;
  size_t stop = name.size();
  while (stop > start && (name[stop - 1] == ' ' || name[stop - 1] == '\t')) stop--;

  std::string out;
  for (size_t i = start; i < stop && out.size() < kStemMax; i++) {
    const char c = name[i];
    // A separator would name a file in another directory, and the rest are
    // either illegal on FAT or match more than one file.
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' ||
        c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      continue;
    }
    out.push_back(c);
  }
  // A name that is only dots names this directory or its parent.
  if (out == "." || out == "..") out.clear();
  return out;
}

const char* nameOf(const ParseError error) {
  switch (error) {
    case ParseError::None:
      return "none";
    case ParseError::NotJson:
      return "not json";
    case ParseError::NameMissing:
      return "no name";
    case ParseError::QrMissing:
      return "no qr";
    case ParseError::QrTooLong:
      return "qr too long";
    case ParseError::TooLarge:
      return "ticket too large";
  }
  return "unknown";
}

}  // namespace tickets
