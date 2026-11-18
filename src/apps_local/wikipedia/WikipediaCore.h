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
  // A pack in a language after the first says what it is: its language
  // code, its own name for itself ("Deutsch") and the heading the builder
  // put over the infobox ("Kurzinfo"). The English packs predate the fields.
  std::string lang;
  std::string name;
  std::string facts;
  std::string snapshot;
  std::string built;  // the build, not the data: two builds of one snapshot differ
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

// What the chooser and the install screen call a pack: its own name when
// the manifest has one, "English" for the first pack, which has none, else
// its code in capitals.
std::string packName(const Manifest& manifest);
// The heading over the infobox's grid, which is not a section anchor.
std::string factsHeading(const Manifest& manifest);
// The language the search keyboard should type: the manifest's, else the
// pack code.
std::string packLanguage(const Manifest& manifest);

// ------------------------------------------------------------ many packs
//
// One pack is the card's /wikipedia/ (where the first, English one has always
// lived, and where it stays); every other is a folder inside it named by its
// code, /wikipedia/de/. A folder holding a manifest is a pack.

// "/wikipedia" for the root pack (dir ""), "/wikipedia/de" for "de".
std::string packPath(const std::string& dir);
// A folder name that may be a pack: a lowercase letter, then lowercase
// letters, digits and '-', at most 15, and never one of the pack's own
// folders ("shards", "overlay").
bool isPackDirName(const char* name);

struct PackSummary {
  std::string dir;  // "" for the root pack
  std::string pack;
  std::string name;
  std::string snapshot;
  uint32_t articles = 0;
  int shardsPresent = 0;
  int shardsTotal = 0;
  std::string continueTitle;  // the article its CONTINUE names; empty when none
};

// install.json: what the page reads before it copies. The top-level pack
// fields describe the root pack, as before, so a page that predates the list
// still says the right thing about English; "packs" lists every pack and
// "want" is the code the device asked about (the pack whose door opened the
// install screen), or null.
std::string installJson(const std::vector<PackSummary>& packs, const char* want, const char* firmware,
                        const char* device);

// "2026-10-10" -> "October 2026"; anything else as it came.
std::string snapshotMonth(const std::string& snapshot);

// The install screen's line about the card, in the reading face: the pack
// whose door opened the screen ("On the card: Deutsch, October 2026."), the
// one that would not open (`failed`, its code), every pack when the chooser
// opened it, or what to do on an empty card. `open` is null unless a pack is.
std::string installLine(const std::vector<PackSummary>& packs, const PackSummary* open, const std::string& failed);

// ------------------------------------------------------------- blocks.dir

struct BlockRecord {
  uint16_t shard = 0;
  uint16_t slots = 0;
  uint32_t offset = 0;
  uint32_t csize = 0;
  uint32_t usize = 0;
};

// 16 bytes a block, and the full English pack has 657,085 of them: 10.5 MB
// in one allocation, which this device does not have once the framebuffer,
// the fonts and zstd have theirs (it aborted on exactly that, 1.12.56). So
// the directory is read from the card a record at a time, like the title
// index, and the last record is kept because a lookup asks for it twice.
class BlocksDir {
 public:
  bool open(ByteSource& source);
  uint32_t count() const { return count_; }
  bool record(uint32_t block, BlockRecord& out) const;

 private:
  static constexpr uint32_t kNoBlock = 0xFFFFFFFFu;
  ByteSource* source_ = nullptr;
  uint32_t count_ = 0;
  mutable uint32_t cachedBlock_ = kNoBlock;
  mutable BlockRecord cached_;
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

// One row per article: an article and its redirects that all match a query
// ("STRABAG", "Strabag", "Strabag Rail") are one article on the panel, shown
// by its own title when that matched, else by the first name that did.
// Order is kept; at most `max` stay.
void oneRowPerArticle(std::vector<IndexEntry>& entries, size_t max);

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
