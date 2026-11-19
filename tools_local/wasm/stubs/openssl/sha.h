#pragma once
// <openssl/sha.h> for the browser build: the two one-shot hashes, nothing else.
//
// The simulator's content-protection shims (WolfsslCrypto.h and
// wolfssl/wolfcrypt/hash.h) hash with CommonCrypto on macOS and with OpenSSL
// everywhere else, and Emscripten has neither. They call exactly SHA1() and
// SHA256(), with OpenSSL's signatures, so this answers those two with a
// self-contained implementation (FIPS 180-4) and the include resolves.
//
// What reaches them in the browser is book identity and the sync hash; nothing
// security bearing runs here. host-tests/wasmstubs checks both against the
// standard test vectors and against the Mac's own CommonCrypto.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wasmsha {

inline uint32_t rotl(const uint32_t x, const int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr(const uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }

// Both hashes pad the same way: 0x80, zeros, the bit length big-endian, in
// 64-byte blocks. `block` is called once per block.
template <typename Block>
inline void padded(const unsigned char* data, const size_t len, Block block) {
  size_t done = 0;
  for (; len - done >= 64; done += 64) block(data + done);
  unsigned char tail[128] = {};
  const size_t rest = len - done;
  std::memcpy(tail, data + done, rest);
  tail[rest] = 0x80;
  const size_t blocks = rest + 1 + 8 <= 64 ? 1 : 2;
  const uint64_t bits = static_cast<uint64_t>(len) * 8u;
  for (int i = 0; i < 8; ++i) tail[blocks * 64 - 1 - i] = static_cast<unsigned char>(bits >> (8 * i));
  block(tail);
  if (blocks == 2) block(tail + 64);
}

inline uint32_t be32(const unsigned char* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void put32(unsigned char* p, const uint32_t v) {
  p[0] = static_cast<unsigned char>(v >> 24);
  p[1] = static_cast<unsigned char>(v >> 16);
  p[2] = static_cast<unsigned char>(v >> 8);
  p[3] = static_cast<unsigned char>(v);
}

}  // namespace wasmsha

inline unsigned char* SHA1(const unsigned char* data, const size_t len, unsigned char* out) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  wasmsha::padded(data, len, [&h](const unsigned char* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = wasmsha::be32(block + 4 * i);
    for (int i = 16; i < 80; ++i) w[i] = wasmsha::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f;
      uint32_t k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t t = wasmsha::rotl(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = wasmsha::rotl(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  });
  for (int i = 0; i < 5; ++i) wasmsha::put32(out + 4 * i, h[i]);
  return out;
}

inline unsigned char* SHA256(const unsigned char* data, const size_t len, unsigned char* out) {
  static constexpr uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  wasmsha::padded(data, len, [&h](const unsigned char* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = wasmsha::be32(block + 4 * i);
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = wasmsha::rotr(w[i - 15], 7) ^ wasmsha::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = wasmsha::rotr(w[i - 2], 17) ^ wasmsha::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = wasmsha::rotr(e, 6) ^ wasmsha::rotr(e, 11) ^ wasmsha::rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + s1 + ch + k[i] + w[i];
      const uint32_t s0 = wasmsha::rotr(a, 2) ^ wasmsha::rotr(a, 13) ^ wasmsha::rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  });
  for (int i = 0; i < 8; ++i) wasmsha::put32(out + 4 * i, h[i]);
  return out;
}
