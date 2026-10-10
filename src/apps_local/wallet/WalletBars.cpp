#include "WalletBars.h"

#include <cstring>

namespace wallet {

namespace {

struct KindInfo {
  CodeKind kind;
  const char* name;
  const char* label;
};

constexpr KindInfo kKinds[] = {
    {CodeKind::Qr, "qr", "QR"},
    {CodeKind::Code128, "code128", "CODE 128"},
    {CodeKind::Code39, "code39", "CODE 39"},
    {CodeKind::Ean13, "ean13", "EAN-13"},
    {CodeKind::Ean8, "ean8", "EAN-8"},
    {CodeKind::UpcA, "upca", "UPC-A"},
    {CodeKind::UpcE, "upce", "UPC-E"},
    {CodeKind::Itf, "itf", "ITF"},
    {CodeKind::Codabar, "codabar", "CODABAR"},
};

// Bar and space widths, bar first. Values 0-102 are symbols, 103-105 the
// three starts; the stop, which has a seventh element, is kCode128Stop.
constexpr const char* kCode128[106] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212", "221213", "221312",
    "231212", "112232", "122132", "122231", "113222", "123122", "123221", "223211", "221132", "221231", "213212",
    "223112", "312131", "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321", "232121",
    "111323", "131123", "131321", "112313", "132113", "132311", "211313", "231113", "231311", "112133", "112331",
    "132131", "113123", "113321", "133121", "313121", "211331", "231131", "213113", "213311", "213131", "311123",
    "311321", "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224", "111422", "121124",
    "121421", "141122", "141221", "112214", "112412", "122114", "122411", "142112", "142211", "241211", "221114",
    "413111", "241112", "134111", "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311",
    "113141", "114131", "311141", "411131", "211412", "211214", "211232"};
constexpr const char* kCode128Stop = "2331112";
constexpr int kStartA = 103, kStartB = 104, kStartC = 105;
constexpr int kCodeA = 101, kCodeB = 100, kCodeC = 99;

// Space, bar, space, bar: the L code of each digit. R is the same widths bar
// first; G is L reversed.
constexpr uint8_t kUpcL[10][4] = {{3, 2, 1, 1}, {2, 2, 2, 1}, {2, 1, 2, 2}, {1, 4, 1, 1}, {1, 1, 3, 2},
                                  {1, 2, 3, 1}, {1, 1, 1, 4}, {1, 3, 1, 2}, {1, 2, 1, 3}, {3, 1, 1, 2}};
// Which of EAN-13's left six digits use G, by the first digit; bit 5 is the
// leftmost.
constexpr uint8_t kEanParity[10] = {0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A};
// The same for UPC-E's six, by number system and check digit.
constexpr uint8_t kUpcEParity[2][10] = {{0x38, 0x34, 0x32, 0x31, 0x2C, 0x26, 0x23, 0x2A, 0x29, 0x25},
                                        {0x07, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A}};

constexpr const char* kCode39Alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
// Nine elements, bar first; bit 8 is the first, 1 is wide.
constexpr uint16_t kCode39[43] = {0x034, 0x121, 0x061, 0x160, 0x031, 0x130, 0x070, 0x025, 0x124, 0x064, 0x109,
                                  0x049, 0x148, 0x019, 0x118, 0x058, 0x00D, 0x10C, 0x04C, 0x01C, 0x103, 0x043,
                                  0x142, 0x013, 0x112, 0x052, 0x007, 0x106, 0x046, 0x016, 0x181, 0x0C1, 0x1C0,
                                  0x091, 0x190, 0x0D0, 0x085, 0x184, 0x0C4, 0x0A8, 0x0A2, 0x08A, 0x02A};
constexpr uint16_t kCode39Star = 0x094;

// Five elements per digit, 1 wide.
constexpr const char* kItf[10] = {"00110", "10001", "01001", "11000", "00101",
                                  "10100", "01100", "00011", "10010", "01010"};

constexpr const char* kCodabarAlphabet = "0123456789-$:/.+ABCD";
// Seven elements, bar first; bit 6 is the first, 1 is wide.
constexpr uint8_t kCodabar[20] = {0x03, 0x06, 0x09, 0x60, 0x12, 0x42, 0x21, 0x24, 0x30, 0x48,
                                  0x0C, 0x18, 0x45, 0x51, 0x54, 0x15, 0x1A, 0x29, 0x0B, 0x0E};

// Wide elements are three modules: the top of the range the specifications
// allow, so a cheap scanner has the widest margin to tell them apart.
constexpr int kWide = 3;

void run(std::vector<uint8_t>& out, const int width, const bool dark) { out.insert(out.end(), width, dark ? 1 : 0); }

// Widths as digits, alternating, starting dark or light.
void widths(std::vector<uint8_t>& out, const char* digits, bool dark) {
  for (; *digits; digits++, dark = !dark) run(out, *digits - '0', dark);
}

bool allDigits(const std::string& text) {
  if (text.empty()) return false;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

// The GTIN check digit of `digits`, the rightmost weighted 3.
int gtinCheck(const std::string& digits) {
  int sum = 0;
  bool three = true;
  for (size_t i = digits.size(); i-- > 0; three = !three) sum += (digits[i] - '0') * (three ? 3 : 1);
  return (10 - sum % 10) % 10;
}

// `digits` with its check digit: computed when one short of `full`, checked
// when complete. False otherwise.
bool withCheck(const std::string& payload, const size_t full, std::string& digits) {
  if (!allDigits(payload)) return false;
  if (payload.size() == full - 1) {
    digits = payload + static_cast<char>('0' + gtinCheck(payload));
    return true;
  }
  if (payload.size() != full) return false;
  digits = payload;
  return gtinCheck(payload.substr(0, full - 1)) == payload.back() - '0';
}

// One UPC/EAN digit as L (space first), G (L reversed) or R (bar first).
void upcDigit(std::vector<uint8_t>& out, const int d, const char set) {
  const uint8_t* w = kUpcL[d];
  if (set == 'G') {
    for (int i = 3; i >= 0; i--) run(out, w[i], i % 2 == 0);
  } else {
    for (int i = 0; i < 4; i++) run(out, w[i], set == 'R' ? i % 2 == 0 : i % 2 == 1);
  }
}

bool ean13(const std::string& digits, std::vector<uint8_t>& out) {
  widths(out, "111", true);
  const uint8_t parity = kEanParity[digits[0] - '0'];
  for (int i = 1; i <= 6; i++) upcDigit(out, digits[i] - '0', (parity >> (6 - i)) & 1 ? 'G' : 'L');
  widths(out, "11111", false);
  for (int i = 7; i <= 12; i++) upcDigit(out, digits[i] - '0', 'R');
  widths(out, "111", true);
  return true;
}

// UPC-E's eight digits as the UPC-A they stand for, without its check digit.
std::string upcEToA(const std::string& e) {
  const std::string d = e.substr(1, 6);
  std::string a(1, e[0]);
  switch (d[5]) {
    case '0':
    case '1':
    case '2':
      a += d.substr(0, 2) + d[5] + "0000" + d.substr(2, 3);
      break;
    case '3':
      a += d.substr(0, 3) + "00000" + d.substr(3, 2);
      break;
    case '4':
      a += d.substr(0, 4) + "00000" + d[4];
      break;
    default:
      a += d.substr(0, 5) + "0000" + d[5];
      break;
  }
  return a;
}

bool upcE(const std::string& payload, std::vector<uint8_t>& out, std::string& text) {
  if (!allDigits(payload) || (payload.size() != 7 && payload.size() != 8)) return false;
  if (payload[0] != '0' && payload[0] != '1') return false;
  const int check = gtinCheck(upcEToA(payload.substr(0, 7) + "0"));
  if (payload.size() == 8 && payload[7] - '0' != check) return false;
  text = payload.substr(0, 7) + static_cast<char>('0' + check);
  widths(out, "111", true);
  const uint8_t parity = kUpcEParity[text[0] - '0'][check];
  for (int i = 1; i <= 6; i++) upcDigit(out, text[i] - '0', (parity >> (6 - i)) & 1 ? 'G' : 'L');
  widths(out, "111111", false);
  return true;
}

bool code128(const std::string& text, std::vector<uint8_t>& out) {
  if (text.empty() || text.size() > 80) return false;
  for (const char c : text) {
    if (static_cast<unsigned char>(c) > 127) return false;
  }
  auto digitRun = [&](size_t i) {
    size_t n = 0;
    while (i + n < text.size() && text[i + n] >= '0' && text[i + n] <= '9') n++;
    return n;
  };
  auto inA = [](const char c) { return static_cast<unsigned char>(c) < 96; };
  auto inB = [](const char c) { return static_cast<unsigned char>(c) >= 32; };
  auto valueIn = [](const char c, const int set) {
    const int v = static_cast<unsigned char>(c);
    if (set == kCodeA) return v < 32 ? v + 64 : v - 32;
    return v - 32;
  };

  std::vector<int> values;
  values.reserve(text.size() + 8);
  int set;
  const size_t lead = digitRun(0);
  if (lead >= 4 && lead % 2 == 0) {
    set = kCodeC;
    values.push_back(kStartC);
  } else if (lead == 2 && text.size() == 2) {
    set = kCodeC;
    values.push_back(kStartC);
  } else if (inB(text[0])) {
    set = kCodeB;
    values.push_back(kStartB);
  } else {
    set = kCodeA;
    values.push_back(kStartA);
  }
  size_t i = 0;
  while (i < text.size()) {
    const size_t digits = digitRun(i);
    if (set == kCodeC) {
      if (digits >= 2) {
        values.push_back((text[i] - '0') * 10 + (text[i + 1] - '0'));
        i += 2;
        continue;
      }
      set = inB(text[i]) ? kCodeB : kCodeA;
      values.push_back(set);
      continue;
    }
    // Four or more digits are cheaper as pairs; an odd run gives its first
    // digit to the current set so the rest pair up.
    if (digits >= 4 && digits % 2 == 0) {
      set = kCodeC;
      values.push_back(kCodeC);
      continue;
    }
    const char c = text[i];
    if (set == kCodeB && !inB(c)) {
      set = kCodeA;
      values.push_back(kCodeA);
    } else if (set == kCodeA && !inA(c)) {
      set = kCodeB;
      values.push_back(kCodeB);
    }
    values.push_back(valueIn(c, set));
    i++;
  }
  int sum = values[0];
  for (size_t k = 1; k < values.size(); k++) sum += static_cast<int>(k) * values[k];
  values.push_back(sum % 103);
  for (const int v : values) widths(out, kCode128[v], true);
  widths(out, kCode128Stop, true);
  return true;
}

void wideNarrow(std::vector<uint8_t>& out, const uint16_t bits, const int count) {
  for (int e = 0; e < count; e++) run(out, (bits >> (count - 1 - e)) & 1 ? kWide : 1, e % 2 == 0);
}

bool code39(const std::string& text, std::vector<uint8_t>& out) {
  if (text.empty() || text.size() > 40) return false;
  wideNarrow(out, kCode39Star, 9);
  for (const char c : text) {
    const char* at = c != '\0' ? std::strchr(kCode39Alphabet, c) : nullptr;
    if (at == nullptr) return false;
    run(out, 1, false);
    wideNarrow(out, kCode39[at - kCode39Alphabet], 9);
  }
  run(out, 1, false);
  wideNarrow(out, kCode39Star, 9);
  return true;
}

bool itf(const std::string& text, std::vector<uint8_t>& out) {
  if (!allDigits(text) || text.size() % 2 != 0 || text.size() > 40) return false;
  widths(out, "1111", true);
  for (size_t i = 0; i < text.size(); i += 2) {
    const char* bars = kItf[text[i] - '0'];
    const char* spaces = kItf[text[i + 1] - '0'];
    for (int e = 0; e < 5; e++) {
      run(out, bars[e] == '1' ? kWide : 1, true);
      run(out, spaces[e] == '1' ? kWide : 1, false);
    }
  }
  run(out, kWide, true);
  widths(out, "11", false);
  return true;
}

bool codabarStartStop(const char c) { return c >= 'A' && c <= 'D'; }

bool codabar(const std::string& payload, std::vector<uint8_t>& out, std::string& text) {
  if (payload.empty() || payload.size() > 40) return false;
  std::string body = payload;
  for (char& c : body) {
    if (c >= 'a' && c <= 'd') c = static_cast<char>(c - 'a' + 'A');
  }
  // Start and stop are part of what some readers return and not of what
  // others do; a payload without them gets A at both ends.
  if (!(body.size() >= 2 && codabarStartStop(body.front()) && codabarStartStop(body.back()))) body = "A" + body + "A";
  for (size_t i = 0; i < body.size(); i++) {
    const char c = body[i];
    const char* at = c != '\0' ? std::strchr(kCodabarAlphabet, c) : nullptr;
    if (at == nullptr) return false;
    const bool ends = i == 0 || i + 1 == body.size();
    if (codabarStartStop(c) != ends) return false;
    if (i > 0) run(out, 1, false);
    wideNarrow(out, kCodabar[at - kCodabarAlphabet], 7);
  }
  text = body.substr(1, body.size() - 2);
  return true;
}

}  // namespace

const char* kindName(const CodeKind kind) {
  for (const KindInfo& info : kKinds) {
    if (info.kind == kind) return info.name;
  }
  return "qr";
}

bool kindFromName(const std::string& name, CodeKind& out) {
  for (const KindInfo& info : kKinds) {
    if (name == info.name) {
      out = info.kind;
      return true;
    }
  }
  return false;
}

const char* kindLabel(const CodeKind kind) {
  for (const KindInfo& info : kKinds) {
    if (info.kind == kind) return info.label;
  }
  return "QR";
}

bool encodeBars(const CodeKind kind, const std::string& payload, std::vector<uint8_t>& modules, std::string& text) {
  modules.clear();
  text = payload;
  std::string digits;
  switch (kind) {
    case CodeKind::Code128:
      return code128(payload, modules);
    case CodeKind::Code39:
      return code39(payload, modules);
    case CodeKind::Ean13:
      if (!withCheck(payload, 13, digits)) return false;
      text = digits;
      return ean13(digits, modules);
    case CodeKind::UpcA:
      if (!withCheck(payload, 12, digits)) return false;
      text = digits;
      return ean13("0" + digits, modules);
    case CodeKind::Ean8: {
      if (!withCheck(payload, 8, digits)) return false;
      text = digits;
      widths(modules, "111", true);
      for (int i = 0; i < 4; i++) upcDigit(modules, digits[i] - '0', 'L');
      widths(modules, "11111", false);
      for (int i = 4; i < 8; i++) upcDigit(modules, digits[i] - '0', 'R');
      widths(modules, "111", true);
      return true;
    }
    case CodeKind::UpcE:
      return upcE(payload, modules, text);
    case CodeKind::Itf:
      return itf(payload, modules);
    case CodeKind::Codabar:
      return codabar(payload, modules, text);
    case CodeKind::Qr:
      break;
  }
  return false;
}

}  // namespace wallet
