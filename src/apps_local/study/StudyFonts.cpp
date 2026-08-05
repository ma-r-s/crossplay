#include "StudyFonts.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFontRegistry.h>

#include <cstdio>

namespace study {

namespace {

// The order Mario's HSK template lists them in `var fonts = [...]`.
constexpr const char* kFamilies[StudyFonts::kFamilyCount] = {
    "SimSun", "SimHei", "MicrosoftYaHei", "KaiTi", "FangSong",
};

constexpr const char* kFontRoot = "/study/fonts";

// Build the family descriptor by hand rather than going through
// SdCardFontRegistry. The registry only scans /.fonts and /fonts, and putting
// these there would list them as reading fonts -- see the header for why that
// is a trap. SdCardFontFamilyInfo is a plain struct, so this costs nothing.
SdCardFontFamilyInfo describe(const char* family, const uint8_t pointSize) {
  SdCardFontFamilyInfo info;
  info.name = family;
  char path[128];
  std::snprintf(path, sizeof(path), "%s/%s/%s_%u.cpfont", kFontRoot, family, family, pointSize);
  info.files.push_back(SdCardFontFileInfo{path, pointSize, 0});
  return info;
}

}  // namespace

const char* StudyFonts::familyName(const int index) {
  if (index < 0 || index >= kFamilyCount) return "";
  return kFamilies[index];
}

bool StudyFonts::load(GfxRenderer& renderer, const int familyIndex) {
  if (familyIndex < 0 || familyIndex >= kFamilyCount) return false;
  if (familyIndex == familyIndex_ && loaded()) return true;

  unload(renderer);
  const char* family = kFamilies[familyIndex];

  // loadFamilyExtraSize is the additive one: two calls leave both sizes
  // resident. loadFamily would unload the first when loading the second.
  const int headword = manager_.loadFamilyExtraSize(describe(family, kHeadwordPointSize), renderer, kHeadwordPointSize);
  const int sentence = manager_.loadFamilyExtraSize(describe(family, kSentencePointSize), renderer, kSentencePointSize);
  if (headword == 0 || sentence == 0) {
    LOG_ERR("STUDY", "Font family %s missing under %s (headword=%d sentence=%d)", family, kFontRoot, headword,
            sentence);
    unload(renderer);
    return false;
  }

  headwordFontId_ = headword;
  sentenceFontId_ = sentence;
  familyIndex_ = familyIndex;
  LOG_DBG("STUDY", "Loaded %s: headword id=%d sentence id=%d", family, headword, sentence);
  return true;
}

void StudyFonts::unload(GfxRenderer& renderer) {
  if (headwordFontId_ == 0 && sentenceFontId_ == 0) return;
  manager_.unloadAll(renderer);
  headwordFontId_ = 0;
  sentenceFontId_ = 0;
  familyIndex_ = -1;
}

void StudyFonts::prewarm(GfxRenderer& renderer, const char* headword, const char* sentence) const {
  FontCacheManager* cache = renderer.getFontCacheManager();
  if (cache == nullptr || !loaded()) return;
  // The headword also appears inside its own example sentence, so both cuts
  // want it. Two calls, one per size, because the cache is keyed by font id.
  if (headword != nullptr && *headword != '\0') {
    cache->prewarmCache(headwordFontId_, headword);
  }
  if (sentence != nullptr && *sentence != '\0') {
    cache->prewarmCache(sentenceFontId_, sentence);
  }
}

}  // namespace study
