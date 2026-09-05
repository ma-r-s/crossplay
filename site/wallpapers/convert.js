// The wallpaper image pipeline, the pure half: luminance, dithering and the
// 1-bit BMP encoder. No DOM, so host-tests/wpupload/ runs it under bun with no
// browser, and wallpapers.js imports the same functions for the page. The
// browser-only half (decoding a file and fitting it to the panel with a canvas)
// lives in wallpapers.js.
//
// The target is what the device's sleep screen actually accepts and what the
// on-device Wallpapers app pins: a 480x800, 1-bit, uncompressed BMP with a
// two-entry palette (black, white). The renderer's 1-bit blit neither scales
// nor rotates, so the image must already BE the panel size -- which is why the
// fitting happens here, in the browser, and never on the ESP32. Format verified
// byte-for-byte against a Pillow-written BMP that renders correctly on hardware.

// The device's default (portrait) sleep canvas. The app verified a 480x800
// wallpaper fills it exactly.
export const WALL_W = 480;
export const WALL_H = 800;

// Which files the page will even try to decode. A cheap gate before handing
// anything to the canvas: a non-image is rejected here with a clear message
// rather than failing deep in an <img> onerror nobody reads. Accepts by MIME
// first (what the browser reports) and by extension as a fallback (drag-drop
// from some file managers reports an empty type).
const IMAGE_EXT = /\.(png|jpe?g|gif|webp|bmp|avif)$/i;
export function imageTypeOk(file) {
  if (!file) return false;
  const type = (file.type || "").toLowerCase();
  if (type.startsWith("image/")) return true;
  if (type && !type.startsWith("image/")) return false;
  return IMAGE_EXT.test(file.name || "");
}

// BT.601 luminance, the grey a 1-bit dither starts from. Input is RGBA bytes
// (canvas order); output is one grey byte per pixel.
export function rgbaToGray(rgba, count) {
  const gray = new Uint8Array(count);
  for (let i = 0; i < count; i++) {
    const r = rgba[i * 4];
    const g = rgba[i * 4 + 1];
    const b = rgba[i * 4 + 2];
    gray[i] = (r * 77 + g * 150 + b * 29) >> 8;
  }
  return gray;
}

// Floyd-Steinberg error diffusion to one bit. Returns a bit per pixel, 1 = white
// and 0 = black, matching the palette the encoder writes. `invert` swaps the two
// so light art on a dark original (or the reverse) can be made to read on the
// panel, which the caller offers as a toggle.
//
// The grey buffer is copied to a signed working buffer first: the diffused
// error routinely pushes a pixel past 0 or 255, and clamping it into the source
// bytes would lose exactly the error this algorithm exists to carry.
export function dither(gray, w, h, opts = {}) {
  const invert = !!opts.invert;
  const buf = new Int16Array(gray); // widened, so error can go out of [0,255]
  const bits = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x;
      const old = buf[i];
      const white = old >= 128;
      bits[i] = white !== invert ? 1 : 0;
      const err = old - (white ? 255 : 0);
      // Distribute the quantisation error to the not-yet-visited neighbours.
      if (x + 1 < w) buf[i + 1] += (err * 7) >> 4;
      if (y + 1 < h) {
        if (x > 0) buf[i + w - 1] += (err * 3) >> 4;
        buf[i + w] += (err * 5) >> 4;
        if (x + 1 < w) buf[i + w + 1] += (err * 1) >> 4;
      }
    }
  }
  return bits;
}

// Pack a bit-per-pixel buffer into an uncompressed 1-bpp BMP. Bottom-up rows,
// each padded to a 4-byte boundary, most-significant bit is the leftmost pixel,
// palette[0] black and palette[1] white -- so a set bit is white, which is the
// convention `dither` produces and the device reads.
export function encodeBmp1bit(bits, w, h) {
  const rowBytes = ((w + 31) >> 5) << 2; // ceil(w/32)*4
  const dataSize = rowBytes * h;
  const offBits = 14 + 40 + 8; // file header + BITMAPINFOHEADER + 2-colour palette
  const size = offBits + dataSize;
  const out = new Uint8Array(size);
  const dv = new DataView(out.buffer);

  // BITMAPFILEHEADER
  out[0] = 0x42; // 'B'
  out[1] = 0x4d; // 'M'
  dv.setUint32(2, size, true);
  dv.setUint32(10, offBits, true);
  // BITMAPINFOHEADER
  dv.setUint32(14, 40, true);
  dv.setInt32(18, w, true);
  dv.setInt32(22, h, true); // positive: bottom-up
  dv.setUint16(26, 1, true); // planes
  dv.setUint16(28, 1, true); // bpp
  dv.setUint32(30, 0, true); // BI_RGB, uncompressed
  dv.setUint32(34, dataSize, true);
  dv.setInt32(38, 2835, true); // ~72 DPI, cosmetic
  dv.setInt32(42, 2835, true);
  dv.setUint32(46, 2, true); // colours used
  dv.setUint32(50, 2, true); // colours important
  // Palette: index 0 = black, index 1 = white (BGRA)
  // (bytes 54..57 already zero -> black)
  out[58] = 0xff;
  out[59] = 0xff;
  out[60] = 0xff;
  out[61] = 0x00;

  // Pixels, bottom row first.
  for (let y = 0; y < h; y++) {
    const srcRow = (h - 1 - y) * w;
    let p = offBits + y * rowBytes;
    for (let x = 0; x < w; x += 8) {
      let byte = 0;
      for (let b = 0; b < 8; b++) {
        const px = x + b;
        if (px < w && bits[srcRow + px]) byte |= 0x80 >> b;
      }
      out[p++] = byte;
    }
  }
  return out;
}

// The whole pure pipeline: RGBA pixels of a 480x800 (by default) frame in, a
// device-ready BMP out. `pixels` is {data, width, height} -- a browser
// ImageData satisfies it, and so does a plain object in a test.
export function pixelsToBmp(pixels, opts = {}) {
  const { data, width, height } = pixels;
  const gray = rgbaToGray(data, width * height);
  const bits = dither(gray, width, height, opts);
  return encodeBmp1bit(bits, width, height);
}
