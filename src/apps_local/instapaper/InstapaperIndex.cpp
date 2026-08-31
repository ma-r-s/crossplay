#include "InstapaperIndex.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace instapaper {

namespace {

constexpr char kMagic[] = "instapaper";
// 1 is the first. When a column is added or moved, bump this and teach
// parseIndex the old order -- see the header for what happens otherwise.
constexpr int kVersion = 1;

// Flags share one column because a bool per column is a column per bool, and
// every one of those is a chance to add the next one in the wrong place.
constexpr uint32_t kFlagRenderable = 1u << 0;
constexpr uint32_t kFlagProgressDirty = 1u << 1;
constexpr uint32_t kFlagArchivePending = 1u << 2;

std::string field(std::string_view row, size_t& cursor) {
  if (cursor > row.size()) return {};
  const size_t tab = row.find('\t', cursor);
  const size_t end = tab == std::string_view::npos ? row.size() : tab;
  std::string value(row.substr(cursor, end - cursor));
  cursor = tab == std::string_view::npos ? row.size() + 1 : tab + 1;
  return value;
}

uint32_t toUint(const std::string& text) { return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 10)); }

int64_t toInt64(const std::string& text) { return static_cast<int64_t>(std::strtoll(text.c_str(), nullptr, 10)); }

float toFloat(const std::string& text) {
  const double value = std::strtod(text.c_str(), nullptr);
  if (!(value > 0.0)) return 0.0f;  // also catches NaN, which would poison every later comparison
  return value > 1.0 ? 1.0f : static_cast<float>(value);
}

// Hex only, and bounded. Both fields come off the wire and both end up in a
// filename or a URL path, so a stray slash or dot is worth refusing here
// rather than trusting three layers down.
std::string sanitizeToken(std::string_view text, size_t limit) {
  std::string out;
  for (const char c : text) {
    if (out.size() >= limit) break;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) out.push_back(c);
  }
  return out;
}

}  // namespace

std::string sanitizeField(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool lastWasSpace = false;
  for (const char c : text) {
    const bool isSpace = c == '\t' || c == '\n' || c == '\r' || c == ' ';
    if (isSpace) {
      if (!out.empty() && !lastWasSpace) out.push_back(' ');
      lastWasSpace = true;
      continue;
    }
    out.push_back(c);
    lastWasSpace = false;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::string articleFileName(const int64_t id) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "a%lld.txt", static_cast<long long>(id));
  return buf;
}

std::string serializeIndex(const std::vector<Article>& articles) {
  std::string out = std::string(kMagic) + "\t" + std::to_string(kVersion) + "\n";
  for (const Article& a : articles) {
    if (a.id == 0) continue;
    uint32_t flags = 0;
    if (a.renderable) flags |= kFlagRenderable;
    if (a.progressDirty) flags |= kFlagProgressDirty;
    if (a.archivePending) flags |= kFlagArchivePending;
    char numbers[160];
    std::snprintf(numbers, sizeof(numbers), "%lld\t%s\t%s\t%u\t%u\t%u\t%.4f\t%u\t%u\t", static_cast<long long>(a.id),
                  a.hash.c_str(), a.sha.c_str(), a.savedAt, a.words, static_cast<unsigned>(a.minutes),
                  static_cast<double>(a.progress), a.progressAt, flags);
    out += numbers;
    out += sanitizeField(a.domain);
    out += '\t';
    // Title last: the only free-form column, so damage stops at the newline.
    out += sanitizeField(a.title);
    out += '\n';
  }
  return out;
}

bool parseIndex(const std::string_view text, std::vector<Article>& out) {
  out.clear();
  size_t pos = 0;
  const size_t firstBreak = text.find('\n');
  if (firstBreak == std::string_view::npos) return false;
  const std::string_view header = text.substr(0, firstBreak);
  if (header.rfind(kMagic, 0) != 0) return false;
  size_t headerCursor = 0;
  field(header, headerCursor);
  const int version = std::atoi(field(header, headerCursor).c_str());
  if (version < 1 || version > kVersion) return false;
  pos = firstBreak + 1;

  while (pos < text.size()) {
    const size_t lineEnd = text.find('\n', pos);
    const std::string_view row = text.substr(pos, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - pos);
    pos = lineEnd == std::string_view::npos ? text.size() : lineEnd + 1;
    if (row.empty()) continue;

    size_t cursor = 0;
    Article a;
    a.id = toInt64(field(row, cursor));
    a.hash = sanitizeToken(field(row, cursor), 32);
    a.sha = sanitizeToken(field(row, cursor), 32);
    a.savedAt = toUint(field(row, cursor));
    a.words = toUint(field(row, cursor));
    a.minutes = static_cast<uint16_t>(toUint(field(row, cursor)));
    a.progress = toFloat(field(row, cursor));
    a.progressAt = toUint(field(row, cursor));
    const uint32_t flags = toUint(field(row, cursor));
    a.renderable = (flags & kFlagRenderable) != 0;
    a.progressDirty = (flags & kFlagProgressDirty) != 0;
    a.archivePending = (flags & kFlagArchivePending) != 0;
    a.domain = field(row, cursor);
    a.title = field(row, cursor);

    // A row with no id is damage rather than data. A row with no title is
    // not: an untitled bookmark is a thing Instapaper really returns, and
    // the queue draws its domain instead.
    if (a.id == 0) continue;
    out.push_back(std::move(a));
  }
  return true;
}

void sortForQueue(std::vector<Article>& articles) {
  std::stable_sort(articles.begin(), articles.end(), [](const Article& a, const Article& b) {
    if (a.savedAt != b.savedAt) return a.savedAt > b.savedAt;
    return a.id > b.id;
  });
}

std::vector<const Article*> visible(const std::vector<Article>& articles) {
  std::vector<const Article*> out;
  out.reserve(articles.size());
  for (const Article& a : articles) {
    if (a.archivePending) continue;
    out.push_back(&a);
  }
  return out;
}

MergePlan mergeSummary(std::vector<Article>& local, const std::vector<Article>& incoming,
                       const std::vector<int64_t>& deleted, const std::vector<int64_t>& archived,
                       const std::vector<int64_t>& hasText) {
  MergePlan plan;
  const auto names = [](const std::vector<int64_t>& ids, const int64_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
  };

  // Every article in the index was in the `have` string this sync sent, so a
  // sync that reached `done` has delivered every dirty progress value. It is
  // cleared here rather than per article, because the confirmation is the
  // sync completing and not any particular row coming back.
  for (Article& a : local) a.progressDirty = false;

  for (const Article& in : incoming) {
    auto it = std::find_if(local.begin(), local.end(), [&](const Article& a) { return a.id == in.id; });
    if (it == local.end()) {
      local.push_back(in);
      local.back().progressDirty = false;
      plan.download.push_back(in.id);
      continue;
    }
    const std::string previousSha = it->sha;
    const float localProgress = it->progress;
    const uint32_t localProgressAt = it->progressAt;
    const bool wasPending = it->archivePending;
    *it = in;
    it->progressDirty = false;
    it->archivePending = wasPending;
    // Timestamps decide, both ways. The bridge sent ours up in the same call
    // that fetched these, so a server value that is still older means our
    // reading is the newer fact and putting the server's back would undo it.
    if (localProgressAt > in.progressAt) {
      it->progress = localProgress;
      it->progressAt = localProgressAt;
    }
    if (it->sha != previousSha) plan.download.push_back(it->id);
  }

  // An article the index knows and the card does not must be fetched however
  // untouched its metadata looks: a download can fail after the index was
  // saved, and without this the row would sit there forever opening nothing.
  for (const Article& a : local) {
    if (names(hasText, a.id)) continue;
    if (!names(plan.download, a.id)) plan.download.push_back(a.id);
  }

  const auto gone = [&](const Article& a) { return names(deleted, a.id) || names(archived, a.id); };
  for (const Article& a : local) {
    if (gone(a)) plan.drop.push_back(a.id);
  }
  local.erase(std::remove_if(local.begin(), local.end(), gone), local.end());

  sortForQueue(local);
  if (local.size() > kMaxArticles) {
    for (size_t i = kMaxArticles; i < local.size(); ++i) plan.drop.push_back(local[i].id);
    local.resize(kMaxArticles);
  }
  // Anything trimmed off the end must not also be queued for download: it is
  // about to lose its file, and fetching it first would spend a round trip
  // writing bytes this function has already decided to delete.
  plan.download.erase(std::remove_if(plan.download.begin(), plan.download.end(),
                                     [&](const int64_t id) { return names(plan.drop, id); }),
                      plan.download.end());
  return plan;
}

}  // namespace instapaper
