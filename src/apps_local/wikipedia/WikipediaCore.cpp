#include "WikipediaCore.h"

#include <algorithm>
#include <cstring>

#include "WikipediaFold.h"

namespace wikipedia {

std::string fold(const std::string& title) {
  std::string out(title.size(), '\0');
  size_t outLen = 0;
  foldTitle(title.data(), title.size(), out.data(), out.size(), &outLen);
  out.resize(outLen);
  return out;
}

// ----------------------------------------------------------------- JSON
//
// Just enough JSON for two machine-written files: the manifest and the state.
// A value tree over std::string and std::vector; the inputs are a few KB.

namespace {

struct JsonValue {
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<JsonValue> items;                           // Array
  std::vector<std::pair<std::string, JsonValue>> fields;  // Object

  const JsonValue* get(const char* key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& f : fields) {
      if (f.first == key) return &f.second;
    }
    return nullptr;
  }
  uint64_t asU64() const { return type == Type::Number && number > 0 ? static_cast<uint64_t>(number) : 0; }
  int asInt() const { return type == Type::Number ? static_cast<int>(number) : 0; }
  const std::string& asStr() const {
    static const std::string kEmpty;
    return type == Type::String ? str : kEmpty;
  }
};

class JsonReader {
 public:
  JsonReader(const char* text, const size_t len) : text_(text), len_(len) {}

  bool parse(JsonValue& out) {
    skipSpace();
    if (!value(out, 0)) return false;
    skipSpace();
    return pos_ == len_;
  }

 private:
  static constexpr int kMaxDepth = 16;

  void skipSpace() {
    while (pos_ < len_ && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
      ++pos_;
    }
  }
  bool consume(const char c) {
    if (pos_ < len_ && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool literal(const char* word) {
    const size_t n = strlen(word);
    if (pos_ + n <= len_ && memcmp(text_ + pos_, word, n) == 0) {
      pos_ += n;
      return true;
    }
    return false;
  }
  static void appendUtf8(std::string& s, const uint32_t cp) {
    if (cp < 0x80) {
      s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  bool string(std::string& out) {
    if (!consume('"')) return false;
    while (pos_ < len_) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= len_) return false;
      const char e = text_[pos_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out.push_back(e);
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 'b':
        case 'f':
          break;
        case 'u': {
          if (pos_ + 4 > len_) return false;
          uint32_t cp = 0;
          for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') {
              cp |= static_cast<uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
              cp |= static_cast<uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
              cp |= static_cast<uint32_t>(h - 'A' + 10);
            } else {
              return false;
            }
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }
  bool number(double& out) {
    const size_t start = pos_;
    if (consume('-')) {
    }
    while (pos_ < len_ && ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' || text_[pos_] == 'e' ||
                           text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) {
      ++pos_;
    }
    if (pos_ == start) return false;
    std::string s(text_ + start, pos_ - start);
    out = strtod(s.c_str(), nullptr);
    return true;
  }
  bool value(JsonValue& out, const int depth) {
    if (depth > kMaxDepth) return false;
    skipSpace();
    if (pos_ >= len_) return false;
    const char c = text_[pos_];
    if (c == '{') {
      ++pos_;
      out.type = JsonValue::Type::Object;
      skipSpace();
      if (consume('}')) return true;
      while (true) {
        skipSpace();
        std::string key;
        if (!string(key)) return false;
        skipSpace();
        if (!consume(':')) return false;
        JsonValue v;
        if (!value(v, depth + 1)) return false;
        out.fields.emplace_back(std::move(key), std::move(v));
        skipSpace();
        if (consume(',')) continue;
        return consume('}');
      }
    }
    if (c == '[') {
      ++pos_;
      out.type = JsonValue::Type::Array;
      skipSpace();
      if (consume(']')) return true;
      while (true) {
        JsonValue v;
        if (!value(v, depth + 1)) return false;
        out.items.push_back(std::move(v));
        skipSpace();
        if (consume(',')) continue;
        return consume(']');
      }
    }
    if (c == '"') {
      out.type = JsonValue::Type::String;
      return string(out.str);
    }
    if (literal("true")) {
      out.type = JsonValue::Type::Bool;
      out.boolean = true;
      return true;
    }
    if (literal("false")) {
      out.type = JsonValue::Type::Bool;
      return true;
    }
    if (literal("null")) return true;
    out.type = JsonValue::Type::Number;
    return number(out.number);
  }

  const char* text_;
  size_t len_;
  size_t pos_ = 0;
};

void readFile(const JsonValue* v, ManifestFile& out) {
  if (!v || v->type != JsonValue::Type::Object) return;
  if (const auto* f = v->get("file")) out.file = f->asStr();
  if (const auto* b = v->get("bytes")) out.bytes = static_cast<uint32_t>(b->asU64());
  if (const auto* t = v->get("tier")) out.tier = t->asInt();
  if (const auto* fb = v->get("firstBlock")) out.firstBlock = static_cast<uint32_t>(fb->asU64());
  if (const auto* bl = v->get("blocks")) out.blocks = static_cast<uint32_t>(bl->asU64());
}

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void jsonEscape(std::string& out, const std::string& s) {
  for (const char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += ' ';
        } else {
          out.push_back(c);
        }
    }
  }
}

}  // namespace

bool parseManifest(const char* json, const size_t len, Manifest& out) {
  out = Manifest{};
  JsonValue root;
  JsonReader reader(json, len);
  if (!reader.parse(root) || root.type != JsonValue::Type::Object) return false;
  if (const auto* f = root.get("format")) out.format = f->asInt();
  if (out.format != 1) return false;
  if (const auto* v = root.get("pack")) out.pack = v->asStr();
  if (const auto* v = root.get("snapshot")) out.snapshot = v->asStr();
  if (const auto* v = root.get("built")) out.built = v->asStr();
  if (const auto* v = root.get("articles")) out.articles = static_cast<uint32_t>(v->asU64());
  if (const auto* v = root.get("entries")) out.entries = static_cast<uint32_t>(v->asU64());
  if (const auto* v = root.get("blocks")) out.blocks = static_cast<uint32_t>(v->asU64());
  readFile(root.get("dict"), out.dict);
  readFile(root.get("blocksdir"), out.blocksdir);
  if (const auto* t = root.get("titles"); t && t->type == JsonValue::Type::Array) {
    for (const auto& item : t->items) {
      ManifestFile f;
      readFile(&item, f);
      out.titles.push_back(std::move(f));
    }
  }
  if (const auto* s = root.get("shards"); s && s->type == JsonValue::Type::Array) {
    for (const auto& item : s->items) {
      ManifestFile f;
      readFile(&item, f);
      out.shards.push_back(std::move(f));
    }
  }
  if (const auto* t = root.get("tiers"); t && t->type == JsonValue::Type::Array) {
    for (const auto& item : t->items) {
      ManifestTier tier;
      if (const auto* n = item.get("name")) tier.name = n->asStr();
      if (const auto* n = item.get("shards")) tier.shards = n->asInt();
      if (const auto* n = item.get("articles")) tier.articles = static_cast<uint32_t>(n->asU64());
      if (const auto* n = item.get("bytes")) tier.bytes = n->asU64();
      out.tiers.push_back(std::move(tier));
    }
  }
  out.valid = !out.dict.file.empty() && !out.blocksdir.file.empty() && !out.titles.empty() && !out.shards.empty();
  return out.valid;
}

// ------------------------------------------------------------- blocks.dir

bool BlocksDir::load(std::vector<uint8_t> bytes) {
  count_ = 0;
  if (bytes.size() < 12 || memcmp(bytes.data(), "WKBD", 4) != 0 || bytes[4] != 1) return false;
  const uint32_t count = readU32(bytes.data() + 8);
  if (bytes.size() < 12 + static_cast<size_t>(count) * 16) return false;
  bytes_ = std::move(bytes);
  count_ = count;
  return true;
}

bool BlocksDir::record(const uint32_t block, BlockRecord& out) const {
  if (block >= count_) return false;
  const uint8_t* p = bytes_.data() + 12 + static_cast<size_t>(block) * 16;
  out.shard = readU16(p);
  out.slots = readU16(p + 2);
  out.offset = readU32(p + 4);
  out.csize = readU32(p + 8);
  out.usize = readU32(p + 12);
  return true;
}

// ------------------------------------------------------------ titles.N.idx

bool TitleIndex::open(ByteSource& source) {
  source_ = nullptr;
  sampler_.clear();
  uint8_t header[kHeaderBytes];
  if (source.size() < kHeaderBytes || !source.read(0, header, kHeaderBytes)) return false;
  if (memcmp(header, "WKTI", 4) != 0 || header[4] != 1) return false;
  entries_ = readU32(header + 8);
  blocks_ = readU32(header + 12);
  blockBytes_ = readU32(header + 16);
  const uint32_t samplerOffset = readU32(header + 20);
  const uint32_t samplerBytes = readU32(header + 24);
  if (blockBytes_ < 64 || blockBytes_ > 65536 || blocks_ == 0) return false;
  if (static_cast<uint64_t>(samplerOffset) + samplerBytes > source.size() || samplerBytes < 4) return false;
  std::vector<uint8_t> sampler(samplerBytes);
  if (!source.read(samplerOffset, sampler.data(), samplerBytes)) return false;
  const uint32_t count = readU32(sampler.data());
  if (count != blocks_) return false;
  sampler_.reserve(count);
  size_t pos = 4;
  for (uint32_t i = 0; i < count; ++i) {
    if (pos >= sampler.size()) return false;
    const uint8_t len = sampler[pos++];
    if (pos + len > sampler.size()) return false;
    sampler_.emplace_back(reinterpret_cast<const char*>(sampler.data() + pos), len);
    pos += len;
  }
  source_ = &source;
  return true;
}

uint32_t TitleIndex::blockFor(const std::string& foldedKey) const {
  // Last sampler key <= foldedKey; the first block when nothing is.
  const auto it = std::upper_bound(sampler_.begin(), sampler_.end(), foldedKey);
  if (it == sampler_.begin()) return 0;
  return static_cast<uint32_t>((it - sampler_.begin()) - 1);
}

bool TitleIndex::readBlock(const uint32_t block, std::vector<uint8_t>& buf) const {
  if (!source_ || block >= blocks_) return false;
  buf.resize(blockBytes_);
  return source_->read(kHeaderBytes + block * blockBytes_, buf.data(), blockBytes_);
}

template <typename Fn>
bool TitleIndex::scan(const uint32_t block, Fn&& fn) const {
  std::vector<uint8_t> buf;
  if (!readBlock(block, buf)) return false;
  const uint16_t count = readU16(buf.data());
  size_t pos = 2;
  std::string prev;
  for (uint16_t i = 0; i < count; ++i) {
    if (pos + 8 > buf.size()) return false;
    const uint8_t shared = buf[pos];
    const uint8_t suffixLen = buf[pos + 1];
    const uint8_t flags = buf[pos + 2];
    const uint32_t locator = readU32(buf.data() + pos + 4);
    pos += 8;
    if (pos + suffixLen > buf.size() || shared > prev.size()) return false;
    IndexEntry entry;
    entry.title.assign(prev, 0, shared);
    entry.title.append(reinterpret_cast<const char*>(buf.data() + pos), suffixLen);
    pos += suffixLen;
    entry.locator = locator;
    entry.redirect = (flags & 1) != 0;
    prev = entry.title;
    if (!fn(entry, fold(entry.title))) return false;
  }
  return true;
}

bool TitleIndex::find(const std::string& title, IndexEntry& out) const {
  if (!source_) return false;
  const std::string key = fold(title);
  if (key.empty()) return false;
  bool found = false;
  bool past = false;
  for (uint32_t block = blockFor(key); block < blocks_ && !found && !past; ++block) {
    scan(block, [&](const IndexEntry& entry, const std::string& folded) {
      if (folded == key) {
        out = entry;
        found = true;
        return false;
      }
      if (folded > key) {
        past = true;
        return false;
      }
      return true;
    });
  }
  return found;
}

int TitleIndex::prefix(const std::string& query, const int max, std::vector<IndexEntry>& out) const {
  if (!source_ || max <= 0) return 0;
  const std::string key = fold(query);
  if (key.empty()) return 0;
  int added = 0;
  bool past = false;
  for (uint32_t block = blockFor(key); block < blocks_ && added < max && !past; ++block) {
    scan(block, [&](const IndexEntry& entry, const std::string& folded) {
      if (folded.compare(0, key.size(), key) == 0) {
        out.push_back(entry);
        ++added;
        return added < max;
      }
      if (folded > key) {
        past = true;
        return false;
      }
      return true;
    });
  }
  return added;
}

bool TitleIndex::entryAt(const uint32_t block, const uint32_t ordinal, IndexEntry& out) const {
  bool found = false;
  uint32_t i = 0;
  scan(block, [&](const IndexEntry& entry, const std::string&) {
    if (i++ == ordinal) {
      out = entry;
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

// ------------------------------------------------------- a decoded block

uint16_t blockSlots(const uint8_t* data, const size_t len) {
  if (!data || len < 4) return 0;
  const uint16_t slots = readU16(data);
  if (len < 4 + static_cast<size_t>(slots + 1) * 4) return 0;
  return slots;
}

bool blockArticle(const uint8_t* data, const size_t len, const uint32_t slot, ArticleView& out) {
  const uint16_t slots = blockSlots(data, len);
  if (slot >= slots) return false;
  const uint32_t start = readU32(data + 4 + slot * 4);
  const uint32_t end = readU32(data + 4 + (slot + 1) * 4);
  if (start > end || end > len) return false;
  const uint8_t* p = data + start;
  size_t remaining = end - start;
  if (remaining < 2) return false;
  const uint16_t titleLen = readU16(p);
  p += 2;
  remaining -= 2;
  if (remaining < titleLen + 2u) return false;
  out.title.assign(reinterpret_cast<const char*>(p), titleLen);
  p += titleLen;
  remaining -= titleLen;
  const uint16_t sections = readU16(p);
  p += 2;
  remaining -= 2;
  out.headings.clear();
  out.headings.reserve(sections);
  for (uint16_t i = 0; i < sections; ++i) {
    if (remaining < 1) return false;
    const uint8_t hl = *p++;
    remaining -= 1;
    if (remaining < hl) return false;
    out.headings.emplace_back(reinterpret_cast<const char*>(p), hl);
    p += hl;
    remaining -= hl;
  }
  if (remaining < 4) return false;
  const uint32_t xhtmlLen = readU32(p);
  p += 4;
  remaining -= 4;
  if (xhtmlLen > remaining) return false;
  out.xhtml = p;
  out.xhtmlLen = xhtmlLen;
  return true;
}

// ----------------------------------------------------------------- state

void State::touch(const RecentEntry& entry) {
  for (auto it = recent.begin(); it != recent.end();) {
    if (it->locator == entry.locator) {
      it = recent.erase(it);
    } else {
      ++it;
    }
  }
  recent.insert(recent.begin(), entry);
  if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
}

std::string State::toJson() const {
  std::string s = "{\"continue\":{\"locator\":" + std::to_string(current.locator) +
                  ",\"page\":" + std::to_string(currentPage) + ",\"title\":\"";
  jsonEscape(s, current.title);
  s += "\"},\"recent\":[";
  for (size_t i = 0; i < recent.size(); ++i) {
    if (i) s += ',';
    s += "{\"locator\":" + std::to_string(recent[i].locator) + ",\"title\":\"";
    jsonEscape(s, recent[i].title);
    s += "\"}";
  }
  s += "]}";
  return s;
}

bool State::fromJson(const char* json, const size_t len) {
  JsonValue root;
  JsonReader reader(json, len);
  if (!reader.parse(root) || root.type != JsonValue::Type::Object) return false;
  *this = State{};
  if (const auto* c = root.get("continue"); c && c->type == JsonValue::Type::Object) {
    if (const auto* v = c->get("locator")) current.locator = static_cast<uint32_t>(v->asU64());
    if (const auto* v = c->get("page")) currentPage = v->asInt();
    if (const auto* v = c->get("title")) current.title = v->asStr();
  }
  if (const auto* r = root.get("recent"); r && r->type == JsonValue::Type::Array) {
    for (const auto& item : r->items) {
      RecentEntry e;
      if (const auto* v = item.get("locator")) e.locator = static_cast<uint32_t>(v->asU64());
      if (const auto* v = item.get("title")) e.title = v->asStr();
      if (!e.title.empty()) recent.push_back(std::move(e));
      if (recent.size() >= kMaxRecent) break;
    }
  }
  return true;
}

}  // namespace wikipedia
