// The browser build's <openssl/sha.h> stand-in, against the FIPS 180-4 test
// vectors and, on the Mac, against CommonCrypto for every length 0..300 (both
// one- and two-block padding, and every boundary around 55, 56 and 64 bytes).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../tools_local/wasm/stubs/openssl/sha.h"

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

static int checks = 0;
static int failed = 0;

static std::string hex(const unsigned char* p, const size_t n) {
  std::string out;
  char b[3];
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(b, sizeof(b), "%02x", p[i]);
    out += b;
  }
  return out;
}

static void expect(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failed;
    std::printf("FAIL %s\n", what);
  }
}

static void vector(const std::string& msg, const char* sha1, const char* sha256) {
  unsigned char out1[20];
  unsigned char out256[32];
  const auto* d = reinterpret_cast<const unsigned char*>(msg.data());
  expect(hex(SHA1(d, msg.size(), out1), 20) == sha1,
         ("SHA1 of a " + std::to_string(msg.size()) + "-byte vector").c_str());
  expect(hex(SHA256(d, msg.size(), out256), 32) == sha256,
         ("SHA256 of a " + std::to_string(msg.size()) + "-byte vector").c_str());
}

int main() {
  vector("", "da39a3ee5e6b4b0d3255bfef95601890afd80709",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  vector("abc", "a9993e364706816aba3e25717850c26c9cd0d89d",
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  vector("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  vector(std::string(1000000, 'a'), "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
         "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

#ifdef __APPLE__
  std::vector<unsigned char> buf(300);
  for (size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<unsigned char>(i * 131 + 7);
  for (size_t n = 0; n <= buf.size(); ++n) {
    unsigned char mine[32];
    unsigned char theirs[32];
    SHA1(buf.data(), n, mine);
    CC_SHA1(buf.data(), static_cast<CC_LONG>(n), theirs);
    expect(std::memcmp(mine, theirs, 20) == 0, ("SHA1 matches CommonCrypto at " + std::to_string(n)).c_str());
    SHA256(buf.data(), n, mine);
    CC_SHA256(buf.data(), static_cast<CC_LONG>(n), theirs);
    expect(std::memcmp(mine, theirs, 32) == 0, ("SHA256 matches CommonCrypto at " + std::to_string(n)).c_str());
  }
#else
  std::printf("SKIP wasmstubs  no CommonCrypto here; the cross-check against it ran only the vectors\n");
#endif

  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
