#pragma once

// The Wikipedia pack, read. Freestanding C++17 over a ByteSource so the same
// code runs on the device (HalFile) and in host-tests/wikipedia (a std::ifstream
// over a pack the Python writer produced). The format is
// docs/apps/wikipedia-pack-format.md; nothing here knows about the panel, the
// card or zstd. Decoding a block is the device layer's job (WikipediaPack); this
// file parses what comes out of it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wikipedia {

// Random access into one file. Every read seeks; the index and the directory
// are read in small pieces from places far apart.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual bool read(uint32_t offset, void* dst, uint32_t length) = 0;
  virtual uint32_t size() const = 0;
};

// The fold, as a convenience over WikipediaFold.h's buffer form.
std::string fold(const std::string& title);

// Locators: block index in the high 20 bits, slot in the low 12.
inline uint32_t makeLocator(const uint32_t block, const uint32_t slot) { return (block << 12) | (slot & 0xFFF); }
inline uint32_t locatorBlock(const uint32_t locator) { return locator >> 12; }
inline uint32_t locatorSlot(const uint32_t locator) { return locator & 0xFFF; }

// ---------------------------------------------------------------- manifest

struct ManifestFile {
  std::string file;
  uint32_t bytes = 0;
  int tier = 0;
  uint32_t firstBlock = 0;  // shards only
  uint32_t blocks = 0;      // shards only
};

struct ManifestTier {
  std::string name;
  int shards = 0;
  uint32_t articles = 0;
  uint64_t bytes = 0;
};

struct Manifest {
  int format = 0;
  std::string pack;
  std::string snapshot;
  uint32_t articles = 0;
  uint32_t entries = 0;
  uint32_t blocks = 0;
  ManifestFile dict;
  ManifestFile blocksdir;
  std::vector<ManifestFile> titles;
  std::vector<ManifestFile> shards;
  std::vector<ManifestTier> tiers;
  bool valid = false;
};

// A small, tolerant reader for the machine-written manifest: enough JSON to
// find the fields above. Anything it cannot find leaves the default. Returns
// false when the text is not a format-1 manifest at all.
bool parseManifest(const char* json, size_t len, Manifest& out);

// ------------------------------------------------------------- blocks.dir

struct BlockRecord {
  uint16_t shard = 0;
  uint16_t slots = 0;
  uint32_t offset = 0;
  uint32_t csize = 0;
  uint32_t usize = 0;
};

// The directory is small enough to hold whole (16 bytes a block), so the
// caller reads the file into memory once and hands it here.
class BlocksDir {
 public:
  bool load(std::vector<uint8_t> bytes);
  uint32_t count() const { return count_; }
  bool record(uint32_t block, BlockRecord& out) const;

 private:
  std::vector<uint8_t> bytes_;
  uint32_t count_ = 0;
};

// ------------------------------------------------------------ titles.N.idx

struct IndexEntry {
  std::string title;  // the display title
  uint32_t locator = 0;
  bool redirect = false;
};

class TitleIndex {
 public:
  static constexpr uint32_t kHeaderBytes = 32;

  // Parses the header and loads the sampler; the index blocks stay in the
  // source and are read one at a time. The source must outlive the index.
  bool open(ByteSource& source);
  bool isOpen() const { return source_ != nullptr; }
  uint32_t entries() const { return entries_; }
  uint32_t blockCount() const { return blocks_; }

  // Exact match on the folded title. A redirect resolves to the target's
  // locator; `out.title` is the entry's own display title either way.
  bool find(const std::string& title, IndexEntry& out) const;

  // Entries whose folded title starts with the folded query, in index order,
  // at most `max`. Returns how many were appended to `out`.
  int prefix(const std::string& query, int max, std::vector<IndexEntry>& out) const;

  // The entry at (block, ordinal), for tests and for RANDOM.
  bool entryAt(uint32_t block, uint32_t ordinal, IndexEntry& out) const;

 private:
  // The index block whose first key is the greatest <= key (0 when key sorts
  // before everything).
  uint32_t blockFor(const std::string& foldedKey) const;
  bool readBlock(uint32_t block, std::vector<uint8_t>& buf) const;
  // Walks one block's entries in order; fn(entry, foldedTitle) returns false to stop.
  template <typename Fn>
  bool scan(uint32_t block, Fn&& fn) const;

  ByteSource* source_ = nullptr;
  uint32_t entries_ = 0;
  uint32_t blocks_ = 0;
  uint32_t blockBytes_ = 4096;
  std::vector<std::string> sampler_;  // folded first key of every block
};

// ------------------------------------------------------- a decoded block

struct ArticleView {
  std::string title;
  std::vector<std::string> headings;
  const uint8_t* xhtml = nullptr;
  uint32_t xhtmlLen = 0;
};

// The slot count of a decoded block, or 0 when the bytes are not a block.
uint16_t blockSlots(const uint8_t* data, size_t len);
// Parses article `slot` out of a decoded block. The view points into `data`.
bool blockArticle(const uint8_t* data, size_t len, uint32_t slot, ArticleView& out);

// ----------------------------------------------------------------- state

struct RecentEntry {
  std::string title;
  uint32_t locator = 0;
};

struct State {
  static constexpr size_t kMaxRecent = 10;
  RecentEntry current;  // "continue": empty title when none
  int currentPage = 0;
  std::vector<RecentEntry> recent;

  // Moves `entry` to the front of recent, dropping duplicates and the tail.
  void touch(const RecentEntry& entry);
  std::string toJson() const;
  bool fromJson(const char* json, size_t len);
};

}  // namespace wikipedia
