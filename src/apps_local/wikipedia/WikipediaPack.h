#pragma once

// The pack on the card: what WikipediaCore reads, opened over HalStorage and
// decoded with zstd. One instance per app session; opening loads the manifest,
// the dictionary, the block directory and every title index present, and
// notes which shards are on the card at their manifest size. Nothing here
// draws.

#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "WikipediaCore.h"

namespace wikipedia {

class FileSource final : public ByteSource {
 public:
  bool open(const char* path);
  bool read(uint32_t offset, void* dst, uint32_t length) override;
  uint32_t size() const override { return size_; }

 private:
  HalFile file_;
  uint32_t size_ = 0;
};

struct Article {
  std::string title;
  std::vector<std::string> headings;
  std::string xhtml;
};

class Pack {
 public:
  static constexpr const char* kDir = "/wikipedia";
  static constexpr const char* kManifestPath = "/wikipedia/manifest.json";
  static constexpr const char* kStatePath = "/wikipedia/state.json";
  static constexpr const char* kInstallPath = "/wikipedia/install.json";

  Pack() = default;
  ~Pack();
  Pack(const Pack&) = delete;
  Pack& operator=(const Pack&) = delete;

  // True when a manifest is on the card and the dictionary, the directory and
  // at least the first title index loaded. A pack with no shard present at
  // all still opens: search works and every article says "not on the card".
  bool open();
  void close();
  bool isOpen() const { return open_; }
  // The dictionary and the block directory (half a megabyte off the card)
  // are not needed until an article is read; open() leaves them for warm(),
  // which the activity calls once the search screen is on the panel. Any
  // reader of a block warms on its own if it comes first.
  bool warm();
  bool isWarm() const { return warm_; }
  const Manifest& manifest() const { return manifest_; }
  int shardsPresent() const { return shardsPresent_; }
  int shardsTotal() const { return static_cast<int>(manifest_.shards.size()); }
  // The essentials tier's shard count, 1 when the manifest names no tiers.
  int essentialShards() const;

  bool onCard(uint32_t locator) const;
  bool find(const std::string& title, IndexEntry& out) const;
  // Merged over every index file, sorted by folded title, at most `max`.
  int prefix(const std::string& query, int max, std::vector<IndexEntry>& out) const;
  // A random article from the essentials (the first tier), title included.
  bool random(IndexEntry& out);
  // Reads and decodes the block, copies the article out. False when the shard
  // is not on the card, the block is corrupt, or memory is short; `error` says
  // which in a word for the log.
  bool readArticle(uint32_t locator, Article& out, const char** error);

  bool loadState(State& state) const;
  bool saveState(const State& state) const;
  // What the install page may read to know the drive and what is already on
  // it. `freeBytes` negative writes null: the count walks the whole FAT, so
  // the install screen no longer asks.
  bool writeInstallJson(int64_t freeBytes, const char* firmwareVersion, const char* deviceName) const;

 private:
  bool loadManifest();
  bool loadDict();
  bool loadDirectory();
  bool loadIndexes();
  void checkShards();
  bool readBlock(uint32_t block, std::vector<uint8_t>& raw, const char** error);
  // warm() from a const reader: the loads change nothing a caller can see.
  bool ensureWarm() const { return warm_ || const_cast<Pack*>(this)->warm(); }

  bool open_ = false;
  bool warm_ = false;
  Manifest manifest_;
  BlocksDir dir_;
  std::vector<uint8_t> dict_;
  std::vector<std::unique_ptr<FileSource>> indexSources_;
  std::vector<std::unique_ptr<TitleIndex>> indexes_;
  std::vector<bool> shardPresent_;
  int shardsPresent_ = 0;
  void* dctx_ = nullptr;   // ZSTD_DCtx*
  void* ddict_ = nullptr;  // ZSTD_DDict*
  uint32_t seed_ = 0;
};

}  // namespace wikipedia
