// Host tests for the browser wallpaper converter's pure half. No browser: the
// DOM-free functions in site/wallpapers/convert.js are exactly the ones a test
// can pin, and they are where a wrong BMP byte or a rejected image would come
// from. The canvas fitting is verified in the browser instead.

import {
  WALL_W,
  WALL_H,
  imageTypeOk,
  rgbaToGray,
  dither,
  encodeBmp1bit,
  pixelsToBmp,
} from "../../site/wallpapers/convert.js";

let run = 0;
let failed = 0;
function check(cond, what) {
  run++;
  if (!cond) {
    failed++;
    console.log("FAIL wpupload  " + what);
  }
}

// --- A non-image is rejected -------------------------------------------------
check(imageTypeOk({ name: "photo.png", type: "image/png" }), "png by mime");
check(imageTypeOk({ name: "photo.jpg", type: "image/jpeg" }), "jpg by mime");
check(imageTypeOk({ name: "PHOTO.JPEG", type: "" }), "jpeg by extension when type is blank");
check(imageTypeOk({ name: "art.webp", type: "" }), "webp by extension");
check(!imageTypeOk({ name: "notes.txt", type: "text/plain" }), "reject text/plain");
check(!imageTypeOk({ name: "deck.apkg", type: "application/zip" }), "reject a zip");
check(!imageTypeOk({ name: "noext", type: "" }), "reject no type and no extension");
check(!imageTypeOk(null), "reject a missing file");
// A wrong MIME with an image extension is still rejected: the browser's own
// type wins when it disagrees, so a renamed .exe cannot sneak through.
check(!imageTypeOk({ name: "x.png", type: "application/octet-stream" }), "mime overrides a lying extension");

// --- The BMP is byte-for-byte what the device accepts ------------------------
function allWhite(w, h) {
  const bits = new Uint8Array(w * h);
  bits.fill(1);
  return bits;
}
const bmp = encodeBmp1bit(allWhite(WALL_W, WALL_H), WALL_W, WALL_H);
const dv = new DataView(bmp.buffer);
check(bmp.length === 62 + 60 * 800, "480x800 1-bpp BMP is 48062 bytes");
check(bmp[0] === 0x42 && bmp[1] === 0x4d, "starts with 'BM'");
check(dv.getUint32(10, true) === 62, "pixel data offset 62");
check(dv.getUint32(14, true) === 40, "BITMAPINFOHEADER");
check(dv.getInt32(18, true) === WALL_W, "width 480");
check(dv.getInt32(22, true) === WALL_H, "height 800 (positive = bottom-up)");
check(dv.getUint16(28, true) === 1, "1 bit per pixel");
check(dv.getUint32(30, true) === 0, "BI_RGB, uncompressed");
check(dv.getUint32(46, true) === 2, "two palette colours");
// palette[0] black, palette[1] white
check(bmp[54] === 0 && bmp[55] === 0 && bmp[56] === 0, "palette[0] is black");
check(bmp[58] === 0xff && bmp[59] === 0xff && bmp[60] === 0xff, "palette[1] is white");
check(bmp[62] === 0xff, "an all-white row packs to 0xFF (set bit = white)");

// Row padding: a width that is not a multiple of 32 still pads each row to 4
// bytes, or the device reads the next row shifted.
const odd = encodeBmp1bit(allWhite(30, 2), 30, 2);
const oddRow = ((30 + 31) >> 5) << 2; // 4 bytes
check(odd.length === 62 + oddRow * 2, "a 30px row is padded to 4 bytes");

// --- Dithering actually runs, and invert flips it ----------------------------
function solidGray(w, h, v) {
  const g = new Uint8Array(w * h);
  g.fill(v);
  return g;
}
const midBits = dither(solidGray(64, 64, 128), 64, 64);
let whites = 0;
for (const b of midBits) whites += b;
// A flat mid grey must become a mix, not a solid field -- that is the whole
// point of dithering. Somewhere near half, never all-or-nothing.
check(whites > 64 * 64 * 0.2 && whites < 64 * 64 * 0.8, "mid grey dithers to a mix, not a solid");

const black = dither(solidGray(16, 16, 0), 16, 16);
check(black.every((b) => b === 0), "pure black is all 0 bits");
const white = dither(solidGray(16, 16, 255), 16, 16);
check(white.every((b) => b === 1), "pure white is all 1 bits");
const blackInv = dither(solidGray(16, 16, 0), 16, 16, { invert: true });
check(blackInv.every((b) => b === 1), "invert turns black into white bits");

// --- An oversized frame is handled, not crashed ------------------------------
// The browser downsizes to 480x800 before this runs; the pure pipeline must
// still cope with any frame it is handed and always emit a valid BMP of that
// size. A large frame here stands in for "the user dropped a 6000x4000 photo".
const big = { data: new Uint8ClampedArray(2000 * 1500 * 4).fill(200), width: 2000, height: 1500 };
const bigBmp = pixelsToBmp(big);
check(bigBmp.length === 62 + ((((2000 + 31) >> 5) << 2) * 1500), "a huge frame encodes without error");
check(bigBmp[0] === 0x42 && bigBmp[1] === 0x4d, "the huge frame's BMP is still a BMP");

// gray helper sanity: white pixel -> ~255, black -> 0
const g = rgbaToGray(new Uint8ClampedArray([255, 255, 255, 255, 0, 0, 0, 255]), 2);
check(g[0] > 250 && g[1] === 0, "luminance maps white high and black to zero");

console.log(`wpupload: ${run} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
