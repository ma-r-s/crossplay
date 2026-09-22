// Live: draw a note here, a reader on somebody's fridge shows it.
//
// THE SERVICE IS ON ANOTHER HOST. This page is part of the CrossPlay site;
// fridge.ma-r-s.com answers /api/ and has no page of its own. Both names sit
// under ma-r-s.com, so the sender cookie (scoped to .ma-r-s.com by the service)
// is first-party for both and rides these requests -- but only because every
// fetch below asks for it: `credentials: "include"` is what a cross-ORIGIN
// request needs, and "same-origin", which is the default, would send nothing at
// all and every call would come back as "not connected".

// UNROUTABLE FROM THE INTERNET. This is now only about ?demo: it is the one
// flag that shows made-up content, so it is allowed only where the page cannot
// be something somebody shares. It was also the gate on ?local, which is now
// decided from the origin instead.
const PRIVATE_HOST =
  /^(localhost|127\.\d+\.\d+\.\d+|\[::1\]|10\.\d+\.\d+\.\d+|192\.168\.\d+\.\d+|172\.(1[6-9]|2\d|3[01])\.\d+\.\d+|.*\.local)$/;

const LIVE_API = "https://fridge.ma-r-s.com";
// THE PRODUCTION PAGE, and the only origin the service will talk to: its CORS
// list is exactly this one string, with credentials, because a list of origins
// is a list of sites allowed to draw on somebody's reader.
const SITE_ORIGIN = "https://crossplay.ma-r-s.com";

// OFF PRODUCTION, USE THE PROXY. No flag, no hostname list.
//
// This was `?local`, honoured on localhost, and the default off production was
// therefore the broken one: Mario opened the dev server on his phone without
// the query string, the page called fridge.ma-r-s.com from an IP-address
// origin, the service refused the origin outright, and the page said "could not
// reach the service" -- accurate, useless, and recoverable only by a flag
// nobody would guess.
//
// Decided from the page's OWN ORIGIN rather than from a list of hostnames, so a
// laptop, a phone on the LAN, a tunnel and a preview deployment all behave the
// same without anybody enumerating them. `?local` still works and still forces
// the proxy, so nothing that relies on it breaks; it is simply no longer the
// only way.
const params = new URLSearchParams(location.search);
const offProduction = location.origin !== SITE_ORIGIN;
const local = offProduction || params.has("local");
const API = local ? "" : LIVE_API;

// WHETHER THIS ORIGIN HAS A PROXY AT ALL is a different question from whether
// it is production, and the two were conflated. site/serve.py proxies /api/ to
// the real service; a preview deployment is static hosting and does not. Both
// are "off production", and only one of them can reach a reader.
//
// Answered by what /api/ actually replies rather than by guessing from the
// host: `probedProxy` is set on the first call, and until then nothing claims
// either way.
let probedProxy = null;

// ?demo=N fills the page with made-up entries and needs no service at all,
// which is the right way to judge a layout. Localhost-ish only, so it can never
// dress up a real reader's page: `offProduction` is true on a preview too, and
// a made-up history on a URL somebody might share is a lie with a link.
const demoCount =
  PRIVATE_HOST.test(location.hostname) && params.has("demo")
    ? +params.get("demo")
    : null;

const keep = [];
if (local) keep.push("local");
if (demoCount !== null) keep.push("demo=" + demoCount);
if (PRIVATE_HOST.test(location.hostname) && params.has("pending"))
  keep.push("pending");
const cleanUrl = () =>
  location.pathname + (keep.length ? "?" + keep.join("&") : "");

const W = 480;
const H = 800;
const pad = document.getElementById("pad");
const ctx = pad.getContext("2d", { willReadFrequently: true });
// One byte per pixel holding a LEVEL 0..3: 0 black, 3 paper. Not one bit.
let bits = new Uint8Array(W * H).fill(3);
let pen = 10;
let inverted = false;
let mode = "draw";
let photo = null;
let photoMode = "fill";

// The four levels as the panel renders them, so the canvas IS the screen.
const LEVEL_GREY = [0, 85, 170, 255];

// PACKED, because an undo stack of full level arrays is 384KB a step and this
// runs on a phone: twenty of them is 7.7MB of the tab's budget spent on the
// history of one drawing. Two bits a pixel is the same information in 96KB,
// which is also exactly the form the reader is sent, so nothing is approximated
// by storing it this way.
function pack(levels) {
  const out = new Uint8Array((W * H) >> 2);
  for (let i = 0; i < W * H; i++)
    out[i >> 2] |= (levels[i] & 3) << ((3 - (i & 3)) * 2);
  return out;
}
function unpack(packed) {
  const out = new Uint8Array(W * H);
  for (let i = 0; i < W * H; i++)
    out[i] = (packed[i >> 2] >> ((3 - (i & 3)) * 2)) & 3;
  return out;
}

const undoStack = [];
const snap = () => {
  undoStack.push(pack(bits));
  if (undoStack.length > 20) undoStack.shift();
};

// ONE BUFFER, KEPT. Allocating a 480x800 ImageData and rewriting all 384,000
// pixels is fine once; it was happening on every pointermove, which is what
// made a fast finger lag. The buffer is allocated once and only the pixels
// that changed are rewritten.
const frame = ctx.createImageData(W, H);
{
  const d = frame.data;
  for (let i = 3; i < d.length; i += 4) d[i] = 255;
}

// The box a stroke has touched since the last blit, in panel pixels.
let dirty = null;
function markDirty(x0, y0, x1, y1) {
  if (!dirty) dirty = [x0, y0, x1, y1];
  else {
    if (x0 < dirty[0]) dirty[0] = x0;
    if (y0 < dirty[1]) dirty[1] = y0;
    if (x1 > dirty[2]) dirty[2] = x1;
    if (y1 > dirty[3]) dirty[3] = y1;
  }
}

function blit(x0, y0, x1, y1) {
  const d = frame.data;
  for (let yy = y0; yy <= y1; yy++) {
    const row = yy * W;
    for (let xx = x0; xx <= x1; xx++) {
      const i = row + xx;
      const v = LEVEL_GREY[bits[i]];
      d[i * 4] = d[i * 4 + 1] = d[i * 4 + 2] = v;
    }
  }
  ctx.putImageData(frame, 0, 0, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

// The whole panel. Every caller that replaces `bits` wholesale -- undo, clear,
// a photo, a draft restored -- goes through here, so none of them can be left
// showing a stale region.
function render() {
  blit(0, 0, W - 1, H - 1);
  dirty = null;
  markTools();
  saveDraftSoon();
}

// Just what the current stroke touched. Used only by the two paths that mark
// the panel a few pixels at a time.
// Pixels only. Enabling undo and saving the draft are per-STROKE facts, not
// per-event ones, so they happen once when the finger lifts instead of on
// every sample; running them here put a style recalc in the middle of a line.
// ONE PAINT PER FRAME. A phone delivers pointermove far faster than it can
// composite -- 120Hz sampling and higher with coalescing -- so painting on
// every sample does work the screen never shows and starves the frame that
// matters. The marks still land on every sample; only the blit is batched.
let painting = 0;
function renderStroke() {
  if (!dirty || painting) return;
  painting = requestAnimationFrame(() => {
    painting = 0;
    if (!dirty) return;
    blit(dirty[0], dirty[1], dirty[2], dirty[3]);
    dirty = null;
  });
}
function renderStrokeNow() {
  if (painting) {
    cancelAnimationFrame(painting);
    painting = 0;
  }
  if (!dirty) return;
  blit(dirty[0], dirty[1], dirty[2], dirty[3]);
  dirty = null;
}

// THE DRAWING SURVIVES A RELOAD, which is what makes an unconfirmed Clear an
// honest offer. Undo covers a mis-tap inside the session; a phone discarding
// the tab while somebody answers the door is the other half, and without this
// the drawing was simply gone and "Undo puts it back" would have been a
// sentence that is only usually true. Packed, so it is 96KB rather than 384KB.
const DRAFT_KEY = "liveDraft";
let draftTimer = 0;
function saveDraftSoon() {
  clearTimeout(draftTimer);
  draftTimer = setTimeout(saveDraft, 900);
}
function saveDraft() {
  try {
    const p = pack(bits);
    let s = "";
    // In chunks: String.fromCharCode spread over 96000 bytes overflows the
    // argument stack on Safari and throws where nothing is wrong.
    for (let i = 0; i < p.length; i += 8192)
      s += String.fromCharCode.apply(null, p.subarray(i, i + 8192));
    localStorage.setItem(DRAFT_KEY, btoa(s));
  } catch (e) {
    /* private window, or storage full. The board still works. */
  }
}
function loadDraft() {
  try {
    const raw = localStorage.getItem(DRAFT_KEY);
    if (!raw) return false;
    const s = atob(raw);
    if (s.length !== (W * H) >> 2) return false;
    const p = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) p[i] = s.charCodeAt(i);
    bits = unpack(p);
    return true;
  } catch (e) {
    return false;
  }
}

const grey = (px, n) => {
  const g = new Uint8Array(n);
  for (let i = 0; i < n; i++)
    g[i] = (px[i * 4] * 77 + px[i * 4 + 1] * 150 + px[i * 4 + 2] * 29) >> 8;
  return g;
};

// ONE offscreen canvas, kept. Allocating a 480x800 canvas per raster meant a
// new 1.5MB surface for every keystroke, and the old ones waiting on the
// collector while somebody was still typing.
const off = document.createElement("canvas");
off.width = W;
off.height = H;
const offCtx = off.getContext("2d", { willReadFrequently: true });

function rasterise(draw, useDither) {
  const o = offCtx;
  o.fillStyle = "#fff";
  o.fillRect(0, 0, W, H);
  draw(o);
  const src = o.getImageData(0, 0, W, H).data;
  // A PHOTOGRAPH IS DITHERED ACROSS THE FOUR LEVELS; text is thresholded.
  //
  // Snapping each pixel to its nearest level, which is what this did, turns a
  // photograph into four flat bands: every face becomes a poster. Using all
  // four levels AND diffusing the error between them is not a trade, it is
  // strictly more tone -- the four shades stay exactly as available as before
  // and the eye reconstructs everything in between. Text still thresholds,
  // because an edge wants to be an edge and diffusion only fringes it.
  // Text is QUANTISED, not thresholded. A hard cut at 128 throws the
  // anti-aliasing away and leaves every letter jagged; the panel has four
  // levels and the softened edge pixels are exactly what they are for. It is
  // not dithered, because diffusing error across a letterform fringes it.
  if (useDither) {
    // Diffusion has to see the whole plane before it can place a pixel, so it
    // keeps its own pass over a greyscale copy.
    bits = diffuse(grey(src, W * H), inverted);
    scanInk();
    render();
  } else {
    // ONE PASS. Greying, quantising and filling the display buffer each walked
    // all 384,000 pixels separately, and a fourth walk blitted them. They read
    // the same pixel and write the same index, so they are one loop that ends
    // in a single upload.
    const px = bits;
    const d = frame.data;
    let ink = false;
    for (let i = 0; i < W * H; i++) {
      const k = i * 4;
      let v = (src[k] * 77 + src[k + 1] * 150 + src[k + 2] * 29) >> 8;
      if (inverted) v = 255 - v;
      const lvl = NEAREST[v];
      px[i] = lvl;
      if (lvl !== 3) ink = true;
      const out = LEVEL_GREY[lvl];
      d[k] = d[k + 1] = d[k + 2] = out;
    }
    inked = ink;
    ctx.putImageData(frame, 0, 0);
    dirty = null;
    markTools();
    saveDraftSoon();
  }
}

// Nearest level by the brightness the PANEL actually shows, not by index.
//
// A TABLE, not a search. There are 256 possible inputs and four levels, so the
// answer is precomputed once; calling a four-way search per pixel meant about
// 1.5 million comparisons for every keystroke, which is what made typing crawl
// after this replaced a single threshold compare.
const NEAREST = new Uint8Array(256);
for (let v = 0; v < 256; v++) {
  let best = 0;
  let bestD = Infinity;
  for (let i = 0; i < LEVEL_GREY.length; i++) {
    const d = Math.abs(v - LEVEL_GREY[i]);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  NEAREST[v] = best;
}
// Diffusion carries error, so the value can leave 0..255 and has to be clamped
// into the table rather than indexing past it.
function nearestLevel(v) {
  return NEAREST[v < 0 ? 0 : v > 255 ? 255 : v | 0];
}

// Floyd-Steinberg, serpentine. Serpentine because scanning every row the same
// way walks the error in one direction and lays down the faint diagonal combing
// that gives cheap dithering away; alternating the direction cancels it.
function diffuse(g, inv) {
  const err = new Float32Array(W * H);
  const out = new Uint8Array(W * H);
  for (let y = 0; y < H; y++) {
    const ltr = (y & 1) === 0;
    for (let k = 0; k < W; k++) {
      const x = ltr ? k : W - 1 - k;
      const i = y * W + x;
      const want = (inv ? 255 - g[i] : g[i]) + err[i];
      const lvl = nearestLevel(want);
      out[i] = lvl;
      const e = want - LEVEL_GREY[lvl];
      const fwd = ltr ? 1 : -1;
      const right = x + fwd;
      if (right >= 0 && right < W) err[i + fwd] += (e * 7) / 16;
      if (y + 1 < H) {
        const below = i + W;
        if (right >= 0 && right < W) err[below + fwd] += (e * 1) / 16;
        err[below] += (e * 5) / 16;
        const back = x - fwd;
        if (back >= 0 && back < W) err[below - fwd] += (e * 3) / 16;
      }
    }
  }
  return out;
}

// FOUR TONES, AND THEY ARE REAL. The X4 Pro's panel driver declares
// AbsolutePlanes grayscale and renderCustomSleepScreen takes the grayscale path
// when the panel supports it, so the sleep screen shows four levels. A mid grey
// is therefore SENT as a mid grey rather than as a pattern of black pixels
// pretending to be one.
const TONES = [
  { name: "Black", level: 0 },
  { name: "Dark", level: 1 },
  { name: "Light", level: 2 },
  { name: "Erase", level: 3 },
];
let tone = 0;

function stampAt(x, y, r) {
  const lv = TONES[tone].level;
  const r2 = r * r;
  const x0 = Math.max(0, (x - r) | 0);
  const x1 = Math.min(W - 1, (x + r) | 0);
  const y0 = Math.max(0, (y - r) | 0);
  const y1 = Math.min(H - 1, (y + r) | 0);
  for (let yy = y0; yy <= y1; yy++) {
    const dy = yy - y;
    for (let xx = x0; xx <= x1; xx++) {
      const dx = xx - x;
      if (dx * dx + dy * dy <= r2) {
        bits[yy * W + xx] = lv;
        if (lv !== 3) inked = true;
      }
    }
  }
  markDirty(x0, y0, x1, y1);
}

function line(x0, y0, x1, y1, r) {
  const dx = x1 - x0;
  const dy = y1 - y0;
  const n = Math.max(1, Math.ceil(Math.hypot(dx, dy)));
  for (let i = 0; i <= n; i++) stampAt(x0 + (dx * i) / n, y0 + (dy * i) / n, r);
}

// The reader's file: 480x800 at TWO bits per pixel, four-entry palette,
// bottom-up, rows padded to four bytes. 96070 bytes, which the service checks
// byte-exactly: a wrong-sized file that still parses is drawn half-rendered on
// the reader forever rather than refused.
//
// Two bits rather than eight because radio time is the battery cost, and a
// quarter of the bytes carries exactly the levels the panel can show.
function toBmp(levels) {
  const src = levels || bits;
  const rowBytes = ((W * 2 + 31) >> 5) << 2;
  const off = 14 + 40 + 4 * 4;
  const size = off + rowBytes * H;
  const b = new Uint8Array(size);
  const v = new DataView(b.buffer);
  b[0] = 0x42;
  b[1] = 0x4d;
  v.setUint32(2, size, true);
  v.setUint32(10, off, true);
  v.setUint32(14, 40, true);
  v.setInt32(18, W, true);
  v.setInt32(22, H, true);
  v.setUint16(26, 1, true);
  v.setUint16(28, 2, true);
  v.setUint32(30, 0, true);
  v.setUint32(34, rowBytes * H, true);
  v.setUint32(46, 4, true);
  v.setUint32(50, 4, true);
  for (let i = 0; i < 4; i++) {
    const g = LEVEL_GREY[i];
    const o = 54 + i * 4;
    b[o] = g;
    b[o + 1] = g;
    b[o + 2] = g;
    b[o + 3] = 0;
  }
  for (let y = 0; y < H; y++) {
    const s = (H - 1 - y) * W;
    const dst = off + y * rowBytes;
    for (let x = 0; x < W; x++)
      b[dst + (x >> 2)] |= (src[s + x] & 3) << ((3 - (x & 3)) * 2);
  }
  return b;
}

// --- the surface -----------------------------------------------------------

const stage = document.getElementById("stage");
const nib = document.getElementById("nib");

// ZOOM IS A VIEW OVER A FIXED DRAWING, and that is the whole of the design.
// `view` says which rectangle of the 480x800 panel the stage is showing; the
// canvas keeps its 480x800 backing store and is magnified with a transform. A
// stroke is therefore recorded in panel pixels whatever the magnification, so
// a line drawn at 6x is the same width on the reader as one drawn at 1x. The
// obvious alternative, growing the canvas, gets that wrong in a way nobody
// sees until the picture is on the fridge.
//
// The page itself never zooms: `touch-action: none` on the stage takes the
// pinch before the browser can, which is the same rule that stops a stroke
// being a scroll.
const MAX_ZOOM = 8;
const view = { s: 1, x: 0, y: 0 };
const zoomCtl = document.getElementById("zoomCtl");
const zoomMap = document.getElementById("zoomMap");
const zoomBox = document.getElementById("zoomBox");
const zoomIn = document.getElementById("zoomIn");
const zoomOut = document.getElementById("zoomOut");
const zoomFit = document.getElementById("zoomFit");

function clampView() {
  view.s = Math.min(MAX_ZOOM, Math.max(1, view.s));
  const vw = W / view.s;
  const vh = H / view.s;
  view.x = Math.min(W - vw, Math.max(0, view.x));
  view.y = Math.min(H - vh, Math.max(0, view.y));
}

function applyView() {
  clampView();
  pad.style.transform = `scale(${view.s}) translate(${(-view.x / W) * 100}%, ${(-view.y / H) * 100}%)`;
  const zoomed = view.s > 1.001;
  // THE MAP APPEARING IS THE INDICATOR. At 1x its box would fill it and say
  // nothing, so it is absent at 1x and its presence is the signal.
  zoomMap.hidden = !zoomed;
  zoomFit.hidden = !zoomed;
  zoomOut.disabled = !zoomed;
  zoomIn.disabled = view.s >= MAX_ZOOM - 0.001;
  if (zoomed) {
    zoomBox.style.left = (view.x / W) * 100 + "%";
    zoomBox.style.top = (view.y / H) * 100 + "%";
    zoomBox.style.width = 100 / view.s + "%";
    zoomBox.style.height = 100 / view.s + "%";
  }
}

// Panel coordinates under a point on the screen. Everything that has to know
// where a finger is goes through this, so there is one place the magnification
// is undone and no second copy of the arithmetic to drift.
// THE STAGE'S BOX, READ ONCE PER GESTURE RATHER THAN PER EVENT.
// getBoundingClientRect forces style and layout, and this stage is a size
// container whose canvas is sized in container units, so every read also
// re-resolved that. Four reads per pointermove (here, the nib, and the pan and
// pinch branches) made a fast finger measurably laggy. The box cannot change
// during a stroke, so it is cached and invalidated on the things that do move
// it: a resize, an orientation change, or entering the surface again.
let stageBox = null;
// THE PICTURE'S BOX, NOT THE WINDOW'S. The panel is CONTAINED in the stage,
// so at most widths there is a bar down one pair of sides and the two boxes are
// not the same. Measured against the window, the window's edge mapped to the
// picture's edge and every point was squeezed inward: a stroke near the left
// landed to its right, one near the right landed to its left, and the middle
// looked perfect.
//
// This is the box BEFORE the zoom transform, which is what the callers expect:
// they divide by view.s themselves.
function stageRect() {
  if (!stageBox) {
    const s = stage.getBoundingClientRect();
    const w = Math.min(s.width, (s.height * W) / H);
    const h = (w * H) / W;
    const left = s.left + (s.width - w) / 2;
    const top = s.top + (s.height - h) / 2;
    stageBox = { left, top, width: w, height: h, right: left + w, bottom: top + h };
  }
  return stageBox;
}
function forgetStageRect() {
  stageBox = null;
}
addEventListener("resize", forgetStageRect);
addEventListener("orientationchange", forgetStageRect);
addEventListener("scroll", forgetStageRect, true);

function atClient(cx, cy) {
  const r = stageRect();
  return [
    view.x + ((cx - r.left) / r.width) * (W / view.s),
    view.y + ((cy - r.top) / r.height) * (H / view.s),
  ];
}
const pointAt = (e) => atClient(e.clientX, e.clientY);

// Zoom about a point, so what is under the fingers (or the cursor) stays under
// them. Zooming about the middle instead is the thing that loses people.
function zoomAbout(factor, cx, cy) {
  const before = atClient(cx, cy);
  view.s = Math.min(MAX_ZOOM, Math.max(1, view.s * factor));
  clampView();
  const after = atClient(cx, cy);
  view.x += before[0] - after[0];
  view.y += before[1] - after[1];
  applyView();
}

function fitView() {
  view.s = 1;
  view.x = 0;
  view.y = 0;
  applyView();
}

zoomIn.onclick = () => zoomAbout(1.6, ...stageCentre());
zoomOut.onclick = () => zoomAbout(1 / 1.6, ...stageCentre());
zoomFit.onclick = fitView;
function stageCentre() {
  const r = stageRect();
  return [r.left + r.width / 2, r.top + r.height / 2];
}

// --- gestures --------------------------------------------------------------
//
// One finger draws. Two fingers zoom and move, and the page stays exactly where
// it is. On a desktop the wheel zooms about the cursor and a drag with shift or
// the middle button moves.

let drawing = false;
let last = null;
const pointers = new Map();
let pinch = null;
let panning = null;

const onControls = (e) => !!(e.target.closest && e.target.closest(".lv-zoom"));
const mid = (a, b) => [(a.x + b.x) / 2, (a.y + b.y) / 2];
const spread = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

// The snapshot the stroke in progress pushed, so a gesture can take the stroke
// back rather than merely stop extending it. null between strokes.
let strokeUndo = null;

const endStroke = () => {
  drawing = false;
  last = null;
  strokeUndo = null;
};

// A STROKE THAT TURNED OUT TO BE A PINCH NEVER HAPPENED.
//
// Mario, on the shipped page: "whenever I try to zoom in [it] confuses stuff
// with me drawing and leaves dots around." The first finger of a pinch lands
// alone, and for the fifty-odd milliseconds before the second one arrives it is
// an ordinary stroke: it snapshots, it stamps, it paints. Ending the stroke
// when the second finger arrives stops it growing and leaves the dot, so every
// attempt to zoom cost one mark. Repeat it a few times and the drawing is
// freckled.
//
// Taking it back is the whole fix, and it is exact: the stroke pushed one
// snapshot, so this pops that exact snapshot and restores it. It is not "undo
// the last thing", which would eat a real stroke if the stack had moved under
// it -- the identity check is what makes it safe.
function unstroke() {
  if (!strokeUndo) {
    endStroke();
    return;
  }
  if (undoStack.length && undoStack[undoStack.length - 1] === strokeUndo) {
    bits = unpack(undoStack.pop());
    endStroke();
    render();
    return;
  }
  endStroke();
}

stage.addEventListener("pointerdown", (e) => {
  forgetStageRect();
  if (onControls(e)) return;
  // The gesture belongs to the drawing and to nothing else: without this a drag
  // that starts on the canvas is also a text selection (the page paints blue
  // straight through the picture) and a long press is an iOS callout over it.
  // `touch-action: none` in the stylesheet stops the scroll and the zoom; this
  // stops the selection and the callout.
  e.preventDefault();
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  stage.setPointerCapture(e.pointerId);

  if (pointers.size >= 2) {
    // A second finger says the first one was never a stroke. Whatever it drew
    // is taken back, not merely stopped: see unstroke. A palm landing on the
    // glass mid-stroke arrives here too and is treated the same way, which is
    // right for a surface drawn on with fingers.
    unstroke();
    const [a, b] = [...pointers.values()];
    pinch = { dist: spread(a, b), mid: mid(a, b) };
    return;
  }
  if (e.button === 1 || e.shiftKey) {
    panning = [e.clientX, e.clientY];
    return;
  }
  if (mode !== "draw") return;
  snap();
  strokeUndo = undoStack[undoStack.length - 1];
  drawing = true;
  last = pointAt(e);
  stampAt(last[0], last[1], pen / 2);
  renderStroke();
  markTools();
});

stage.addEventListener("pointermove", (e) => {
  if (pointers.has(e.pointerId))
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (pinch && pointers.size >= 2) {
    e.preventDefault();
    const [a, b] = [...pointers.values()];
    const d = spread(a, b);
    const m = mid(a, b);
    if (pinch.dist > 4) {
      const before = atClient(m[0], m[1]);
      view.s = Math.min(MAX_ZOOM, Math.max(1, view.s * (d / pinch.dist)));
      clampView();
      const after = atClient(m[0], m[1]);
      view.x += before[0] - after[0];
      view.y += before[1] - after[1];
      const r = stageRect();
      view.x -= ((m[0] - pinch.mid[0]) / r.width) * (W / view.s);
      view.y -= ((m[1] - pinch.mid[1]) / r.height) * (H / view.s);
      applyView();
    }
    pinch = { dist: d, mid: m };
    return;
  }
  if (panning) {
    e.preventDefault();
    const r = stageRect();
    view.x -= ((e.clientX - panning[0]) / r.width) * (W / view.s);
    view.y -= ((e.clientY - panning[1]) / r.height) * (H / view.s);
    panning = [e.clientX, e.clientY];
    applyView();
    return;
  }
  if (!drawing) return;
  e.preventDefault();
  // EVERY SAMPLE, not just the one the browser chose to deliver. Coalesced
  // events are the ones a fast finger produced between frames; dropping them
  // is what turns a quick stroke into a polygon.
  const pts = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
  for (const c of pts.length ? pts : [e]) {
    const p = pointAt(c);
    line(last[0], last[1], p[0], p[1], pen / 2);
    last = p;
  }
  renderStroke();
});

const liftPointer = (e) => {
  if (drawing) {
    renderStrokeNow();
    markTools();
    saveDraftSoon();
  }
  pointers.delete(e.pointerId);
  if (pointers.size < 2) pinch = null;
  if (pointers.size === 0) {
    panning = null;
    // KEPT, not taken back. A stroke the system interrupted -- a notification,
    // a call -- is a real stroke that got cut short, and undo is right there
    // for somebody who disagrees. Only a second finger says the mark was never
    // meant, because only a second finger is a different gesture.
    endStroke();
  }
};
addEventListener("pointerup", liftPointer);
// A stroke the system took away (a phone call, the browser deciding the gesture
// was a scroll after all) ENDS the stroke rather than leaving `drawing` true:
// left set, the next pointermove anywhere drew a line from wherever the finger
// had got to, straight across the picture.
addEventListener("pointercancel", liftPointer);
stage.addEventListener("contextmenu", (e) => e.preventDefault());
stage.addEventListener(
  "wheel",
  (e) => {
    e.preventDefault();
    zoomAbout(Math.exp(-e.deltaY * 0.0022), e.clientX, e.clientY);
  },
  { passive: false },
);

// The nib, shown at the size it will really mark, which means it grows with the
// magnification: the mark is fixed in panel pixels, so on screen it is exactly
// as much bigger as everything else under the glass.
function placeNib(e) {
  // A finger covers it, and positioning it cost a style write on every sample.
  if (e.pointerType === "touch") return;
  if (onControls(e)) return;
  const r = stageRect();
  const d = pen * (r.width / W) * view.s;
  nib.style.width = d + "px";
  nib.style.height = d + "px";
  nib.style.left = e.clientX - r.left + "px";
  nib.style.top = e.clientY - r.top + "px";
}
stage.addEventListener("pointermove", placeNib);
stage.addEventListener("pointerenter", placeNib);

// Said only where a pointer can be told what to do with it.
document.getElementById("stageHint").textContent =
  "Wheel to zoom, shift-drag to move.";

// The swatch shows the mark at the size it is DRAWN AT ON SCREEN, which means
// scaling by the same factor the stage scales the panel by. Sized in raw panel
// pixels it is about a third too big and the widest one bursts its button.
const SIZES = [4, 10, 18, 30];
const sizesEl = document.getElementById("sizes");
const SWATCH = 34;
const SWATCH_MAX = SWATCH - 11; // always clear of the button's edge
function stageScale() {
  const r = stageRect();
  return (r.width || 300) / W;
}
function sizeSwatches() {
  const k = stageScale();
  [...sizesEl.children].forEach((b, i) => {
    const d = Math.max(3, Math.min(SWATCH_MAX, Math.round(SIZES[i] * k)));
    b.firstElementChild.style.width = d + "px";
    b.firstElementChild.style.height = d + "px";
  });
}
SIZES.forEach((px) => {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "lv-btn lv-size";
  b.setAttribute("aria-pressed", String(px === pen));
  b.title = px + " pixels on the panel";
  b.setAttribute("aria-label", px + " pixels on the panel");
  b.style.width = SWATCH + "px";
  b.style.height = SWATCH + "px";
  b.innerHTML = "<i></i>";
  b.onclick = () => {
    pen = px;
    [...sizesEl.children].forEach((c) =>
      c.setAttribute("aria-pressed", "false"),
    );
    b.setAttribute("aria-pressed", "true");
  };
  sizesEl.appendChild(b);
});
sizeSwatches();
// The stage is fluid, so the scale changes with the window and the swatches
// have to follow or they stop telling the truth.
addEventListener("resize", sizeSwatches);

// Each tone swatch is drawn with the tone's own level, so the button shows the
// grey the panel really produces rather than a CSS one it cannot.
const tonesEl = document.getElementById("tones");
TONES.forEach((t, i) => {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "lv-btn lv-tone";
  b.title = t.name;
  b.setAttribute("aria-label", t.name);
  b.setAttribute("aria-pressed", String(i === tone));
  const c = document.createElement("canvas");
  c.width = c.height = 16;
  const g = c.getContext("2d");
  const img = g.createImageData(16, 16);
  for (let k = 0; k < 16 * 16; k++) {
    const v = LEVEL_GREY[t.level];
    img.data[k * 4] = img.data[k * 4 + 1] = img.data[k * 4 + 2] = v;
    img.data[k * 4 + 3] = 255;
  }
  g.putImageData(img, 0, 0);
  b.appendChild(c);
  b.onclick = () => {
    tone = i;
    [...tonesEl.children].forEach((c2) =>
      c2.setAttribute("aria-pressed", "false"),
    );
    b.setAttribute("aria-pressed", "true");
  };
  tonesEl.appendChild(b);
});

const writePane = document.getElementById("writePane");
const photoPane = document.getElementById("photoPane");
const penTools = document.getElementById("penTools");
const undoBtn = document.getElementById("undo");
const clearBtn = document.getElementById("clear");

// A FLAG, not a scan. This walked 384,000 elements through a callback and ran
// twice per raster (markTools and markWay both ask). Every writer of `bits`
// already knows whether it put ink down, so it says so.
let inked = false;
const isBlank = () => !inked;
function scanInk() {
  inked = false;
  for (let i = 0; i < bits.length; i++)
    if (bits[i] !== 3) {
      inked = true;
      return;
    }
}

// Both icons say whether they can do anything, which is the whole of what a
// word used to say: an undo with nothing behind it and a clear on blank paper
// are dimmed rather than silently doing nothing.
function markTools() {
  undoBtn.disabled = undoStack.length === 0;
  clearBtn.disabled = isBlank();
  markWay();
}

// CLEAR IS NOT CONFIRMED. It is undoable, it says so, and it points at the
// button that does it.
//
// The other way round costs a second tap on the ordinary case -- clearing to
// start again is most of what this button is for -- and on a phone the confirm
// would be a dialog over the drawing, which is the one shape this layout exists
// to get rid of. A mis-tap is the rare case, and the rare case is the one that
// should pay: undo lights up, pulses, and the line under the rail says what to
// press. The drawing also survives a reload (see saveDraft), so the offer holds
// even if the tab goes away while somebody reads it.
clearBtn.onclick = () => {
  if (isBlank()) return;
  snap();
  bits.fill(3);
  photo = null;
  render();
  clearBtn.blur();
  undoBtn.classList.remove("is-pulsing");
  void undoBtn.offsetWidth; // restart the animation on a second clear
  undoBtn.classList.add("is-pulsing");
  setTimeout(() => undoBtn.classList.remove("is-pulsing"), 1600);
  say("Cleared. Undo puts it back.");
};
undoBtn.onclick = () => {
  const s = undoStack.pop();
  if (s) {
    bits = unpack(s);
    render();
  }
};

const drawText = (o) => {
  o.fillStyle = "#000";
  o.textAlign = "center";
  o.textBaseline = "middle";
  const size = 64;
  o.font = size + "px ui-serif, Georgia, serif";
  const lines = document.getElementById("msg").value.split("\n");
  const lh = size * 1.25;
  const y0 = H / 2 - ((lines.length - 1) * lh) / 2;
  lines.forEach((l, i) => o.fillText(l, W / 2, y0 + i * lh));
};
const drawPhoto = (o) => {
  if (!photo) return;
  const s =
    photoMode === "fill"
      ? Math.max(W / photo.width, H / photo.height)
      : Math.min(W / photo.width, H / photo.height);
  const dw = photo.width * s;
  const dh = photo.height * s;
  o.imageSmoothingQuality = "high";
  o.drawImage(photo, (W - dw) / 2, (H - dh) / 2, dw, dh);
};
function regen() {
  if (mode === "write") {
    snap();
    rasterise(drawText, false);
  } else if (mode === "photo" && photo) {
    snap();
    rasterise(drawPhoto, true);
  }
}
// --- the two screens -------------------------------------------------------
//
// A phone gets a home page (when the reader looks, what is going out, what has
// been sent) and a drawing surface that is the whole screen. A desktop gets
// neither: it has the room for both at once and always did, so `openCompose`
// is a no-op there beyond setting the mode.
const compose = document.getElementById("compose");
const ways = document.getElementById("ways");
const goDraw = document.getElementById("goDraw");

const onPhone = () => !matchMedia("(min-width: 900px)").matches;

function setMode(next) {
  mode = next;
  document
    .querySelectorAll("#tabs button")
    .forEach((x) =>
      x.setAttribute("aria-selected", String(x.dataset.mode === next)),
    );
  writePane.hidden = next !== "write";
  photoPane.hidden = next !== "photo";
  penTools.hidden = next !== "draw";
  regen();
  sizeSwatches();
}

function openCompose(next) {
  // The surface is about to change size, so the cached box is stale.
  forgetStageRect();
  setMode(next);
  if (!onPhone()) return;
  document.body.classList.add("lv-drawing");
  // The stage had no size while the surface was closed, so the brush dots were
  // drawn against the 300px fallback and would be a lie until something else
  // resized them.
  requestAnimationFrame(() => {
    sizeSwatches();
    applyView();
  });
}

function closeCompose() {
  document.body.classList.remove("lv-drawing");
  markWay();
}

// WHAT THE DOOR SAYS depends on what is behind it. A drawing survives being
// left, so the button that goes back to it should not say "Draw" as though the
// paper were blank.
function markWay() {
  goDraw.textContent = isBlank() ? "Send something new" : "Keep going";
}

goDraw.onclick = () => openCompose("draw");
document.getElementById("closeCompose").onclick = closeCompose;
// The surface is a screen, so the phone's back gesture should leave it rather
// than leaving the page. Nothing is pushed on open, so this only ever fires for
// a real back on the page itself; the class is cleared either way.
addEventListener("pagehide", closeCompose);

document.querySelectorAll("#tabs button").forEach((t) => {
  t.onclick = () => setMode(t.dataset.mode);
});
// ONE RASTER PER FRAME, ONE UNDO STEP PER EDIT. Typing used to re-rasterise
// the whole panel and push an undo snapshot on every keystroke: five passes
// over 384,000 pixels and a 96KB snapshot per character, which a 20-deep undo
// stack fills with a single word. The textarea has its own undo for typing;
// what belongs here is one step for "the message changed".
let typing = 0;
let typedSnap = false;
document.getElementById("msg").addEventListener("input", () => {
  if (!typedSnap) {
    typedSnap = true;
    snap();
  }
  // LIVE, BUT NEVER MORE THAN ONCE A FRAME. Waiting for a pause made the
  // preview lag behind deliberately; rastering on every keystroke let the work
  // queue up behind a fast typist, which is what took five seconds to catch
  // up. One per frame is both: the panel follows within a frame of the letter,
  // and a burst of twenty keystroke between two frames still costs one raster.
  //
  // The keystroke handler itself does nothing but schedule, so typing latency
  // does not depend on how long a raster takes. A slower phone shows a preview
  // at a lower rate; it never shows slower typing.
  if (typing) return;
  typing = requestAnimationFrame(() => {
    typing = 0;
    rasterise(drawText, false);
  });
});
document.getElementById("msg").addEventListener("blur", () => {
  typedSnap = false;
});
document.getElementById("file").addEventListener("change", (e) => {
  const f = e.target.files[0];
  if (!f) return;
  const img = new Image();
  img.onload = () => {
    photo = img;
    regen();
  };
  img.src = URL.createObjectURL(f);
});
document.getElementById("fit").onclick = () => {
  photoMode = "fit";
  regen();
};
document.getElementById("fill").onclick = () => {
  photoMode = "fill";
  regen();
};
document.getElementById("invert").onclick = () => {
  inverted = !inverted;
  regen();
};

// --- the service -----------------------------------------------------------

const gate = document.getElementById("gate");
const app = document.getElementById("app");

// THE SERVICE BEING UNREACHABLE IS NOT AN EXCEPTION, it is an answer.
//
// `fetch` REJECTS on a dropped connection, a DNS failure or a CORS refusal, and
// the rejection used to travel straight out of refresh() before it could decide
// what to show. The result was a page with a headline on it and nothing else:
// no board, no pairing box, no sentence, because the line that draws one of the
// two never ran. Aeroplane mode reproduces it exactly.
//
// This sentence is the page's own, and it is allowed to be: the rule is that a
// decision the SERVICE made is quoted verbatim and never reworded, and a
// service nobody could reach made no decision.
const OFFLINE = "Could not reach the service.";
const OFFLINE_HINT = "Check the connection and reload this page.";
// A DIFFERENT CAUSE, SAID DIFFERENTLY. "Could not reach the service" is what a
// browser reports for a refused preflight, a refused origin, a dropped
// connection and a dead host alike, and tonight three of those four have
// happened: a missing allowed header, then a refused origin, and it sent
// somebody looking in the wrong place both times. This one is the case the page
// CAN tell apart -- an origin with no proxy behind it -- so it says that and
// nothing broader.
const NO_PROXY = "This copy of the page has no way to reach the reader.";
const NO_PROXY_HINT =
  "Only crossplay.ma-r-s.com can talk to the service. To look at the layout " +
  "without a reader, add ?demo=12 to the address.";
// Why a pairing does not follow you off production. It is a cookie for
// ma-r-s.com, so a dev server or a preview is a different site to the browser
// and starts with nothing -- which looks exactly like never having paired, and
// that is the sentence somebody needs rather than the ordinary invitation.
const OTHER_ORIGIN =
  "You are not on crossplay.ma-r-s.com, and a connected reader is remembered " +
  "per site, so this copy of the page starts with none.";

async function api(path, opts) {
  // credentials: "include" and not "same-origin" -- see the note at the top.
  let r;
  try {
    r = await fetch(API + path, { credentials: "include", ...opts });
  } catch (e) {
    return { ok: false, status: 0, offline: true, body: { error: OFFLINE } };
  }
  // IS THERE A PROXY HERE AT ALL? Off production the calls go same-origin, and
  // a static host answers /api/ with its own 404 rather than with the
  // service's JSON. That is a different fact from "not connected" and from "the
  // service is down", and it is the one a preview deployment produces.
  if (probedProxy === null && local) {
    probedProxy =
      r.status !== 404 ||
      (r.headers.get("content-type") || "").includes("json");
  }
  if (probedProxy === false) {
    return { ok: false, status: 0, noProxy: true, body: { error: NO_PROXY } };
  }
  let body = null;
  try {
    body = await r.json();
  } catch (e) {
    /* a 204 or a refusal with no body; the status carries it */
  }
  // 401 means this browser was revoked on the reader, or the reader was reset.
  // Fall back to the pairing step rather than showing an error for a state that
  // is not an error: somebody took access away on purpose, and the way back is
  // a new code.
  if (r.status === 401 && path !== "/api/claim") showGate();
  return { ok: r.ok, status: r.status, body };
}

const devNote = document.getElementById("devNote");

function showGate() {
  gate.hidden = false;
  app.hidden = true;
  document.body.classList.remove("lv-connected");
  // Off production and unpaired is not the same story as unpaired, and showing
  // the ordinary invitation made it look as though a reader he had already
  // connected had been forgotten.
  const note =
    demoCount !== null
      ? "Made-up entries, for looking at the layout. No reader is involved."
      : offProduction
        ? OTHER_ORIGIN
        : "";
  devNote.textContent = note;
  devNote.hidden = !note;
}

// A name the reader can tell apart, taken from the browser rather than asked
// for: four entries all reading "A phone" would be useless on the one screen
// that revokes them, and a name field is friction on the step that has to be
// frictionless.
function browserName() {
  const u = navigator.userAgent;
  if (/iPhone/.test(u)) return "iPhone";
  if (/iPad/.test(u)) return "iPad";
  if (/Android/.test(u)) return "Android phone";
  if (/Macintosh/.test(u)) return "Mac";
  if (/Windows/.test(u)) return "Windows PC";
  if (/Linux/.test(u)) return "Linux";
  return "A phone";
}

// THE READER'S OWN BANDS, ported rather than invented, because the two surfaces
// must never name different numbers for one moment. These are live::roughSpan
// in src/apps_local/live/LiveCore.cpp, edge for edge and rounding for rounding:
// minutes in fives, then the singular bands that stop "80 minutes" being either
// a figure nobody needs or "an hour", which is wrong by a third.
function roughSpan(sec) {
  if (sec < 45 * 60) {
    return `${Math.max(5, Math.floor((sec + 150) / 300) * 5)} minutes`;
  }
  if (sec < 90 * 60) return "an hour";
  if (sec < 22 * 3600) return `${Math.floor((sec + 1800) / 3600)} hours`;
  if (sec < 36 * 3600) return "a day";
  return `${Math.floor((sec + 43200) / 86400)} days`;
}
// "in about 5 hours". The panel says "In 5 hours" instead, and not because it
// is more confident: "In about 45 minutes" measures 464px at its display cut
// against a 448px body, so the word does not fit. The rounding is the panel's
// way of saying the same thing.
const human = (sec) => `in about ${roughSpan(sec)}`;
const ago = (epoch) =>
  `about ${roughSpan(Math.max(0, Math.floor(Date.now() / 1000) - epoch))} ago`;

// THE COUNT, TO THE SECOND, and it is the one figure on either surface that is
// spelled more precisely than it is known. That is deliberate and it was asked
// for: a person watching a countdown wants to see it move, and "in about 5
// hours" standing still for an hour reads as a page that has stopped working.
// What it must not do is claim the precision it is spelled with, so the word
// "about" is rendered immediately before it and "left" immediately after, both
// in the small grey the rest of the hedging uses, and the line under it says
// the figure drifts. The reader's sleep timer runs off an RC oscillator at
// percent-level accuracy: a day's wake is a quarter of an hour either way.
function clockSpan(sec) {
  const s = Math.max(0, Math.floor(sec));
  const days = Math.floor(s / 86400);
  const hh = String(Math.floor((s % 86400) / 3600)).padStart(2, "0");
  const mm = String(Math.floor((s % 3600) / 60)).padStart(2, "0");
  const ss = String(s % 60).padStart(2, "0");
  return (days ? `${days}d ` : "") + `${hh}:${mm}:${ss}`;
}

// "about every 6 hours", from live::scheduleNote's bands.
function everyPhrase(sec) {
  if (sec >= 604800 && sec % 604800 === 0) {
    const weeks = sec / 604800;
    return weeks === 1 ? "about every week" : `about every ${weeks} weeks`;
  }
  if (sec >= 23 * 3600) {
    const days = Math.floor((sec + 43200) / 86400);
    return days <= 1 ? "about every day" : `about every ${days} days`;
  }
  if (sec >= 55 * 60) {
    const hours = Math.floor((sec + 1800) / 3600);
    return hours <= 1 ? "about every hour" : `about every ${hours} hours`;
  }
  return `about every ${Math.floor(sec / 60)} minutes`;
}

// HOW LATE IS LATE, and it is deliberately generous in both terms.
//
// A reader only fetches on its way into sleep, so one that somebody picked up
// in the morning and put down at night is half a day past due with nothing
// whatever wrong with it. That is the floor. The other term is a whole missed
// check, because on a weekly cadence being a day late is nothing and being a
// week late is real.
//
// Under this the page states a fact and stops: a flat battery, a router that
// moved and Live switched off without the reader getting a word out are
// indistinguishable from here, and naming one would be a diagnosis the service
// cannot make.
const LATE_FLOOR_S = 12 * 3600;
const lateAfter = (intervalSeconds) =>
  Math.max(LATE_FLOOR_S, intervalSeconds || 0);

// Inside this, the check is due now rather than in the future. The reader is
// awake in somebody's hands or out of contact, and either way the honest
// sentence is the mechanism rather than a countdown stuck at zero. Three
// minutes, the same floor live::nextCheckPhrase uses for "Any moment".
const DUE_WINDOW_S = 180;

const whenLine = document.getElementById("whenLine");
const whenTick = document.getElementById("whenTick");
const tickClock = document.getElementById("tickClock");
const whenSub = document.getElementById("whenSub");
const pendingLine = document.getElementById("pendingLine");
const sendNote = document.getElementById("sendNote");
let state = null;
let band = "";

// THE LINE UNDER THE RAIL HAS A STANDING SENTENCE and it always comes back.
//
// It says which entry the reader takes next, and things that just happened
// borrow it for a few seconds. Without the second half, one tap on Clear
// replaced "Next up: the drawing iPhone sent today 07:12" with "Cleared." for
// the rest of the session, and the one place the page says what is going out
// was simply gone until somebody touched the rail.
let sayTimer = 0;
const say = (text, sticky) => {
  clearTimeout(sayTimer);
  sendNote.textContent = text;
  if (!sticky) sayTimer = setTimeout(() => restoreNote(), 4500);
};
function restoreNote() {
  clearTimeout(sayTimer);
  sendNote.textContent = historyNote();
}

const secondsLeft = () =>
  state && state.nextExpected
    ? state.nextExpected - Math.floor(Date.now() / 1000)
    : 0;

// FIVE STATES, and every one of them names when the next look happens. Only two
// of them have a countdown; the other three would have to invent one, and the
// figure would be the fiction the rest of this file exists to avoid.
function bandNow() {
  if (!state || !state.connected) return "none";
  // `=== false`, not `!liveOn`: the page and the service deploy separately, and
  // a missing key must never produce a positive claim about somebody's device.
  if (state.liveOn === false) return "off";
  const left = secondsLeft();
  if (left < -lateAfter(cadenceSeconds())) return "late";
  if (left < DUE_WINDOW_S) return "due";
  return "counting";
}

function paint() {
  if (!state || !state.connected) return;
  // THE SAME SENTENCE THE PANEL DRAWS, or nothing at all.
  //
  // The service sends `pending` while the reader is still asleep on the cadence
  // it last picked up, and the reader prints it on its foot line. The page read
  // neither copy, so the panel said something about the reader that the page
  // never mentioned -- and the person best placed to be confused by that is the
  // one who just changed the schedule here and was told nothing.
  //
  // An ABSENT key is nothing pending. It is never turned into a positive claim,
  // which is the mistake this feature has made twice in the other direction.
  const pending = typeof state.pending === "string" ? state.pending : "";
  pendingLine.textContent = pending;
  pendingLine.hidden = !pending;
  whenLine.className = "lv-when-line";
  // ONE PHRASE FOR THE CADENCE, whichever shape the schedule has. "about every
  // day" and "07:00 each day" answer the same question, and the chip, the small
  // print and the open panel all take it from here.
  const every =
    schedule.mode === "daily"
      ? `${schedule.dailyTime} each day`
      : everyPhrase(schedule.intervalSeconds);
  const looks =
    schedule.mode === "daily" ? `it aims for ${every}` : `it looks ${every}`;
  const Looks =
    schedule.mode === "daily" ? `It aims for ${every}` : `It looks ${every}`;
  schedChipText.textContent = scheduleWords();
  band = bandNow();
  whenTick.hidden = band !== "counting";
  // Always shown. It was hidden while counting on a phone because the count
  // and the sentence could not both fit beside the canvas; the canvas has its
  // own screen now and this card is only ever on the home page.
  whenLine.hidden = false;

  if (band === "off") {
    // OFF ON THE READER. No countdown, because there is no next check: the
    // service knows because the reader said so on its way out, which is the one
    // thing that tells this apart from a reader nobody has heard from.
    whenLine.className = "lv-when-line lv-stale";
    whenLine.textContent = "Live is off on the reader.";
    whenSub.textContent =
      "Your drawing is saved and appears the moment Live is switched back on.";
    return;
  }
  if (band === "late") {
    // LATE. A fact and nothing else.
    whenLine.className = "lv-when-line lv-stale";
    whenLine.textContent = state.lastCheckin
      ? `The reader last checked in ${ago(state.lastCheckin)}.`
      : "The reader has not checked in since you connected.";
    whenSub.textContent = `${Looks} when it can reach us.`;
    return;
  }
  if (band === "due") {
    // DUE NOW. It only looks on its way into sleep, so this is what happens
    // next, said as the gesture that causes it. It can legitimately sit here
    // for hours while somebody is reading on it, and that is not an error.
    whenLine.textContent =
      "They will see this the next time the reader is put down.";
    whenSub.textContent = `${Looks}, and only on its way to sleep.`;
    return;
  }
  // COUNTING. The figure is the headline; the sentence above it is the same
  // thing in the panel's own rounding and only fits where there is room.
  const left = secondsLeft();
  tickClock.textContent = clockSpan(left);
  whenLine.textContent = "They will see this in about";
  // NO HEDGING PARAGRAPH. The headline already says when, the chip already
  // says how often, and a sentence explaining the reader's sleep habits is
  // something nobody opened this page to read.
  whenSub.textContent = "";
}

const wide = () => matchMedia("(min-width: 900px)").matches;

// One tick a second, and it recomputes from the clock rather than counting
// down: a phone that slept for an hour comes back with the right figure instead
// of one an hour stale. When the count crosses into another band the whole
// block is repainted, so a countdown never reaches zero and sits there.
setInterval(() => {
  if (!state || !state.connected) return;
  if (bandNow() !== band) {
    paint();
    return;
  }
  if (band === "counting") tickClock.textContent = clockSpan(secondsLeft());
}, 1000);
addEventListener("resize", () => {
  if (state && state.connected) paint();
});

// --- the history -----------------------------------------------------------
//
// Everything ever sent to this reader, newest first, by anybody connected to
// it. SHARED on purpose: it is the record of what that reader has shown, not
// of what you personally sent, so every phone sees the same rail and any of
// them can send an old one again or delete one. Each entry says who sent it.
//
// Sending is what puts something here, and the new entry is picked: that is how
// the page says "this is what the reader takes next". Picking an older one
// re-points the reader at it without making a second copy.

const rail = document.getElementById("rail");
const histAct = document.getElementById("histAct");
let sent = { entries: [], selected: null };
let focused = null; // the entry the line under the rail is talking about
let askingDelete = null;
let askTimer = 0;

const KIND_WORD = { drawing: "drawing", message: "message", photo: "picture" };

// "today 21:40", "yesterday 08:05", "19 Sep 21:40". The date is what Mario asked
// the rail to carry; the time is what makes two drawings from one morning
// distinguishable.
function whenStamp(at) {
  const d = new Date(at * 1000);
  const now = new Date();
  const hm = d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  const day = (x) => new Date(x).setHours(0, 0, 0, 0);
  const diff = (day(now) - day(d)) / 86400000;
  if (diff === 0) return `today ${hm}`;
  if (diff === 1) return `yesterday ${hm}`;
  return `${d.toLocaleDateString([], { day: "numeric", month: "short" })} ${hm}`;
}

function tile(e) {
  const card = document.createElement("div");
  card.className = "lv-card";
  card.dataset.id = e.id;
  card.dataset.selected = String(e.id === sent.selected);
  card.dataset.gone = String(!!e.gone);

  const pick = document.createElement("button");
  pick.type = "button";
  pick.className = "lv-card-pick";
  pick.setAttribute("aria-pressed", card.dataset.selected);
  const what = KIND_WORD[e.kind] || "picture";
  pick.setAttribute(
    "aria-label",
    `${what} from ${e.by}, ${whenStamp(e.at)}` +
      (e.gone ? ", picture missing" : ""),
  );
  pick.title = pick.getAttribute("aria-label");

  const thumb = document.createElement("span");
  thumb.className = "lv-card-thumb";
  if (e.gone) {
    // AN ENTRY WHOSE PICTURE HAS GONE. It is still a true record of something
    // this reader showed, so it is not hidden and it can still be deleted; it
    // simply cannot be sent again, and the tile says which of the two it is
    // rather than showing a broken image and letting somebody press it.
    const g = document.createElement("span");
    g.className = "lv-card-gone";
    g.textContent = "Picture missing";
    thumb.appendChild(g);
    pick.disabled = true;
  } else {
    const img = document.createElement("img");
    img.loading = "lazy";
    img.decoding = "async";
    img.alt = "";
    // THE COOKIE HAS TO RIDE. A thumbnail lives behind the same session as the
    // history it belongs to, and an <img> sends no credentials by default, so
    // in production every tile asked the service anonymously and got a 401.
    // It never showed up in development because the dev server proxies the API
    // onto the page's own origin, where the cookie is sent without asking.
    // use-credentials also puts the request in CORS mode, which is what lets
    // the service's own Cross-Origin-Resource-Policy satisfy the site's COEP.
    img.crossOrigin = "use-credentials";
    // A VERSION IN THE URL. The response is immutable for a year, which is
    // right for something content-addressed and wrong the moment a browser has
    // cached a FAILURE under that address: it never revalidates, so a tile that
    // broke once stays broken for a year. The entry's own timestamp changes the
    // address whenever the picture behind it does, and never otherwise.
    // Only a fetched URL takes the version. A data: URL carries its own bytes
    // and a query string appended to one corrupts it.
    img.src = /^https?:/i.test(e.thumb)
      ? e.thumb + (e.thumb.includes("?") ? "&" : "?") + "v=" + (e.at || 0)
      : e.thumb;
    thumb.appendChild(img);
  }
  const badge = document.createElement("span");
  badge.className = "lv-card-badge";
  badge.textContent = "Next up";
  thumb.appendChild(badge);
  pick.appendChild(thumb);

  // NO CAPTION UNDER THE TILE. A time and a device under every picture made
  // the rail a table of records rather than a row of pictures, and the line
  // under the rail already names the one that is going out. The tile's own
  // label still carries both for anything reading the page aloud.

  pick.onclick = () => select(e);
  card.appendChild(pick);
  return card;
}

function emptyTile() {
  const d = document.createElement("div");
  d.className = "lv-empty";
  d.innerHTML =
    "<strong>Nothing sent yet</strong>" +
    "Draw something and send it. It lands here, and every phone on this reader sees it.";
  return d;
}

// MOVING THE PICK DOES NOT REBUILD THE RAIL. renderHistory empties it and
// makes every tile again, which recreates every <img> and makes the whole
// carousel blink -- for a change that is one attribute on two tiles. The
// pictures themselves have not changed, so nothing about them should be
// touched.
function markSelected() {
  for (const card of rail.querySelectorAll(".lv-card")) {
    const on = String(card.dataset.id === sent.selected);
    if (card.dataset.selected !== on) {
      card.dataset.selected = on;
      const pick = card.querySelector(".lv-card-pick");
      if (pick) pick.setAttribute("aria-pressed", on);
    }
  }
}

function renderHistory() {
  rail.textContent = "";
  if (!sent.entries.length) {
    rail.appendChild(emptyTile());
    focused = null;
    renderAct();
    requestAnimationFrame(markArrows);
    return;
  }
  for (const e of sent.entries) rail.appendChild(tile(e));
  requestAnimationFrame(markArrows);
  if (!focused || !sent.entries.some((e) => e.id === focused))
    focused = sent.selected || sent.entries[0].id;
  renderAct();
}

// The line under the rail, and the only control that deletes. A trash icon on a
// 46px tile sits a thumb's width from the control that merely picks, and
// deleting here removes a drawing for everybody on the reader: it asks, in
// place, and the question goes away by itself.
function renderAct() {
  histAct.textContent = "";
  const e = sent.entries.find((x) => x.id === focused);
  if (!e) return;
  // ONE BUTTON THAT CHANGES ITS MIND. Swapping the line for a question and
  // two more buttons reflowed the row under the rail every time somebody
  // reached for Delete, which is a layout jumping under a finger that is
  // about to tap. The same button asks instead: press it and it says what it
  // is about to do, press it again and it does it. It goes back by itself.
  // A WORD, NOT AN ICON, and this is the reason: the board's Clear is an
  // eraser, this is a bin, and on a phone they sit a thumb's width apart while
  // meaning completely different things -- rub out a drawing you can undo, and
  // remove a record from everybody's reader forever. An icon cannot carry that
  // difference. A word can, and this is the one control on the page rare enough
  // to spend the room on one.
  const armed = askingDelete === e.id;
  const del = document.createElement("button");
  del.type = "button";
  del.className = armed ? "lv-btn is-yes" : "lv-btn";
  del.textContent = armed ? "Really delete?" : "Delete";
  const what = armed
    ? "Press again to delete this from the reader, for everyone"
    : "Delete this from the reader, for everyone";
  del.title = what;
  del.setAttribute("aria-label", what);
  del.onclick = () => {
    if (armed) {
      clearTimeout(askTimer);
      remove(e);
      return;
    }
    askingDelete = e.id;
    renderAct();
    clearTimeout(askTimer);
    askTimer = setTimeout(() => {
      askingDelete = null;
      renderAct();
    }, 5000);
  };
  histAct.appendChild(del);
}

// What the line under the rail says when nothing else has just happened: which
// entry is going out, who made it and when.
function historyNote() {
  if (!sent.entries.length) return "";
  const sel = sent.entries.find((e) => e.id === sent.selected);
  if (!sel) return "Nothing is picked. The reader keeps what is on it.";
  // The tile beside it is the picture, so this says the two things a picture
  // cannot: who sent it and when. It is also short enough to leave room for the
  // Delete beside it on a 350px phone.
  return `Next up: ${sel.by}, ${whenStamp(sel.at)}.`;
}

async function select(e) {
  if (e.gone) return;
  // Moved here first and reported second, because a tap has to feel like a
  // tap. It is put back below if the service disagrees.
  const was = sent.selected;
  sent.selected = e.id;
  focused = e.id;
  askingDelete = null;
  markSelected();
  renderAct();
  say(historyNote(), true);
  if (demoCount !== null) return;
  const r = await api(`/api/history/${encodeURIComponent(e.id)}/select`, {
    method: "POST",
  });
  if (!r.ok) {
    // TWO PHONES SHARE THIS RAIL. The ordinary way this fails is the other one
    // deleting the entry between this list being drawn and the tap landing.
    // The service is right, so the list is fetched again and the service's own
    // sentence is what gets said.
    sent.selected = was;
    await loadHistory();
    say((r.body && r.body.error) || "That did not work.");
  }
}

async function remove(e) {
  askingDelete = null;
  const wasSelected = sent.selected === e.id;
  if (demoCount === null) {
    const r = await api(`/api/history/${encodeURIComponent(e.id)}`, {
      method: "DELETE",
    });
    if (!r.ok) {
      // Already gone, or somebody's access was taken away on the reader. Both
      // are answered by asking the service what is really there.
      await loadHistory();
      say((r.body && r.body.error) || "That did not work.");
      return;
    }
  }
  sent.entries = sent.entries.filter((x) => x.id !== e.id);
  if (wasSelected)
    sent.selected = sent.entries.length ? sent.entries[0].id : null;
  focused = sent.selected;
  renderHistory();
  // DELETING THE PICKED ONE MOVES THE PICK, and says so. Silently re-pointing a
  // device in another country as a side effect of tidying is the sort of thing
  // nobody notices until the wrong picture is on the fridge.
  say(
    wasSelected
      ? sent.entries.length
        ? "Deleted. " + historyNote()
        : "Deleted. Nothing is picked, so the reader keeps what is on it."
      : "Deleted.",
    true,
  );
}

// The arrows are the only thing on the rail saying it goes on, so they appear
// only when it does: on an empty rail they were two controls offering to scroll
// a thing with nothing in it.
const histOlderBtn = document.getElementById("histOlder");
const histNewerBtn = document.getElementById("histNewer");
function markArrows() {
  const more = rail.scrollWidth > rail.clientWidth + 2;
  histOlderBtn.disabled =
    !more || rail.scrollLeft >= rail.scrollWidth - rail.clientWidth - 2;
  histNewerBtn.disabled = !more || rail.scrollLeft <= 2;
}
rail.addEventListener("scroll", markArrows, { passive: true });
addEventListener("resize", markArrows);

document.getElementById("histOlder").onclick = () =>
  rail.scrollBy({ left: rail.clientWidth * 0.8, behavior: "smooth" });
document.getElementById("histNewer").onclick = () =>
  rail.scrollBy({ left: -rail.clientWidth * 0.8, behavior: "smooth" });

// --- loading the history ---------------------------------------------------

async function loadHistory() {
  if (demoCount !== null) {
    sent = demoHistory(demoCount);
    renderHistory();
    say(historyNote(), true);
    return;
  }
  const r = await api("/api/history");
  if (!r.ok || !r.body) {
    // Not connected, or the service could not be reached; both are said
    // elsewhere. An empty rail is the honest shape either way.
    sent = { entries: [], selected: null };
    renderHistory();
    return;
  }
  // The thumbnail path the service gives is relative to the SERVICE, which is
  // another host. Left as it came it resolved against this page and every tile
  // asked crossplay.ma-r-s.com for a picture only fridge.ma-r-s.com has.
  sent = {
    entries: (r.body.entries || []).map((e) => ({
      ...e,
      thumb: API + e.thumb,
    })),
    selected: r.body.selected || null,
  };
  renderHistory();
  say(historyNote(), true);
}

// Made-up entries, localhost only, so the rail can be judged full as well as
// empty. Every drawing here is drawn at 480x800 and quantised through the same
// path the real ones take, so the tiles are real pictures at a real scale.
function demoHistory(n) {
  const people = ["iPhone", "Mac", "Android phone", "iPad"];
  const kinds = ["drawing", "message", "photo"];
  const notes = [
    "Good\nmorning",
    "Te amo",
    "Call\nme",
    "Buenos\ndias",
    "Miss\nyou",
  ];
  const now = Math.floor(Date.now() / 1000);
  const entries = [];
  for (let i = 0; i < n; i++) {
    const kind = kinds[i % 3];
    const off = document.createElement("canvas");
    off.width = W;
    off.height = H;
    const o = off.getContext("2d");
    o.fillStyle = "#fff";
    o.fillRect(0, 0, W, H);
    o.fillStyle = "#000";
    if (kind === "message") {
      o.textAlign = "center";
      o.textBaseline = "middle";
      o.font = "78px ui-serif, Georgia, serif";
      const lines = notes[i % notes.length].split("\n");
      lines.forEach((l, k) =>
        o.fillText(l, W / 2, H / 2 + (k - (lines.length - 1) / 2) * 96),
      );
    } else if (kind === "photo") {
      for (let b = 0; b < 26; b++) {
        o.fillStyle = ["#000", "#555", "#aaa"][(b + i) % 3];
        o.fillRect(((b * 71 + i * 37) % W) - 40, ((b * 113) % H) - 30, 118, 96);
      }
    } else {
      o.strokeStyle = "#000";
      o.lineWidth = 12 + (i % 3) * 8;
      o.lineCap = "round";
      o.beginPath();
      for (let k = 0; k < 44; k++) {
        const x = W / 2 + Math.sin(k / 3.1 + i) * (110 + (i % 4) * 28);
        const y = 110 + k * 14;
        k ? o.lineTo(x, y) : o.moveTo(x, y);
      }
      o.stroke();
    }
    // Down to a tile, through the same four levels the panel has.
    // THE PANEL'S OWN SIZE, because anything less is guesswork about the
    // screen it lands on. 72x120 was a seven-times upscale; 240x400 was still
    // nearly a two-times one on a 3x phone, because a 152px tile is 456 real
    // pixels there. 480x800 is the source's full resolution, so the tile is
    // always downscaled and never invented, whatever the density.
    //
    // It is NOT re-quantised to the four levels either: the source is already
    // dithered, and cutting a shrunken copy back to four shades throws away
    // the averaging that makes a dither work, which is what left blotches.
    const t = document.createElement("canvas");
    t.width = W;
    t.height = H;
    const tc = t.getContext("2d");
    tc.imageSmoothingQuality = "high";
    tc.drawImage(off, 0, 0, W, H);
    entries.push({
      id: "d" + i,
      kind,
      by: people[i % people.length],
      at: now - i * (3600 * 7 + i * 900),
      // One entry in the set has lost its picture, because that state has to be
      // looked at too and it is the one nobody builds a tile for.
      gone: n > 6 && i === 4,
      thumb: t.toDataURL("image/png"),
    });
  }
  return { entries, selected: entries.length ? entries[0].id : null };
}

async function refresh() {
  if (demoCount !== null) {
    // Localhost, and only to look at the board. A reader that exists is not
    // needed to judge whether the controls fit the screen, and pairing one to
    // take a screenshot would mean a real device for every layout question.
    state = {
      connected: true,
      lastCheckin: Math.floor(Date.now() / 1000) - 3600 * 5,
      intervalSeconds: 86400,
      nextExpected: Math.floor(Date.now() / 1000) + 18750,
      liveOn: true,
    };
    schedule = {
      mode: "daily",
      intervalSeconds: 86400,
      dailyTime: "07:00",
      tz: browserTz(),
    };
    // ?demo=12&pending puts the service's pending sentence on the card, so the
    // state can be LOOKED AT without a reader that has not woken up yet. The
    // words are the service's own template (app.PENDING_TEMPLATE crossed with
    // store.cadence_words), not a plausible-looking sentence typed here: a
    // render of a screen that cannot occur is worse than no render.
    if (params.has("pending")) {
      state.pending = "Changing to every 15 minutes after the next check.";
    }
  } else {
    const r = await api("/api/state");
    if (r.offline || r.noProxy) {
      // A board already on screen STAYS on screen. Tearing it down over one
      // failed poll would take somebody's drawing away because a lift lost
      // signal for ten seconds; the honest thing is to say the page may be out
      // of date and leave it alone.
      if (app.hidden) {
        showGate();
        codeError.textContent = r.noProxy ? NO_PROXY : OFFLINE;
        codeHint.textContent = r.noProxy ? NO_PROXY_HINT : OFFLINE_HINT;
      } else {
        say(
          (r.noProxy ? NO_PROXY : OFFLINE) +
            " What is on screen may be out of date.",
        );
      }
      return;
    }
    state = r.body || { connected: false };
    if (state.schedule) {
      // THE SCHEDULE IS THE READER'S, not this browser's. Four phones can be
      // connected and the one that opens the page second has to see what the
      // first one chose, not a default it would then quietly re-apply.
      schedule = {
        mode: state.schedule.mode,
        intervalSeconds: state.schedule.intervalSeconds,
        dailyTime: state.schedule.dailyTime,
        // NOT the stored one. The zone is defined as wherever the person
        // reading this page is standing, so it is asserted every time rather
        // than adopted: echoing back what the service happens to hold let a
        // "UTC" that got in once perpetuate itself, and 01:00 was then
        // computed in UTC -- five hours off, which read as "in 19 hours" for a
        // check due in forty minutes.
        tz: browserTz(),
      };
    }
  }
  if (state.connected) {
    gate.hidden = true;
    app.hidden = false;
    document.body.classList.add("lv-connected");
    paint();
    // The swatches are drawn at the scale the stage really has, and the stage
    // has no size at all while the board is hidden: sized before this point
    // every dot falls back to the 300px guess and stops telling the truth.
    sizeSwatches();
    if (!sent.entries.length) say(historyNote(), true);
  } else {
    showGate();
  }
}

// The reader's QR encodes this page's address with ?c=<code> on it, so fill the
// box in and go. Somebody who scanned a screen has already done the one hard
// part; making them read the digits off and type them back would be the worst
// of both ways in.
const codeInput = document.getElementById("code");
const codeError = document.getElementById("codeError");
const codeHint = document.getElementById("codeHint");

// WHAT TO DO ABOUT IT, beside the service's sentence rather than instead of it.
// "That code did not work. Check the reader's screen." is true and it is not an
// instruction: the first person through this path scanned a code, was told it
// did not work, and worked out on his own that the reader was showing a
// different one by then. That recovery is the fix, so the page performs it.
const CODE_MOVED =
  "A reader's code changes. Type the six digits it is showing now.";
// Said when a scanned code is refused and this browser already has a reader.
//
// It does NOT claim the code belongs to a different reader, because the page
// cannot know that: the test below reads browser storage, and storage can be
// evicted while the server-set cookie survives, so somebody re-scanning their
// OWN reader can land here. It says what happened and what to do, and neither
// half stops being true in that case.
const CODE_AND_A_READER =
  "That code did not work here, and this page is still connected to the reader it had. " +
  CODE_MOVED +
  " Reloading this page keeps the reader you already have.";

// Codes THIS browser has spent. A second scan of a QR that already worked is
// not a failure and must not be reported as one -- the reader goes on showing
// that code until it expires, and somebody scanning it twice is connected
// already. Without this the only way to tell that apart from a code meant for
// another reader is to say nothing at all, which is what the page used to do
// and is the bug below.
//
// Browser storage, so it is per viewer and can come back empty: a cleared
// profile costs one honest "that code did not work", never a wrong connection.
const SPENT_KEY = "liveSpentCodes";
function spentCodes() {
  try {
    const all = JSON.parse(localStorage.getItem(SPENT_KEY) || "[]");
    return Array.isArray(all) ? all : [];
  } catch (e) {
    return [];
  }
}
function rememberSpent(code) {
  try {
    const all = spentCodes().filter((c) => c !== code);
    all.push(code);
    localStorage.setItem(SPENT_KEY, JSON.stringify(all.slice(-8)));
  } catch (e) {
    /* a private window, or storage turned off. The page still works. */
  }
}

codeInput.addEventListener("input", () => {
  codeInput.value = codeInput.value.replace(/\D/g, "").slice(0, 6);
});
codeInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") pair();
});

// Returns the service's refusal, or null once this browser has a reader.
// `quiet` holds the sentence back rather than printing it: the link path at the
// foot of this file has a second thing to try before a refusal is news.
async function pair(quiet) {
  if (codeInput.value.length !== 6) {
    const short = "Six digits, from the reader's screen.";
    if (!quiet) {
      codeError.textContent = short;
      codeHint.textContent = "";
    }
    return short;
  }
  codeError.textContent = "";
  codeHint.textContent = "";
  const code = codeInput.value;
  const r = await api("/api/claim", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ code, name: browserName() }),
  });
  // The service's own sentence, never one invented here. What IS invented here
  // is the line under it, which says what to do about it.
  if (!r.ok) {
    const said = (r.body && r.body.error) || "That did not work.";
    if (!quiet) {
      codeError.textContent = said;
      codeHint.textContent = CODE_MOVED;
    }
    return said;
  }
  rememberSpent(code);
  await refresh();
  // A CLAIM THAT WORKED AND LEFT YOU ON THE GATE IS ITS OWN CONDITION.
  //
  // Mario, on his phone: "The code adds the device but doesn't take me to the
  // ui to send." The claim returned 200, the reader gained the phone, and the
  // very next /api/state said not connected -- because the session cookie had
  // been silently refused. The page redrew the same screen, which reads as the
  // six digits being wrong when they were right, and there is nothing on it to
  // suggest otherwise.
  //
  // This is diagnosable and narrow: the service accepted the code, so the
  // reader IS paired; what did not survive is the cookie that says which phone
  // this is. Say that, rather than inviting another code.
  if (!app.hidden) {
    await loadHistory();
    return null;
  }
  codeError.textContent =
    "The reader took the code, but this browser was not remembered.";
  codeHint.textContent =
    "The code worked and the reader has this phone. What did not stick is the " +
    "cookie that signs you in, which a browser refuses over a plain http " +
    "address. Open the page on crossplay.ma-r-s.com.";
  return null;
}
document.getElementById("pair").onclick = () => pair(false);

// --- when it looks ---------------------------------------------------------
//
// TWO SHAPES, AND THE SERVICE OWNS THE CALENDAR.
//
// "Every day at seven" is the use this whole feature exists for: somebody wakes
// up to a drawing. The reader cannot express it and does not have to. It has no
// wall clock worth trusting -- a wake is a chip reset and the timer is an RC
// oscillator -- so it is told a NUMBER OF SECONDS to sleep for on every check
// (X-Next-Wake, clamped to 15 minutes..7 days by live::clampInterval) and goes
// back down. A daily alarm is therefore the service working out how many
// seconds are left until the next 07:00 in a named timezone, which is
// arithmetic a device never hears about and a change no firmware needs.
//
// What the panel says is a separate question from what the device sleeps for,
// and today they are one number. See the note in the report: under a daily
// schedule the first sleep is a part-day, and the panel's "Every N hours" is
// composed from that same figure, so it would announce a cadence that is not
// the cadence. The service has to send the two apart.
const schedChip = document.getElementById("schedChip");
const schedChipText = document.getElementById("schedChipText");
const schedPanel = document.getElementById("sched");
const modeEvery = document.getElementById("modeEvery");
const modeDaily = document.getElementById("modeDaily");
const intervalSel = document.getElementById("interval");
const dailyTime = document.getElementById("dailyTime");
const tzWords = document.getElementById("tzWords");
const schedFine = document.getElementById("schedFine");

const browserTz = () => {
  try {
    return Intl.DateTimeFormat().resolvedOptions().timeZone || "UTC";
  } catch (e) {
    return "UTC";
  }
};
// "GMT-5", from the browser rather than from a table this page would have to
// keep in step with the world's legislatures.
function offsetOf(zone) {
  try {
    const parts = new Intl.DateTimeFormat("en-GB", {
      timeZone: zone,
      timeZoneName: "shortOffset",
    }).formatToParts(new Date());
    const tzp = parts.find((x) => x.type === "timeZoneName");
    return tzp ? tzp.value : "";
  } catch (e) {
    return "";
  }
}
// "Bogota", not "America/Bogota": the place, in the words somebody would use.
const placeOf = (zone) =>
  String(zone || "")
    .split("/")
    .pop()
    .replace(/_/g, " ");

let schedule = {
  mode: "every",
  intervalSeconds: 86400,
  dailyTime: "07:00",
  tz: browserTz(),
};

// The words the chip carries, and the same words the small print uses. One
// function, so the two can never name different schedules.
function scheduleWords() {
  if (schedule.mode === "daily") return `${schedule.dailyTime} daily`;
  return everyPhrase(schedule.intervalSeconds).replace(/^about /, "");
}
const cadenceSeconds = () =>
  schedule.mode === "daily" ? 86400 : schedule.intervalSeconds;

// HOW GOOD THE HOUR IS, in one sentence, neither promising 07:00 sharp nor
// hedged until it reads as broken. The sleep drifts about a percent, so a day
// lands within roughly a quarter of an hour; every check-in re-syncs, so the
// error never accumulates past one interval.
function paintSchedule() {
  schedChipText.textContent = scheduleWords();
  modeEvery.checked = schedule.mode === "every";
  modeDaily.checked = schedule.mode === "daily";
  intervalSel.value = String(schedule.intervalSeconds);
  intervalSel.disabled = schedule.mode !== "every";
  dailyTime.value = schedule.dailyTime;
  dailyTime.disabled = schedule.mode !== "daily";
  // The zone is simply where this browser is. Not asked, and not announced
  // either: "07:00" already means seven where you are standing, so saying so
  // is a line spent telling somebody something they assumed correctly.
  tzWords.textContent = "";
  // ONE LINE. Three sentences of caveat about drift, re-syncing and readers in
  // somebody's hands is a paragraph nobody reads to set an alarm. The only
  // part that changes what a person expects is that it is approximate.
  schedFine.textContent =
    schedule.mode === "daily" ? "Give or take a quarter of an hour." : "";
}

function openSched(open) {
  schedPanel.hidden = !open;
  schedChip.setAttribute("aria-expanded", String(open));
  if (open) paintSchedule();
}
schedChip.onclick = () => openSched(schedPanel.hidden);
[modeEvery, modeDaily].forEach((r) => {
  r.onchange = () => {
    schedule.mode = r.value;
    paintSchedule();
  };
});
intervalSel.onchange = () => {
  schedule.intervalSeconds = +intervalSel.value;
  paintSchedule();
};
dailyTime.onchange = () => {
  schedule.dailyTime = dailyTime.value || "07:00";
  paintSchedule();
};
document.getElementById("schedDone").onclick = async () => {
  openSched(false);
  paint();
  if (demoCount !== null) return;
  const r = await api("/api/schedule", {
    method: "PUT",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      mode: schedule.mode,
      intervalSeconds: schedule.intervalSeconds,
      dailyTime: schedule.dailyTime,
      tz: browserTz(),
    }),
  });
  if (!r.ok) {
    say((r.body && r.body.error) || "That did not work.");
    return;
  }
  // The reply carries the pending sentence too; refresh() is what puts it on
  // the card, from /api/state, so both copies come from one place.
  await refresh();
};

const sendBtn = document.getElementById("send");
sendBtn.onclick = async () => {
  sendBtn.disabled = true;
  const was = sendBtn.textContent;
  sendBtn.textContent = "Sending...";
  const r = await api("/api/history", {
    method: "POST",
    headers: {
      "content-type": "application/octet-stream",
      // WHICH TAB MADE IT. The bytes cannot say: the same bitmap typed and
      // drawn are two different records, and the rail names one "the message"
      // and the other "the drawing".
      "x-kind":
        mode === "write" ? "message" : mode === "photo" ? "photo" : "drawing",
    },
    body: toBmp(),
  });
  sendBtn.textContent = was;
  sendBtn.disabled = false;
  if (!r.ok) {
    say((r.body && r.body.error) || "It did not send.");
    return;
  }
  await refresh();
  // SENDING IS THE WAY OUT. Two taps become one, and the screen it lands on is
  // the one that shows the consequence: the new entry at the head of the rail,
  // picked, with the line under it saying when the reader takes it. Sending
  // from the home page instead would have been Done-then-Send, two decisions
  // for one act, with a finished drawing sitting in limbo in between.
  closeCompose();
  // REBUILT FROM THE SERVICE rather than pushed to locally, because the rail
  // is shared: fetching it back is also how this phone finds out what the
  // others did while it was drawing.
  await loadHistory();
  // Sending says BOTH things: that the rail has one more in it and is pointed
  // at it, and when the reader will take it. The countdown above says the
  // second on its own, but this line is where a thumb already is.
  const band2 = bandNow();
  say(
    band2 === "counting"
      ? `Sent. The reader takes it ${human(secondsLeft())}.`
      : band2 === "due"
        ? "Sent. The reader takes it the next time it is put down."
        : band2 === "off"
          ? "Sent. It appears when Live is switched back on."
          : "Sent. Waiting for the reader to come back.",
  );
};

// The board is restored before anything is drawn on it, so a reload lands on
// the drawing that was in progress rather than on blank paper.
loadDraft();
render();
markTools();
applyView();
paintSchedule();
markWay();

// A code in the address claims itself: there is nothing else to decide on that
// screen, and a filled box with a button still to find reads as "did it work?".
//
// THE CLAIM IS TRIED FIRST, not the state, because scanning a code is an
// explicit request for THAT reader -- a browser already connected to another
// one still has to be moved. But it is tried QUIETLY, because one way this path
// fails is somebody scanning a QR they have already used: the code has been
// spent, the service rightly refuses it, and printing "That code did not work"
// over a page that is about to connect perfectly well is a screen calling a
// success a failure.
//
// THE REFUSAL USED TO BE SWALLOWED WHOLE when this browser had any reader at
// all, and that is a different case wearing the same clothes. Somebody who
// scans a second reader's code while connected to a first got no error, no
// notice and no hint: the page simply carried on showing the reader they were
// already on, and the next drawing went to the wrong fridge. The two are told
// apart by whether this browser is the one that spent that code, which is a
// fact only this browser holds.
const fromLink = params.get("c");
if (fromLink && /^\d{4,8}$/.test(fromLink)) {
  const linked = fromLink.slice(0, 6);
  codeInput.value = linked;
  pair(true).then(async (refusal) => {
    if (!refusal) {
      // Spent, and it worked. Take it out of the address so a reload is not a
      // second attempt at a code that can only be used once.
      window.history.replaceState(null, "", cleanUrl());
      return;
    }
    await refresh();
    const hadReader = !app.hidden;
    window.history.replaceState(null, "", cleanUrl());
    if (hadReader && spentCodes().includes(linked)) {
      await loadHistory();
      return;
    }
    showGate();
    codeError.textContent = refusal;
    codeHint.textContent = hadReader ? CODE_AND_A_READER : CODE_MOVED;
  });
} else {
  refresh().then(loadHistory);
}
setInterval(refresh, 60000);
// Two phones share this rail, so what it holds can change while nobody here is
// touching it. Coming back to the tab is the cheapest moment to find out.
addEventListener("visibilitychange", () => {
  if (!document.hidden && !app.hidden) {
    refresh();
    loadHistory();
  }
});
