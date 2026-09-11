// The Wikipedia install page, the pure half. No DOM, no fetch, no file handles:
// everything here takes plain values and returns plain values, so
// tests/plan.test.js pins it under node with no browser. wikipedia.js is the
// browser half and owns the picker, the streams and the screen.
//
// What lives here, in the order the page uses it:
//   parseManifest      the published manifest.json, checked field by field
//   ROUTES             the two routes and their wording, from docs/apps/wikipedia-plan.md
//   filesForTier       which files a tier needs, in the order they are written
//   makePlan           which of those to copy, verify or skip, given what is on the card
//   RollingRate        MB/s over the last ten seconds, for the progress line
//   twentyMinuteCheck  the rule: measure first, and stop to say so if the
//                      projection for the chosen tier crosses twenty minutes
//   formatBytes, formatRate, formatDuration   the numbers as the page prints them

export const TWENTY_MINUTES_S = 20 * 60;

// A projection made from less than this is TCP slow start and the first small
// file, not a rate. Both must hold before the page prints or acts on one.
export const PROJECTION_MIN_BYTES = 4 * 1024 * 1024;
export const PROJECTION_MIN_SECONDS = 2;

export const RATE_WINDOW_MS = 10000;

export const SHA256_RE = /^[0-9a-f]{64}$/;

// The marker the page leaves beside the pack: which files it has written and
// verified, by sha256, so the next visit can skip them without re-reading a
// gigabyte. It travels with the card, which a browser's own storage would not.
export const MARKER_FILE = "copied.json";

// --- the manifest -----------------------------------------------------------

function fail(what) {
  throw new Error(
    "The pack's manifest is not one this page understands: " + what,
  );
}

function isInt(n) {
  return typeof n === "number" && Number.isInteger(n) && n >= 0;
}

function checkFile(entry, where) {
  if (!entry || typeof entry !== "object") fail(where + " is missing");
  if (typeof entry.file !== "string" || !entry.file) fail(where + ".file");
  if (entry.file.startsWith("/") || entry.file.includes("..")) {
    fail(where + ".file points outside the pack");
  }
  if (!isInt(entry.bytes)) fail(where + ".bytes");
  if (typeof entry.sha256 !== "string" || !SHA256_RE.test(entry.sha256)) {
    fail(where + ".sha256");
  }
  return { file: entry.file, bytes: entry.bytes, sha256: entry.sha256 };
}

// Takes the manifest as text or as an already-parsed object. Returns a clean
// copy holding only the fields the page uses, or throws with one plain
// sentence saying which field is wrong. Extra fields are ignored, so a format
// that grows does not break an older page; a format number this page has not
// seen does, because the file layout may have changed under it.
export function parseManifest(input) {
  let m = input;
  if (typeof input === "string") {
    try {
      m = JSON.parse(input);
    } catch (e) {
      fail("it is not JSON");
    }
  }
  if (!m || typeof m !== "object") fail("it is not an object");
  if (m.format !== 1) fail("format " + JSON.stringify(m.format));
  if (typeof m.pack !== "string" || !m.pack) fail("pack");
  if (typeof m.snapshot !== "string" || !m.snapshot) fail("snapshot");
  if (!isInt(m.articles)) fail("articles");
  const dict = checkFile(m.dict, "dict");
  // One title index per tier (titles.0.idx, titles.1.idx, ...), in tier
  // order: the spec's shape, so the essentials are readable with their own
  // titles before the rest of the shards exist on the card.
  if (!Array.isArray(m.titles) || m.titles.length === 0) fail("titles");
  const titles = m.titles.map((t, i) => {
    const f = checkFile(t, "titles[" + i + "]");
    if (!isInt(t.tier)) fail("titles[" + i + "].tier");
    return { ...f, tier: t.tier };
  });
  const blocksdir = checkFile(m.blocksdir, "blocksdir");
  if (!Array.isArray(m.shards) || m.shards.length === 0) fail("shards");
  const shards = m.shards.map((s, i) => {
    const f = checkFile(s, "shards[" + i + "]");
    if (!isInt(s.firstBlock)) fail("shards[" + i + "].firstBlock");
    if (!isInt(s.blocks)) fail("shards[" + i + "].blocks");
    return { ...f, firstBlock: s.firstBlock, blocks: s.blocks };
  });
  if (!Array.isArray(m.tiers) || m.tiers.length === 0) fail("tiers");
  const tiers = m.tiers.map((t, i) => {
    if (!t || typeof t.name !== "string" || !t.name)
      fail("tiers[" + i + "].name");
    if (!isInt(t.shards) || t.shards < 1 || t.shards > shards.length) {
      fail("tiers[" + i + "].shards");
    }
    if (!isInt(t.articles)) fail("tiers[" + i + "].articles");
    if (!isInt(t.bytes)) fail("tiers[" + i + "].bytes");
    return {
      name: t.name,
      shards: t.shards,
      articles: t.articles,
      bytes: t.bytes,
    };
  });
  return {
    format: 1,
    pack: m.pack,
    snapshot: m.snapshot,
    articles: m.articles,
    dict,
    titles,
    blocksdir,
    shards,
    tiers,
  };
}

export function tierByName(manifest, name) {
  return manifest.tiers.find((t) => t.name === name) || null;
}

// --- the two routes ---------------------------------------------------------
//
// The wording is the plan's (docs/apps/wikipedia-plan.md, "The page"). The
// "about" times are the plan's promise, not a measurement: the page measures
// the moment it starts copying and says so if the measurement disagrees.

export const ROUTES = [
  {
    id: "essentials",
    tier: "essentials",
    title: "The essentials, with the cable.",
    time: "About ten minutes.",
    instruction: "Plug the reader into the computer with its cable.",
    pick: "Choose the reader",
    picked: "the reader",
    done: "Eject the reader, then unplug it.",
    doneWhy:
      "The eject is what restarts it into Wikipedia; the screen changing is the sign that it worked.",
  },
  {
    id: "all",
    tier: "all",
    title: "All of Wikipedia, with the card in the computer.",
    time: "About fifteen minutes.",
    instruction:
      "Take the card out of the reader and put it in the computer, in its slot or in a card reader.",
    pick: "Choose the card",
    picked: "the card",
    done: "Put the card back in the reader and open Wikipedia.",
    doneWhy: "",
  },
];

export function routeById(id) {
  return ROUTES.find((r) => r.id === id) || null;
}

export function otherRoute(id) {
  return ROUTES.find((r) => r.id !== id) || null;
}

// --- files and the plan -----------------------------------------------------

// Every file a tier needs, in the order the page writes them, which is the
// spec's: dict.zst, blocks.dir, then for each tier up to the one wanted its
// title index followed by its shards. So after the first tier the essentials
// are complete and readable with their own titles, and nothing written for
// them is dead weight. The manifest is not in this list; it is written last,
// by itself, because its presence is what tells the device a pack is there.
export function filesForTier(manifest, tierName) {
  const wanted = manifest.tiers.findIndex((t) => t.name === tierName);
  if (wanted < 0) throw new Error("The pack has no tier called " + tierName + ".");
  const out = [
    { ...manifest.dict, kind: "dict" },
    { ...manifest.blocksdir, kind: "blocksdir" },
  ];
  let from = 0;
  for (let k = 0; k <= wanted; k++) {
    const index = manifest.titles.find((t) => t.tier === k);
    if (index) out.push({ ...index, kind: "titles" });
    const upTo = manifest.tiers[k].shards;
    for (let i = from; i < upTo; i++) {
      out.push({ ...manifest.shards[i], kind: "shard", shard: i });
    }
    from = upTo;
  }
  return out;
}

export function tierTotalBytes(manifest, tierName) {
  return filesForTier(manifest, tierName).reduce((n, f) => n + f.bytes, 0);
}

// The download list for a browser that cannot write the card itself: the
// tier's files plus the manifest, last, as the by-hand instructions say.
export function downloadList(manifest, tierName) {
  return filesForTier(manifest, tierName).concat([
    { file: "manifest.json", bytes: 0, sha256: "", kind: "manifest" },
  ]);
}

// What to do with each file, given what is already on the card.
//
//   existing  { [file]: { bytes } }         what the card holds, by size
//   markers   { [file]: { bytes, sha256 } } what this page verified on an
//                                           earlier visit (MARKER_FILE)
//
// A file whose size differs from the manifest is copied. One whose size
// matches AND whose marker names the manifest's sha256 is skipped outright.
// One whose size matches with no marker (a pack copied by hand, or a marker
// lost) is verified: the page reads it back and hashes it, which costs a card
// read rather than a download, and then either skips or copies it. A file the
// card does not have is copied.
export function makePlan(manifest, tierName, existing, markers) {
  const files = filesForTier(manifest, tierName);
  const have = existing || {};
  const marks = markers || {};
  const steps = files.map((f) => {
    const on = have[f.file];
    const mark = marks[f.file];
    let action = "copy";
    if (on && on.bytes === f.bytes) {
      action =
        mark && mark.bytes === f.bytes && mark.sha256 === f.sha256
          ? "skip"
          : "verify";
    }
    return { ...f, action };
  });
  const bytesTotal = steps.reduce((n, s) => n + s.bytes, 0);
  const bytesSkipped = steps
    .filter((s) => s.action === "skip")
    .reduce((n, s) => n + s.bytes, 0);
  return {
    tier: tierByName(manifest, tierName),
    steps,
    bytesTotal,
    bytesSkipped,
    bytesToCopy: bytesTotal - bytesSkipped,
  };
}

// --- markers ----------------------------------------------------------------

export function parseMarkers(text) {
  if (!text) return {};
  let m;
  try {
    m = JSON.parse(text);
  } catch (e) {
    return {};
  }
  if (!m || typeof m !== "object" || !m.files || typeof m.files !== "object") {
    return {};
  }
  const out = {};
  for (const [file, v] of Object.entries(m.files)) {
    if (
      v &&
      isInt(v.bytes) &&
      typeof v.sha256 === "string" &&
      SHA256_RE.test(v.sha256)
    ) {
      out[file] = { bytes: v.bytes, sha256: v.sha256 };
    }
  }
  return out;
}

export function withMarker(markers, file, bytes, sha256) {
  return { ...markers, [file]: { bytes, sha256 } };
}

export function serializeMarkers(markers) {
  return JSON.stringify({ format: 1, files: markers }, null, 1);
}

// --- rate and time ----------------------------------------------------------

// Bytes per second over the last `windowMs`. push() takes the time and the
// cumulative byte count so far; rate() answers from the newest sample and the
// newest sample at or before the window's start, so a stall shows up as the
// number falling rather than freezing at the last burst.
export class RollingRate {
  constructor(windowMs = RATE_WINDOW_MS) {
    this.windowMs = windowMs;
    this.samples = [];
  }

  push(tMs, cumulativeBytes) {
    this.samples.push({ t: tMs, c: cumulativeBytes });
    // Keep one sample older than the window as the anchor; drop the rest.
    const start = tMs - this.windowMs;
    let keepFrom = 0;
    for (let i = this.samples.length - 1; i >= 0; i--) {
      if (this.samples[i].t <= start) {
        keepFrom = i;
        break;
      }
    }
    if (keepFrom > 0) this.samples.splice(0, keepFrom);
  }

  // null until two samples at least half a second apart exist.
  rate(nowMs) {
    const s = this.samples;
    if (s.length < 2) return null;
    const last = s[s.length - 1];
    const start = (nowMs === undefined ? last.t : nowMs) - this.windowMs;
    let first = s[0];
    for (let i = 0; i < s.length - 1; i++) {
      if (s[i].t <= start) first = s[i];
      else break;
    }
    const dt = (last.t - first.t) / 1000;
    if (dt < 0.5) return null;
    return (last.c - first.c) / dt;
  }
}

// Seconds left for `bytesRemaining` at `rate` bytes/s, or null when there is
// no rate to project from. Never prints a number it did not measure.
export function secondsLeft(bytesRemaining, rate) {
  if (!rate || rate <= 0) return null;
  return bytesRemaining / rate;
}

// The twenty-minute rule.
//
//   transferredBytes  bytes that came over the network this session (skipped
//                     files do not count; they were not measured)
//   transferSeconds   wall time spent transferring them
//   bytesRemaining    what the chosen tier still needs
//   budgetS           twenty minutes, unless a test says otherwise
//
// Returns { measured, rate, projectedS, exceeds }. `measured` is false until
// enough has been transferred to call the rate a rate, and then nothing else
// is filled in; the page must not act on an unmeasured projection. When
// measured, projectedS is the whole job's time at the session's average rate:
// what has taken this long so far, plus the rest at the same speed.
export function twentyMinuteCheck({
  transferredBytes,
  transferSeconds,
  bytesRemaining,
  budgetS = TWENTY_MINUTES_S,
}) {
  if (
    !(transferredBytes >= PROJECTION_MIN_BYTES) ||
    !(transferSeconds >= PROJECTION_MIN_SECONDS)
  ) {
    return { measured: false, rate: null, projectedS: null, exceeds: false };
  }
  const rate = transferredBytes / transferSeconds;
  const projectedS = transferSeconds + bytesRemaining / rate;
  return { measured: true, rate, projectedS, exceeds: projectedS > budgetS };
}

// --- formatting -------------------------------------------------------------
//
// Decimal units, the plan's own spelling: 450 MB, 11.5 GB.

export function formatBytes(n) {
  if (!(n >= 0)) return "0 KB";
  if (n < 1e6) return Math.max(1, Math.round(n / 1e3)) + " KB";
  if (n < 1e9) return Math.round(n / 1e6) + " MB";
  return (n / 1e9).toFixed(1).replace(/\.0$/, "") + " GB";
}

export function formatRate(bytesPerSecond) {
  if (!(bytesPerSecond > 0)) return "";
  const mb = bytesPerSecond / 1e6;
  if (mb < 0.1) return Math.round(bytesPerSecond / 1e3) + " KB/s";
  return (mb < 10 ? mb.toFixed(1) : Math.round(mb)) + " MB/s";
}

export function formatDuration(seconds) {
  if (seconds === null || seconds === undefined || !(seconds >= 0)) return "";
  if (seconds < 60) return "under a minute";
  const minutes = Math.round(seconds / 60);
  if (minutes < 60)
    return "about " + minutes + (minutes === 1 ? " minute" : " minutes");
  const hours = Math.floor(minutes / 60);
  const rest = minutes % 60;
  let s = "about " + hours + (hours === 1 ? " hour" : " hours");
  if (rest) s += " " + rest + (rest === 1 ? " minute" : " minutes");
  return s;
}

export function formatCount(n) {
  return Number(n).toLocaleString("en-US");
}

// "2026-05-13" as the app's settings row says it: "May 2026". Anything that
// is not a date of that shape is printed as it came.
const MONTHS = [
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December",
];
export function formatSnapshot(snapshot) {
  const m = /^(\d{4})-(\d{2})(?:-(\d{2}))?$/.exec(String(snapshot));
  if (!m) return String(snapshot);
  const month = MONTHS[Number(m[2]) - 1];
  return month ? month + " " + m[1] : String(snapshot);
}
