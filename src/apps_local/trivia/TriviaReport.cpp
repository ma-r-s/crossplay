#include "TriviaReport.h"

#include <cstdio>
#include <cstring>

namespace trivia {
namespace {

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void writeU32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint32_t entryOffset(const uint32_t i) { return kReportHeaderBytes + i * kReportEntryBytes; }

// Digits up to a legitimate terminator. The terminator set covers both callers:
// end of line for pack.meta, and a comma or a closing brace for the manifest.
// Anything else that is not a digit is a REFUSAL, not a stopping point -- "1x0"
// must not read as 1, because a count that is quietly wrong compares equal to
// nothing and disables the very check it feeds.
bool isNumberEnd(const char c) { return c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == ',' || c == '}'; }

bool parseU32(const char* s, const char* end, uint32_t& out) {
  uint32_t v = 0;
  bool any = false;
  for (const char* p = s; p < end; ++p) {
    if (isNumberEnd(*p)) break;
    if (*p < '0' || *p > '9') return false;
    // Refuse rather than wrap. A wrapped count compares equal to something and
    // that something is not the pack.
    if (v > (0xFFFFFFFFu - static_cast<uint32_t>(*p - '0')) / 10u) return false;
    v = v * 10u + static_cast<uint32_t>(*p - '0');
    any = true;
  }
  if (!any) return false;
  out = v;
  return true;
}

bool copyId(char* dst, const char* src, const size_t length) {
  if (length == 0 || length > kPackIdBytes) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = src[i];
    // The id is hex today, but the check is "is this a safe, printable token"
    // rather than "is this hex": the endpoint's own guard is the same shape,
    // and a stricter reader here would strand a device on a future id format.
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
    dst[i] = c;
  }
  dst[length] = '\0';
  return true;
}

}  // namespace

const char* reasonName(const Reason reason) {
  switch (reason) {
    case Reason::None:
      return "none";
    case Reason::Wrong:
      return "wrong";
    case Reason::Nonsense:
      return "nonsense";
    case Reason::Giveaway:
      return "giveaway";
    case Reason::Ambiguous:
      return "ambiguous";
    case Reason::Outdated:
      return "outdated";
    case Reason::Broken:
      return "broken";
    case Reason::Regional:
      return "regional";
    case Reason::Us:
      return "us";
    case Reason::Hard:
      return "hard";
    case Reason::Easy:
      return "easy";
    case Reason::Count:
      break;
  }
  // Unreachable for a stored reason -- add() refuses anything outside the enum
  // -- and deliberately NOT a name the endpoint accepts, so a future value that
  // slipped through would be refused there rather than filed as something else.
  return "";
}

// --- the queue ---------------------------------------------------------------

bool ReportQueue::writeHeader() {
  uint8_t head[kReportHeaderBytes] = {};
  std::memcpy(head, kReportMagic, sizeof(kReportMagic));
  writeU16(head + 8, kReportVersion);
  writeU16(head + 10, 0);
  writeU32(head + 12, packCount_);
  writeU32(head + 16, sent_);
  const size_t idLen = std::strlen(packId_);
  std::memcpy(head + 20, packId_, idLen);
  return source_->write(0, head, kReportHeaderBytes) && source_->flush();
}

QueueOpen ReportQueue::open(WritableByteSource& source, const char* packId, const uint32_t packCount) {
  source_ = nullptr;
  count_ = 0;
  sent_ = 0;
  packCount_ = 0;
  packId_[0] = '\0';

  // No id means the card's build is unknown. Nothing may be queued: a report
  // that cannot name its pack cannot ever be resolved.
  if (packId == nullptr || packId[0] == '\0') return QueueOpen::Unusable;
  if (!copyId(packId_, packId, std::strlen(packId))) {
    packId_[0] = '\0';
    return QueueOpen::Unusable;
  }
  packCount_ = packCount;

  const uint32_t size = source.size();
  if (size == 0) {
    source_ = &source;
    return writeHeader() ? QueueOpen::Started : QueueOpen::Unusable;
  }
  if (size < kReportHeaderBytes) return QueueOpen::Unusable;

  uint8_t head[kReportHeaderBytes] = {};
  if (!source.read(0, head, kReportHeaderBytes)) return QueueOpen::Unusable;
  if (std::memcmp(head, kReportMagic, sizeof(kReportMagic)) != 0) return QueueOpen::Unusable;
  if (readU16(head + 8) != kReportVersion) return QueueOpen::Unusable;

  const uint32_t body = size - kReportHeaderBytes;
  // A torn tail is refused rather than rounded down. The device appends one
  // fixed-width record at a time, so a partial entry means something else wrote
  // this file, and guessing where the entries end is how one report becomes
  // another question's.
  if (body % kReportEntryBytes != 0) return QueueOpen::Unusable;

  const uint32_t stored = body / kReportEntryBytes;
  const uint32_t storedSent = readU32(head + 16);
  if (storedSent > stored) return QueueOpen::Unusable;

  char storedId[kPackIdBytes + 1] = {};
  size_t idLen = 0;
  while (idLen < kPackIdBytes && head[20 + idLen] != 0) ++idLen;
  if (idLen == 0 || !copyId(storedId, reinterpret_cast<const char*>(head + 20), idLen)) return QueueOpen::Unusable;

  const bool samePack = std::strcmp(storedId, packId_) == 0 && readU32(head + 12) == packCount;
  if (samePack) {
    source_ = &source;
    count_ = stored;
    sent_ = storedSent;
    return QueueOpen::Ready;
  }

  // A different pack. Undelivered reports about it are NOT re-labelled -- they
  // are about questions that no longer sit at those indices. They stay, and the
  // caller must sync them under their own header before this card's can be
  // queued. Only a fully delivered queue is safe to reuse.
  if (stored > storedSent) {
    std::strncpy(packId_, storedId, kPackIdBytes);
    packId_[kPackIdBytes] = '\0';
    packCount_ = readU32(head + 12);
    source_ = &source;
    count_ = stored;
    sent_ = storedSent;
    return QueueOpen::Foreign;
  }
  // Reusing a fully delivered queue for a DIFFERENT pack. The file cannot
  // shrink, so its old entries are still there, and they are about questions
  // that are no longer at those indices. They must be neutralised, not merely
  // stepped over:
  //
  //   * leaving them and rewriting the header would present them as PENDING
  //     reports for the new pack the next time this file is opened, because
  //     `stored` comes from the file's size and `sent` would have been reset to
  //     zero. That sends old indices under a new pack id -- exactly the
  //     mislabelling this whole class exists to prevent;
  //   * leaving them and setting the cursor past them fixes the sending, but
  //     find() would still match one by index, and add() treats a match below
  //     the cursor as already reported -- so the first report of a question
  //     that happens to share an index with an old entry would be silently
  //     dropped.
  //
  // Tombstoning is the only option that is correct on both paths. Bounded by
  // kMaxQueuedReports, so it is at most 64 eight-byte writes.
  source_ = &source;
  for (uint32_t i = 0; i < stored; ++i) {
    uint8_t rec[kReportEntryBytes] = {};
    writeU32(rec, kWithdrawnIndex);
    if (!source.write(entryOffset(i), rec, kReportEntryBytes)) return QueueOpen::Unusable;
  }
  count_ = stored;
  sent_ = stored;
  return writeHeader() ? QueueOpen::Started : QueueOpen::Unusable;
}

bool ReportQueue::find(const uint32_t index, uint32_t& slotOut) const {
  for (uint32_t i = 0; i < count_; ++i) {
    uint8_t rec[kReportEntryBytes] = {};
    if (!source_->read(entryOffset(i), rec, kReportEntryBytes)) return false;
    if (readU32(rec) == index) {
      slotOut = i;
      return true;
    }
  }
  return false;
}

bool ReportQueue::add(const uint32_t index, const Reason reason) {
  if (source_ == nullptr || packId_[0] == '\0') return false;
  if (index >= packCount_) return false;
  if (reason >= Reason::Count) return false;

  uint32_t slot = 0;
  if (find(index, slot)) {
    // Already queued. Only an undelivered entry may be amended; one already
    // sent is a fact on the far end and re-writing it here would silently
    // disagree with what was stored there.
    if (slot < sent_) return true;
    if (reason == Reason::None) return true;
    uint8_t rec[kReportEntryBytes] = {};
    writeU32(rec, index);
    rec[4] = static_cast<uint8_t>(reason);
    return source_->write(entryOffset(slot), rec, kReportEntryBytes) && source_->flush();
  }

  // A withdrawn slot is NOT reused. Reusing it would put a new report before an
  // older one in send order, and `sent` is a position: an out-of-order queue
  // would make markSent deliver something the device thinks it has not sent.
  //
  // Full: drop the NEWEST, which is this one. The first report of a question is
  // the one the player meant, and a queue this full means sync has not run in a
  // very long time. Reported as success because the FLAGGED bit still landed --
  // the question is hidden, which is what the player asked for.
  if (count_ >= kMaxQueuedReports) return true;

  uint8_t rec[kReportEntryBytes] = {};
  writeU32(rec, index);
  rec[4] = static_cast<uint8_t>(reason);
  if (!source_->write(entryOffset(count_), rec, kReportEntryBytes)) return false;
  ++count_;
  return writeHeader();
}

bool ReportQueue::setReason(const uint32_t index, const Reason reason) {
  if (source_ == nullptr) return false;
  if (reason >= Reason::Count) return false;
  uint32_t slot = 0;
  if (!find(index, slot)) return false;
  if (slot < sent_) return false;
  uint8_t rec[kReportEntryBytes] = {};
  writeU32(rec, index);
  rec[4] = static_cast<uint8_t>(reason);
  return source_->write(entryOffset(slot), rec, kReportEntryBytes) && source_->flush();
}

bool ReportQueue::entry(const uint32_t i, uint32_t& indexOut, Reason& reasonOut) const {
  if (source_ == nullptr || i >= count_) return false;
  uint8_t rec[kReportEntryBytes] = {};
  if (!source_->read(entryOffset(i), rec, kReportEntryBytes)) return false;
  indexOut = readU32(rec);
  if (rec[4] >= static_cast<uint8_t>(Reason::Count)) return false;
  reasonOut = static_cast<Reason>(rec[4]);
  return true;
}

bool ReportQueue::withdraw(const uint32_t index) {
  if (source_ == nullptr || index == kWithdrawnIndex) return false;
  uint32_t slot = 0;
  if (!find(index, slot)) return false;
  if (slot < sent_) return false;
  uint8_t rec[kReportEntryBytes] = {};
  writeU32(rec, kWithdrawnIndex);
  rec[4] = static_cast<uint8_t>(Reason::None);
  return source_->write(entryOffset(slot), rec, kReportEntryBytes) && source_->flush();
}

bool ReportQueue::markSent(const uint32_t n) {
  if (source_ == nullptr) return false;
  // Never moves backwards, and never past what exists. A server that answers
  // for more than was sent is not a reason to forget reports.
  if (n > count_ || n < sent_) return false;
  sent_ = n;
  return writeHeader();
}

// --- pack.meta ---------------------------------------------------------------

PackMeta parseMeta(const char* text, const size_t length, const uint32_t actualCount, const uint32_t actualBytes) {
  PackMeta meta;
  if (text == nullptr || length == 0) return meta;

  bool haveId = false, haveCount = false, haveBytes = false;
  const char* p = text;
  const char* end = text + length;
  while (p < end) {
    const char* lineEnd = p;
    while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
    const char* tab = p;
    while (tab < lineEnd && *tab != '\t') ++tab;
    if (tab < lineEnd) {
      const size_t keyLen = static_cast<size_t>(tab - p);
      const char* value = tab + 1;
      if (keyLen == 2 && std::strncmp(p, "id", 2) == 0) {
        size_t vlen = 0;
        while (value + vlen < lineEnd && value[vlen] != '\r') ++vlen;
        haveId = copyId(meta.id, value, vlen);
      } else if (keyLen == 5 && std::strncmp(p, "count", 5) == 0) {
        haveCount = parseU32(value, lineEnd, meta.count);
      } else if (keyLen == 5 && std::strncmp(p, "bytes", 5) == 0) {
        haveBytes = parseU32(value, lineEnd, meta.bytes);
      }
    }
    p = lineEnd < end ? lineEnd + 1 : end;
  }

  if (!haveId || !haveCount || !haveBytes) return meta;
  // The two free checks. Either failing means this meta describes a pack that
  // is no longer the pack beside it, and the answer is "I do not know", never
  // a repair.
  if (meta.count != actualCount || meta.bytes != actualBytes) return meta;
  meta.valid = true;
  return meta;
}

size_t formatMeta(char* out, const size_t capacity, const char* id, const uint32_t count, const uint32_t bytes) {
  if (out == nullptr || id == nullptr) return 0;
  const int n = std::snprintf(out, capacity, "id\t%s\ncount\t%u\nbytes\t%u\n", id, static_cast<unsigned>(count),
                              static_cast<unsigned>(bytes));
  if (n <= 0 || static_cast<size_t>(n) >= capacity) return 0;
  return static_cast<size_t>(n);
}

// --- the manifest ------------------------------------------------------------

namespace {

// Finds `"key"` and returns the first character after the following colon.
const char* valueAfter(const char* json, const size_t length, const char* key) {
  const size_t keyLen = std::strlen(key);
  if (length < keyLen + 3) return nullptr;
  for (size_t i = 0; i + keyLen + 2 <= length; ++i) {
    if (json[i] != '"') continue;
    if (std::strncmp(json + i + 1, key, keyLen) != 0) continue;
    if (json[i + 1 + keyLen] != '"') continue;
    size_t j = i + 2 + keyLen;
    while (j < length && (json[j] == ' ' || json[j] == '\t')) ++j;
    if (j >= length || json[j] != ':') continue;
    ++j;
    while (j < length && (json[j] == ' ' || json[j] == '\t')) ++j;
    return j < length ? json + j : nullptr;
  }
  return nullptr;
}

}  // namespace

PackManifest parseManifest(const char* json, const size_t length) {
  PackManifest man;
  if (json == nullptr || length == 0) return man;

  const char* idAt = valueAfter(json, length, "id");
  const char* countAt = valueAfter(json, length, "count");
  const char* bytesAt = valueAfter(json, length, "bytes");
  if (idAt == nullptr || countAt == nullptr || bytesAt == nullptr) return man;
  if (*idAt != '"') return man;
  ++idAt;
  const char* idEnd = idAt;
  const char* end = json + length;
  while (idEnd < end && *idEnd != '"') ++idEnd;
  if (idEnd >= end) return man;
  if (!copyId(man.id, idAt, static_cast<size_t>(idEnd - idAt))) return man;

  if (!parseU32(countAt, end, man.count)) return man;
  if (!parseU32(bytesAt, end, man.bytes)) return man;
  // A manifest naming an empty pack is not a manifest we can act on: it would
  // make every index out of range and every comparison meaningless.
  if (man.count == 0 || man.bytes == 0) return man;
  man.valid = true;
  return man;
}

Freshness compare(const PackMeta& held, const PackManifest& published) {
  if (!published.valid) return Freshness::NoManifest;
  if (!held.valid) return Freshness::Unknown;
  return std::strcmp(held.id, published.id) == 0 ? Freshness::Current : Freshness::Newer;
}

}  // namespace trivia
