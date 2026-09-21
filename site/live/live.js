// Live: draw a note here, a reader on somebody's fridge shows it.
//
// THE SERVICE IS ON ANOTHER HOST. This page is part of the CrossPlay site;
// fridge.ma-r-s.com answers /api/ and has no page of its own. Both names sit
// under ma-r-s.com, so the sender cookie (scoped to .ma-r-s.com by the service)
// is first-party for both and rides these requests -- but only because every
// fetch below asks for it: `credentials: "include"` is what a cross-ORIGIN
// request needs, and "same-origin", which is the default, would send nothing at
// all and every call would come back as "not connected".

const LIVE_API = "https://fridge.ma-r-s.com";
// Local work only, and it is the one thing that can point this page anywhere
// else: ?local sends every request to this page's own origin, which
// site/serve.py proxies to the real service. Honoured on localhost alone, so a
// link carrying it can never redirect somebody's drawing off the real host.
const onLocalhost = ["localhost", "127.0.0.1", "[::1]"].includes(
  location.hostname,
);
const API =
  onLocalhost && new URLSearchParams(location.search).has("local")
    ? ""
    : LIVE_API;

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
const undoStack = [];
const snap = () => {
  undoStack.push(bits.slice());
  if (undoStack.length > 20) undoStack.shift();
};

// The four levels as the panel renders them, so the canvas IS the screen.
const LEVEL_GREY = [0, 85, 170, 255];
function render() {
  const img = ctx.createImageData(W, H);
  const d = img.data;
  for (let i = 0; i < W * H; i++) {
    const v = LEVEL_GREY[bits[i]];
    d[i * 4] = d[i * 4 + 1] = d[i * 4 + 2] = v;
    d[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}

const grey = (px, n) => {
  const g = new Uint8Array(n);
  for (let i = 0; i < n; i++)
    g[i] = (px[i * 4] * 77 + px[i * 4 + 1] * 150 + px[i * 4 + 2] * 29) >> 8;
  return g;
};

function rasterise(draw, useDither) {
  const off = document.createElement("canvas");
  off.width = W;
  off.height = H;
  const o = off.getContext("2d");
  o.fillStyle = "#fff";
  o.fillRect(0, 0, W, H);
  draw(o);
  const g = grey(o.getImageData(0, 0, W, H).data, W * H);
  // QUANTISED to the four levels, not dithered: the panel can show these greys,
  // so faking them with black and white would throw away three quarters of what
  // it can do. Flat ink still thresholds, because text wants an edge and not a
  // tone, and error diffusion over solid black only fringes its edges.
  bits = useDither
    ? Uint8Array.from(g, (v) => {
        const x = inverted ? 255 - v : v;
        return x < 43 ? 0 : x < 128 ? 1 : x < 213 ? 2 : 3;
      })
    : Uint8Array.from(g, (v) => (v >= 128 !== inverted ? 3 : 0));
  render();
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

function stamp(x, y, r) {
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
      if (dx * dx + dy * dy <= r2) bits[yy * W + xx] = lv;
    }
  }
}

function line(x0, y0, x1, y1, r) {
  const dx = x1 - x0;
  const dy = y1 - y0;
  const n = Math.max(1, Math.ceil(Math.hypot(dx, dy)));
  for (let i = 0; i <= n; i++) stamp(x0 + (dx * i) / n, y0 + (dy * i) / n, r);
}

// The reader's file: 480x800 at TWO bits per pixel, four-entry palette,
// bottom-up, rows padded to four bytes. 96070 bytes, which the service checks
// byte-exactly: a wrong-sized file that still parses is drawn half-rendered on
// the reader forever rather than refused.
//
// Two bits rather than eight because radio time is the battery cost, and a
// quarter of the bytes carries exactly the levels the panel can show.
function toBmp() {
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
    const src = (H - 1 - y) * W;
    const dst = off + y * rowBytes;
    for (let x = 0; x < W; x++)
      b[dst + (x >> 2)] |= (bits[src + x] & 3) << ((3 - (x & 3)) * 2);
  }
  return b;
}

// --- the surface -----------------------------------------------------------

const stage = document.getElementById("stage");
const nib = document.getElementById("nib");
let drawing = false;
let last = null;
const pointAt = (e) => {
  const r = stage.getBoundingClientRect();
  return [
    ((e.clientX - r.left) / r.width) * W,
    ((e.clientY - r.top) / r.height) * H,
  ];
};
stage.addEventListener("pointerdown", (e) => {
  if (mode !== "draw") return;
  stage.setPointerCapture(e.pointerId);
  snap();
  drawing = true;
  last = pointAt(e);
  stamp(last[0], last[1], pen / 2);
  render();
});
stage.addEventListener("pointermove", (e) => {
  if (!drawing) return;
  const p = pointAt(e);
  line(last[0], last[1], p[0], p[1], pen / 2);
  last = p;
  render();
});
addEventListener("pointerup", () => {
  drawing = false;
});

// The nib, shown at the size it will really mark. Sized from the stage's own
// width so it tracks the panel's scale rather than a guess.
function placeNib(e) {
  const r = stage.getBoundingClientRect();
  const d = pen * (r.width / W);
  nib.style.width = d + "px";
  nib.style.height = d + "px";
  nib.style.left = e.clientX - r.left + "px";
  nib.style.top = e.clientY - r.top + "px";
}
stage.addEventListener("pointermove", placeNib);
stage.addEventListener("pointerenter", placeNib);

// The swatch shows the mark at the size it is DRAWN AT ON SCREEN, which means
// scaling by the same factor the stage scales the panel by. Sized in raw panel
// pixels it is about a third too big and the widest one bursts its button.
const SIZES = [4, 10, 18, 30];
const sizesEl = document.getElementById("sizes");
const SWATCH = 38;
const SWATCH_MAX = SWATCH - 12; // always clear of the button's edge
function stageScale() {
  const r = stage.getBoundingClientRect();
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

document.getElementById("clear").onclick = () => {
  snap();
  bits.fill(3);
  photo = null;
  render();
};
document.getElementById("undo").onclick = () => {
  const s = undoStack.pop();
  if (s) {
    bits = s;
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
document.querySelectorAll("#tabs button").forEach((t) => {
  t.onclick = () => {
    document
      .querySelectorAll("#tabs button")
      .forEach((x) => x.setAttribute("aria-selected", String(x === t)));
    mode = t.dataset.mode;
    writePane.hidden = mode !== "write";
    photoPane.hidden = mode !== "photo";
    penTools.hidden = mode !== "draw";
    regen();
  };
});
document.getElementById("msg").addEventListener("input", regen);
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

async function api(path, opts) {
  // credentials: "include" and not "same-origin" -- see the note at the top.
  const r = await fetch(API + path, { credentials: "include", ...opts });
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

function showGate() {
  gate.hidden = false;
  app.hidden = true;
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

function human(sec) {
  if (sec < 60) return "in under a minute";
  const h = sec / 3600;
  if (h < 1) return `in about ${Math.max(1, Math.round(sec / 60))} minutes`;
  if (h < 2) return "in about an hour";
  if (h < 48) return `in about ${Math.round(h)} hours`;
  return `in about ${Math.round(h / 24)} days`;
}

const whenLine = document.getElementById("whenLine");
const whenSub = document.getElementById("whenSub");
const sendNote = document.getElementById("sendNote");
let state = null;

function paint() {
  if (!state || !state.connected) return;
  document.getElementById("interval").value = String(state.intervalSeconds);
  whenLine.className = "lv-when-line";
  if (!state.lastCheckin) {
    whenLine.textContent = "The reader has not checked in yet.";
    whenSub.textContent = "It will pick this up the first time it looks.";
    sendNote.textContent = "";
    return;
  }
  const left = state.nextExpected - Math.floor(Date.now() / 1000);
  if (left < -1800) {
    // Past its window. Stop counting down to a moment that already went by.
    whenLine.className = "lv-when-line lv-stale";
    whenLine.textContent = "The reader has not checked in when it was due.";
    whenSub.textContent = "It will pick this up the next time it reaches us.";
    sendNote.textContent = "Waiting for it to come back.";
    return;
  }
  const morning = new Date(state.nextExpected * 1000).getHours() < 11;
  whenLine.textContent = morning
    ? "They will see this in the morning."
    : "They will see this later today.";
  // Deliberately vague: this is the service's estimate, and the reader's sleep
  // timer drifts percent-level, so a figure to the second would be a small lie.
  whenSub.textContent = `Next check ${human(left)}`;
  sendNote.textContent = morning
    ? "Arrives in the morning."
    : "Arrives later today.";
}

async function refresh() {
  const r = await api("/api/state");
  state = r.body || { connected: false };
  if (state.connected) {
    gate.hidden = true;
    app.hidden = false;
    paint();
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
    const short = "Six digits, from the reader’s screen.";
    if (!quiet) codeError.textContent = short;
    return short;
  }
  codeError.textContent = "";
  const r = await api("/api/claim", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ code: codeInput.value, name: browserName() }),
  });
  // The service's own sentence, never one invented here.
  if (!r.ok) {
    const said = (r.body && r.body.error) || "That did not work.";
    if (!quiet) codeError.textContent = said;
    return said;
  }
  await refresh();
  return null;
}
document.getElementById("pair").onclick = () => pair(false);

document.getElementById("interval").addEventListener("change", async (e) => {
  const r = await api("/api/interval", {
    method: "PUT",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ seconds: +e.target.value }),
  });
  if (r.ok) await refresh();
});

const sendBtn = document.getElementById("send");
sendBtn.onclick = async () => {
  sendBtn.disabled = true;
  const was = sendBtn.textContent;
  sendBtn.textContent = "Sending…";
  const r = await api("/api/image", {
    method: "PUT",
    headers: { "content-type": "application/octet-stream" },
    body: toBmp(),
  });
  sendBtn.textContent = was;
  sendBtn.disabled = false;
  if (!r.ok) {
    sendNote.textContent = (r.body && r.body.error) || "It did not send.";
    return;
  }
  await refresh();
  sendNote.textContent = "Sent. " + sendNote.textContent;
};

render();

// A code in the address claims itself: there is nothing else to decide on that
// screen, and a filled box with a button still to find reads as "did it work?".
//
// THE CLAIM IS TRIED FIRST, not the state, because scanning a code is an
// explicit request for THAT reader -- a browser already connected to another
// one still has to be moved. But it is tried QUIETLY, because the commonest way
// this path fails is somebody RELOADING the page they already connected with:
// the code in the address has been spent, the service rightly refuses it, and
// printing "That code did not work" over a page that is about to connect
// perfectly well is a screen calling a success a failure. The refusal is held
// until /api/state has said this browser has no reader either.
const fromLink = new URLSearchParams(location.search).get("c");
if (fromLink && /^\d{4,8}$/.test(fromLink)) {
  codeInput.value = fromLink.slice(0, 6);
  pair(true).then(async (refusal) => {
    if (!refusal) {
      // Spent, and it worked. Take it out of the address so a reload is not a
      // second attempt at a code that can only be used once.
      history.replaceState(null, "", location.pathname);
      return;
    }
    await refresh();
    if (app.hidden) codeError.textContent = refusal;
  });
} else {
  refresh();
}
setInterval(refresh, 60000);
