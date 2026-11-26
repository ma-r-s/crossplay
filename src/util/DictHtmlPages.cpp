#include "DictHtmlPages.h"

#include <Arduino.h>
#include <Epub/htmlEntities.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "network/PsramMallocScope.h"
#if (FREEINK_DEVICE_PICCO || FREEINK_DEVICE_X4PRO) && !defined(SIMULATOR)
#include <HalMemory.h>
#endif

namespace {

// Normalized XHTML staged here for the file-driven parser; truncated on each
// use, removed after the parse.
constexpr const char* TMP_HTML_PATH = "/.crosspoint/dicthtml.tmp";

// Entry includes the parser's measured ~21 KB working set in addition to layout headroom.
constexpr size_t MIN_STYLED_FREE_HEAP = 64 * 1024;
constexpr size_t MIN_STYLED_MAX_ALLOC = 40 * 1024;

// Checked per completed page: allow ~8 KB for the next page before its 16 KB
// SD-font scratch request, with additional room for paragraph layout and mappings.
constexpr size_t MIN_STYLED_RETAIN_HEAP = 40 * 1024;
constexpr size_t MIN_STYLED_RETAIN_ALLOC = 32 * 1024;

// Bound retained layout independently of input bytes: compact markup can emit
// far more objects than its source size suggests.
constexpr size_t MAX_STYLED_PAGES = 64;
constexpr size_t MAX_STYLED_PAGE_ELEMENTS = 512;

struct LayoutHeap {
  unsigned freeBytes;
  unsigned largestBlockBytes;
};

// The heap the layout allocates from, which the floors above apply to. Where PsramMallocScope is live that includes
// PSRAM, and the few tens of KB of internal heap left with Wi-Fi up say nothing about whether the layout fits.
LayoutHeap layoutHeap() {
#if (FREEINK_DEVICE_PICCO || FREEINK_DEVICE_X4PRO) && !defined(SIMULATOR)
  const auto heap = HalMemory::getDefaultHeap();
  return {static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.largestBlockBytes)};
#else
  return {ESP.getFreeHeap(), ESP.getMaxAllocHeap()};
#endif
}

class BufferedFileWriter {
 public:
  explicit BufferedFileWriter(HalFile& file) : file(file) {}

  bool append(const char c) { return append(&c, 1); }

  bool append(const char* data, size_t len) {
    while (len > 0) {
      const size_t available = sizeof(buffer) - used;
      const size_t chunk = std::min(available, len);
      memcpy(buffer + used, data, chunk);
      used += chunk;
      data += chunk;
      len -= chunk;
      if (used == sizeof(buffer) && !flush()) return false;
    }
    return true;
  }

  bool append(const char* text) { return append(text, strlen(text)); }

  bool flush() {
    if (used == 0) return true;
    if (file.write(buffer, used) != used) return false;
    used = 0;
    return true;
  }

 private:
  HalFile& file;
  // Fixed stack staging avoids an expansion-sized XHTML heap allocation.
  char buffer[128] = {};
  size_t used = 0;
};

// HTML void elements: legal without a closing tag in HTML, but must be
// self-closed to be well-formed XML.
bool isVoidElement(const char* name, const size_t len) {
  static constexpr const char* VOID_ELEMENTS[] = {"area",  "base", "br",   "col",   "embed",  "hr",    "img",
                                                  "input", "link", "meta", "param", "source", "track", "wbr"};
  return std::any_of(std::begin(VOID_ELEMENTS), std::end(VOID_ELEMENTS),
                     [name, len](const char* v) { return strlen(v) == len && strncmp(v, name, len) == 0; });
}

// True for a well-formed entity reference at html[pos] ('&'): &name; &#123;
// or &#x1F;. On success *end is the index of the ';'.
bool isEntityRef(const std::string& html, const size_t pos, size_t* end) {
  size_t j = pos + 1;
  const size_t n = html.size();
  if (j < n && html[j] == '#') {
    j++;
    if (j < n && (html[j] == 'x' || html[j] == 'X')) j++;
    const size_t digits = j;
    while (j < n && std::isxdigit(static_cast<unsigned char>(html[j]))) j++;
    if (j == digits) return false;
  } else {
    const size_t letters = j;
    while (j < n && std::isalnum(static_cast<unsigned char>(html[j]))) j++;
    if (j == letters) return false;
  }
  if (j >= n || html[j] != ';') return false;
  *end = j;
  return true;
}

// Writes a well-formed reference found by isEntityRef().
bool appendEntityRef(BufferedFileWriter& out, const char* ref, const size_t len) {
#if FREEINK_DEVICE_PICCO || FREEINK_DEVICE_X4PRO
  // Without a DTD expat knows only the XML names and rejects the whole document over any other, so HTML names
  // become their characters and unknown ones literal text.
  static constexpr const char* XML_ENTITIES[] = {"&amp;", "&apos;", "&gt;", "&lt;", "&quot;"};
  const bool isXml =
      ref[1] == '#' || std::any_of(std::begin(XML_ENTITIES), std::end(XML_ENTITIES),
                                   [ref, len](const char* e) { return strlen(e) == len && strncmp(e, ref, len) == 0; });
  if (!isXml) {
    // Only the XML names stand for markup characters or quotes, so a value needs no escaping.
    if (const char* value = lookupHtmlEntity(ref, len)) return out.append(value);
    return out.append("&amp;") && out.append(ref + 1, len - 1);
  }
#endif
  return out.append(ref, len);
}

// Copies a tag's attribute bytes [pos, end). expat checks references there too, and rejects a bare '&' or '<'.
bool appendAttributes(BufferedFileWriter& out, const std::string& html, size_t pos, const size_t end) {
  while (pos < end) {
    size_t entityEnd = 0;
    if (html[pos] == '&' && isEntityRef(html, pos, &entityEnd) && entityEnd < end) {
      if (!appendEntityRef(out, html.data() + pos, entityEnd - pos + 1)) return false;
      pos = entityEnd + 1;
    } else {
      const char c = html[pos++];
      const bool ok = c == '&' ? out.append("&amp;") : c == '<' ? out.append("&lt;") : out.append(c);
      if (!ok) return false;
    }
  }
  return true;
}

// StarDict HTML is tag soup; expat is a strict XML parser. Produce a
// well-formed XHTML document from the fragment: wrap it in a root element,
// lowercase tag names (XML is case-sensitive and the parser matches
// lowercase), self-close void elements (<br> → <br/>), drop stray void
// closers (</br>) and <!…>/<?…> constructs, and escape '&'/'<' characters
// that are not part of markup. Structural damage this cannot repair
// (mismatched tags, unquoted attribute values) surfaces as a parse error and
// the caller falls back to the plain-text path.
bool writeNormalizedXhtml(const std::string& html, HalFile& file) {
  BufferedFileWriter out(file);
  if (!out.append("<html><body>")) return false;

  const size_t n = html.size();
  size_t i = 0;
  while (i < n) {
    const char c = html[i];
    if (c == '<' && i + 1 < n && (html[i + 1] == '!' || html[i + 1] == '?')) {
      // Comment, doctype or processing instruction: drop it entirely.
      const bool isComment = html.compare(i, 4, "<!--") == 0;
      const size_t j = isComment ? html.find("-->", i + 4) : html.find('>', i);
      i = (j == std::string::npos) ? n : j + (isComment ? 3 : 1);
      continue;
    }
    if (c == '<' && i + 1 < n && (html[i + 1] == '/' || std::isalpha(static_cast<unsigned char>(html[i + 1])))) {
      // Find the tag end, honouring quoted attribute values.
      size_t j = i + 1;
      char quote = 0;
      while (j < n) {
        const char d = html[j];
        if (quote) {
          if (d == quote) quote = 0;
        } else if (d == '"' || d == '\'') {
          quote = d;
        } else if (d == '>') {
          break;
        }
        j++;
      }
      if (j == n) {  // unterminated tag: treat the '<' as literal text
        if (!out.append("&lt;")) return false;
        i++;
        continue;
      }

      const bool closing = html[i + 1] == '/';
      const size_t nameStart = i + (closing ? 2 : 1);
      size_t nameEnd = nameStart;
      char nameBuf[16] = {};
      size_t nameLen = 0;
      while (nameEnd < j && std::isalnum(static_cast<unsigned char>(html[nameEnd]))) {
        if (nameLen < sizeof(nameBuf) - 1) {
          nameBuf[nameLen++] = static_cast<char>(std::tolower(static_cast<unsigned char>(html[nameEnd])));
        }
        nameEnd++;
      }
      const bool isVoid = isVoidElement(nameBuf, nameLen);
      if (closing && isVoid) {  // "</br>" — no XML equivalent, drop it
        i = j + 1;
        continue;
      }
      if (!out.append('<')) return false;
      if (closing && !out.append('/')) return false;
      if (!out.append(nameBuf, nameLen)) return false;
      if (!appendAttributes(out, html, nameEnd, j)) return false;
      if (!closing && isVoid && html[j - 1] != '/' && !out.append('/')) return false;
      if (!out.append('>')) return false;
      i = j + 1;
      continue;
    }
    if (c == '<') {  // stray '<' in text ("x < y")
      if (!out.append("&lt;")) return false;
      i++;
      continue;
    }
    if (c == '&') {
      size_t entityEnd = 0;
      if (isEntityRef(html, i, &entityEnd)) {
        if (!appendEntityRef(out, html.data() + i, entityEnd - i + 1)) return false;
        i = entityEnd + 1;
      } else {  // bare ampersand ("Tom & Jerry")
        if (!out.append("&amp;")) return false;
        i++;
      }
      continue;
    }
    if (!out.append(c)) return false;
    i++;
  }

  return out.append("</body></html>") && out.flush();
}

}  // namespace

bool buildDictionaryHtmlPages(GfxRenderer& renderer, const std::string& definition, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, std::vector<std::unique_ptr<Page>>& pagesOut) {
  const LayoutHeap entryHeap = layoutHeap();
  if (entryHeap.freeBytes < MIN_STYLED_FREE_HEAP || entryHeap.largestBlockBytes < MIN_STYLED_MAX_ALLOC) {
    LOG_ERR("DHTML", "Low heap for styled definition (%u free, %u max block)", entryHeap.freeBytes,
            entryHeap.largestBlockBytes);
    return false;
  }

  // Keeps the parser's working set and the returned pages out of internal DRAM where the board allows it.
  [[maybe_unused]] const PsramMallocScope psramMalloc;

  {
    HalFile tmp = Storage.open(TMP_HTML_PATH, O_WRITE | O_CREAT | O_TRUNC);
    if (!tmp) {
      LOG_ERR("DHTML", "Cannot create %s", TMP_HTML_PATH);
      return false;
    }
    if (!writeNormalizedXhtml(definition, tmp)) {
      LOG_ERR("DHTML", "Short write to %s", TMP_HTML_PATH);
      return false;
    }
  }  // destructor closes the file before the parser reopens the same path

  pagesOut.clear();
  // One fixed allocation (256 bytes on C3); pages must outlive the parser.
  pagesOut.reserve(MAX_STYLED_PAGES);

  bool ok = false;
  const char* limitReason = nullptr;
  size_t retainedElements = 0;
  {
    const std::string tmpPath = TMP_HTML_PATH;  // the parser stores a reference
    // Heap-allocated as Section does — the parser object is far too large for
    // a stack local. Null epub is safe: imageRendering=2 suppresses <img>
    // handling, the only path that dereferences it.
    std::unique_ptr<ChapterHtmlSlimParser> parser;
    parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
        nullptr, tmpPath, renderer, SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
        SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
        SETTINGS.hyphenationEnabled, SETTINGS.focusReadingEnabled,
        [&pagesOut, &parser, &retainedElements, &limitReason](std::unique_ptr<Page> page, uint16_t, uint16_t,
                                                              uint32_t) {
          if (limitReason) return;
          const size_t pageElements = page->elements.size();
          const LayoutHeap heap = layoutHeap();
          // Name the limit that fired. The three causes mean different things --
          // the count caps say the definition is genuinely too big to hold,
          // while the heap floor says only that this moment was a bad one -- and
          // a single "exceeded the budget" message cannot tell them apart.
          if (pagesOut.size() >= MAX_STYLED_PAGES) {
            limitReason = "page count";
          } else if (pageElements > MAX_STYLED_PAGE_ELEMENTS - retainedElements) {
            limitReason = "element count";
          } else if (heap.freeBytes < MIN_STYLED_RETAIN_HEAP || heap.largestBlockBytes < MIN_STYLED_RETAIN_ALLOC) {
            limitReason = "free heap";
          }
          if (limitReason != nullptr) {
            LOG_ERR("DHTML", "Styled definition stopped on %s (pages=%u elements=%u free=%u contig=%u)", limitReason,
                    static_cast<unsigned>(pagesOut.size()), static_cast<unsigned>(retainedElements + pageElements),
                    heap.freeBytes, heap.largestBlockBytes);
            pagesOut.clear();
            parser->requestStop();
            return;
          }
          retainedElements += pageElements;
          pagesOut.push_back(std::move(page));
        },
        /*embeddedStyle=*/false, /*contentBase=*/"", /*imageBasePath=*/"", /*imageRendering=*/2);
    if (!parser) {
      LOG_ERR("DHTML", "OOM: ChapterHtmlSlimParser");
    } else {
      parser->setTextSpacing(SETTINGS.getCharacterSpacing(), SETTINGS.wordSpacing);
      parser->setParagraphIndentSpaces(SETTINGS.paragraphIndentSpaces);
      ok = parser->parseAndBuildPages();  // closes the file on both outcomes
    }
  }
  Storage.remove(TMP_HTML_PATH);

  if (limitReason) {
    LOG_ERR("DHTML", "Styled definition exceeded page heap budget");
  }
  if (!ok || limitReason || pagesOut.empty()) {
    if (!ok && !limitReason) {
      LOG_ERR("DHTML", "Definition parse/layout failed");
    } else if (!limitReason && pagesOut.empty()) {
      LOG_ERR("DHTML", "Definition produced no styled pages");
    }
    pagesOut.clear();
    return false;
  }
  LOG_DBG("DHTML", "Styled definition: pages=%u internal free=%u max block=%u", static_cast<unsigned>(pagesOut.size()),
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}
