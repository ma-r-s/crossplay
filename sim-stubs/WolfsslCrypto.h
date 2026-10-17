#pragma once

// Simulator stand-in for FreeInk's WolfsslCrypto. The host build has no
// wolfSSL, and the real header compiles its class only under
// FREEINK_CONTENT_WOLFSSL, so CrossPointWebServer's /api/crypto endpoint (SD
// plugins, upstream 2026-09) has nothing to name. Every operation FAILS rather
// than faking a result: a plugin exercised in the simulator gets an error it
// can show, never bytes that look like real crypto output.

#include <Crypto.h>

#include <cstring>

namespace freeink {
namespace content {

class WolfsslCrypto final : public Crypto {
 public:
  std::string lastError = "no crypto in the simulator";

  int32_t rsaPrivateRaw(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) override { return -1; }
  bool aes128CbcDecrypt(const uint8_t[16], const uint8_t[16], const uint8_t*, size_t, uint8_t*) override {
    return false;
  }
  void sha1(const uint8_t*, size_t, uint8_t out[20]) override { std::memset(out, 0, 20); }
  void sha256(const uint8_t*, size_t, uint8_t out[32]) override { std::memset(out, 0, 32); }
  bool rsaGenerate(RsaKeyPairDer*) override { return false; }
  bool rsaPublicEncrypt(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t, size_t*) override {
    return false;
  }
  bool rsaPrivateSignRaw(const uint8_t*, size_t, const uint8_t[20], uint8_t[128]) override { return false; }
  bool aes128CbcEncrypt(const uint8_t[16], const uint8_t[16], const uint8_t*, size_t, uint8_t*) override {
    return false;
  }
  bool pkcs12Extract(const uint8_t*, size_t, const std::string&, std::vector<uint8_t>*,
                     std::vector<uint8_t>*) override {
    return false;
  }
  void randomBytes(uint8_t* out, size_t len) override { std::memset(out, 0, len); }
};

}  // namespace content
}  // namespace freeink
