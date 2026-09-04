#include "OpenSearchParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cstring>

namespace {
constexpr size_t MAX_TEMPLATE_CHARS = 768;

// 3 = an OPDS acquisition feed, 2 = plain Atom, 1 = anything else.
int rankType(const char* type) {
  if (type == nullptr) return 1;
  if (strstr(type, "atom+xml") == nullptr) return 1;
  return strstr(type, "opds-catalog") != nullptr ? 3 : 2;
}

// Expat hands attributes over as a flat name/value array.
const char* findAttribute(const XML_Char** atts, const char* name) {
  for (size_t i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}
}  // namespace

OpenSearchParser::OpenSearchParser() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("OPDS", "OpenSearch: couldn't allocate parser");
    return;
  }
  XML_SetUserData(parser, this);
  XML_SetStartElementHandler(parser, startElement);
}

OpenSearchParser::~OpenSearchParser() { destroyXmlParser(parser); }

void OpenSearchParser::feed(const char* xmlData, const size_t length) {
  if (errorOccured || !parser) return;

  const char* currentPos = xmlData;
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    void* const buf = XML_GetBuffer(parser, toRead);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("OPDS", "OpenSearch: couldn't allocate buffer");
      destroyXmlParser(parser);
      return;
    }

    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("OPDS", "OpenSearch: parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return;
    }

    currentPos += toRead;
    remaining -= toRead;
  }
}

void OpenSearchParser::finish() {
  if (errorOccured || !parser) return;
  if (XML_ParseBuffer(parser, 0, 1) == XML_STATUS_ERROR) {
    errorOccured = true;
    LOG_DBG("OPDS", "OpenSearch: parse error at end of document");
  }
}

void XMLCALL OpenSearchParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* const self = static_cast<OpenSearchParser*>(userData);

  // Namespaced documents are common, so match the local name too.
  if (strcmp(name, "Url") != 0 && strstr(name, ":Url") == nullptr) return;

  const char* const tmpl = findAttribute(atts, "template");
  if (!tmpl || strstr(tmpl, "{searchTerms}") == nullptr) return;

  const int rank = rankType(findAttribute(atts, "type"));
  if (self->searchTemplate.empty() || rank > self->templateRank) {
    self->searchTemplate.assign(tmpl, strnlen(tmpl, MAX_TEMPLATE_CHARS));
    self->templateRank = rank;
  }
}
