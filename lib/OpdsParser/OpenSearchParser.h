#pragma once
#include <expat.h>

#include <cstddef>
#include <string>

/**
 * Parser for OpenSearch description documents.
 *
 * OPDS feeds normally advertise search by pointing `rel="search"` at one of
 * these rather than inlining the template, so without this the search icon
 * never appears for Standard Ebooks, Calibre-Web, Kavita or Komga.
 *
 * Only the `template` attribute of `<Url>` elements is of interest. An Atom
 * result type is preferred over any other, since the browser feeds the response
 * straight back into OpdsParser.
 *
 * Deliberately not a Print/Stream like OpdsParser: these documents are a few
 * hundred bytes and the caller already has them in a string, so a plain feed()
 * avoids the Arduino dependency and lets this be covered by a host test.
 */
class OpenSearchParser final {
 public:
  OpenSearchParser();
  ~OpenSearchParser();

  OpenSearchParser(const OpenSearchParser&) = delete;
  OpenSearchParser& operator=(const OpenSearchParser&) = delete;

  void feed(const char* data, size_t length);
  void finish();

  bool error() const { return errorOccured; }

  /** Best template found, or empty when the document advertised none. */
  const std::string& getSearchTemplate() const { return searchTemplate; }

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);

  XML_Parser parser = nullptr;
  std::string searchTemplate;
  // Rank of the template currently held; a higher-ranked <Url> replaces it.
  // Plain Atom is NOT good enough: Standard Ebooks offers both
  // feeds/atom/all (a syndication feed, no acquisition links) and
  // feeds/opds/all, and picking the former yields a catalog whose rows cannot
  // be downloaded. A non-Atom template is still kept over nothing at all.
  int templateRank = 0;
  bool errorOccured = false;
};
