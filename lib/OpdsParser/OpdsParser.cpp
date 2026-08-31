#include "OpdsParser.h"

#include <Logging.h>
#include <Utf8.h>
#include <XmlParserUtils.h>

#include <cctype>
#include <cstring>
#include <string>

namespace {
constexpr size_t ENTRY_STORAGE_CAPACITY = 64;
constexpr size_t MAX_ENTRIES = ENTRY_STORAGE_CAPACITY - 2;
constexpr size_t MAX_TITLE_CHARS = 160;
constexpr size_t MAX_AUTHOR_CHARS = 120;
constexpr size_t MAX_ID_CHARS = 128;
constexpr size_t MAX_LANGUAGE_CHARS = 16;
constexpr size_t MAX_SUMMARY_CHARS = 400;
constexpr size_t MAX_HREF_CHARS = 768;
constexpr size_t MAX_SEARCH_TEMPLATE_CHARS = 768;
constexpr size_t MAX_PAGE_URL_CHARS = 768;

// "en-US" -> "en", "EN" -> "en". Feeds are inconsistent about both region
// subtags and case, and a filter comparing raw values matches almost nothing.
std::string primaryLanguageSubtag(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (const char c : raw) {
    if (c == '-' || c == '_') break;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}
}  // namespace

OpdsParser::OpdsParser() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("OPDS", "Couldn't allocate memory for parser");
    return;
  }
  entries.reserve(ENTRY_STORAGE_CAPACITY);
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
}

OpdsParser::~OpdsParser() { destroyXmlParser(parser); }

size_t OpdsParser::write(uint8_t c) { return write(&c, 1); }

size_t OpdsParser::write(const uint8_t* xmlData, const size_t length) {
  if (errorOccured) return length;

  const char* currentPos = reinterpret_cast<const char*>(xmlData);
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    void* const buf = XML_GetBuffer(parser, toRead);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("OPDS", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return length;
    }

    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("OPDS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return length;
    }
    currentPos += toRead;
    remaining -= toRead;
  }
  return length;
}

void OpdsParser::flush() {
  if (errorOccured || !parser) return;
  if (XML_Parse(parser, nullptr, 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    destroyXmlParser(parser);
  }
}

bool OpdsParser::error() const { return errorOccured; }

void OpdsParser::clear() {
  entries.clear();
  searchTemplate.clear();
  subtitle.clear();
  searchDescriptionUrl.clear();
  nextPageUrl.clear();
  prevPageUrl.clear();
  currentEntry = OpdsEntry{};
  currentText.clear();
  inEntry = inTitle = inAuthor = inAuthorName = inId = inLanguage = inSummary = false;
  inSubtitle = false;
  coverIsThumbnail = false;
  collectCurrentEntry = false;
  feedTruncated = false;
}

std::vector<OpdsEntry> OpdsParser::getBooks() const {
  std::vector<OpdsEntry> books;
  for (const auto& entry : entries) {
    if (entry.type == OpdsEntryType::BOOK) books.push_back(entry);
  }
  return books;
}

const char* OpdsParser::findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void OpdsParser::assignBounded(std::string& target, const char* value, const size_t maxLen) {
  if (!value) {
    target.clear();
    return;
  }
  target.assign(value, strnlen(value, maxLen));
}

void OpdsParser::appendBounded(std::string& target, const char* value, const size_t len, const size_t maxLen) {
  if (target.size() >= maxLen) return;
  const size_t remaining = maxLen - target.size();
  target.append(value, len < remaining ? len : remaining);
}

void XMLCALL OpdsParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (strcmp(name, "entry") == 0 || strstr(name, ":entry") != nullptr) {
    self->inEntry = true;
    self->collectCurrentEntry = self->entries.size() < MAX_ENTRIES;
    self->feedTruncated = self->feedTruncated || !self->collectCurrentEntry;
    self->currentEntry = OpdsEntry{};
    self->currentText.clear();
    self->inTitle = self->inAuthor = self->inAuthorName = self->inId = self->inLanguage = false;
    self->inSummary = false;
    self->coverIsThumbnail = false;
    return;
  }

  if (strcmp(name, "link") == 0 || strstr(name, ":link") != nullptr) {
    const char* href = findAttribute(atts, "href");
    if (href) {
      const char* rel = findAttribute(atts, "rel");
      const char* type = findAttribute(atts, "type");

      if (rel && strcmp(rel, "search") == 0) {
        if (strstr(href, "{searchTerms}") != nullptr) {
          assignBounded(self->searchTemplate, href, MAX_SEARCH_TEMPLATE_CHARS);
        } else {
          // The spec's usual form points at an OpenSearch description document
          // rather than inlining the template, which is what Standard Ebooks,
          // Calibre-Web, Kavita and Komga all emit. Remember it so the caller
          // can fetch it; the parser itself cannot do I/O.
          assignBounded(self->searchDescriptionUrl, href, MAX_SEARCH_TEMPLATE_CHARS);
        }
      } else if (rel && strcmp(rel, "next") == 0 && !self->inEntry) {
        assignBounded(self->nextPageUrl, href, MAX_PAGE_URL_CHARS);
      } else if (rel && strcmp(rel, "previous") == 0 && !self->inEntry) {
        assignBounded(self->prevPageUrl, href, MAX_PAGE_URL_CHARS);
      }

      if (self->inEntry && self->collectCurrentEntry) {
        if (rel && type && strstr(rel, "opds-spec.org/acquisition") != nullptr &&
            strcmp(type, "application/epub+zip") == 0) {
          // Prefer plain EPUB links over derived formats when multiple
          // acquisition links are present for one entry.
          const bool isPlainEpub = strstr(href, ".epub") != nullptr || strstr(href, "/epub/") != nullptr;
          const bool alreadyHasPlainEpub = self->currentEntry.type == OpdsEntryType::BOOK &&
                                           (self->currentEntry.href.find(".epub") != std::string::npos ||
                                            self->currentEntry.href.find("/epub/") != std::string::npos);
          if (self->currentEntry.type != OpdsEntryType::BOOK || (isPlainEpub && !alreadyHasPlainEpub)) {
            self->currentEntry.type = OpdsEntryType::BOOK;
            assignBounded(self->currentEntry.href, href, MAX_HREF_CHARS);
          }
        } else if (rel && strstr(rel, "opds-spec.org/image") != nullptr && type && strncmp(type, "image/", 6) == 0) {
          // A feed may carry both, and the FULL image wins. The thumbnail used
          // to, on the reasoning that the detail screen draws small -- but
          // Gutenberg's thumbnail is 66x93, and scaled into a 200-wide box it
          // dithers to a black blob. Nothing upscales, so a too-small source
          // cannot be recovered; a too-large one costs only bytes.
          const bool isThumbnail = strstr(rel, "/thumbnail") != nullptr;
          if (!isThumbnail || self->currentEntry.coverHref.empty()) {
            assignBounded(self->currentEntry.coverHref, href, MAX_HREF_CHARS);
            self->coverIsThumbnail = isThumbnail;
          }
        } else if (type && strstr(type, "application/atom+xml") != nullptr) {
          if (self->currentEntry.type != OpdsEntryType::BOOK) {
            self->currentEntry.type = OpdsEntryType::NAVIGATION;
            assignBounded(self->currentEntry.href, href, MAX_HREF_CHARS);
          }
        }
      }
    }
  }

  // Feed-level <subtitle>, before the entry gate below: it never sits inside
  // an entry, so it has to be read here or not at all.
  if (!self->inEntry && (strcmp(name, "subtitle") == 0 || strstr(name, ":subtitle") != nullptr)) {
    self->inSubtitle = true;
    self->currentText.clear();
    return;
  }

  if (!self->inEntry || !self->collectCurrentEntry) return;

  if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
    self->inTitle = true;
    self->currentText.clear();
  } else if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
    self->inAuthor = true;
  } else if (self->inAuthor && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
    self->inAuthorName = true;
    self->currentText.clear();
  } else if (strcmp(name, "id") == 0 || strstr(name, ":id") != nullptr) {
    self->inId = true;
    self->currentText.clear();
  } else if (strcmp(name, "language") == 0 || strstr(name, ":language") != nullptr) {
    // Matches dc:language and dcterms:language alike; the suffix test covers
    // whichever prefix a feed happens to bind.
    self->inLanguage = true;
    self->currentText.clear();
  } else if (strcmp(name, "summary") == 0 || strstr(name, ":summary") != nullptr || strcmp(name, "content") == 0 ||
             strstr(name, ":content") != nullptr) {
    // Whichever the feed uses; summary wins when both appear because it is the
    // short form and the box on the detail screen is small.
    if (self->currentEntry.summary.empty()) {
      self->inSummary = true;
      self->currentText.clear();
    }
  }
}

void XMLCALL OpdsParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (strcmp(name, "entry") == 0 || strstr(name, ":entry") != nullptr) {
    if (self->collectCurrentEntry && !self->currentEntry.title.empty() && !self->currentEntry.href.empty()) {
      self->entries.push_back(self->currentEntry);
    }
    self->inEntry = false;
    self->collectCurrentEntry = false;
  } else if (self->inSubtitle && (strcmp(name, "subtitle") == 0 || strstr(name, ":subtitle") != nullptr)) {
    self->subtitle = utf8CollapseWhitespace(self->currentText);
    self->inSubtitle = false;
  } else if (self->inEntry) {
    if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
      if (self->inTitle) self->currentEntry.title = utf8CollapseWhitespace(self->currentText);
      self->inTitle = false;
    } else if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
      self->inAuthor = false;
    } else if (self->inAuthorName && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
      self->currentEntry.author = utf8CollapseWhitespace(self->currentText);
      self->inAuthorName = false;
    } else if (strcmp(name, "id") == 0 || strstr(name, ":id") != nullptr) {
      if (self->inId) self->currentEntry.id = self->currentText;
      self->inId = false;
    } else if (strcmp(name, "language") == 0 || strstr(name, ":language") != nullptr) {
      if (self->inLanguage) self->currentEntry.language = primaryLanguageSubtag(self->currentText);
      self->inLanguage = false;
    } else if (self->inSummary && (strcmp(name, "summary") == 0 || strstr(name, ":summary") != nullptr ||
                                   strcmp(name, "content") == 0 || strstr(name, ":content") != nullptr)) {
      self->currentEntry.summary = utf8CollapseWhitespace(self->currentText);
      self->inSummary = false;
    }
  }
}

void XMLCALL OpdsParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<OpdsParser*>(userData);
  if (self->inSubtitle) {
    appendBounded(self->currentText, s, len, MAX_TITLE_CHARS);
    return;
  }
  if (!self->collectCurrentEntry) return;
  if (self->inTitle) {
    appendBounded(self->currentText, s, len, MAX_TITLE_CHARS);
  } else if (self->inAuthorName) {
    appendBounded(self->currentText, s, len, MAX_AUTHOR_CHARS);
  } else if (self->inId) {
    appendBounded(self->currentText, s, len, MAX_ID_CHARS);
  } else if (self->inLanguage) {
    appendBounded(self->currentText, s, len, MAX_LANGUAGE_CHARS);
  } else if (self->inSummary) {
    appendBounded(self->currentText, s, len, MAX_SUMMARY_CHARS);
  }
}
