// Reads the real images.dat that make_images.py produced, with the same code
// the device runs.
//
// The check that matters is not "does a picture come out" but "does the right
// picture come out for the right card, and does a corrupt entry get refused
// rather than drawn". A wrong stride does not fail: it draws the photograph
// sheared into diagonal noise, which looks exactly like a rendering bug and
// gets chased in the wrong file.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../src/apps_local/study/StudyImages.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

class FileSource final : public study::ByteSource {
 public:
  explicit FileSource(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (file_) {
      std::fseek(file_, 0, SEEK_END);
      size_ = static_cast<uint32_t>(std::ftell(file_));
      std::fseek(file_, 0, SEEK_SET);
    }
  }
  ~FileSource() override {
    if (file_) std::fclose(file_);
  }
  bool ok() const { return file_ != nullptr; }
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (!file_ || offset + length > size_) return false;
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, length, file_) == length;
  }
  uint32_t size() const override { return size_; }

 private:
  std::FILE* file_ = nullptr;
  uint32_t size_ = 0;
};

// A source that hands back whatever bytes we tell it, for the corruption cases
// a real file cannot produce on demand.
class MemorySource final : public study::ByteSource {
 public:
  std::string bytes;
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (offset + length > bytes.size()) return false;
    std::memcpy(dst, bytes.data() + offset, length);
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes.size()); }
};

void put16(std::string& s, const uint16_t v) { s.append(reinterpret_cast<const char*>(&v), 2); }
void put32(std::string& s, const uint32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); }

// One entry, one 8x2 image, and whatever header the caller asks for.
MemorySource buildOne(const uint16_t width, const uint16_t height, const uint16_t stride, const uint32_t offset) {
  MemorySource src;
  src.bytes.assign("XSTUDYI", 7);
  src.bytes.push_back('\0');
  put16(src.bytes, 1);
  put16(src.bytes, 0);
  put32(src.bytes, 1);
  put32(src.bytes, offset);
  put16(src.bytes, width);
  put16(src.bytes, height);
  put16(src.bytes, stride);
  src.bytes.append(64, '\xAA');
  return src;
}

void testRealFile(const std::string& dir) {
  // Same as test_deck.cpp: a missing images.dat is a failure. The SKIP that
  // was here was indented two spaces, invisible to check.sh's "^SKIP"
  // surfacing, and taken on every run because nothing ever produced the file.
  FileSource file(dir + "/images.dat");
  if (!file.ok()) {
    check(false, "images.dat exists");
    std::printf("        no images.dat under %s -- host-tests/study/run.sh builds one\n", dir.c_str());
    std::printf("        with make_fixture.py, through make_images.py's own packer\n");
    return;
  }
  study::StudyImages images;
  check(images.open(file), "images.dat parses");
  check(images.ready(), "and has entries");
  std::printf("  %d entries\n", images.count());

  int withImage = 0;
  int badBand = 0;
  uint8_t band[study::kImageBandBytes];
  for (int i = 0; i < images.count(); ++i) {
    const study::ImageRef ref = images.at(file, i);
    if (!ref.valid()) continue;
    ++withImage;

    // Every image must read end to end, band by band, with the rows adding up
    // to exactly its height. A stride or offset that is wrong by one shows up
    // here rather than as noise on the panel.
    int rows = 0;
    while (rows < ref.height) {
      const int got = images.readBand(file, ref, rows, band);
      if (got <= 0) break;
      rows += got;
    }
    if (rows != ref.height) ++badBand;
  }
  std::printf("  %d entries carry an image\n", withImage);
  check(withImage > 0, "at least one card has a photograph");
  check(badBand == 0, "every image reads end to end, band by band");

  // Out-of-range asks must be refused, not clamped into someone else's picture.
  check(!images.at(file, -1).valid(), "a negative index has no image");
  check(!images.at(file, images.count()).valid(), "a past-the-end index has no image");
}

void testRefusesCorruption() {
  uint8_t band[study::kImageBandBytes];
  study::StudyImages images;

  // A width of zero is the file's own way of saying "no photograph here", and
  // must read as absent rather than as broken.
  MemorySource absent = buildOne(0, 0, 0, 0);
  check(images.open(absent), "a file whose only card has no image still parses");
  check(!images.at(absent, 0).valid(), "a zero-width entry reads as no image");

  // A stride narrower than the width would shear the picture diagonally.
  MemorySource sheared = buildOne(64, 2, 4, 26);
  check(images.open(sheared), "header parses");
  check(!images.at(sheared, 0).valid(), "a stride too small for the width is refused");

  // An offset past the end of the file.
  MemorySource past = buildOne(8, 2, 1, 100000);
  check(images.open(past), "header parses");
  check(!images.at(past, 0).valid(), "an offset past the end of the file is refused");

  // Dimensions beyond what the format allows.
  MemorySource huge = buildOne(4000, 4000, 500, 26);
  check(images.open(huge), "header parses");
  check(!images.at(huge, 0).valid(), "an oversized image is refused");

  // A truncated file: the index does not fit.
  MemorySource cut;
  cut.bytes.assign("XSTUDYI", 7);
  cut.bytes.push_back('\0');
  put16(cut.bytes, 1);
  put16(cut.bytes, 0);
  put32(cut.bytes, 5000);
  check(!images.open(cut), "a truncated index is refused");

  // Not our file at all.
  MemorySource alien;
  alien.bytes.assign("XSTUDYD\0", 8);
  alien.bytes.append(64, '\0');
  check(!images.open(alien), "deck.dat is not accepted as images.dat");

  // And a valid one still works, so the checks above are not simply refusing
  // everything.
  MemorySource good = buildOne(8, 2, 1, 26);
  check(images.open(good), "a well-formed file parses");
  const study::ImageRef ref = images.at(good, 0);
  check(ref.valid(), "a well-formed entry is accepted");
  check(images.readBand(good, ref, 0, band) == 2, "both rows read");
  check(images.readBand(good, ref, 2, band) == 0, "reading past the last row stops");
}

}  // namespace

int main(const int argc, char** argv) {
  std::printf("StudyImages\n");
  testRealFile(argc > 1 ? argv[1] : "/tmp/studytest/fresh");
  testRefusesCorruption();
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
