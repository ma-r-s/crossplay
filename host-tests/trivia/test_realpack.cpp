// Does the C++ reader agree with the Python WRITER?
//
// test_trivia.cpp builds its pack in C++, which pins the reader against itself
// and would happily agree with a format both halves get wrong. This one reads a
// pack written by tools_local/trivia/pack_format.py and checks the fields
// against a manifest that same script emitted. If the two ever drift -- a field
// added, an endianness slip, a length prefix widened -- exactly one of them has
// to be edited to make this pass.
//
// argv[1] is the pack, argv[2] the manifest: id-free TSV of
//   difficulty <TAB> year <TAB> nalt <TAB> nwrong <TAB> clue <TAB> answer

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "TriviaCore.h"

using namespace trivia;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

class FileSource final : public ByteSource {
 public:
  explicit FileSource(const char* path) : file_(path, std::ios::binary) {
    if (file_) {
      file_.seekg(0, std::ios::end);
      size_ = static_cast<uint32_t>(file_.tellg());
    }
  }
  bool ok() const { return size_ > 0; }
  bool read(uint32_t offset, void* dst, uint32_t length) override {
    if (static_cast<uint64_t>(offset) + length > size_) return false;
    file_.seekg(offset);
    file_.read(static_cast<char*>(dst), length);
    return static_cast<uint32_t>(file_.gcount()) == length;
  }
  uint32_t size() const override { return size_; }

 private:
  mutable std::ifstream file_;
  uint32_t size_ = 0;
};

struct Row {
  int difficulty, year, nalt, nwrong;
  std::string clue, answer;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: test_realpack <pack.dat> <manifest.tsv>\n");
    return 2;
  }

  std::vector<Row> rows;
  {
    std::ifstream in(argv[2]);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream ls(line);
      Row r;
      std::string d, y, na, nw;
      if (!std::getline(ls, d, '\t')) continue;
      std::getline(ls, y, '\t');
      std::getline(ls, na, '\t');
      std::getline(ls, nw, '\t');
      std::getline(ls, r.clue, '\t');
      std::getline(ls, r.answer, '\t');
      r.difficulty = std::atoi(d.c_str());
      r.year = std::atoi(y.c_str());
      r.nalt = std::atoi(na.c_str());
      r.nwrong = std::atoi(nw.c_str());
      rows.push_back(r);
    }
  }
  CHECK(!rows.empty());

  FileSource source(argv[1]);
  CHECK(source.ok());
  Pack pack;
  CHECK(pack.open(source));
  CHECK(pack.count() == rows.size());

  for (uint32_t i = 0; i < pack.count() && i < rows.size(); ++i) {
    Question q;
    if (!pack.read(i, q)) {
      ++failures;
      ++checks;
      std::printf("FAIL could not read record %u written by pack_format.py\n", i);
      continue;
    }
    CHECK(std::strcmp(q.clue(), rows[i].clue.c_str()) == 0);
    CHECK(std::strcmp(q.answer(), rows[i].answer.c_str()) == 0);
    CHECK(q.difficulty() == rows[i].difficulty);
    CHECK(q.year() == rows[i].year);
    CHECK(q.alternateCount() == rows[i].nalt);
    CHECK(q.distractorCount() == rows[i].nwrong);

    // A record written by the real writer must also DRAW correctly, at every
    // stored-distractor count the writer can emit. distractorCount() agreeing
    // with the manifest only proves the bytes parsed; it says nothing about
    // whether the option set the player is offered is well formed.
    CHECK(q.playableAsChoice() == (rows[i].nwrong >= kOptions - 1));
    if (q.playableAsChoice()) {
      Rng rng(static_cast<uint32_t>(i) + 1u);
      Choices c;
      CHECK(buildChoices(q, rng, c));
      int answers = 0;
      for (int a = 0; a < kOptions; ++a) {
        CHECK(c.option[a] != nullptr && c.option[a][0] != '\0');
        if (c.option[a] != nullptr && std::strcmp(c.option[a], q.answer()) == 0) ++answers;
        for (int b = a + 1; b < kOptions; ++b) {
          CHECK(c.option[a] != nullptr && c.option[b] != nullptr && std::strcmp(c.option[a], c.option[b]) != 0);
        }
      }
      CHECK(answers == 1);
    }
  }

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
