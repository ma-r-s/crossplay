// The boards where PsramMallocScope is live (built with FREEINK_DEVICE_X4PRO): the layout goes to PSRAM, so its heap
// floors must not be judged against internal DRAM.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <esp_heap_caps.h>
#include <gtest/gtest.h>

#include <filesystem>

#include "src/util/DictHtmlPages.h"

namespace {
HalMemory::HeapStats defaultHeap;
constexpr size_t AMPLE = 8 * 1024 * 1024;
}  // namespace

HalMemory::HeapStats HalMemory::getDefaultHeap() { return defaultHeap; }

namespace {

class DictionaryHtmlPagesPsramTest : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;

  void SetUp() override {
    // What an X4 Pro reports with Wi-Fi up.
    ESP.freeHeap = 23 * 1024;
    ESP.maxAllocHeap = 13 * 1024;
    defaultHeap = {AMPLE, AMPLE, AMPLE, AMPLE};
    extmemThreshold = 4096;
    renderer.sdFont = true;
    Storage.dictionaryTempPath = (std::filesystem::temp_directory_path() /
                                  (std::string("crosspoint-dicthtml-psram-") +
                                   ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".tmp"))
                                     .string();
  }
  void TearDown() override {
    std::filesystem::remove(Storage.dictionaryTempPath);
    Storage.dictionaryTempPath.clear();
    ESP = {};
  }
};

TEST_F(DictionaryHtmlPagesPsramTest, LowInternalHeapStillBuildsStyledPagesInPsram) {
  size_t thresholdDuringLayout = 0;
  renderer.onFontPrepare = [&] { thresholdDuringLayout = extmemThreshold; };
  ASSERT_TRUE(buildDictionaryHtmlPages(renderer, "<p>one <b>two</b> three</p>", 160, 64, pages));
  EXPECT_EQ(pages.size(), 1u);
  EXPECT_EQ(thresholdDuringLayout, 16u);
  EXPECT_EQ(extmemThreshold, 4096u);
}

TEST_F(DictionaryHtmlPagesPsramTest, LowDefaultHeapAtEntryAvoidsStartingParser) {
  // PSRAM absent: the default heap is internal DRAM alone.
  defaultHeap = {23 * 1024, 320 * 1024, 23 * 1024, 13 * 1024};
  EXPECT_FALSE(buildDictionaryHtmlPages(renderer, "<p>one two three</p>", 160, 16, pages));
  EXPECT_TRUE(pages.empty());
  EXPECT_EQ(renderer.fontPreparations, 0u);
  EXPECT_EQ(extmemThreshold, 4096u);
}

TEST_F(DictionaryHtmlPagesPsramTest, DefaultHeapRunningLowStopsParsingAndRestoresThreshold) {
  std::string definition;
  for (int i = 0; i < 100; ++i) definition += "<p>one two three four five six seven eight</p>";
  renderer.onFontPrepare = [] { defaultHeap.freeBytes = 30 * 1024; };
  EXPECT_FALSE(buildDictionaryHtmlPages(renderer, definition, 160, 16, pages));
  EXPECT_TRUE(pages.empty());
  EXPECT_EQ(renderer.fontPreparations, 1u);
  EXPECT_EQ(extmemThreshold, 4096u);
}

// Feed descriptions carry HTML entity names, which would otherwise fail the whole layout.
TEST_F(DictionaryHtmlPagesPsramTest, HtmlEntityNamesKeepStyledPages) {
  for (const char* definition :
       {"<p>one&nbsp;<b>two</b> &mdash; three</p>", "<p>&amp; &lt; &gt; &quot; &#160; &#x2014;</p>",
        "<p>an &unknownname; stays</p>", "<p>&LT;b&GT; &AMP;</p>",
        // expat checks references inside attribute values too, where a quote must not end the value.
        "<p title=\"a&nbsp;b &mdash; &unknownname;\">one</p>",
        "<p title=\"&quot;two&quot;\" lang='&apos;&amp;'>two</p>", "<p>one<br data-x=\"&nbsp;\">two</p>",
        "<p><a href=\"https://example.com/book?id=1&edition=2\">here</a></p>", "<p title=\"Tom & Jerry\">one</p>",
        "<p title=\"a<b\">one</p>"}) {
    SCOPED_TRACE(definition);
    pages.clear();
    EXPECT_TRUE(buildDictionaryHtmlPages(renderer, definition, 400, 600, pages));
    EXPECT_FALSE(pages.empty());
  }
}

}  // namespace
