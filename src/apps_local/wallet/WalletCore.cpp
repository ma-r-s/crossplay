#include "WalletCore.h"

#include <algorithm>
#include <cstdio>

namespace wallet {

namespace {

// Byte-mode capacity per version, ISO/IEC 18004 table 7.
constexpr uint16_t kCapacityL[40] = {17,   32,   53,   78,   106,  134,  154,  192,  230,  271,  321,  367,  425,  458,
                                     520,  586,  644,  718,  792,  858,  929,  1003, 1091, 1171, 1273, 1367, 1465, 1528,
                                     1628, 1732, 1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953};
constexpr uint16_t kCapacityM[40] = {14,   26,   42,   62,   84,   106,  122,  152,  180,  213,  251,  287,  331,  362,
                                     412,  450,  504,  560,  624,  666,  711,  779,  857,  911,  997,  1059, 1125, 1190,
                                     1264, 1370, 1452, 1538, 1628, 1722, 1809, 1911, 1989, 2099, 2213, 2331};

// A cut at or below `max` that does not land inside a UTF-8 sequence.
size_t utf8Cut(const std::string& text, size_t max) {
  if (text.size() <= max) return text.size();
  size_t cut = max;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) cut--;
  return cut;
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The next line of `text` from `at`, without its line end; `at` moves past it.
std::string takeLine(const std::string& text, size_t& at) {
  const size_t end = text.find('\n', at);
  std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
  at = end == std::string::npos ? text.size() : end + 1;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

void dropTrailingLineEnds(std::string& text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
}

}  // namespace

std::string cleanLine(const std::string& text, const size_t max) {
  std::string out;
  out.reserve(text.size() < max ? text.size() : max);
  bool space = false;
  for (const char c : text) {
    const bool blank = static_cast<unsigned char>(c) < 0x20 || c == ' ' || c == 0x7F;
    if (blank) {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  out.resize(utf8Cut(out, max));
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool parseCard(const std::string& text, Card& out) {
  size_t at = 0;
  std::string title = takeLine(text, at);
  out.kind = CodeKind::Qr;
  const size_t tab = title.rfind('\t');
  if (tab != std::string::npos && kindFromName(title.substr(tab + 1), out.kind)) title.resize(tab);
  out.title = cleanLine(title, kMaxTitle);
  out.caption = cleanLine(takeLine(text, at), kMaxCaption);
  out.payload = text.substr(at < text.size() ? at : text.size());
  dropTrailingLineEnds(out.payload);
  if (out.payload.size() > kMaxPayload) out.payload.resize(kMaxPayload);
  return !out.payload.empty();
}

std::string formatCard(const Card& card) {
  std::string out;
  out.reserve(card.title.size() + card.caption.size() + card.payload.size() + 3);
  out += cleanLine(card.title, kMaxTitle);
  if (card.kind != CodeKind::Qr) {
    out += '\t';
    out += kindName(card.kind);
  }
  out += '\n';
  out += cleanLine(card.caption, kMaxCaption);
  out += '\n';
  out += card.payload;
  out += '\n';
  return out;
}

std::string fileNameFor(const int number) {
  char name[16];
  std::snprintf(name, sizeof(name), "%04d.txt", number < 0 ? 0 : (number > 9999 ? 9999 : number));
  return name;
}

int numberOf(const std::string& file) {
  if (file.size() != 8 || file.compare(4, 4, ".txt") != 0) return -1;
  int number = 0;
  for (size_t i = 0; i < 4; i++) {
    if (file[i] < '0' || file[i] > '9') return -1;
    number = number * 10 + (file[i] - '0');
  }
  return number;
}

std::string nextFileName(const std::vector<std::string>& files) {
  int highest = 0;
  for (const std::string& file : files) highest = std::max(highest, numberOf(file));
  return fileNameFor(highest + 1);
}

std::vector<std::string> cardFiles(const std::vector<std::string>& names) {
  std::vector<std::string> out;
  out.reserve(names.size());
  for (const std::string& name : names) {
    if (numberOf(name) >= 0) out.push_back(name);
  }
  // Four digits, so the names sort in number order.
  std::sort(out.begin(), out.end());
  return out;
}

bool parseUpload(const std::string& body, Card& out, std::string& error) {
  size_t at = 0;
  out.title = cleanLine(takeLine(body, at), kMaxTitle);
  out.caption = cleanLine(takeLine(body, at), kMaxCaption);
  const std::string hex = takeLine(body, at);
  const std::string kind = takeLine(body, at);
  out.payload.clear();
  out.kind = CodeKind::Qr;
  if (!kind.empty() && !kindFromName(kind, out.kind)) {
    error = "The reader cannot draw that kind of code yet.";
    return false;
  }
  if (out.title.empty()) {
    error = "Give the card a title.";
    return false;
  }
  if (hex.empty()) {
    error = "There is no code to save.";
    return false;
  }
  if (hex.size() % 2 != 0 || hex.size() / 2 > kMaxPayload) {
    error = hex.size() / 2 > kMaxPayload ? "That code holds too much to draw on the reader."
                                         : "The code did not arrive whole.";
    return false;
  }
  out.payload.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int high = hexValue(hex[i]);
    const int low = hexValue(hex[i + 1]);
    if (high < 0 || low < 0) {
      error = "The code did not arrive whole.";
      out.payload.clear();
      return false;
    }
    out.payload.push_back(static_cast<char>(high * 16 + low));
  }
  // The file keeps the payload after the caption and drops trailing line ends
  // when it is read, so they are dropped here too: what the phone is told was
  // saved is what the reader will draw.
  dropTrailingLineEnds(out.payload);
  if (out.payload.empty()) {
    error = "There is no code to save.";
    return false;
  }
  if (out.kind != CodeKind::Qr) {
    std::vector<uint8_t> modules;
    std::string text;
    if (!encodeBars(out.kind, out.payload, modules, text)) {
      error = std::string("That is not a valid ") + kindLabel(out.kind) + " code.";
      return false;
    }
    if (static_cast<int>(modules.size()) + 2 * kQuietModules > kMaxBarModules) {
      error = "That barcode is too long to draw on the reader.";
      return false;
    }
    // The check digit an EAN or UPC was sent without, so the file holds the
    // number printed under the bars.
    if (out.kind == CodeKind::Ean13 || out.kind == CodeKind::Ean8 || out.kind == CodeKind::UpcA ||
        out.kind == CodeKind::UpcE) {
      out.payload = text;
    }
  }
  return true;
}

std::string formatListing(const std::vector<Card>& cards) {
  std::string out;
  for (const Card& card : cards) {
    out += card.file;
    out += '\t';
    out += cleanLine(card.title, kMaxTitle);
    out += '\t';
    out += cleanLine(card.caption, kMaxCaption);
    out += '\t';
    out += kindName(card.kind);
    out += '\n';
  }
  return out;
}

int qrVersionFor(const size_t bytes, const bool medium) {
  const uint16_t* table = medium ? kCapacityM : kCapacityL;
  for (int v = 0; v < 40; v++) {
    if (bytes <= table[v]) return v + 1;
  }
  return 0;
}

std::string formatAsleep(const AsleepChoice& choice) {
  return choice.file + "\n" + std::to_string(choice.previousMode) + "\n" + std::to_string(choice.previousQuickResume) +
         "\n";
}

namespace {
// Digits up to `max`, else -1: a sign or anything else reads as unknown.
int smallNumber(std::string line, const int max) {
  while (!line.empty() && line.back() == ' ') line.pop_back();
  if (line.empty()) return -1;
  int value = 0;
  for (const char c : line) {
    if (c < '0' || c > '9') return -1;
    value = value * 10 + (c - '0');
    if (value > max) return -1;
  }
  return value;
}
}  // namespace

bool parseAsleep(const std::string& text, AsleepChoice& out) {
  out = AsleepChoice{};
  size_t at = 0;
  std::string file = takeLine(text, at);
  while (!file.empty() && file.back() == ' ') file.pop_back();
  if (numberOf(file) < 0) return false;
  out.file = file;
  out.previousMode = smallNumber(takeLine(text, at), 255);
  out.previousQuickResume = smallNumber(takeLine(text, at), 1);
  return true;
}

}  // namespace wallet
