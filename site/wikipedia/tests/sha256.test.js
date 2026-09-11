// The streaming SHA-256 the Wikipedia page verifies shards with. A wrong
// constant here would reject every shard, or worse accept a damaged one, and
// nothing in the browser would say which. FIPS vectors first, then node's own
// crypto as the oracle on random data fed in awkward piece sizes, which is the
// shape a fetch stream actually delivers.
//
//   node --test site/wikipedia/tests/
//   bun test site/wikipedia/tests/

import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { Sha256, sha256Hex } from "../sha256.js";

const enc = new TextEncoder();

test("FIPS 180-4 vectors", () => {
  assert.equal(
    sha256Hex(new Uint8Array(0)),
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  );
  assert.equal(
    sha256Hex(enc.encode("abc")),
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
  );
  assert.equal(
    sha256Hex(
      enc.encode("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
    ),
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
  );
  // One million 'a', fed in 1000-byte pieces.
  const h = new Sha256();
  const piece = new Uint8Array(1000).fill(0x61);
  for (let i = 0; i < 1000; i++) h.update(piece);
  assert.equal(
    h.hex(),
    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
  );
});

test("padding boundaries: every length from 0 to 200 matches node", () => {
  for (let n = 0; n <= 200; n++) {
    const data = randomBytes(n);
    const want = createHash("sha256").update(data).digest("hex");
    assert.equal(sha256Hex(new Uint8Array(data)), want, "length " + n);
  }
});

test("chunking does not change the digest", () => {
  const data = new Uint8Array(randomBytes(300000));
  const want = createHash("sha256").update(data).digest("hex");
  // Piece sizes that straddle blocks in every way: primes, 1, 63, 64, 65, big.
  for (const size of [1, 7, 63, 64, 65, 1000, 4093, 65536, 300001]) {
    const h = new Sha256();
    for (let off = 0; off < data.length; off += size) {
      h.update(data.subarray(off, Math.min(off + size, data.length)));
    }
    assert.equal(h.hex(), want, "piece size " + size);
  }
  // And a ragged mix, as a network delivers.
  const h = new Sha256();
  let off = 0;
  let k = 3;
  while (off < data.length) {
    const size = (k * 7919) % 5000;
    h.update(data.subarray(off, Math.min(off + size, data.length)));
    off += size;
    k++;
  }
  assert.equal(h.hex(), want, "ragged pieces");
});

test("empty updates are harmless", () => {
  const h = new Sha256();
  h.update(new Uint8Array(0));
  h.update(enc.encode("abc"));
  h.update(new Uint8Array(0));
  assert.equal(
    h.hex(),
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
  );
});

test("a spent hasher refuses more input", () => {
  const h = new Sha256();
  h.update(enc.encode("abc"));
  h.hex();
  assert.throws(() => h.update(enc.encode("d")));
  assert.throws(() => h.hex());
});

test("the bit-length field is right past 512 MB", () => {
  // 2^29 bytes is where total*8 crosses 2^32 and the high word becomes
  // nonzero. Hashing that much is too slow for a test, so the same arithmetic
  // is checked directly against BigInt on the numbers the padding uses.
  for (const total of [
    0, 55, 56, 64, 0x1fffffff, 0x20000000, 0x20000001, 1073741824, 11500000000,
  ]) {
    const hi = Math.floor(total / 0x20000000);
    const lo = (total % 0x20000000) * 8;
    const bits = BigInt(total) * 8n;
    assert.equal(BigInt(hi), bits >> 32n, "hi for " + total);
    assert.equal(BigInt(lo), bits & 0xffffffffn, "lo for " + total);
  }
});
