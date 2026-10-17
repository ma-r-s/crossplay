#pragma once

// Simulator stand-in for the one wolfCrypt hash call CrossPointWebServer makes:
// /api/status hashes the device secret with the plugin name into a per-plugin
// deviceId. It fails here, so the simulator's status simply has no deviceId;
// see sim-stubs/wolfssl/wolfcrypt/aes.h for the same shape.

inline int wc_Sha256Hash(const unsigned char*, unsigned int, unsigned char*) { return -1; }
