// The Wikipedia install page, the browser half. Fetches the manifest, lets the
// person pick a route and the card, then streams the pack's files onto the
// card in order, hashing each one as it goes, and writes manifest.json last.
// Every decision that does not need a browser is in plan.js and tested there;
// this file owns the picker, the streams, the timing and the screen.
//
// The flow, in the plan's words (docs/apps/wikipedia-plan.md, "The page"):
// two route cards, choose the reader or the card, one progress bar with the
// measured time left, and the one thing left to do when it is done. The
// twenty-minute rule is enforced at every file boundary: the page measures
// what it has transferred so far, projects the rest of the chosen tier at
// that rate, and stops to say so before continuing if the projection crosses
// twenty minutes. It never prints a number it did not measure.

import { Sha256 } from "./sha256.js";
import {
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
  MARKER_FILE,
  RollingRate,
  secondsLeft,
  twentyMinuteCheck,
  TWENTY_MINUTES_S,
  formatBytes,
  formatRate,
  formatDuration,
  formatCount,
  formatSnapshot,
  partUrl,
} from "./plan.js";

// Where the pack lives: the Orange Pi behind its own Cloudflare Tunnel
// (server/packs/), a stable name that points at the current snapshot. The
// host answers CORS for any origin (GET and HEAD, with Content-Length and
// Range); without that every fetch fails as the browser's opaque "Failed to
// fetch" and the page can only say the connection dropped.
const PACK_BASE_URL = "https://packs.ma-r-s.com/wikipedia/en/";

// Bytes handed to the card per write. Fetch delivers pieces of 16 to 64 KB;
// writing each one is a round trip to the browser's file process, and 2 MB
// keeps that off the critical path without holding much in memory.
const WRITE_CHUNK = 2 * 1024 * 1024;

// ?mock=1 reads site/wikipedia/mock/ instead of the pack host, so the page can
// be looked at and driven from a laptop. Two knobs exist only there: rate=<MB/s>
// throttles the copy so the measured numbers are real numbers, and
// minutes=<n> shortens the twenty-minute budget so the pause can be reached
// on a 12 MB mock. Neither is read outside mock mode.
const params = new URLSearchParams(location.search);
const MOCK = params.get("mock") === "1";
const baseUrl = MOCK ? new URL("./mock/", location.href).href : PACK_BASE_URL;

// Every part's URL carries its own checksum, so a rebuilt pack is a new URL
// to every cache between here and the host. Without it, dict.zst (same name,
// same 110,000 bytes, different bytes after a rebuild) came back from the
// edge as the previous build's and the page called it damaged, twice, on the
// evening of 2026-09-11. The static host ignores the query.
function fileUrl(f) {
  return partUrl(state.manifest, baseUrl, f);
}

// The manifest as the host has it right now, or null. Asked for once, when a
// part's checksum disagrees: a rebuild published while this copy ran means
// the manifest in hand names parts the host no longer serves at these
// addresses, and that is the pack moving on, not a damaged transfer.
async function freshManifest() {
  try {
    const resp = await fetch(baseUrl + "manifest.json", { cache: "no-store" });
    if (!resp.ok) return null;
    return parseManifest(await resp.text());
  } catch (e) {
    return null;
  }
}
const mockRate = MOCK ? Number(params.get("rate")) || 0 : 0;
const budgetS =
  MOCK && params.get("minutes")
    ? Number(params.get("minutes")) * 60
    : TWENTY_MINUTES_S;

const $ = (id) => document.getElementById(id);
const canPickFolders = "showDirectoryPicker" in window;

// The route radios, by route id.
function radio(id) {
  return id === "all" ? $("routeAll") : $("routeEssentials");
}
function meta(id) {
  return id === "all" ? $("routeAllMeta") : $("routeEssentialsMeta");
}

const state = {
  manifest: null,
  card: null, // what the reader said it has, from install.json on the chosen card
  replanned: false, // the pack moved on under one copy already; the next time it is a reload
  manifestText: "",
  route: "essentials",
  root: null, // the card, a FileSystemDirectoryHandle
  running: false,
  abort: null,
  // The twenty-minute pause was answered with "keep going": do not ask again.
  acknowledged: false,
  // Measured this session: bytes over the network and the time spent on them.
  // Skipped and verified files do not count; they were not transferred.
  transferred: 0,
  transferMs: 0,
  rolling: new RollingRate(),
  wakeLock: null,
  lastPaint: 0,
};

class PackError extends Error {
  constructor(kind, message) {
    super(message);
    this.kind = kind; // net | card | full | damaged
  }
}

// --- the manifest -----------------------------------------------------------

async function loadManifest() {
  $("loadError").hidden = true;
  try {
    const resp = await fetch(baseUrl + "manifest.json", { cache: "no-store" });
    if (!resp.ok) throw new Error("the pack's host answered " + resp.status);
    const text = await resp.text();
    state.manifest = parseManifest(text);
    state.manifestText = text;
  } catch (e) {
    showLoadError(e);
    return;
  }
  const m = state.manifest;
  $("packLine").textContent =
    "This is Wikipedia from " +
    formatSnapshot(m.snapshot) +
    ", " +
    formatCount(m.articles) +
    " articles.";
  for (const r of ROUTES) {
    const tier = tierByName(m, r.tier);
    radio(r.id).disabled = !tier;
    const line = meta(r.id);
    line.textContent = "";
    if (tier) {
      line.appendChild(
        document.createTextNode(formatCount(tier.articles) + " articles, "),
      );
      // The size stays on one line: "12 / MB" split across two reads wrong.
      const size = document.createElement("span");
      size.className = "wk-nowrap";
      size.textContent = formatBytes(tierTotalBytes(m, r.tier));
      line.appendChild(size);
    } else {
      line.textContent = "Not published yet";
    }
  }
  if (radio(state.route).disabled) {
    const other = ROUTES.find((r) => !radio(r.id).disabled);
    if (other) {
      state.route = other.id;
      radio(other.id).checked = true;
    }
  }
  renderRoute();
  $("pickBtn").disabled = false;
}

function showLoadError(e) {
  const p = $("loadError");
  p.textContent = "";
  p.appendChild(
    document.createTextNode(
      "The pack could not be reached (" +
        ((e && e.message) || e) +
        "). Nothing on the card was touched. ",
    ),
  );
  const again = document.createElement("button");
  again.type = "button";
  again.className = "linklike";
  again.textContent = "Try again";
  again.addEventListener("click", loadManifest);
  p.appendChild(again);
  p.hidden = false;
  $("pickBtn").disabled = true;
}

// --- the route --------------------------------------------------------------

function currentRoute() {
  return routeById(state.route);
}

function renderRoute() {
  const r = currentRoute();
  $("pickLabel").textContent = r.pick;
  $("pickBtn").textContent = r.pick;
  $("pickText").textContent =
    r.id === "essentials"
      ? r.instruction +
        " On the reader, open Apps, then Wikipedia: it shows this page's address, and its screen says Connected once the computer has the card. Then press the button and pick the drive that just appeared; on a Mac it is called NO NAME."
      : r.instruction + " Then press the button and pick the card.";
  if (!canPickFolders) renderFileList();
}

function renderFileList() {
  const list = $("fileList");
  list.textContent = "";
  if (!state.manifest) return;
  for (const f of downloadList(state.manifest, currentRoute().tier)) {
    const li = document.createElement("li");
    const a = document.createElement("a");
    a.href = fileUrl(f);
    a.setAttribute("download", f.file.split("/").pop());
    a.textContent = f.file;
    li.appendChild(a);
    const size = document.createElement("span");
    size.textContent = f.kind === "manifest" ? "last" : formatBytes(f.bytes);
    li.appendChild(size);
    list.appendChild(li);
  }
}

// --- the card ---------------------------------------------------------------

function setPickStatus(text, kind) {
  const el = $("pickStatus");
  el.textContent = text;
  if (kind) el.dataset.kind = kind;
  else delete el.dataset.kind;
}

async function pick() {
  if (!state.manifest || state.running) return;
  let root;
  try {
    root = await window.showDirectoryPicker({
      mode: "readwrite",
      id: "crossplay-card",
    });
  } catch (e) {
    // Only AbortError is the person closing the picker. Everything else is
    // the call failing, and it must say so: a silent no-op here reads as a
    // dead button.
    if (e && e.name === "AbortError") {
      setPickStatus("Left the card untouched.");
      return;
    }
    console.error("showDirectoryPicker failed", e);
    setPickStatus(
      "This browser would not open a folder picker (" +
        ((e && e.name) || "unknown error") +
        "). Chrome or Edge can; or take the card out and use the by-hand route.",
      "bad",
    );
    return;
  }
  // Every CrossPlay card has /.crosspoint/. Nothing else is looked at.
  try {
    await root.getDirectoryHandle(".crosspoint");
  } catch (e) {
    setPickStatus(
      "That is not the reader's card. It has no .crosspoint folder; choose the card itself, not a folder inside it.",
      "bad",
    );
    return;
  }
  state.root = root;
  state.acknowledged = false;
  // What the reader wrote before handing the card over: which pack it has,
  // so the page can say what this copy changes rather than just "Done".
  state.card = null;
  try {
    const wiki = await root.getDirectoryHandle("wikipedia");
    const text = await readText(wiki, "install.json");
    const info = text ? JSON.parse(text) : null;
    if (info && typeof info.snapshot === "string" && info.snapshot) {
      state.card = { pack: info.pack, snapshot: info.snapshot, shardsPresent: info.shardsPresent };
    }
  } catch (e) {
    state.card = null;
  }
  setPickStatus(
    "This is the reader's card" +
      (root.name ? " (" + root.name + ")" : "") +
      (state.card
        ? ", with Wikipedia from " + formatSnapshot(state.card.snapshot) + " on it."
        : ", with no Wikipedia on it yet."),
    "good",
  );
  run();
}

// --- files on the card ------------------------------------------------------

async function fileHandleAt(dir, path, create) {
  const parts = path.split("/");
  let d = dir;
  for (let i = 0; i < parts.length - 1; i++) {
    d = await d.getDirectoryHandle(parts[i], { create: !!create });
  }
  return d.getFileHandle(parts[parts.length - 1], { create: !!create });
}

async function sizeOf(dir, path) {
  try {
    const h = await fileHandleAt(dir, path, false);
    return (await h.getFile()).size;
  } catch (e) {
    return null;
  }
}

async function readText(dir, name) {
  try {
    const h = await dir.getFileHandle(name);
    return await (await h.getFile()).text();
  } catch (e) {
    return "";
  }
}

async function writeText(dir, name, text) {
  const h = await dir.getFileHandle(name, { create: true });
  const w = await h.createWritable();
  await w.write(text);
  await w.close();
}

async function removeFile(dir, path) {
  const parts = path.split("/");
  let d = dir;
  for (let i = 0; i < parts.length - 1; i++) {
    d = await d.getDirectoryHandle(parts[i]);
  }
  await d.removeEntry(parts[parts.length - 1]);
}

// Read a file already on the card back and hash it. A card read, not a
// download: it costs seconds where a shard costs minutes.
async function hashExisting(dir, step, onProgress) {
  const h = await fileHandleAt(dir, step.file, false);
  const file = await h.getFile();
  const reader = file.stream().getReader();
  const hasher = new Sha256();
  let got = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    hasher.update(value);
    got += value.length;
    onProgress(got);
    throwIfAborted();
  }
  return hasher.hex();
}

// --- the copy ---------------------------------------------------------------

function throwIfAborted() {
  if (state.abort && state.abort.signal.aborted) {
    const e = new Error("aborted");
    e.name = "AbortError";
    throw e;
  }
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function isAbort(e) {
  return e && e.name === "AbortError";
}

function isCardError(e) {
  const n = e && e.name;
  return (
    n === "NotFoundError" ||
    n === "InvalidStateError" ||
    n === "NotAllowedError" ||
    n === "NotReadableError" ||
    n === "NoModificationAllowedError"
  );
}

// Stream one file from the pack host onto the card, hashing as it goes.
// Returns true when the bytes match the manifest, false when they do not
// (the caller deletes and retries once); throws on a lost connection, a
// vanished card or a full one.
async function copyOne(dir, step, meter) {
  const handle = await fileHandleAt(dir, step.file, true);
  const writable = await handle.createWritable();
  const hasher = new Sha256();
  // Two buffers: one being written to the card while the other fills from
  // the network. A write's buffer is not touched again until that write has
  // resolved, which the await before the next write guarantees.
  const bufs = [new Uint8Array(WRITE_CHUNK), new Uint8Array(WRITE_CHUNK)];
  let which = 0;
  let fill = 0;
  let pending = null;
  let got = 0;
  const t0 = performance.now();
  try {
    let resp;
    try {
      resp = await fetch(fileUrl(step), {
        signal: state.abort.signal,
        cache: "no-store",
      });
    } catch (e) {
      if (isAbort(e)) throw e;
      throw new PackError(
        "net",
        "The connection dropped while asking for " + step.file + ".",
      );
    }
    if (!resp.ok) {
      throw new PackError(
        "net",
        "The pack's host answered " + resp.status + " for " + step.file + ".",
      );
    }
    if (!resp.body) {
      throw new PackError(
        "net",
        "The pack's host sent nothing for " + step.file + ".",
      );
    }
    const reader = resp.body.getReader();
    for (;;) {
      let piece;
      try {
        piece = await reader.read();
      } catch (e) {
        if (isAbort(e)) throw e;
        throw new PackError(
          "net",
          "The connection dropped while copying " + step.file + ".",
        );
      }
      if (piece.done) break;
      const value = piece.value;
      // A throttled mock link: this piece took this long to arrive.
      if (mockRate) await sleep((value.length / (mockRate * 1e6)) * 1000);
      hasher.update(value);
      got += value.length;
      meter.transferred(value.length, got);
      let off = 0;
      while (off < value.length) {
        const buf = bufs[which];
        const n = Math.min(WRITE_CHUNK - fill, value.length - off);
        buf.set(value.subarray(off, off + n), fill);
        fill += n;
        off += n;
        if (fill === WRITE_CHUNK) {
          if (pending) await pending;
          pending = writable.write(buf);
          which ^= 1;
          fill = 0;
        }
      }
    }
    if (pending) await pending;
    if (fill) await writable.write(bufs[which].subarray(0, fill));
    await writable.close();
  } catch (e) {
    try {
      await writable.abort();
    } catch (_) {}
    // The entry itself was created before the stream opened, so a cut copy
    // would leave a 0-byte file where the part goes. The device reads a
    // wrong-sized shard as "not on the card yet" and the plan copies it
    // again either way, but nothing is gained by leaving it there.
    await removeFile(dir, step.file).catch(() => {});
    if (e instanceof PackError || isAbort(e)) throw e;
    if (e && e.name === "QuotaExceededError") {
      throw new PackError(
        "full",
        "The card is full. " +
          step.file +
          " needs " +
          formatBytes(step.bytes) +
          " and there is not that much room left on it.",
      );
    }
    if (isCardError(e)) {
      throw new PackError(
        "card",
        "The card went away while copying " + step.file + ".",
      );
    }
    throw e;
  } finally {
    state.transferMs += performance.now() - t0;
  }
  if (got !== step.bytes) return false;
  return hasher.hex() === step.sha256;
}

function partName(step, tier) {
  if (step.kind === "shard") {
    return "part " + (step.shard + 1) + " of " + tier.shards;
  }
  if (step.kind === "dict") return "the dictionary";
  if (step.kind === "titles") return "the title index";
  return "the block directory";
}

async function run() {
  if (state.running || !state.root || !state.manifest) return;
  state.running = true;
  state.abort = new AbortController();
  const route = currentRoute();
  const m = state.manifest;
  showCopy();
  window.addEventListener("beforeunload", warnUnload);
  await holdAwake();
  let currentFile = "";
  try {
    const wiki = await state.root.getDirectoryHandle("wikipedia", {
      create: true,
    });
    let markers = parseMarkers(await readText(wiki, MARKER_FILE));
    setCopyStatus(
      state.card && state.card.snapshot !== m.snapshot
        ? "The card has " +
            formatSnapshot(state.card.snapshot) +
            "; this copies " +
            formatSnapshot(m.snapshot) +
            ", the parts that changed. Looking at what is on the card already."
        : "Looking at what is on the card already.",
    );
    const existing = {};
    for (const f of filesForTier(m, route.tier)) {
      const size = await sizeOf(wiki, f.file);
      if (size !== null) existing[f.file] = { bytes: size };
    }
    const plan = makePlan(m, route.tier, existing, markers);
    const meter = makeMeter(plan);
    let done = 0;
    let firstShardDone = false;
    for (const step of plan.steps) {
      throwIfAborted();
      currentFile = step.file;
      const name = partName(step, plan.tier);
      let settled = step.action === "skip";
      if (step.action === "verify") {
        setCopyStatus(
          "Checking " + name + " already on the card (" + step.file + ").",
        );
        const hex = await hashExisting(wiki, step, (got) =>
          meter.at(done, got),
        );
        settled = hex === step.sha256;
      }
      if (!settled) {
        for (let attempt = 0; ; attempt++) {
          setCopyStatus(
            "Copying " +
              name +
              " (" +
              step.file +
              ", " +
              formatBytes(step.bytes) +
              ")" +
              (attempt ? ", once more: the first copy arrived damaged." : "."),
          );
          meter.at(done, 0);
          if (await copyOne(wiki, step, meter)) break;
          await removeFile(wiki, step.file).catch(() => {});
          if (attempt === 0) {
            const fresh = await freshManifest();
            if (fresh && fresh.built !== m.built) {
              throw new PackError(
                "updated",
                "The pack on the host was updated while this copy ran (built " +
                  (fresh.built || "later") +
                  " instead of " +
                  (m.built || "earlier") +
                  ").",
              );
            }
          }
          if (attempt >= 1) {
            throw new PackError(
              "damaged",
              step.file +
                " arrived damaged twice: its checksum does not match the manifest. Reload this page and choose the reader again; what already matches on the card is kept. If it keeps happening, the published pack is broken and this page cannot fix that.",
            );
          }
        }
      }
      if (step.action !== "skip") {
        markers = withMarker(markers, step.file, step.bytes, step.sha256);
        await writeText(wiki, MARKER_FILE, serializeMarkers(markers));
      }
      done += step.bytes;
      if (step.kind === "shard" && step.shard === 0) firstShardDone = true;
      meter.at(done, 0);
      // The twenty-minute rule, at every boundary with something still to
      // copy, until the person has said to keep going regardless.
      const remaining = plan.bytesTotal - done;
      if (remaining > 0 && !state.acknowledged) {
        const check = twentyMinuteCheck({
          transferredBytes: state.transferred,
          transferSeconds: state.transferMs / 1000,
          bytesRemaining: remaining,
          budgetS,
        });
        if (check.exceeds) {
          pause(check, plan, firstShardDone);
          return;
        }
      }
    }
    currentFile = "manifest.json";
    setCopyStatus("Writing the manifest.");
    await writeText(wiki, "manifest.json", state.manifestText);
    finish(plan);
  } catch (e) {
    stop(e, currentFile);
  } finally {
    state.running = false;
    state.abort = null;
    window.removeEventListener("beforeunload", warnUnload);
    releaseAwake();
  }
}

// --- the meter --------------------------------------------------------------
//
// One bar for the whole tier. Skipped parts count as done the moment the plan
// is made, so a resumed copy starts where the last one stopped. The rate is
// the rolling ten-second one; the time left uses it, or the session average
// while the window is still filling.

function makeMeter(plan) {
  const total = plan.bytesTotal;
  let base = 0; // bytes accounted for before the current file
  function paint(doneBytes, force) {
    const now = performance.now();
    if (!force && now - state.lastPaint < 250) return;
    state.lastPaint = now;
    const pct = total ? Math.min(100, (doneBytes / total) * 100) : 100;
    $("barFill").style.width = pct.toFixed(2) + "%";
    $("bar").setAttribute("aria-valuenow", String(Math.round(pct)));
    $("bytesLine").textContent =
      formatBytes(doneBytes) + " of " + formatBytes(total);
    const rolling = state.rolling.rate(now);
    const average =
      state.transferMs > 0
        ? state.transferred / (state.transferMs / 1000)
        : null;
    $("rateLine").textContent = rolling ? formatRate(rolling) : "";
    const left = secondsLeft(total - doneBytes, rolling || average);
    $("leftLine").textContent =
      left === null || doneBytes >= total ? "" : formatDuration(left) + " left";
  }
  return {
    // A network piece arrived: `n` bytes just now, `got` so far in this file.
    transferred(n, got) {
      state.transferred += n;
      state.rolling.push(performance.now(), state.transferred);
      paint(base + got, false);
    },
    // Progress that is not a transfer: a verify read, or a boundary.
    at(doneBefore, got) {
      base = doneBefore;
      paint(doneBefore + got, true);
    },
  };
}

// --- the screens ------------------------------------------------------------

function setCopyStatus(text, kind) {
  const el = $("copyStatus");
  el.textContent = text;
  if (kind) el.dataset.kind = kind;
  else delete el.dataset.kind;
}

// Nothing is moving, so the last rate and the time left are no longer
// measurements of anything; the bar and the byte count stay.
function clearRate() {
  $("rateLine").textContent = "";
  $("leftLine").textContent = "";
}

function lockRoutes(locked) {
  const m = state.manifest;
  for (const r of ROUTES) {
    radio(r.id).disabled = locked || !tierByName(m, r.tier);
  }
  $("pickBtn").disabled = locked;
}

function showCopy() {
  $("stepCopy").hidden = false;
  $("pauseBox").hidden = true;
  $("stopBox").hidden = true;
  $("doneBox").hidden = true;
  $("copyFine").hidden = false;
  lockRoutes(true);
  setCopyStatus("");
  $("stepCopy").scrollIntoView({ behavior: "smooth", block: "nearest" });
}

function pause(check, plan, firstShardDone) {
  const route = currentRoute();
  const other = otherRoute(route.id);
  const rate = formatRate(check.rate);
  const left = formatDuration(check.projectedS - state.transferMs / 1000);
  const spent = formatDuration(state.transferMs / 1000);
  let text;
  if (route.id === "all") {
    const ess = tierByName(state.manifest, "essentials");
    text =
      "At the speed measured so far (" +
      rate +
      "), the rest of Wikipedia needs " +
      left +
      " more, and this page promised twenty minutes. " +
      (firstShardDone
        ? "The essentials are on the card already: stop here and the reader has its " +
          formatCount(ess ? ess.articles : 0) +
          " most important articles now, and the rest can be added later from this page with the card in the computer."
        : "Stopping at the essentials gets the reader its most important articles first; the rest can be added later from this page.");
    $("switchBtn").textContent = "Stop at the essentials";
    $("switchBtn").onclick = () => {
      state.route = other.id;
      radio(other.id).checked = true;
      state.acknowledged = true;
      renderRoute();
      run();
    };
  } else {
    text =
      "At the speed measured so far (" +
      rate +
      "), the essentials need " +
      left +
      " more, with " +
      spent +
      " spent, and this page promised twenty minutes. The cable is the slow part: with the card in the computer the same files copy many times faster, and the page continues where it stopped.";
    $("switchBtn").textContent = "Use the card in the computer instead";
    $("switchBtn").onclick = () => {
      state.route = other.id;
      radio(other.id).checked = true;
      $("pauseBox").hidden = true;
      $("stepCopy").hidden = true;
      lockRoutes(false);
      renderRoute();
      setPickStatus(
        "Eject the reader, take its card out and put it in the computer. Then press " +
          other.pick +
          ".",
      );
      $("stepPick").scrollIntoView({ behavior: "smooth", block: "nearest" });
    };
  }
  $("pauseText").textContent = text;
  $("keepBtn").onclick = () => {
    state.acknowledged = true;
    run();
  };
  clearRate();
  setCopyStatus("Paused before the next part. Nothing is being copied.");
  $("pauseBox").hidden = false;
  $("pauseBox").scrollIntoView({ behavior: "smooth", block: "nearest" });
}

function stop(e, file) {
  clearRate();
  if (isAbort(e)) {
    setCopyStatus("Stopped. What was copied so far is kept.");
    lockRoutes(false);
    return;
  }
  console.error("copy stopped", e);
  let head = "The copy stopped.";
  let text;
  if (e instanceof PackError) {
    text = e.message;
    if (e.kind === "net") {
      text +=
        " What was copied so far is kept; press Resume when the connection is back and it continues from the part that was cut.";
    } else if (e.kind === "card") {
      head = "The card went away.";
      text +=
        " Put it back, then press Resume. If the page cannot find it, choose it again.";
    } else if (e.kind === "full") {
      head = "The card is full.";
      text += " Make room on it, then press Resume.";
    } else if (e.kind === "damaged") {
      head = "A part arrived damaged.";
    } else if (e.kind === "updated") {
      // Once: read the new manifest, plan again over what is on the card
      // (matching parts are kept), and carry on without a hand.
      head = "The pack was updated.";
      text += state.replanned
        ? " Reload this page and choose the reader again to continue with the new one."
        : " Continuing with the new one; parts on the card that still match are kept.";
      if (!state.replanned) {
        state.replanned = true;
        setTimeout(async () => {
          await loadManifest();
          run();
        }, 0);
        setCopyStatus(text);
        return;
      }
    }
  } else if (isCardError(e)) {
    head = "The card went away.";
    text =
      "The card stopped answering" +
      (file ? " while working on " + file : "") +
      ". Put it back, then press Resume. If the page cannot find it, choose it again.";
  } else {
    text =
      "Something went wrong" +
      (file ? " at " + file : "") +
      ": " +
      ((e && e.message) || e) +
      ". What was copied so far is kept.";
  }
  $("stopHead").textContent = head;
  $("stopText").textContent = text;
  $("stopBox").hidden = false;
  setCopyStatus("Stopped.", "bad");
  $("stopBox").scrollIntoView({ behavior: "smooth", block: "nearest" });
}

function finish(plan) {
  const route = currentRoute();
  const m = state.manifest;
  $("barFill").style.width = "100%";
  $("bar").setAttribute("aria-valuenow", "100");
  $("bytesLine").textContent =
    formatBytes(plan.bytesTotal) + " of " + formatBytes(plan.bytesTotal);
  $("rateLine").textContent = "";
  $("leftLine").textContent = "";
  $("copyFine").hidden = true;
  setCopyStatus(
    "Done: " +
      formatCount(plan.tier.articles) +
      " articles from " +
      formatSnapshot(m.snapshot) +
      " are on the card, every part checked.",
    "good",
  );
  $("doneLine").textContent = route.done;
  $("doneWhy").textContent = route.doneWhy;
  $("doneBox").hidden = false;
  lockRoutes(false);
  $("doneBox").scrollIntoView({ behavior: "smooth", block: "nearest" });
}

// --- keeping the machine awake while it copies ------------------------------
//
// Fifteen minutes is longer than a laptop's lid stays open by habit, and a
// sleeping machine stalls the copy silently. Best effort: the lock is not
// granted everywhere and the copy resumes either way.

async function holdAwake() {
  try {
    if (navigator.wakeLock) {
      state.wakeLock = await navigator.wakeLock.request("screen");
    }
  } catch (e) {
    state.wakeLock = null;
  }
}

function releaseAwake() {
  if (state.wakeLock) {
    state.wakeLock.release().catch(() => {});
    state.wakeLock = null;
  }
}

function warnUnload(ev) {
  ev.preventDefault();
  ev.returnValue = "";
}

// --- wiring -----------------------------------------------------------------

for (const r of ROUTES) {
  radio(r.id).addEventListener("change", (ev) => {
    if (ev.target.checked) {
      state.route = r.id;
      state.acknowledged = false;
      renderRoute();
    }
  });
}
$("pickBtn").addEventListener("click", pick);
$("resumeBtn").addEventListener("click", () => {
  $("stopBox").hidden = true;
  run();
});
$("repickBtn").addEventListener("click", () => {
  $("stopBox").hidden = true;
  $("stepCopy").hidden = true;
  lockRoutes(false);
  pick();
});

if (!canPickFolders) {
  $("stepPick").hidden = true;
  $("stepCopy").hidden = true;
  $("unsupported").hidden = false;
}

renderRoute();
loadManifest();
