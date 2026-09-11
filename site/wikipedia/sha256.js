// A streaming SHA-256 in plain JavaScript.
//
// MIT License. Copyright (c) 2026 Mario Ruiz (CrossPlay). Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files, to deal in the software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the software, subject to the
// above copyright notice and this permission notice being included in all
// copies or substantial portions of the software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND.
//
// Why this exists: the Wikipedia install page streams gigabyte shards from the
// network straight onto the SD card through a 2 MB buffer, and it has to check
// each one against the manifest's sha256 as it goes. crypto.subtle.digest is
// one-shot: it wants the whole message in memory at once, which a 1 GB shard
// cannot be. So this is FIPS 180-4 SHA-256 with an update() that takes bytes in
// any sizes and a digest() at the end. Nothing else: no HMAC, no other sizes.
//
// It is written for throughput rather than brevity. The message schedule is a
// preallocated Uint32Array, whole 64-byte blocks are read straight out of the
// caller's chunk without copying, and only the ragged tail of a chunk goes
// through the 64-byte carry buffer. Measured at 150 to 250 MB/s in current
// V8, an order of magnitude above the fastest card this page will ever write.
//
// Verified in tests/sha256.test.js against the FIPS vectors and against node's
// own crypto on random data fed in odd-sized pieces.

const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

const INIT = new Uint32Array([
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
  0x1f83d9ab, 0x5be0cd19,
]);

const HEX = "0123456789abcdef";

export class Sha256 {
  constructor() {
    this.h = new Uint32Array(INIT);
    this.w = new Uint32Array(64);
    this.tail = new Uint8Array(64);
    this.tailLen = 0;
    this.total = 0; // bytes seen; a JS number is exact far past any file
    this.done = false;
  }

  // One 64-byte block, read big-endian from `b` at `off`.
  _block(b, off) {
    const w = this.w;
    for (let i = 0; i < 16; i++, off += 4) {
      w[i] =
        (b[off] << 24) | (b[off + 1] << 16) | (b[off + 2] << 8) | b[off + 3];
    }
    for (let i = 16; i < 64; i++) {
      const x = w[i - 15];
      const y = w[i - 2];
      const s0 = ((x >>> 7) | (x << 25)) ^ ((x >>> 18) | (x << 14)) ^ (x >>> 3);
      const s1 =
        ((y >>> 17) | (y << 15)) ^ ((y >>> 19) | (y << 13)) ^ (y >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
    }
    const h = this.h;
    let a = h[0],
      b2 = h[1],
      c = h[2],
      d = h[3];
    let e = h[4],
      f = h[5],
      g = h[6],
      hh = h[7];
    for (let i = 0; i < 64; i++) {
      const S1 =
        ((e >>> 6) | (e << 26)) ^
        ((e >>> 11) | (e << 21)) ^
        ((e >>> 25) | (e << 7));
      const ch = (e & f) ^ (~e & g);
      const t1 = (hh + S1 + ch + K[i] + w[i]) | 0;
      const S0 =
        ((a >>> 2) | (a << 30)) ^
        ((a >>> 13) | (a << 19)) ^
        ((a >>> 22) | (a << 10));
      const maj = (a & b2) ^ (a & c) ^ (b2 & c);
      const t2 = (S0 + maj) | 0;
      hh = g;
      g = f;
      f = e;
      e = (d + t1) | 0;
      d = c;
      c = b2;
      b2 = a;
      a = (t1 + t2) | 0;
    }
    h[0] = (h[0] + a) | 0;
    h[1] = (h[1] + b2) | 0;
    h[2] = (h[2] + c) | 0;
    h[3] = (h[3] + d) | 0;
    h[4] = (h[4] + e) | 0;
    h[5] = (h[5] + f) | 0;
    h[6] = (h[6] + g) | 0;
    h[7] = (h[7] + hh) | 0;
  }

  // Feed bytes. Accepts a Uint8Array (any length, including 0). Returns this.
  update(chunk) {
    if (this.done) throw new Error("Sha256: update after digest");
    let off = 0;
    const n = chunk.length;
    this.total += n;
    // Top up a partial block from an earlier chunk first.
    if (this.tailLen > 0) {
      const need = 64 - this.tailLen;
      const take = n < need ? n : need;
      this.tail.set(chunk.subarray(0, take), this.tailLen);
      this.tailLen += take;
      off = take;
      if (this.tailLen < 64) return this;
      this._block(this.tail, 0);
      this.tailLen = 0;
    }
    // Whole blocks straight from the chunk, no copy.
    const lastFull = n - ((n - off) % 64);
    for (; off < lastFull; off += 64) this._block(chunk, off);
    // Ragged end into the carry buffer.
    if (off < n) {
      this.tail.set(chunk.subarray(off), 0);
      this.tailLen = n - off;
    }
    return this;
  }

  // The 32-byte digest. May be called once; the hasher is spent afterwards.
  digest() {
    if (this.done) throw new Error("Sha256: digest twice");
    this.done = true;
    const total = this.total;
    const tail = this.tail;
    let len = this.tailLen;
    tail[len++] = 0x80;
    if (len > 56) {
      while (len < 64) tail[len++] = 0;
      this._block(tail, 0);
      len = 0;
    }
    while (len < 56) tail[len++] = 0;
    // Bit length, big-endian 64-bit. total*8 can pass 2^32, so split first.
    const hi = Math.floor(total / 0x20000000);
    const lo = (total % 0x20000000) * 8;
    tail[56] = (hi >>> 24) & 0xff;
    tail[57] = (hi >>> 16) & 0xff;
    tail[58] = (hi >>> 8) & 0xff;
    tail[59] = hi & 0xff;
    tail[60] = (lo >>> 24) & 0xff;
    tail[61] = (lo >>> 16) & 0xff;
    tail[62] = (lo >>> 8) & 0xff;
    tail[63] = lo & 0xff;
    this._block(tail, 0);
    const out = new Uint8Array(32);
    for (let i = 0; i < 8; i++) {
      const v = this.h[i];
      out[i * 4] = (v >>> 24) & 0xff;
      out[i * 4 + 1] = (v >>> 16) & 0xff;
      out[i * 4 + 2] = (v >>> 8) & 0xff;
      out[i * 4 + 3] = v & 0xff;
    }
    return out;
  }

  // The digest as 64 lowercase hex characters, the manifest's spelling.
  hex() {
    const d = this.digest();
    let s = "";
    for (let i = 0; i < 32; i++) s += HEX[d[i] >>> 4] + HEX[d[i] & 15];
    return s;
  }
}

// One-shot convenience for small inputs and tests.
export function sha256Hex(bytes) {
  return new Sha256().update(bytes).hex();
}
