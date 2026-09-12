// Host check that WikipediaFold.h agrees with fold_vectors.tsv.
//
//   c++ -std=c++20 -I src/apps_local/wikipedia tools_local/wikipedia/tests/fold_check.cpp -o fold_check
//   ./fold_check tools_local/wikipedia/fold_vectors.tsv
//
// tests/test_fold.py builds and runs this; the device test does the same
// walk over the same file. Exit status is the number of failing vectors.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "WikipediaFold.h"

static std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      switch (n) {
        case 't':
          out += '\t';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 'f':
          out += '\f';
          break;
        case 'v':
          out += '\v';
          break;
        case '\\':
          out += '\\';
          break;
        default:
          out += n;
          break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: fold_check <fold_vectors.tsv>\n");
    return 64;
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 66;
  }
  std::string line;
  int total = 0, failed = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t tab = line.find('\t');
    if (tab == std::string::npos) {
      std::fprintf(stderr, "no tab: %s\n", line.c_str());
      failed++;
      continue;
    }
    std::string input = unescape(line.substr(0, tab));
    std::string expect = unescape(line.substr(tab + 1));
    std::string out(input.size() + 1, '\0');
    size_t outLen = 0;
    wikipedia::foldTitle(input.data(), input.size(), out.data(), out.size(), &outLen);
    out.resize(outLen);
    total++;
    if (out != expect) {
      failed++;
      std::fprintf(stderr, "FAIL [%s] -> [%s], expected [%s]\n", input.c_str(), out.c_str(), expect.c_str());
    }
  }
  // Truncation: a cap smaller than the result stops at a character boundary.
  {
    const char* s =
        "Ab\xC3\xA9"
        "cd";  // "Ab" + e-acute + "cd" folds to "abecd"
    char buf[2];
    size_t n = 0;
    wikipedia::foldTitle(s, std::strlen(s), buf, sizeof(buf), &n);
    total++;
    if (n != 2 || std::memcmp(buf, "ab", 2) != 0) {
      failed++;
      std::fprintf(stderr, "FAIL truncation: got %zu bytes\n", n);
    }
    const char* g = "\xCE\xA9\xCE\xA9";  // omega omega, each 2 bytes
    char small[3];
    wikipedia::foldTitle(g, std::strlen(g), small, sizeof(small), &n);
    total++;
    if (n != 2 || std::memcmp(small, "\xCF\x89", 2) != 0) {
      failed++;
      std::fprintf(stderr, "FAIL boundary truncation: got %zu bytes\n", n);
    }
  }
  // Invalid UTF-8 passes through byte by byte.
  {
    const char s[] = {'A', '\xFF', 'B', 0};
    char buf[8];
    size_t n = 0;
    wikipedia::foldTitle(s, 3, buf, sizeof(buf), &n);
    total++;
    if (n != 3 || std::memcmp(buf,
                              "a\xFF"
                              "b",
                              3) != 0) {
      failed++;
      std::fprintf(stderr, "FAIL invalid utf-8 passthrough\n");
    }
  }
  std::printf("fold_check: %d vectors, %d failed\n", total, failed);
  return failed;
}
