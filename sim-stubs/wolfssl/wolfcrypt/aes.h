#pragma once

// Simulator stand-in for the wolfCrypt AES calls the firmware makes:
// CrossPointWebServer's /api/crypto "aesenc" op, and upstream's per-book
// content keys (lib/Epub/BookKey.cpp, AES-GCM). Every call fails, so the
// endpoint answers "aesenc failed" and a protected book reports its key
// unavailable in the simulator; see sim-stubs/WolfsslCrypto.h.

struct Aes {
  int unused = 0;
};

#define AES_ENCRYPTION 0
#define INVALID_DEVID (-2)

inline int wc_AesInit(Aes*, void*, int) { return -1; }
inline void wc_AesFree(Aes*) {}
inline int wc_AesSetKey(Aes*, const unsigned char*, unsigned int, const unsigned char*, int) { return -1; }
inline int wc_AesCbcEncrypt(Aes*, unsigned char*, const unsigned char*, unsigned int) { return -1; }
inline int wc_AesGcmSetKey(Aes*, const unsigned char*, unsigned int) { return -1; }
inline int wc_AesGcmEncrypt(Aes*, unsigned char*, const unsigned char*, unsigned int, const unsigned char*,
                            unsigned int, unsigned char*, unsigned int, const unsigned char*, unsigned int) {
  return -1;
}
inline int wc_AesGcmDecrypt(Aes*, unsigned char*, const unsigned char*, unsigned int, const unsigned char*,
                            unsigned int, const unsigned char*, unsigned int, const unsigned char*, unsigned int) {
  return -1;
}
