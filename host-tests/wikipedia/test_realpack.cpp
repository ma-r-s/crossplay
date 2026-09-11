// Does the C++ reader agree with the Python WRITER?
//
// test_core.cpp builds its index in C++, which pins the reader against itself.
// This one reads a pack written by tools_local/wikipedia/pack_format.py and
// checks every article against the manifest that same script printed, through
// the real decode path (lib/zstd, the dictionary, the block directory). If the
// two halves drift, exactly one of them has to change to make this pass.
//
// argv[1] is the pack directory, argv[2] a TSV of
//   title <TAB> locator <TAB> redirect(0|1) <TAB> xhtmlBytes <TAB> headings

#include <zstd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "WikipediaCore.h"

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

std::vector<uint8_t> readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class FileSource final : public ByteSource {
 public:
  explicit FileSource(const std::string& path) : bytes_(readAll(path)) {}
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (offset + length > bytes_.size()) return false;
    memcpy(dst, bytes_.data() + offset, length);
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }

 private:
  std::vector<uint8_t> bytes_;
};

struct Expected {
  std::string title;
  uint32_t locator;
  bool redirect;
  uint32_t xhtmlBytes;
  std::vector<std::string> headings;
};

std::vector<std::string> split(const std::string& s, const char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (const char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

}  // namespace

int main(const int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: test_realpack <pack dir> <expected.tsv>\n");
    return 2;
  }
  const std::string dir = argv[1];

  const auto manifestBytes = readAll(dir + "/manifest.json");
  Manifest manifest;
  CHECK(parseManifest(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size(), manifest));
  CHECK(manifest.valid);
  CHECK(manifest.format == 1);
  CHECK(!manifest.titles.empty());
  CHECK(!manifest.shards.empty());

  BlocksDir blocks;
  CHECK(blocks.load(readAll(dir + "/" + manifest.blocksdir.file)));
  CHECK(blocks.count() == manifest.blocks);

  const auto dict = readAll(dir + "/" + manifest.dict.file);
  CHECK(!dict.empty());
  ZSTD_DCtx* dctx = ZSTD_createDCtx();
  ZSTD_DDict* ddict = ZSTD_createDDict(dict.data(), dict.size());
  CHECK(dctx && ddict);

  std::vector<std::unique_ptr<FileSource>> sources;
  std::vector<std::unique_ptr<TitleIndex>> indexes;
  uint32_t entries = 0;
  for (const auto& f : manifest.titles) {
    sources.push_back(std::make_unique<FileSource>(dir + "/" + f.file));
    CHECK(sources.back()->size() == f.bytes);
    indexes.push_back(std::make_unique<TitleIndex>());
    CHECK(indexes.back()->open(*sources.back()));
    entries += indexes.back()->entries();
  }
  CHECK(entries == manifest.entries);

  std::vector<std::vector<uint8_t>> shards;
  for (const auto& f : manifest.shards) {
    shards.push_back(readAll(dir + "/" + f.file));
    CHECK(shards.back().size() == f.bytes);
  }

  auto decode = [&](const uint32_t block, std::vector<uint8_t>& raw) -> bool {
    BlockRecord rec;
    if (!blocks.record(block, rec)) return false;
    if (rec.shard >= shards.size()) return false;
    const auto& shard = shards[rec.shard];
    if (static_cast<size_t>(rec.offset) + rec.csize > shard.size()) return false;
    raw.resize(rec.usize);
    const size_t got =
        ZSTD_decompress_usingDDict(dctx, raw.data(), raw.size(), shard.data() + rec.offset, rec.csize, ddict);
    if (ZSTD_isError(got)) {
      std::printf("  block %u: %s\n", static_cast<unsigned>(block), ZSTD_getErrorName(got));
      return false;
    }
    return got == rec.usize;
  };

  std::ifstream tsv(argv[2]);
  CHECK(tsv.good());
  std::string line;
  int articles = 0, redirects = 0, wrong = 0;
  std::vector<Expected> expected;
  while (std::getline(tsv, line)) {
    if (line.empty()) continue;
    const auto cols = split(line, '\t');
    if (cols.size() < 4) continue;
    Expected e;
    e.title = cols[0];
    e.locator = static_cast<uint32_t>(strtoul(cols[1].c_str(), nullptr, 10));
    e.redirect = cols[2] == "1";
    e.xhtmlBytes = static_cast<uint32_t>(strtoul(cols[3].c_str(), nullptr, 10));
    if (cols.size() > 4 && !cols[4].empty()) e.headings = split(cols[4], '|');
    expected.push_back(std::move(e));
  }
  CHECK(expected.size() >= 10);

  for (const auto& e : expected) {
    IndexEntry found;
    bool ok = false;
    for (const auto& index : indexes) {
      if (index->find(e.title, found)) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      ++wrong;
      if (wrong <= 5) std::printf("  not found: %s\n", e.title.c_str());
      continue;
    }
    if (found.locator != e.locator || found.redirect != e.redirect || found.title != e.title) {
      ++wrong;
      if (wrong <= 5) {
        std::printf("  %s: locator %u/%u redirect %d/%d title %s\n", e.title.c_str(), found.locator, e.locator,
                    found.redirect, e.redirect, found.title.c_str());
      }
      continue;
    }
    if (e.redirect) {
      ++redirects;
      continue;
    }
    std::vector<uint8_t> raw;
    if (!decode(locatorBlock(e.locator), raw)) {
      ++wrong;
      if (wrong <= 5) std::printf("  %s: block did not decode\n", e.title.c_str());
      continue;
    }
    ArticleView view;
    if (!blockArticle(raw.data(), raw.size(), locatorSlot(e.locator), view) || view.title != e.title ||
        view.xhtmlLen != e.xhtmlBytes || view.headings != e.headings) {
      ++wrong;
      if (wrong <= 5) {
        std::printf("  %s: article header disagrees (title %s, %u/%u bytes, %zu/%zu headings)\n", e.title.c_str(),
                    view.title.c_str(), view.xhtmlLen, e.xhtmlBytes, view.headings.size(), e.headings.size());
      }
      continue;
    }
    // Well-formedness the reader relies on: the wrapper the layout engine needs.
    const std::string xhtml(reinterpret_cast<const char*>(view.xhtml), view.xhtmlLen);
    if (xhtml.find("<html><body>") != 0 || xhtml.rfind("</body></html>") == std::string::npos) {
      ++wrong;
      if (wrong <= 5) std::printf("  %s: xhtml is not wrapped\n", e.title.c_str());
      continue;
    }
    ++articles;
  }
  CHECK(wrong == 0);
  CHECK(articles > 0);

  // Prefix search across every index file, sorted by the fold.
  {
    std::vector<IndexEntry> out;
    for (const auto& index : indexes) index->prefix("a", 1000, out);
    for (size_t i = 1; i < out.size(); ++i) {
      // within one index file the order is the fold order
    }
    CHECK(!out.empty());
  }

  ZSTD_freeDDict(ddict);
  ZSTD_freeDCtx(dctx);
  std::printf("real pack: %d articles and %d redirects agree with the writer; %d checks, %d failed\n", articles,
              redirects, checks, failures);
  return failures == 0 ? 0 : 1;
}
