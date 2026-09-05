// The wallpaper uploader, browser half. Decodes the dropped picture with a
// canvas, fits it to the panel, hands the pixels to the pure pipeline in
// convert.js, previews the 1-bit result exactly as the reader will show it, and
// offers the .bmp for download. Nothing is uploaded: every byte stays in this
// tab.

import {
  WALL_W,
  WALL_H,
  imageTypeOk,
  rgbaToGray,
  dither,
  encodeBmp1bit,
} from "./convert.js";

const $ = (id) => document.getElementById(id);

const dropzone = $("dropzone");
const filepick = $("filepick");
const controls = $("controls");
const orientSeg = $("orientSeg");
const fitSeg = $("fitSeg");
const invert = $("invert");
const download = $("download");
const another = $("another");
const preview = $("preview");
const emptyNote = $("emptyNote");
const previewCap = $("previewCap");
const error = $("error");
const pickError = $("pickError");

const state = {
  img: null,
  name: "wallpaper",
  orient: "portrait",
  fit: "cover",
  bits: null,
  w: WALL_W,
  h: WALL_H,
};

function targetSize() {
  return state.orient === "landscape"
    ? { w: WALL_H, h: WALL_W }
    : { w: WALL_W, h: WALL_H };
}

// A file name the device is happy to list: keep letters, digits and a few marks,
// collapse the rest, and never hand back something empty or absurdly long.
function bmpName(sourceName) {
  const base = (sourceName || "wallpaper").replace(/\.[^.]+$/, "");
  let cleaned = base
    .replace(/[^A-Za-z0-9._ -]+/g, "_")
    .replace(/_{2,}/g, "_")
    .trim();
  if (!cleaned) cleaned = "wallpaper";
  if (cleaned.length > 40) cleaned = cleaned.slice(0, 40);
  return cleaned + ".bmp";
}

function showPickError(msg) {
  pickError.textContent = msg;
  pickError.hidden = false;
}

function decode(file) {
  pickError.hidden = true;
  if (!imageTypeOk(file)) {
    showPickError(
      "That is not an image the browser can open. Try a PNG, JPG, WebP or GIF.",
    );
    return;
  }
  const url = URL.createObjectURL(file);
  const img = new Image();
  img.onload = () => {
    URL.revokeObjectURL(url);
    state.img = img;
    state.name = bmpName(file.name);
    controls.hidden = false;
    render();
  };
  img.onerror = () => {
    URL.revokeObjectURL(url);
    showPickError("The browser could not read that file as an image.");
  };
  img.src = url;
}

// Draw the source into a target-sized frame, cover (fill, may crop) or contain
// (fit, letterboxed on white), then dither and preview.
function render() {
  if (!state.img) return;
  error.hidden = true;
  const { w, h } = targetSize();
  state.w = w;
  state.h = h;

  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  const ctx = c.getContext("2d", { willReadFrequently: true });
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";
  ctx.fillStyle = "#fff";
  ctx.fillRect(0, 0, w, h);

  const iw = state.img.naturalWidth;
  const ih = state.img.naturalHeight;
  const scale =
    state.fit === "cover" ? Math.max(w / iw, h / ih) : Math.min(w / iw, h / ih);
  const dw = Math.round(iw * scale);
  const dh = Math.round(ih * scale);
  ctx.drawImage(
    state.img,
    Math.round((w - dw) / 2),
    Math.round((h - dh) / 2),
    dw,
    dh,
  );

  let data;
  try {
    data = ctx.getImageData(0, 0, w, h).data;
  } catch (e) {
    // A cross-origin source would taint the canvas; a dropped local file never
    // does, so this only trips on an unusual paste. Say so rather than freeze.
    error.textContent =
      "Could not read the image pixels. Try downloading the picture first, then dropping the file.";
    error.hidden = false;
    return;
  }

  const gray = rgbaToGray(data, w * h);
  const bits = dither(gray, w, h, { invert: invert.checked });
  state.bits = bits;

  // Preview: paint the 1-bit result back to black/white so what is on screen is
  // exactly what the reader will show, not the smoothed source.
  preview.width = w;
  preview.height = h;
  preview.style.width = state.orient === "landscape" ? "350px" : "210px";
  preview.style.height = state.orient === "landscape" ? "210px" : "350px";
  const pctx = preview.getContext("2d");
  const out = pctx.createImageData(w, h);
  for (let i = 0; i < w * h; i++) {
    const v = bits[i] ? 255 : 0;
    out.data[i * 4] = v;
    out.data[i * 4 + 1] = v;
    out.data[i * 4 + 2] = v;
    out.data[i * 4 + 3] = 255;
  }
  pctx.putImageData(out, 0, 0);

  preview.hidden = false;
  emptyNote.hidden = true;
  previewCap.textContent = `${w} × ${h} · 1-bit`;
  download.disabled = false;
}

function doDownload() {
  if (!state.bits) return;
  const bmp = encodeBmp1bit(state.bits, state.w, state.h);
  const blob = new Blob([bmp], { type: "image/bmp" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = state.name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// --- wiring ------------------------------------------------------------------

function pickFile(files) {
  if (files && files[0]) decode(files[0]);
}

dropzone.addEventListener("click", () => filepick.click());
dropzone.addEventListener("keydown", (e) => {
  if (e.key === "Enter" || e.key === " ") {
    e.preventDefault();
    filepick.click();
  }
});
filepick.addEventListener("change", () => pickFile(filepick.files));

["dragenter", "dragover"].forEach((ev) =>
  dropzone.addEventListener(ev, (e) => {
    e.preventDefault();
    dropzone.classList.add("is-over");
  }),
);
["dragleave", "dragend", "drop"].forEach((ev) =>
  dropzone.addEventListener(ev, (e) => {
    e.preventDefault();
    dropzone.classList.remove("is-over");
  }),
);
dropzone.addEventListener("drop", (e) => {
  if (e.dataTransfer && e.dataTransfer.files) pickFile(e.dataTransfer.files);
});

function wireSegment(seg, key) {
  seg.addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn) return;
    for (const b of seg.querySelectorAll("button"))
      b.setAttribute("aria-pressed", "false");
    btn.setAttribute("aria-pressed", "true");
    state[key] = btn.dataset[key];
    render();
  });
}
wireSegment(orientSeg, "orient");
wireSegment(fitSeg, "fit");

invert.addEventListener("change", render);
download.addEventListener("click", doDownload);
another.addEventListener("click", () => {
  filepick.value = "";
  filepick.click();
});

// The shared topbar's mobile menu toggle, kept local so the page works on its
// own without depending on a site-wide script existing.
const navToggle = document.querySelector(".topnav-toggle");
const nav = $("topnav");
if (navToggle && nav) {
  navToggle.addEventListener("click", () => {
    const open = navToggle.getAttribute("aria-expanded") === "true";
    navToggle.setAttribute("aria-expanded", String(!open));
    nav.classList.toggle("is-open", !open);
  });
}
