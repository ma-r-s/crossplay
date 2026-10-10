// Prints "kind<TAB>payload<TAB>printed<TAB>modules" for tools_local/wallet/bars_roundtrip.sh.
#include <cstdio>

#include "WalletBars.h"
using namespace wallet;
int main() {
  struct {
    CodeKind k;
    const char* p;
  } cases[] = {
      {CodeKind::Code128, "Hello, World 123"},
      {CodeKind::Code128, "1234567890123456"},
      {CodeKind::Code128, "A1234567B"},
      {CodeKind::Code128, "12345"},
      {CodeKind::Code128,
       "ab\x1f"
       "cd"},
      {CodeKind::Code128, "4006381333931"},
      {CodeKind::Code128, "x"},
      {CodeKind::Code128, "99"},
      {CodeKind::Code128, "MEMBER-00042-XY"},
      {CodeKind::Code39, "CODE39 TEST"},
      {CodeKind::Code39, "A-1.$/+%"},
      {CodeKind::Ean13, "400638133393"},
      {CodeKind::Ean13, "5901234123457"},
      {CodeKind::Ean13, "0012345678905"},
      {CodeKind::Ean8, "9638507"},
      {CodeKind::Ean8, "96385074"},
      {CodeKind::UpcA, "03600029145"},
      {CodeKind::UpcA, "012345678905"},
      {CodeKind::UpcE, "0123456"},
      {CodeKind::UpcE, "01234565"},
      {CodeKind::UpcE, "0654321"},
      {CodeKind::UpcE, "1234530"},
      {CodeKind::UpcE, "1234564"},
      {CodeKind::UpcE, "0123403"},
      {CodeKind::Itf, "12345678901231"},
      {CodeKind::Itf, "00012345678905"},
      {CodeKind::Itf, "123456"},
      {CodeKind::Codabar, "A40156B"},
      {CodeKind::Codabar, "31117013206375"},
      {CodeKind::Codabar, "1234-5678:90/.+$"},
  };
  for (auto& c : cases) {
    std::vector<uint8_t> m;
    std::string text;
    if (!encodeBars(c.k, c.p, m, text)) {
      std::printf("%s\t%s\tFAIL\n", kindName(c.k), c.p);
      continue;
    }
    std::printf("%s\t%s\t%s\t", kindName(c.k), c.p, text.c_str());
    for (uint8_t b : m) std::putchar(b ? '1' : '0');
    std::putchar('\n');
  }
}
