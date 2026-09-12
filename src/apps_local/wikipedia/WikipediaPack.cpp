#include "WikipediaPack.h"

#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>
#include <zstd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wikipedia {

namespace {

constexpr const char* kTag = "WIKI";
// Compressed frames are read through this DRAM buffer in pieces: the SDMMC
// driver degrades to single-sector commands when handed a PSRAM destination.
constexpr size_t kBounceBytes = 4096;
uint8_t g_bounce[kBounceBytes];

bool readWhole(const char* path, std::vector<uint8_t>& out, const size_t maxBytes) {
  HalFile file;
  if (!Storage.openFileForRead(kTag, path, file)) return false;
  const size_t size = file.size();
  if (size == 0 || size > maxBytes) {
    LOG_ERR(kTag, "%s: %u bytes, refusing", path, static_cast<unsigned>(size));
    return false;
  }
  out.resize(size);
  size_t done = 0;
  while (done < size) {
    const size_t want = std::min(kBounceBytes, size - done);
    if (file.read(g_bounce, want) != static_cast<int>(want)) return false;
    memcpy(out.data() + done, g_bounce, want);
    done += want;
  }
  return true;
}

}  // namespace

// ------------------------------------------------------------ FileSource

bool FileSource::open(const char* path) {
  if (!Storage.openFileForRead(kTag, path, file_)) return false;
  size_ = static_cast<uint32_t>(file_.size());
  return true;
}

bool FileSource::read(const uint32_t offset, void* dst, const uint32_t length) {
  if (length == 0) return true;
  if (offset > size_ || offset + length > size_) return false;
  if (!file_.seekSet(offset)) return false;
  return file_.read(dst, length) == static_cast<int>(length);
}

// ------------------------------------------------------------------ Pack

Pack::~Pack() { close(); }

void Pack::close() {
  warm_ = false;
  if (dctx_) {
    ZSTD_freeDCtx(static_cast<ZSTD_DCtx*>(dctx_));
    dctx_ = nullptr;
  }
  if (ddict_) {
    ZSTD_freeDDict(static_cast<ZSTD_DDict*>(ddict_));
    ddict_ = nullptr;
  }
  indexes_.clear();
  indexSources_.clear();
  dict_.clear();
  dict_.shrink_to_fit();
  dir_ = BlocksDir{};
  shardPresent_.clear();
  shardsPresent_ = 0;
  manifest_ = Manifest{};
  open_ = false;
}

bool Pack::open() {
  close();
  if (!loadManifest()) return false;
  if (!loadIndexes()) {
    close();
    return false;
  }
  checkShards();
  open_ = true;
  LOG_INF(kTag, "pack %s %s: %u articles, %u blocks, %d/%d shards on the card", manifest_.pack.c_str(),
          manifest_.snapshot.c_str(), static_cast<unsigned>(manifest_.articles),
          static_cast<unsigned>(manifest_.blocks), shardsPresent_, shardsTotal());
  return true;
}

bool Pack::warm() {
  if (warm_) return true;
  if (!open_) return false;
  if (!loadDict() || !loadDirectory()) return false;
  warm_ = true;
  return true;
}

bool Pack::loadManifest() {
  std::vector<uint8_t> bytes;
  if (!Storage.exists(kManifestPath)) return false;
  if (!readWhole(kManifestPath, bytes, 256 * 1024)) return false;
  if (!parseManifest(reinterpret_cast<const char*>(bytes.data()), bytes.size(), manifest_)) {
    LOG_ERR(kTag, "manifest.json did not parse");
    return false;
  }
  return true;
}

bool Pack::loadDict() {
  const std::string path = std::string(kDir) + "/" + manifest_.dict.file;
  if (!readWhole(path.c_str(), dict_, 4 * 1024 * 1024)) {
    LOG_ERR(kTag, "dictionary missing: %s", path.c_str());
    return false;
  }
  dctx_ = ZSTD_createDCtx();
  // The DDict keeps its own copy of the dictionary (110 KB, lands in PSRAM),
  // so the file bytes are let go once it exists.
  ddict_ = ZSTD_createDDict(dict_.data(), dict_.size());
  dict_.clear();
  dict_.shrink_to_fit();
  if (!dctx_ || !ddict_) {
    LOG_ERR(kTag, "OOM: zstd context");
    return false;
  }
  return true;
}

bool Pack::loadDirectory() {
  const std::string path = std::string(kDir) + "/" + manifest_.blocksdir.file;
  dirSource_ = makeUniqueNoThrow<FileSource>();
  if (!dirSource_) {
    LOG_ERR(kTag, "OOM: blocks.dir source");
    return false;
  }
  if (!dirSource_->open(path.c_str()) || !dir_.open(*dirSource_)) {
    LOG_ERR(kTag, "blocks.dir missing or invalid: %s", path.c_str());
    return false;
  }
  LOG_INF(kTag, "blocks.dir: %u blocks, read from the card", dir_.count());
  return true;
}

bool Pack::loadIndexes() {
  for (const auto& f : manifest_.titles) {
    const std::string path = std::string(kDir) + "/" + f.file;
    if (!Storage.exists(path.c_str())) break;  // later tiers may not have arrived yet
    auto source = makeUniqueNoThrow<FileSource>();
    auto index = makeUniqueNoThrow<TitleIndex>();
    if (!source || !index) {
      LOG_ERR(kTag, "OOM: index");
      return false;
    }
    if (!source->open(path.c_str()) || source->size() != f.bytes || !index->open(*source)) {
      LOG_ERR(kTag, "index unusable: %s", path.c_str());
      break;
    }
    indexSources_.push_back(std::move(source));
    indexes_.push_back(std::move(index));
  }
  if (indexes_.empty()) {
    LOG_ERR(kTag, "no title index on the card");
    return false;
  }
  return true;
}

void Pack::checkShards() {
  shardPresent_.assign(manifest_.shards.size(), false);
  shardsPresent_ = 0;
  for (size_t i = 0; i < manifest_.shards.size(); ++i) {
    const std::string path = std::string(kDir) + "/" + manifest_.shards[i].file;
    HalFile file;
    if (!Storage.exists(path.c_str()) || !Storage.openFileForRead(kTag, path.c_str(), file)) continue;
    if (static_cast<uint32_t>(file.size()) != manifest_.shards[i].bytes) continue;
    shardPresent_[i] = true;
    ++shardsPresent_;
  }
}

int Pack::essentialShards() const {
  if (manifest_.tiers.empty()) return 1;
  return std::max(1, manifest_.tiers.front().shards);
}

bool Pack::onCard(const uint32_t locator) const {
  if (!ensureWarm()) return false;
  BlockRecord rec;
  if (!dir_.record(locatorBlock(locator), rec)) return false;
  return rec.shard < shardPresent_.size() && shardPresent_[rec.shard];
}

bool Pack::find(const std::string& title, IndexEntry& out) const {
  for (const auto& index : indexes_) {
    if (index->find(title, out)) return true;
  }
  return false;
}

int Pack::prefix(const std::string& query, const int max, std::vector<IndexEntry>& out) const {
  std::vector<IndexEntry> merged;
  for (const auto& index : indexes_) {
    index->prefix(query, max, merged);
  }
  std::stable_sort(merged.begin(), merged.end(),
                   [](const IndexEntry& a, const IndexEntry& b) { return fold(a.title) < fold(b.title); });
  if (static_cast<int>(merged.size()) > max) merged.resize(max);
  for (auto& e : merged) out.push_back(std::move(e));
  return static_cast<int>(std::min<size_t>(merged.size(), max));
}

bool Pack::random(IndexEntry& out) {
  if (!ensureWarm()) return false;
  // Blocks of the essentials: everything the first tier's shards hold.
  uint32_t blocks = 0;
  const int shards = std::min(essentialShards(), shardsTotal());
  for (int i = 0; i < shards; ++i) blocks = manifest_.shards[i].firstBlock + manifest_.shards[i].blocks;
  if (blocks == 0) blocks = dir_.count();
  if (blocks == 0) return false;
  // xorshift over a seed that moves every call. The first seed comes from the
  // hardware RNG: seeded from the block count, every session's first RANDOM
  // was the same article (Ecumenism, on the panel and in the simulator alike).
  if (seed_ == 0) seed_ = esp_random() ^ 0x9E3779B9u;
  if (seed_ == 0) seed_ = 0x9E3779B9u ^ blocks;
  for (int attempt = 0; attempt < 8; ++attempt) {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    const uint32_t block = seed_ % blocks;
    BlockRecord rec;
    if (!dir_.record(block, rec) || rec.slots == 0) continue;
    const uint32_t slot = (seed_ >> 8) % rec.slots;
    const uint32_t locator = makeLocator(block, slot);
    if (!onCard(locator)) continue;
    Article article;
    const char* error = nullptr;
    if (!readArticle(locator, article, &error)) continue;
    out.title = article.title;
    out.locator = locator;
    out.redirect = false;
    return true;
  }
  return false;
}

bool Pack::readBlock(const uint32_t block, std::vector<uint8_t>& raw, const char** error) {
  if (!ensureWarm()) {
    *error = "no directory";
    return false;
  }
  BlockRecord rec;
  if (!dir_.record(block, rec)) {
    *error = "no such block";
    return false;
  }
  if (rec.shard >= shardPresent_.size() || !shardPresent_[rec.shard]) {
    *error = "not on the card";
    return false;
  }
  const std::string path = std::string(kDir) + "/" + manifest_.shards[rec.shard].file;
  HalFile file;
  if (!Storage.openFileForRead(kTag, path.c_str(), file) || !file.seekSet(rec.offset)) {
    *error = "shard unreadable";
    return false;
  }
  std::vector<uint8_t> compressed;
  compressed.resize(rec.csize);
  if (compressed.size() != rec.csize) {
    *error = "low memory";
    return false;
  }
  size_t done = 0;
  while (done < rec.csize) {
    const size_t want = std::min(kBounceBytes, static_cast<size_t>(rec.csize - done));
    if (file.read(g_bounce, want) != static_cast<int>(want)) {
      *error = "shard read failed";
      return false;
    }
    memcpy(compressed.data() + done, g_bounce, want);
    done += want;
  }
  raw.resize(rec.usize);
  if (raw.size() != rec.usize) {
    *error = "low memory";
    return false;
  }
  const size_t got = ZSTD_decompress_usingDDict(static_cast<ZSTD_DCtx*>(dctx_), raw.data(), raw.size(),
                                                compressed.data(), compressed.size(), static_cast<ZSTD_DDict*>(ddict_));
  if (ZSTD_isError(got) || got != rec.usize) {
    LOG_ERR(kTag, "block %u: decode failed (%u of %u)", static_cast<unsigned>(block), static_cast<unsigned>(got),
            static_cast<unsigned>(rec.usize));
    *error = "block corrupt";
    return false;
  }
  return true;
}

bool Pack::readArticle(const uint32_t locator, Article& out, const char** error) {
  const char* why = "";
  if (!error) error = &why;
  *error = "";
  if (!open_) {
    *error = "no pack";
    return false;
  }
  std::vector<uint8_t> raw;
  if (!readBlock(locatorBlock(locator), raw, error)) return false;
  ArticleView view;
  if (!blockArticle(raw.data(), raw.size(), locatorSlot(locator), view)) {
    *error = "block corrupt";
    return false;
  }
  out.title = std::move(view.title);
  out.headings = std::move(view.headings);
  out.xhtml.assign(reinterpret_cast<const char*>(view.xhtml), view.xhtmlLen);
  return true;
}

bool Pack::loadState(State& state) const {
  std::vector<uint8_t> bytes;
  if (!Storage.exists(kStatePath) || !readWhole(kStatePath, bytes, 16 * 1024)) return false;
  return state.fromJson(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool Pack::saveState(const State& state) const {
  const std::string json = state.toJson();
  HalFile file;
  if (!Storage.openFileForWrite(kTag, kStatePath, file)) return false;
  return file.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) == json.size();
}

bool Pack::writeInstallJson(const int64_t freeBytes, const char* firmwareVersion, const char* deviceName) const {
  char buf[512];
  char freeField[32];
  if (freeBytes < 0) {
    snprintf(freeField, sizeof(freeField), "null");
  } else {
    snprintf(freeField, sizeof(freeField), "%lld", static_cast<long long>(freeBytes));
  }
  if (open_) {
    snprintf(buf, sizeof(buf),
             "{\"device\":\"%s\",\"firmware\":\"%s\",\"free\":%s,\"pack\":\"%s\",\"snapshot\":\"%s\","
             "\"shardsPresent\":%d,\"shardsTotal\":%d}\n",
             deviceName, firmwareVersion, freeField, manifest_.pack.c_str(), manifest_.snapshot.c_str(), shardsPresent_,
             shardsTotal());
  } else {
    snprintf(buf, sizeof(buf),
             "{\"device\":\"%s\",\"firmware\":\"%s\",\"free\":%s,\"pack\":null,\"snapshot\":null,"
             "\"shardsPresent\":0,\"shardsTotal\":0}\n",
             deviceName, firmwareVersion, freeField);
  }
  if (!Storage.exists(kDir) && !Storage.mkdir(kDir)) return false;
  HalFile file;
  if (!Storage.openFileForWrite(kTag, kInstallPath, file)) return false;
  const size_t len = strlen(buf);
  return file.write(reinterpret_cast<const uint8_t*>(buf), len) == len;
}

}  // namespace wikipedia
