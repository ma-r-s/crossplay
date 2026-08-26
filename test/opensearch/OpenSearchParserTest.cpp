#include <gtest/gtest.h>

#include <string>

#include "OpenSearchParser.h"

namespace {

std::string parse(const std::string& document) {
  OpenSearchParser parser;
  parser.feed(document.data(), document.size());
  parser.finish();
  return parser.getSearchTemplate();
}

// The document standardebooks.org actually serves, kept verbatim. Every
// interesting case is already in it: a self link with no {searchTerms}, an HTML
// template listed before any Atom one, a plain Atom feed, and the OPDS
// acquisition feed that is the only correct answer.
constexpr const char* STANDARD_EBOOKS = R"(<?xml version="1.0" encoding="utf-8"?>
<OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/">
  <ShortName>Standard Ebooks</ShortName>
  <Url type="application/opensearchdescription+xml" rel="self" template="https://standardebooks.org/opensearch"/>
  <Url type="text/html" template="https://standardebooks.org/ebooks?query={searchTerms}&amp;per-page={count}&amp;page={startPage}"/>
  <Url type="application/xhtml+xml" template="https://standardebooks.org/ebooks?query={searchTerms}&amp;per-page={count}&amp;page={startPage}"/>
  <Url type="application/atom+xml" template="https://standardebooks.org/feeds/atom/all?query={searchTerms}&amp;per-page={count}&amp;page={startPage}"/>
  <Url type="application/atom+xml;profile=opds-catalog;kind=acquisition" template="https://standardebooks.org/feeds/opds/all?query={searchTerms}&amp;per-page={count}&amp;page={startPage}"/>
  <Url type="application/opds+json" template="https://standardebooks.org/feeds/opds/all?query={searchTerms}"/>
</OpenSearchDescription>)";

TEST(OpenSearchParser, PicksTheOpdsAcquisitionFeedNotThePlainAtomOne) {
  // feeds/atom/all is a syndication feed with no acquisition links: choosing it
  // yields a catalog whose every row is un-downloadable, which is exactly the
  // bug this ranking exists to prevent.
  EXPECT_EQ(parse(STANDARD_EBOOKS),
            "https://standardebooks.org/feeds/opds/all?query={searchTerms}&per-page={count}&page={startPage}");
}

TEST(OpenSearchParser, PrefersOpdsEvenWhenPlainAtomComesLater) {
  const std::string doc = R"(<OpenSearchDescription>
    <Url type="application/atom+xml;profile=opds-catalog" template="/opds?q={searchTerms}"/>
    <Url type="application/atom+xml" template="/atom?q={searchTerms}"/>
  </OpenSearchDescription>)";
  EXPECT_EQ(parse(doc), "/opds?q={searchTerms}");
}

TEST(OpenSearchParser, IgnoresUrlsWithoutSearchTerms) {
  const std::string doc = R"(<OpenSearchDescription>
    <Url type="application/opensearchdescription+xml" rel="self" template="/opensearch"/>
  </OpenSearchDescription>)";
  EXPECT_TRUE(parse(doc).empty());
}

TEST(OpenSearchParser, FallsBackToANonAtomTemplateRatherThanNothing) {
  // A catalog offering only an HTML search still beats showing no search icon.
  const std::string doc = R"(<OpenSearchDescription>
    <Url type="text/html" template="/search?q={searchTerms}"/>
  </OpenSearchDescription>)";
  EXPECT_EQ(parse(doc), "/search?q={searchTerms}");
}

TEST(OpenSearchParser, HandlesNamespacePrefixedElements) {
  const std::string doc = R"(<os:OpenSearchDescription xmlns:os="http://a9.com/-/spec/opensearch/1.1/">
    <os:Url type="application/atom+xml" template="/feed?q={searchTerms}"/>
  </os:OpenSearchDescription>)";
  EXPECT_EQ(parse(doc), "/feed?q={searchTerms}");
}

TEST(OpenSearchParser, AUrlWithoutATypeStillCounts) {
  const std::string doc = R"(<OpenSearchDescription>
    <Url template="/search?q={searchTerms}"/>
  </OpenSearchDescription>)";
  EXPECT_EQ(parse(doc), "/search?q={searchTerms}");
}

TEST(OpenSearchParser, MalformedDocumentYieldsNoTemplateAndDoesNotCrash) {
  OpenSearchParser parser;
  const std::string doc = "<OpenSearchDescription><Url template=\"/x?q={searchTerms}\"";
  parser.feed(doc.data(), doc.size());
  parser.finish();
  EXPECT_TRUE(parser.error());
  EXPECT_TRUE(parser.getSearchTemplate().empty());
}

TEST(OpenSearchParser, EmptyDocumentIsHandled) {
  OpenSearchParser parser;
  parser.finish();
  EXPECT_TRUE(parser.getSearchTemplate().empty());
}

}  // namespace
