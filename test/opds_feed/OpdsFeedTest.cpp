#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "OpdsParser.h"

namespace {

std::vector<OpdsEntry> parse(const std::string& feed) {
  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(feed.data()), feed.size());
  parser.flush();
  return parser.getEntries();
}

std::string feedAround(const std::string& entryBody) {
  return R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:dcterms="http://purl.org/dc/terms/">
  <title>Test</title>
  <entry>)" +
         entryBody + R"(</entry>
</feed>)";
}

constexpr const char* EPUB_LINK =
    R"(<link rel="http://opds-spec.org/acquisition" href="/dl/x.epub" type="application/epub+zip"/>)";

TEST(OpdsFeed, ReadsDctermsLanguage) {
  const auto entries =
      parse(feedAround(std::string("<title>A Book</title><dcterms:language>en</dcterms:language>") + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].language, "en");
  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
}

TEST(OpdsFeed, NormalizesRegionSubtagAndCase) {
  // Feeds are wildly inconsistent here; a filter comparing raw values would
  // match almost nothing.
  const auto entries =
      parse(feedAround(std::string("<title>A Book</title><dcterms:language>EN-US</dcterms:language>") + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].language, "en");
}

TEST(OpdsFeed, MissingLanguageStaysEmpty) {
  // Empty must read as "keep" downstream: most catalogs tag nothing, and a
  // filter that hides untagged books hides the catalog.
  const auto entries = parse(feedAround(std::string("<title>A Book</title>") + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_TRUE(entries[0].language.empty());
}

TEST(OpdsFeed, AnAcquisitionTypeThatIsNotExactlyEpubZipDropsTheEntry) {
  // The type match is a strcmp, so any parameter on it -- ";charset=utf-8",
  // a profile, anything -- means no href is recorded and the entry is
  // discarded rather than shown as a row that does nothing when tapped.
  // Worth knowing when writing a feed: the book simply vanishes from the list.
  const auto entries = parse(feedAround(
      R"(<title>A Book</title><link rel="http://opds-spec.org/acquisition" href="/dl/x.epub" type="application/epub+zip;charset=utf-8"/>)"));
  EXPECT_TRUE(entries.empty());
}

TEST(OpdsFeed, NavigationEntriesAreDetected) {
  const auto entries = parse(feedAround(
      R"(<title>Subsection</title><link rel="subsection" href="/opds/x" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>)"));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].type, OpdsEntryType::NAVIGATION);
}

TEST(OpdsFeed, InlineSearchTemplateIsPickedUp) {
  const std::string feed = R"(<feed xmlns="http://www.w3.org/2005/Atom">
    <link rel="search" type="application/atom+xml" href="/opds/search?q={searchTerms}"/>
  </feed>)";
  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(feed.data()), feed.size());
  parser.flush();
  EXPECT_EQ(parser.getSearchTemplate(), "/opds/search?q={searchTerms}");
  EXPECT_TRUE(parser.getSearchDescriptionUrl().empty());
}

TEST(OpdsFeed, DescriptionDocumentIsRememberedSeparately) {
  // The spec-correct shape: no inline template, so the browser has to go and
  // fetch the description document before a search icon can appear.
  const std::string feed = R"(<feed xmlns="http://www.w3.org/2005/Atom">
    <link rel="search" type="application/opensearchdescription+xml" href="/opensearch"/>
  </feed>)";
  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(feed.data()), feed.size());
  parser.flush();
  EXPECT_TRUE(parser.getSearchTemplate().empty());
  EXPECT_EQ(parser.getSearchDescriptionUrl(), "/opensearch");
}

TEST(OpdsFeed, PrefersTheFullCoverOverTheThumbnail) {
  // The reverse of what this asserted until 7e72b5e9. Gutenberg's thumbnail is
  // 66x93; scaled into the detail screen's 200-wide box it dithers to a black
  // blob, and nothing upscales, so a too-small source cannot be recovered
  // while a too-large one costs only bytes. The rule is in OpdsParser.cpp.
  const auto entries =
      parse(feedAround(std::string("<title>A Book</title>") +
                       R"(<link rel="http://opds-spec.org/image" href="/big.jpg" type="image/jpeg"/>)"
                       R"(<link rel="http://opds-spec.org/image/thumbnail" href="/thumb.jpg" type="image/jpeg"/>)" +
                       EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].coverHref, "/big.jpg");
}

TEST(OpdsFeed, TheFullCoverWinsEvenWhenTheThumbnailComesFirst) {
  const auto entries =
      parse(feedAround(std::string("<title>A Book</title>") +
                       R"(<link rel="http://opds-spec.org/image/thumbnail" href="/thumb.jpg" type="image/jpeg"/>)"
                       R"(<link rel="http://opds-spec.org/image" href="/big.jpg" type="image/jpeg"/>)" +
                       EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].coverHref, "/big.jpg");
}

TEST(OpdsFeed, AThumbnailIsUsedWhenItIsTheOnlyImage) {
  // Still better than no cover at all, and the flag says which it is.
  const auto entries = parse(feedAround(
      std::string("<title>A Book</title>") +
      R"(<link rel="http://opds-spec.org/image/thumbnail" href="/thumb.jpg" type="image/jpeg"/>)" + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].coverHref, "/thumb.jpg");
}

TEST(OpdsFeed, FullImageIsUsedWhenNoThumbnailExists) {
  const auto entries =
      parse(feedAround(std::string("<title>A Book</title>") +
                       R"(<link rel="http://opds-spec.org/image" href="/big.jpg" type="image/jpeg"/>)" + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].coverHref, "/big.jpg");
}

TEST(OpdsFeed, SummaryIsRead) {
  const auto entries = parse(feedAround(
      std::string("<title>A Book</title><summary>A towel is about the most massively useful thing.</summary>") +
      EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].summary, "A towel is about the most massively useful thing.");
}

TEST(OpdsFeed, SummaryWinsOverContentWhenBothArePresent) {
  const auto entries = parse(feedAround(
      std::string("<title>A Book</title><summary>Short.</summary><content>Long version.</content>") + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].summary, "Short.");
}

TEST(OpdsFeed, ContentIsUsedWhenThereIsNoSummary) {
  const auto entries =
      parse(feedAround(std::string("<title>A Book</title><content>Only content here.</content>") + EPUB_LINK));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].summary, "Only content here.");
}

}  // namespace
