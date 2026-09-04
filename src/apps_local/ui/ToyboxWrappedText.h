#pragma once

// A long document, wrapped once and read from many times.
//
// ---------------------------------------------------------------------------
// The bug this exists for.
//
// A reader drawing one page of a long article used to wrap the WHOLE article
// twice on every paint: once through fui::textAreaMeasure(), for the line
// count the pager needs, and once inside fui::textArea(), which walks from
// byte zero to find the twenty lines it is going to draw. A wrap is not cheap
// -- it asks the font for the width of every candidate prefix, so it costs
// bytes x line length -- and both walks are on the render path. Mario reported
// the shape of it from real use before anyone measured it: a long article
// takes a long time to OPEN, and every page turn pays exactly the same again.
//
// So: wrap once per opening, keep a sparse index of where the lines start, and
// let a page turn walk only the window it draws.
//
// ---------------------------------------------------------------------------
// The dangerous half is not the speed, it is the staleness.
//
// The line count is not decoration. Instapaper's reading position is the top
// line over the total and that number goes up to somebody's real account. A
// count kept from a wrap that no longer describes the panel -- a rotation, a
// bigger reading size, a different cut installed off the SD card -- would send
// a wrong position, and there would be nothing on screen to say so. A cache
// that goes stale here is worse than the slowness it removes.
//
// Two layers stop it, and neither is a list of things somebody thought of.
//
// 1. THE KEY IS PROBED, NOT ENUMERATED. The wrap reads the outside world
//    through exactly one door: target.measureText(style.font, ., style). So
//    the key fingerprints THAT FUNCTION, asked about THIS DOCUMENT'S OWN
//    ALPHABET -- every distinct character the document is written in, measured
//    one at a time, and then all of them together so a change in kerning moves
//    it too. Anything that changes what the door returns changes the key,
//    including causes this file has never heard of: a font size setting, a cut
//    swapped in at runtime, a TextStyle field the SDK grows next year. Keying
//    on `font` and `width` would have been the enumeration, and enumeration is
//    what fails silently here.
//
//    The alphabet is collected when the document is wrapped, not on every
//    paint, because the text cannot change without the content hash changing
//    -- and that is in the key too.
//
// 2. THE INDEX CHECKS ITSELF AGAINST THE DOCUMENT. Drawing a window re-wraps
//    it from the checkpoint below it, so the walk that draws is also a walk
//    that can DISAGREE: when the lines between two checkpoints stop coming out
//    at the count the index recorded, the index is wrong about the world and
//    is thrown away before anything is drawn. It costs nothing, because the
//    window had to be walked anyway.
//
//    What layer 2 covers is the window, which is what is on the panel. It
//    cannot see a divergence elsewhere in the document, so it narrows the gap
//    left by layer 1 rather than closing it. Both layers, stated plainly, are
//    the honest position: this is very hard to make stale and it is not
//    provably impossible.
//
// Disagreement therefore costs a rebuild, never a wrong page. That is the
// property to preserve if this is ever changed.
// ---------------------------------------------------------------------------

#include <FreeInkUI.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace toybox {

namespace fui = freeink::ui;

class WrappedText {
 public:
  // Total visual lines of `text` wrapped to `width`, wrapping only when the
  // cached answer no longer describes this target, style, width or text.
  uint32_t lineCount(const fui::DrawTarget& target, const int16_t width, const char* text,
                     const fui::TextStyle& style) {
    ensure(target, width, text, style);
    return lineCount_;
  }

  // The window of lines starting at `topLine`, drawn into `rect` exactly as
  // fui::textArea() would draw them -- same wrap, same single-line style, same
  // spacing -- but walking only the lines around the window instead of the
  // whole document.
  void draw(fui::DrawTarget& target, const fui::Rect rect, const char* text, const fui::TextStyle& style,
            const uint32_t topLine) {
    if (rect.empty()) return;
    if (text == nullptr) text = "";
    const int16_t lh = target.lineHeight(style.font);
    if (lh <= 0) return;
    const uint16_t visible = static_cast<uint16_t>(rect.height / lh);
    if (visible == 0) return;

    fui::TextStyle lineStyle = style;
    lineStyle.maxLines = 1;
    lineStyle.align = fui::TextAlign::Left;

    // Twice at most: once as the index stands, and if the window disagrees
    // with it, once more against a freshly built one. A third round would mean
    // the wrap is not a function of its inputs, which is not a thing to loop
    // over on a render path.
    for (int attempt = 0; attempt < 2; ++attempt) {
      ensure(target, rect.width, text, style);
      if (lineCount_ == 0) return;
      uint32_t firstLine = 0;
      uint32_t expected = 0;
      if (!windowFor(text, topLine, visible, firstLine, expected)) return;

      // Walked and COLLECTED, then drawn -- not drawn as it is walked. Layer 2
      // can only reject the window after it has seen all of it, and a version
      // of this that drew inside the walk had already put the stale lines on
      // the panel by the time it found out, so the rebuilt ones landed on top
      // of them. What a check finds has to still be preventable when it finds
      // it.
      spans_.clear();
      uint32_t produced = 0;
      fui::textAreaWalk(target, rect.width, slice_.c_str(), style,
                        [&](const uint32_t idx, const fui::TextAreaLine& ln) {
                          ++produced;
                          const uint32_t line = firstLine + idx;
                          if (line < topLine || line >= topLine + visible) return;
                          spans_.push_back(Span{ln.start, ln.len});
                        });

      // On the last attempt the index was just built from this very target, so
      // a disagreement there is impossible unless the wrap is not a function
      // of its inputs. Draw anyway rather than return: a reader who is handed
      // a blank page has been handed a crash.
      if (produced != expected && attempt == 0) {
        key_.built = false;
        continue;
      }
      char buf[224];
      for (size_t i = 0; i < spans_.size(); ++i) {
        const uint16_t n = spans_[i].len < 220 ? spans_[i].len : 220;
        std::memcpy(buf, slice_.c_str() + spans_[i].start, n);
        buf[n] = '\0';
        const int16_t y = static_cast<int16_t>(rect.y + i * lh);
        target.text(fui::Rect{rect.x, y, rect.width, lh}, buf, lineStyle);
      }
      return;
    }
  }

  // How many times the document has actually been wrapped. The regression
  // tests are about this number: once per opening is the fix, once per paint
  // is the bug.
  uint32_t wraps() const { return wraps_; }

 private:
  // One checkpoint every kStride lines. Sparse because a feature-length
  // article is thousands of lines and this runs on a microcontroller: at this
  // stride a 64KB read costs a few hundred bytes, and a window walk is bounded
  // by the stride plus the lines on screen rather than by the document.
  static constexpr uint32_t kStride = 16;
  // How many distinct characters of the document the key probes. Latin prose
  // uses well under this. A document with more (a CJK article, which this fork
  // has no letters for anyway) is probed on the first kAlphabet it is written
  // in, which is a weaker fingerprint and the reason layer 2 exists.
  static constexpr uint16_t kAlphabet = 128;

  struct Key {
    uint32_t textHash = 0;
    uint32_t textLen = 0;
    uint32_t metrics = 0;
    int16_t width = 0;
    bool built = false;
  };

  static uint32_t hashBytes(uint32_t h, const void* data, const size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
      h ^= p[i];
      h *= 16777619u;
    }
    return h;
  }

  // Every distinct character the document is written in, as packed UTF-8
  // sequences. Collected once per wrap; see layer 1 above for why the key
  // probes these rather than a fixed set somebody chose.
  void buildAlphabet(const char* text, const uint32_t len) {
    std::memset(ascii_, 0, sizeof(ascii_));
    wideCount_ = 0;
    for (uint32_t i = 0; i < len;) {
      const uint8_t lead = static_cast<uint8_t>(text[i]);
      if (lead < 0x80) {
        ascii_[lead >> 5] |= 1u << (lead & 31);
        ++i;
        continue;
      }
      uint32_t n = 1;
      if ((lead & 0xE0) == 0xC0)
        n = 2;
      else if ((lead & 0xF0) == 0xE0)
        n = 3;
      else if ((lead & 0xF8) == 0xF0)
        n = 4;
      if (i + n > len) n = len - i;
      uint32_t packed = 0;
      for (uint32_t b = 0; b < n && b < 4; ++b)
        packed |= static_cast<uint32_t>(static_cast<uint8_t>(text[i + b])) << (b * 8);
      if (wideCount_ < kAlphabet) {
        bool seen = false;
        for (uint16_t k = 0; k < wideCount_; ++k) {
          if (wide_[k] == packed) {
            seen = true;
            break;
          }
        }
        if (!seen) wide_[wideCount_++] = packed;
      }
      i += n;
    }
  }

  // A fingerprint of the one function a wrap reads the world through, asked
  // about the alphabet above. Cheap: one measurement per distinct character
  // plus one for all of them, against tens of thousands for a wrap.
  uint32_t metricsOf(const fui::DrawTarget& target, const fui::TextStyle& style) const {
    uint32_t h = 2166136261u;
    const int16_t lh = target.lineHeight(style.font);
    h = hashBytes(h, &lh, sizeof(lh));

    char run[224];
    size_t runLen = 0;
    const auto probe = [&](const char* seq, const size_t n) {
      char one[8];
      std::memcpy(one, seq, n);
      one[n] = '\0';
      const int16_t w = target.measureText(style.font, one, style).width;
      h = hashBytes(h, &w, sizeof(w));
      // And the same characters run together, which moves when kerning does
      // while every individual advance stays exactly where it was.
      if (runLen + n >= sizeof(run) - 1) {
        run[runLen] = '\0';
        const int16_t rw = target.measureText(style.font, run, style).width;
        h = hashBytes(h, &rw, sizeof(rw));
        runLen = 0;
      }
      std::memcpy(run + runLen, seq, n);
      runLen += n;
    };

    for (int c = 0; c < 128; ++c) {
      if ((ascii_[c >> 5] & (1u << (c & 31))) == 0) continue;
      if (c == '\n' || c == '\0') continue;  // never measured by the wrap
      const char seq[1] = {static_cast<char>(c)};
      probe(seq, 1);
    }
    for (uint16_t k = 0; k < wideCount_; ++k) {
      char seq[4];
      size_t n = 0;
      for (uint32_t b = 0; b < 4; ++b) {
        const char byte = static_cast<char>((wide_[k] >> (b * 8)) & 0xFF);
        if (byte == '\0') break;
        seq[n++] = byte;
      }
      if (n > 0) probe(seq, n);
    }
    if (runLen > 0) {
      run[runLen] = '\0';
      const int16_t rw = target.measureText(style.font, run, style).width;
      h = hashBytes(h, &rw, sizeof(rw));
    }
    return h;
  }

  void ensure(const fui::DrawTarget& target, const int16_t width, const char* text, const fui::TextStyle& style) {
    if (text == nullptr) text = "";
    const uint32_t len = static_cast<uint32_t>(std::strlen(text));
    const uint32_t textHash = hashBytes(2166136261u, text, len);
    // Probed against the alphabet of the text this index was BUILT from. That
    // is sound because the alphabet can only be wrong if the text changed, and
    // the text changing is the hash's job on the very next line.
    const uint32_t metrics = key_.built ? metricsOf(target, style) : 0;
    if (key_.built && key_.textLen == len && key_.textHash == textHash && key_.metrics == metrics &&
        key_.width == width) {
      return;
    }

    checkpoints_.clear();
    buildAlphabet(text, len);
    lineCount_ = fui::textAreaWalk(target, width, text, style, [&](const uint32_t idx, const fui::TextAreaLine& ln) {
      if (idx % kStride == 0) checkpoints_.push_back(ln.start);
    });
    key_.textLen = len;
    key_.textHash = textHash;
    key_.width = width;
    key_.metrics = metricsOf(target, style);
    key_.built = true;
    ++wraps_;
  }

  // The slice of the document that certainly contains lines [topLine, topLine
  // + visible), cut at checkpoints so both ends are line starts and the wrap
  // inside it is the wrap the whole document would have given.
  bool windowFor(const char* text, const uint32_t topLine, const uint16_t visible, uint32_t& firstLine,
                 uint32_t& expectedLines) {
    if (checkpoints_.empty() || lineCount_ == 0) return false;
    uint32_t first = topLine / kStride;
    if (first >= checkpoints_.size()) first = static_cast<uint32_t>(checkpoints_.size() - 1);
    uint32_t last = (topLine + visible + kStride - 1) / kStride;
    if (last <= first) last = first + 1;

    const uint32_t startByte = checkpoints_[first];
    uint32_t endByte;
    const bool toTheEnd = last >= checkpoints_.size();
    if (!toTheEnd) {
      endByte = checkpoints_[last];
      expectedLines = (last - first) * kStride;
    } else {
      endByte = key_.textLen;
      expectedLines = lineCount_ - first * kStride;
    }
    if (endByte < startByte || endByte > key_.textLen) return false;
    uint32_t n = endByte - startByte;
    // A '\n' that the NEXT line's start sits just past was consumed as a break
    // by the line before it. Cut the slice there and the walk sees a hard
    // break with nothing after it, and emits the empty line the SDK keeps for
    // a caret -- one line the whole document does not have.
    //
    // Only when the slice stops at a checkpoint. A slice that runs to the end
    // of the document ends where the document ends, and if THAT is a newline
    // the empty final line is real: the full wrap counted it, so trimming here
    // would drop a line the index knows about and read as a disagreement on
    // every paint of the last page. One test in this file passed and its twin
    // failed on exactly that, because one generated document happened to end
    // in a newline and the other did not.
    if (!toTheEnd && n > 0 && text[startByte + n - 1] == '\n') --n;
    // Reused rather than rebuilt: this runs on every paint, and a fresh
    // few-kilobyte allocation each time is how a long reading session
    // fragments a microcontroller's heap.
    slice_.assign(text + startByte, n);
    firstLine = first * kStride;
    return true;
  }

  struct Span {
    uint32_t start;
    uint16_t len;
  };

  Key key_;
  // The document is NOT copied here. It is the biggest thing in RAM while an
  // article is open, and a second copy of it is a second copy of the reason
  // this app runs out of memory.
  std::string slice_;
  std::vector<uint32_t> checkpoints_;
  std::vector<Span> spans_;
  uint32_t ascii_[4] = {0, 0, 0, 0};
  uint32_t wide_[kAlphabet] = {0};
  uint16_t wideCount_ = 0;
  uint32_t lineCount_ = 0;
  uint32_t wraps_ = 0;
};

}  // namespace toybox
