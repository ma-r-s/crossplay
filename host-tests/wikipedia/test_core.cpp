// The freestanding half of the Wikipedia app, checked without a panel.
//
// argv[1], optional: fold_vectors.tsv from the builder (input TAB expected),
// which is the proof the C++ fold and the Python fold are one function.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "WikipediaCore.h"
#include "WikipediaFold.h"

using namespace wikipedia;

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

class MemSource final : public ByteSource {
 public:
  explicit MemSource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (offset + length > bytes_.size()) return false;
    memcpy(dst, bytes_.data() + offset, length);
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }

 private:
  std::vector<uint8_t> bytes_;
};

void putU16(std::vector<uint8_t>& v, const uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}
void putU32(std::vector<uint8_t>& v, const uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}
void putStr(std::vector<uint8_t>& v, const std::string& s) { v.insert(v.end(), s.begin(), s.end()); }

struct Title {
  std::string display;
  uint32_t locator;
  bool redirect;
};

// A titles.N.idx with 4096-byte blocks, built the way the spec says: sorted by
// folded key, front-coded display titles, a sampler of folded first keys.
std::vector<uint8_t> buildIndex(std::vector<Title> titles, const uint32_t blockBytes = 4096) {
  std::sort(titles.begin(), titles.end(), [](const Title& a, const Title& b) {
    const std::string fa = fold(a.display), fb = fold(b.display);
    return fa != fb ? fa < fb : a.display < b.display;
  });
  std::vector<std::vector<uint8_t>> blocks;
  std::vector<std::string> firstKeys;
  std::vector<uint8_t> cur;
  std::string prev;
  uint16_t count = 0;
  auto flush = [&]() {
    std::vector<uint8_t> block;
    putU16(block, count);
    block.insert(block.end(), cur.begin(), cur.end());
    block.resize(blockBytes, 0);
    blocks.push_back(block);
    cur.clear();
    prev.clear();
    count = 0;
  };
  for (const auto& t : titles) {
    size_t shared = 0;
    while (shared < prev.size() && shared < t.display.size() && shared < 255 && prev[shared] == t.display[shared]) {
      ++shared;
    }
    std::vector<uint8_t> entry;
    entry.push_back(static_cast<uint8_t>(shared));
    entry.push_back(static_cast<uint8_t>(t.display.size() - shared));
    entry.push_back(t.redirect ? 1 : 0);
    entry.push_back(0);
    putU32(entry, t.locator);
    entry.insert(entry.end(), t.display.begin() + shared, t.display.end());
    if (2 + cur.size() + entry.size() > blockBytes) flush();
    if (count == 0) {
      firstKeys.push_back(fold(t.display));
      // the first entry of a block shares nothing
      entry[0] = 0;
      entry[1] = static_cast<uint8_t>(t.display.size());
      entry.resize(8);
      entry.insert(entry.end(), t.display.begin(), t.display.end());
    }
    cur.insert(cur.end(), entry.begin(), entry.end());
    prev = t.display;
    ++count;
  }
  if (count > 0) flush();
  std::vector<uint8_t> file;
  putStr(file, "WKTI");
  file.push_back(1);
  file.push_back(0);
  putU16(file, 0);
  putU32(file, static_cast<uint32_t>(titles.size()));
  putU32(file, static_cast<uint32_t>(blocks.size()));
  putU32(file, blockBytes);
  std::vector<uint8_t> sampler;
  putU32(sampler, static_cast<uint32_t>(blocks.size()));
  for (const auto& k : firstKeys) {
    sampler.push_back(static_cast<uint8_t>(k.size()));
    putStr(sampler, k);
  }
  const uint32_t samplerOffset = 32 + static_cast<uint32_t>(blocks.size() * blockBytes);
  putU32(file, samplerOffset);
  putU32(file, static_cast<uint32_t>(sampler.size()));
  putU32(file, 0);
  for (const auto& b : blocks) file.insert(file.end(), b.begin(), b.end());
  file.insert(file.end(), sampler.begin(), sampler.end());
  return file;
}

// The vectors file spells control characters as \t \n \r \f \v and a
// backslash as \\, so a tab can be a test input without breaking the TSV.
std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      out.push_back(s[i]);
      continue;
    }
    const char e = s[++i];
    switch (e) {
      case 't':
        out.push_back('\t');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'v':
        out.push_back('\v');
        break;
      case '\\':
        out.push_back('\\');
        break;
      default:
        out.push_back('\\');
        out.push_back(e);
        break;
    }
  }
  return out;
}

void testFold(const char* vectorsPath) {
  CHECK(fold("New York City") == "new york city");
  CHECK(fold("  New__York  ") == "new york");
  CHECK(fold("Émile Zola") == "emile zola");
  CHECK(fold("Ωmega") == "ωmega");
  CHECK(fold("Йод") == "иод");
  CHECK(fold("東京") == "東京");
  CHECK(fold("ß") == "ß");
  CHECK(fold("") == "");
  // A truncating buffer cuts at a code point boundary and never overruns.
  {
    char out[4];
    size_t len = 99;
    foldTitle("ÉÉ", 4, out, sizeof(out), &len);
    CHECK(len == 2);
    CHECK(out[0] == 'e' && out[1] == 'e');
  }
  if (!vectorsPath) return;
  std::ifstream in(vectorsPath);
  CHECK(in.good());
  std::string line;
  int vectors = 0, wrong = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    const std::string input = unescape(line.substr(0, tab));
    const std::string expected = unescape(line.substr(tab + 1));
    ++vectors;
    if (fold(input) != expected) {
      ++wrong;
      if (wrong <= 5)
        std::printf("  fold(%s) = %s, builder says %s\n", input.c_str(), fold(input).c_str(), expected.c_str());
    }
  }
  CHECK(vectors >= 100);
  CHECK(wrong == 0);
  std::printf("fold vectors: %d checked, %d disagree\n", vectors, wrong);
}

void testManifest() {
  const char* json = R"({"format":1,"pack":"en","snapshot":"2026-05-13","articles":12,"entries":15,"blocks":3,
    "dict":{"file":"dict.zst","bytes":110000,"sha256":"a"},
    "blocksdir":{"file":"blocks.dir","bytes":60,"sha256":"b"},
    "titles":[{"file":"titles.0.idx","tier":0,"entries":10,"bytes":4200,"sha256":"c"},
              {"file":"titles.1.idx","tier":1,"entries":5,"bytes":4200,"sha256":"d"}],
    "shards":[{"file":"shards/000.blk","tier":0,"bytes":5000,"sha256":"e","firstBlock":0,"blocks":2},
              {"file":"shards/001.blk","tier":1,"bytes":3000,"sha256":"f","firstBlock":2,"blocks":1}],
    "tiers":[{"name":"essentials","shards":1,"articles":8,"bytes":119260},
             {"name":"all","shards":2,"articles":12,"bytes":122260}]})";
  Manifest m;
  CHECK(parseManifest(json, strlen(json), m));
  CHECK(m.valid);
  CHECK(m.format == 1);
  CHECK(m.pack == "en");
  CHECK(m.snapshot == "2026-05-13");
  CHECK(m.articles == 12);
  CHECK(m.entries == 15);
  CHECK(m.blocks == 3);
  CHECK(m.dict.file == "dict.zst" && m.dict.bytes == 110000);
  CHECK(m.titles.size() == 2 && m.titles[1].tier == 1 && m.titles[1].file == "titles.1.idx");
  CHECK(m.shards.size() == 2 && m.shards[1].firstBlock == 2 && m.shards[1].blocks == 1);
  CHECK(m.tiers.size() == 2 && m.tiers[0].shards == 1 && m.tiers[1].bytes == 122260);
  // Not a manifest at all, or the wrong format, refuses rather than half-loads.
  Manifest bad;
  CHECK(!parseManifest("{\"format\":2}", 12, bad));
  CHECK(!parseManifest("nonsense", 8, bad));
  CHECK(!parseManifest("{\"format\":1}", 12, bad));
  // Escapes in strings survive.
  const char* esc =
      R"({"format":1,"pack":"a\"b","snapshot":"é","dict":{"file":"d"},"blocksdir":{"file":"b"},"titles":[{"file":"t"}],"shards":[{"file":"s"}]})";
  Manifest e;
  CHECK(parseManifest(esc, strlen(esc), e));
  CHECK(e.pack == "a\"b");
  CHECK(e.snapshot == "\xC3\xA9");
}

void testBlocksDir() {
  std::vector<uint8_t> bytes;
  putStr(bytes, "WKBD");
  bytes.push_back(1);
  bytes.push_back(0);
  putU16(bytes, 0);
  putU32(bytes, 2);
  putU16(bytes, 0);
  putU16(bytes, 12);
  putU32(bytes, 0);
  putU32(bytes, 20000);
  putU32(bytes, 65000);
  putU16(bytes, 1);
  putU16(bytes, 1);
  putU32(bytes, 20000);
  putU32(bytes, 90000);
  putU32(bytes, 247000);
  BlocksDir dir;
  CHECK(dir.load(bytes));
  CHECK(dir.count() == 2);
  BlockRecord r;
  CHECK(dir.record(1, r));
  CHECK(r.shard == 1 && r.slots == 1 && r.offset == 20000 && r.csize == 90000 && r.usize == 247000);
  CHECK(!dir.record(2, r));
  std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 3);
  BlocksDir bad;
  CHECK(!bad.load(truncated));
  CHECK(makeLocator(5, 7) == (5u << 12 | 7u));
  CHECK(locatorBlock(makeLocator(1048575, 4095)) == 1048575);
  CHECK(locatorSlot(makeLocator(1048575, 4095)) == 4095);
}

void testBlock() {
  // Two articles in one block.
  std::vector<uint8_t> a, b;
  auto article = [](std::vector<uint8_t>& out, const std::string& title, const std::vector<std::string>& heads,
                    const std::string& xhtml) {
    putU16(out, static_cast<uint16_t>(title.size()));
    putStr(out, title);
    putU16(out, static_cast<uint16_t>(heads.size()));
    for (const auto& h : heads) {
      out.push_back(static_cast<uint8_t>(h.size()));
      putStr(out, h);
    }
    putU32(out, static_cast<uint32_t>(xhtml.size()));
    putStr(out, xhtml);
  };
  article(a, "Cat", {"Quick facts", "Etymology"}, "<html><body><h1>Cat</h1><p>A cat.</p></body></html>");
  article(b, "Dog", {}, "<html><body><h1>Dog</h1></body></html>");
  std::vector<uint8_t> block;
  putU16(block, 2);
  putU16(block, 0);
  const uint32_t base = 4 + 3 * 4;
  putU32(block, base);
  putU32(block, base + static_cast<uint32_t>(a.size()));
  putU32(block, base + static_cast<uint32_t>(a.size() + b.size()));
  block.insert(block.end(), a.begin(), a.end());
  block.insert(block.end(), b.begin(), b.end());
  CHECK(blockSlots(block.data(), block.size()) == 2);
  ArticleView v;
  CHECK(blockArticle(block.data(), block.size(), 0, v));
  CHECK(v.title == "Cat");
  CHECK(v.headings.size() == 2 && v.headings[1] == "Etymology");
  CHECK(std::string(reinterpret_cast<const char*>(v.xhtml), v.xhtmlLen).find("A cat.") != std::string::npos);
  CHECK(blockArticle(block.data(), block.size(), 1, v));
  CHECK(v.title == "Dog" && v.headings.empty());
  CHECK(!blockArticle(block.data(), block.size(), 2, v));
  // A sentinel past the end is refused, not read.
  std::vector<uint8_t> torn = block;
  torn[4 + 2 * 4] = 0xFF;
  torn[4 + 2 * 4 + 1] = 0xFF;
  CHECK(!blockArticle(torn.data(), torn.size(), 1, v));
}

void testIndex() {
  std::vector<Title> titles;
  // Enough titles to span several 4 KB blocks, plus the shapes that matter.
  for (int i = 0; i < 900; ++i) {
    titles.push_back(
        {"Article number " + std::to_string(i), makeLocator(static_cast<uint32_t>(i / 12), i % 12), false});
  }
  titles.push_back({"New York City", makeLocator(7000, 1), false});
  titles.push_back({"NYC", makeLocator(7000, 1), true});
  titles.push_back({"Émile Zola", makeLocator(7001, 0), false});
  titles.push_back({"Colour", makeLocator(7002, 0), true});
  titles.push_back({"Color", makeLocator(7002, 0), false});
  titles.push_back({"Zzz last", makeLocator(7003, 0), false});
  MemSource source(buildIndex(titles));
  TitleIndex index;
  CHECK(index.open(source));
  CHECK(index.entries() == titles.size());
  CHECK(index.blockCount() >= 2);

  IndexEntry e;
  CHECK(index.find("new york city", e));
  CHECK(e.title == "New York City" && e.locator == makeLocator(7000, 1) && !e.redirect);
  CHECK(index.find("nyc", e));
  CHECK(e.redirect && e.locator == makeLocator(7000, 1) && e.title == "NYC");
  CHECK(index.find("emile zola", e));
  CHECK(e.title == "Émile Zola");
  CHECK(index.find("ÉMILE_ZOLA", e));
  CHECK(index.find("Article number 899", e));
  CHECK(e.locator == makeLocator(899 / 12, 899 % 12));
  CHECK(index.find("Zzz last", e));
  CHECK(index.find("article number 0", e));
  CHECK(!index.find("article number 900", e));
  CHECK(!index.find("", e));
  CHECK(!index.find("aaaa", e));

  std::vector<IndexEntry> out;
  CHECK(index.prefix("col", 8, out) == 2);
  CHECK(out[0].title == "Color" && out[1].title == "Colour");
  out.clear();
  CHECK(index.prefix("article number 1", 8, out) == 8);
  CHECK(out[0].title == "Article number 1");
  // Bytewise on the folded key: 1, 10, 100, 101, ... 105.
  CHECK(out[7].title == "Article number 105");
  out.clear();
  CHECK(index.prefix("Article number 89", 100, out) == 11);
  out.clear();
  CHECK(index.prefix("zz", 8, out) == 1);
  out.clear();
  CHECK(index.prefix("q", 8, out) == 0);
  // A query that straddles a block boundary still finds everything after it.
  out.clear();
  CHECK(index.prefix("article number", 2000, out) == 900);
}

void testState() {
  State s;
  s.touch({"Cat", 5});
  s.touch({"Dog", 6});
  s.touch({"Cat", 5});
  CHECK(s.recent.size() == 2 && s.recent[0].title == "Cat" && s.recent[1].title == "Dog");
  for (int i = 0; i < 20; ++i) s.touch({"T" + std::to_string(i), 100u + i});
  CHECK(s.recent.size() == State::kMaxRecent);
  s.current = {"Say \"hi\"", 9};
  s.currentPage = 3;
  const std::string json = s.toJson();
  State back;
  CHECK(back.fromJson(json.data(), json.size()));
  CHECK(back.current.title == "Say \"hi\"" && back.current.locator == 9 && back.currentPage == 3);
  CHECK(back.recent.size() == s.recent.size() && back.recent[0].title == s.recent[0].title);
  State bad;
  CHECK(!bad.fromJson("{", 1));
}

}  // namespace

int main(const int argc, char** argv) {
  testFold(argc > 1 ? argv[1] : nullptr);
  testManifest();
  testBlocksDir();
  testBlock();
  testIndex();
  testState();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
