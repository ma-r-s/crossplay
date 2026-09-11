// The Wikipedia install page's pure half: the manifest, the plan, the rate
// window, the twenty-minute rule and the wording. Everything the page decides
// without touching the network or the card is decided here, so this is where
// a wrong decision would be caught.
//
//   node --test site/wikipedia/tests/
//   bun test site/wikipedia/tests/

import { test } from "node:test";
import assert from "node:assert/strict";
import {
  TWENTY_MINUTES_S,
  PROJECTION_MIN_BYTES,
  PROJECTION_MIN_SECONDS,
  ROUTES,
  routeById,
  otherRoute,
  parseManifest,
  tierByName,
  filesForTier,
  tierTotalBytes,
  downloadList,
  makePlan,
  parseMarkers,
  withMarker,
  serializeMarkers,
  RollingRate,
  secondsLeft,
  twentyMinuteCheck,
  formatBytes,
  formatRate,
  formatDuration,
} from "../plan.js";

const SHA = "a".repeat(64);
const MB = 1e6;

function manifest(overrides = {}) {
  const shards = [];
  for (let i = 0; i < 4; i++) {
    shards.push({
      file: "shards/00" + i + ".blk",
      bytes: 100 * MB,
      sha256: String(i).repeat(64),
      firstBlock: i * 1000,
      blocks: 1000,
    });
  }
  return {
    format: 1,
    pack: "en",
    snapshot: "2026-05-13",
    built: "2026-09-11T02:00:00Z",
    articles: 7238251,
    entries: 19217771,
    blocks: 4000,
    dict: { file: "dict.zst", bytes: 110000, sha256: SHA },
    titles: { file: "titles.idx", bytes: 30 * MB, sha256: SHA },
    blocksdir: { file: "blocks.dir", bytes: 64000, sha256: SHA },
    shards,
    tiers: [
      { name: "essentials", shards: 1, articles: 49938, bytes: 130174000 },
      { name: "all", shards: 4, articles: 7238251, bytes: 430174000 },
    ],
    ...overrides,
  };
}

// --- the manifest -----------------------------------------------------------

test("a good manifest parses, from text or object, and drops what it does not use", () => {
  const m = parseManifest(JSON.stringify(manifest()));
  assert.equal(m.pack, "en");
  assert.equal(m.snapshot, "2026-05-13");
  assert.equal(m.shards.length, 4);
  assert.equal(m.tiers.length, 2);
  assert.equal(m.built, undefined);
  assert.deepEqual(parseManifest(manifest()), m);
});

test("a manifest with the wrong shape is refused with one plain sentence", () => {
  const bad = [
    ["not json", "{"],
    ["a list", []],
    ["format 2", manifest({ format: 2 })],
    ["no pack", manifest({ pack: "" })],
    ["no snapshot", manifest({ snapshot: 7 })],
    ["dict missing", manifest({ dict: undefined })],
    [
      "short sha",
      manifest({ dict: { file: "dict.zst", bytes: 1, sha256: "abc" } }),
    ],
    [
      "upper-case sha",
      manifest({
        dict: { file: "dict.zst", bytes: 1, sha256: "A".repeat(64) },
      }),
    ],
    [
      "negative bytes",
      manifest({ dict: { file: "dict.zst", bytes: -1, sha256: SHA } }),
    ],
    [
      "fractional bytes",
      manifest({ dict: { file: "dict.zst", bytes: 1.5, sha256: SHA } }),
    ],
    [
      "path escape",
      manifest({ dict: { file: "../dict.zst", bytes: 1, sha256: SHA } }),
    ],
    [
      "absolute path",
      manifest({ dict: { file: "/etc/passwd", bytes: 1, sha256: SHA } }),
    ],
    ["no shards", manifest({ shards: [] })],
    [
      "shard without firstBlock",
      manifest({ shards: [{ file: "s", bytes: 1, sha256: SHA, blocks: 1 }] }),
    ],
    ["no tiers", manifest({ tiers: [] })],
    [
      "tier past the shard list",
      manifest({ tiers: [{ name: "all", shards: 5, articles: 1, bytes: 1 }] }),
    ],
    [
      "tier of zero shards",
      manifest({ tiers: [{ name: "all", shards: 0, articles: 1, bytes: 1 }] }),
    ],
    [
      "tier without a name",
      manifest({ tiers: [{ shards: 1, articles: 1, bytes: 1 }] }),
    ],
  ];
  for (const [what, input] of bad) {
    assert.throws(
      () => parseManifest(input),
      (e) =>
        /^The pack's manifest is not one this page understands: /.test(
          e.message,
        ),
      what,
    );
  }
});

test("tiers are found by name", () => {
  const m = parseManifest(manifest());
  assert.equal(tierByName(m, "essentials").shards, 1);
  assert.equal(tierByName(m, "all").shards, 4);
  assert.equal(tierByName(m, "everything"), null);
});

// --- the routes -------------------------------------------------------------

test("the two routes carry the plan's wording, essentials first", () => {
  assert.equal(ROUTES.length, 2);
  assert.equal(ROUTES[0].id, "essentials");
  assert.equal(ROUTES[0].title, "The essentials, with the cable.");
  assert.equal(ROUTES[0].time, "About ten minutes.");
  assert.equal(
    ROUTES[0].instruction,
    "Plug the reader into the computer with its cable.",
  );
  assert.equal(ROUTES[0].pick, "Choose the reader");
  assert.equal(ROUTES[0].done, "Eject the reader, then unplug it.");
  assert.equal(ROUTES[1].id, "all");
  assert.equal(
    ROUTES[1].title,
    "All of Wikipedia, with the card in the computer.",
  );
  assert.equal(ROUTES[1].time, "About fifteen minutes.");
  assert.match(
    ROUTES[1].instruction,
    /^Take the card out of the reader and put it in the computer/,
  );
  assert.equal(ROUTES[1].pick, "Choose the card");
  assert.equal(
    ROUTES[1].done,
    "Put the card back in the reader and open Wikipedia.",
  );
  assert.equal(routeById("all").tier, "all");
  assert.equal(otherRoute("all").id, "essentials");
  assert.equal(otherRoute("essentials").id, "all");
  // No em-dashes, no exclamation marks, no enthusiasm: the site's voice.
  for (const r of ROUTES) {
    for (const v of Object.values(r)) {
      assert.doesNotMatch(v, /[—!]/, r.id);
    }
  }
});

// --- files and the plan -----------------------------------------------------

test("a tier's files come in write order: dict, titles, blocks.dir, then shards", () => {
  const m = parseManifest(manifest());
  const ess = filesForTier(m, "essentials").map((f) => f.file);
  assert.deepEqual(ess, [
    "dict.zst",
    "titles.idx",
    "blocks.dir",
    "shards/000.blk",
  ]);
  const all = filesForTier(m, "all").map((f) => f.file);
  assert.deepEqual(all, [
    "dict.zst",
    "titles.idx",
    "blocks.dir",
    "shards/000.blk",
    "shards/001.blk",
    "shards/002.blk",
    "shards/003.blk",
  ]);
  assert.equal(filesForTier(m, "all")[3].kind, "shard");
  assert.equal(filesForTier(m, "all")[3].shard, 0);
  assert.throws(() => filesForTier(m, "nope"), /no tier called nope/);
});

test("the tier total is the sum of its files, and the manifest is last in the download list", () => {
  const m = parseManifest(manifest());
  assert.equal(
    tierTotalBytes(m, "essentials"),
    110000 + 30 * MB + 64000 + 100 * MB,
  );
  assert.equal(tierTotalBytes(m, "all"), 110000 + 30 * MB + 64000 + 400 * MB);
  const list = downloadList(m, "essentials").map((f) => f.file);
  assert.equal(list[list.length - 1], "manifest.json");
  assert.equal(list.length, 5);
});

test("an empty card: everything is copied", () => {
  const m = parseManifest(manifest());
  const p = makePlan(m, "all", {}, {});
  assert.equal(p.steps.length, 7);
  assert.ok(p.steps.every((s) => s.action === "copy"));
  assert.equal(p.bytesToCopy, p.bytesTotal);
  assert.equal(p.bytesSkipped, 0);
  assert.equal(p.tier.name, "all");
});

test("a file of the right size with a matching marker is skipped; a wrong size is copied", () => {
  const m = parseManifest(manifest());
  const existing = {
    "dict.zst": { bytes: 110000 },
    "titles.idx": { bytes: 30 * MB - 1 }, // short: an interrupted hand copy
    "shards/000.blk": { bytes: 100 * MB },
    "shards/001.blk": { bytes: 100 * MB },
  };
  let markers = {};
  markers = withMarker(markers, "dict.zst", 110000, SHA);
  markers = withMarker(markers, "shards/000.blk", 100 * MB, "0".repeat(64));
  markers = withMarker(markers, "shards/001.blk", 100 * MB, "f".repeat(64)); // stale marker
  const p = makePlan(m, "all", existing, markers);
  const byFile = Object.fromEntries(p.steps.map((s) => [s.file, s.action]));
  assert.equal(byFile["dict.zst"], "skip");
  assert.equal(byFile["titles.idx"], "copy");
  assert.equal(byFile["blocks.dir"], "copy");
  assert.equal(byFile["shards/000.blk"], "skip");
  assert.equal(
    byFile["shards/001.blk"],
    "verify",
    "size matches, marker names another hash",
  );
  assert.equal(byFile["shards/002.blk"], "copy");
  assert.equal(p.bytesSkipped, 110000 + 100 * MB);
  assert.equal(p.bytesToCopy, p.bytesTotal - p.bytesSkipped);
});

test("a file of the right size with no marker is verified, not trusted and not re-downloaded", () => {
  const m = parseManifest(manifest());
  const p = makePlan(
    m,
    "essentials",
    { "shards/000.blk": { bytes: 100 * MB } },
    {},
  );
  assert.equal(
    p.steps.find((s) => s.file === "shards/000.blk").action,
    "verify",
  );
});

test("the essentials plan never touches shards past the first", () => {
  const m = parseManifest(manifest());
  const p = makePlan(m, "essentials", {}, {});
  assert.ok(!p.steps.some((s) => s.file === "shards/001.blk"));
});

// --- markers ----------------------------------------------------------------

test("markers round-trip and garbage reads as nothing", () => {
  let mk = withMarker({}, "shards/000.blk", 5, SHA);
  const text = serializeMarkers(mk);
  assert.deepEqual(parseMarkers(text), mk);
  assert.deepEqual(parseMarkers(""), {});
  assert.deepEqual(parseMarkers("{"), {});
  assert.deepEqual(parseMarkers("[]"), {});
  assert.deepEqual(
    parseMarkers('{"files":{"x":{"bytes":"5","sha256":"zz"}}}'),
    {},
  );
  assert.deepEqual(
    parseMarkers('{"files":{"x":{"bytes":5,"sha256":"' + SHA + '"}}}'),
    {
      x: { bytes: 5, sha256: SHA },
    },
  );
});

// --- the rate window --------------------------------------------------------

test("the rolling rate is over the last ten seconds, not since the start", () => {
  const r = new RollingRate(10000);
  assert.equal(r.rate(), null, "nothing yet");
  r.push(0, 0);
  assert.equal(r.rate(), null, "one sample");
  r.push(200, 1 * MB);
  assert.equal(r.rate(), null, "under half a second apart");
  // 1 MB/s for the first 10 s
  for (let t = 1000; t <= 10000; t += 1000) r.push(t, t * 1000);
  assert.ok(Math.abs(r.rate() - 1 * MB) < 1, "1 MB/s");
  // then 5 MB/s for the next 10 s: the window forgets the slow start
  let c = 10 * MB;
  for (let t = 11000; t <= 20000; t += 1000) {
    c += 5 * MB;
    r.push(t, c);
  }
  assert.ok(
    Math.abs(r.rate() - 5 * MB) < 1,
    "5 MB/s, the slow start is gone: " + r.rate(),
  );
  // a stall is a falling number, not a frozen one
  r.push(25000, c);
  assert.ok(r.rate() < 4 * MB && r.rate() > 0, "stall shows: " + r.rate());
  // the window is bounded in memory
  assert.ok(r.samples.length <= 12, "samples kept: " + r.samples.length);
});

test("time left is remaining over rate, and nothing without a rate", () => {
  assert.equal(secondsLeft(100 * MB, null), null);
  assert.equal(secondsLeft(100 * MB, 0), null);
  assert.equal(secondsLeft(100 * MB, 10 * MB), 10);
});

// --- the twenty-minute rule -------------------------------------------------

test("no verdict until enough has been transferred over enough time", () => {
  const small = twentyMinuteCheck({
    transferredBytes: PROJECTION_MIN_BYTES - 1,
    transferSeconds: 60,
    bytesRemaining: 11.5e9,
  });
  assert.equal(small.measured, false);
  assert.equal(small.exceeds, false);
  const quick = twentyMinuteCheck({
    transferredBytes: 100 * MB,
    transferSeconds: PROJECTION_MIN_SECONDS - 0.1,
    bytesRemaining: 11.5e9,
  });
  assert.equal(quick.measured, false);
  assert.equal(quick.exceeds, false);
});

test("the projection is the session so far plus the rest at the same rate", () => {
  // 100 MB in 10 s = 10 MB/s; 1100 MB left = 110 s more; 120 s total
  const c = twentyMinuteCheck({
    transferredBytes: 100 * MB,
    transferSeconds: 10,
    bytesRemaining: 1100 * MB,
  });
  assert.equal(c.measured, true);
  assert.equal(c.rate, 10 * MB);
  assert.equal(c.projectedS, 120);
  assert.equal(c.exceeds, false);
});

test("all of Wikipedia at 1 MB/s crosses twenty minutes; at 15 MB/s it does not", () => {
  const slow = twentyMinuteCheck({
    transferredBytes: 450 * MB,
    transferSeconds: 450,
    bytesRemaining: 11.05e9,
  });
  assert.equal(slow.exceeds, true);
  assert.ok(
    slow.projectedS > 3 * 3600,
    "hours, not minutes: " + slow.projectedS,
  );
  const fast = twentyMinuteCheck({
    transferredBytes: 450 * MB,
    transferSeconds: 30,
    bytesRemaining: 11.05e9,
  });
  assert.equal(fast.exceeds, false);
  assert.ok(fast.projectedS < TWENTY_MINUTES_S);
});

test("the essentials over a 0.3 MB/s cable cross twenty minutes too", () => {
  // The rule holds on the cable route as well: 450 MB at 0.3 MB/s is 25 min.
  const c = twentyMinuteCheck({
    transferredBytes: 30 * MB,
    transferSeconds: 100,
    bytesRemaining: 420 * MB,
  });
  assert.equal(c.exceeds, true);
});

test("the budget is exactly twenty minutes and a test can shrink it", () => {
  assert.equal(TWENTY_MINUTES_S, 1200);
  const c = twentyMinuteCheck({
    transferredBytes: 10 * MB,
    transferSeconds: 10,
    bytesRemaining: 0,
    budgetS: 5,
  });
  assert.equal(c.exceeds, true, "10 s spent against a 5 s budget");
  const d = twentyMinuteCheck({
    transferredBytes: 10 * MB,
    transferSeconds: 10,
    bytesRemaining: 0,
  });
  assert.equal(d.exceeds, false);
});

// --- formatting -------------------------------------------------------------

test("bytes print as the plan spells them", () => {
  assert.equal(formatBytes(110000), "110 KB");
  assert.equal(formatBytes(500), "1 KB");
  assert.equal(formatBytes(452e6), "452 MB");
  assert.equal(formatBytes(11.5e9), "11.5 GB");
  assert.equal(formatBytes(12e9), "12 GB");
  assert.equal(formatBytes(-1), "0 KB");
});

test("rates and durations", () => {
  assert.equal(formatRate(0), "");
  assert.equal(formatRate(50000), "50 KB/s");
  assert.equal(formatRate(1.23e6), "1.2 MB/s");
  assert.equal(formatRate(18.7e6), "19 MB/s");
  assert.equal(formatDuration(null), "");
  assert.equal(formatDuration(30), "under a minute");
  assert.equal(formatDuration(89), "about 1 minute");
  assert.equal(formatDuration(600), "about 10 minutes");
  assert.equal(formatDuration(3600), "about 1 hour");
  assert.equal(formatDuration(3900), "about 1 hour 5 minutes");
  assert.equal(formatDuration(7260), "about 2 hours 1 minute");
});
