#pragma once

// The five CJK faces, and the rule that only one is resident at a time.
//
// Mario's Anki template randomises the hanzi across SimSun, SimHei,
// MicrosoftYaHei, KaiTi and FangSong, and the whole point of that is to stop
// him locking onto one letterform -- he learned to read in a single face and
// found the others hard afterwards. So the device randomises too, per card,
// exactly as the template does. See tools_local/study/make_fonts.py for how the
// .cpfont files are built from Anki's own media folder.
//
// **One family at a time.** A subset CJK face carries ~2000 Unicode intervals,
// and SdCardFont holds that table in RAM: 24KB per file, 48KB for a family's
// two sizes. Holding all five would be 240KB, which is most of the budget on a
// device whose reader also wants memory. Switching costs one load of the
// interval tables, which is cheap next to the 300ms the panel spends on a
// partial refresh -- so the card that is about to be drawn pays it, and the
// four families that are not about to be drawn cost nothing.
//
// These live under /study/fonts/ rather than /.fonts/ on purpose. The registry
// scans /.fonts and /fonts, and a family installed there appears in
// Settings > Reader > Font Family -- where these would be a trap, because they
// are subset to the deck's own 2769 glyphs and would show boxes for every
// character a real Chinese book contains that this deck does not.

#include <SdCardFontManager.h>

#include <cstdint>

class GfxRenderer;

namespace study {

class StudyFonts {
 public:
  static constexpr int kFamilyCount = 5;

  // The five families, in the order Mario's card template lists them.
  static const char* familyName(int index);

  // Load one family's two sizes, unloading whatever was resident. Returns
  // false and leaves nothing loaded if the files are missing, which is the
  // normal state before the font pipeline has been run onto the card.
  bool load(GfxRenderer& renderer, int familyIndex);

  // Load `preferred`, or the next family after it that is actually on the
  // card. Returns the index loaded, or -1 if none of the five are there.
  //
  // The five faces are a preference, not a requirement: the card decides which
  // one it wants and any of them will do if that one is missing. Without this,
  // a card whose turn landed on an absent face drew its hanzi in the UI font,
  // which has no CJK at all, so the headword came out as tofu while the pinyin
  // and the gloss beside it were fine. A partial font install is the normal
  // state of a card someone is still setting up, and the browser demo ships
  // one face on purpose.
  int loadPreferred(GfxRenderer& renderer, int preferred);

  void unload(GfxRenderer& renderer);

  // Pull this card's glyphs off the SD card before drawing them. Without this
  // every glyph is fetched during the draw, one seek at a time.
  void prewarm(GfxRenderer& renderer, const char* headword, const char* sentence) const;

  bool loaded() const { return headwordFontId_ != 0 && sentenceFontId_ != 0; }
  int headwordFontId() const { return headwordFontId_; }
  int sentenceFontId() const { return sentenceFontId_; }
  int familyIndex() const { return familyIndex_; }
  const char* familyName() const { return familyName(familyIndex_); }

 private:
  SdCardFontManager manager_;
  int headwordFontId_ = 0;
  int sentenceFontId_ = 0;
  int familyIndex_ = -1;
};

// Point sizes the pipeline builds. These are the stock converter's units,
// which are NOT pixels: it renders about 2.38x larger, so 50 is a ~119px line
// and 17 is a ~40px one. Changing either means rebuilding every face.
inline constexpr uint8_t kHeadwordPointSize = 50;
inline constexpr uint8_t kSentencePointSize = 17;

}  // namespace study
