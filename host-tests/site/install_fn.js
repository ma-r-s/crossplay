// install.js decides which failed installs are the person's own (a closed
// port picker, a device that would not answer on the cable) and posts those
// as info, not error, so they are counted and not carded. The page is a
// browser IIFE, so the decision function is lifted out of the source text
// and run here on its own.
//
//   node host-tests/site/install_fn.js <repo root>
const fs = require("fs");
const path = require("path");
const root = process.argv[2];
const src = fs.readFileSync(path.join(root, "site/assets/install.js"), "utf8");

const m = src.match(/function userSide\(err\) \{[\s\S]*?\n  \}\n/);
if (!m) {
  console.log("  FAIL install.js has no userSide(err); every failed install would be a bug card again");
  process.exit(1);
}
const userSide = new Function(m[0] + "\nreturn userSide;")();

let failed = 0;
function check(label, got, want) {
  if (got === want) console.log("  ok   " + label);
  else { failed++; console.log("  FAIL " + label + " (got " + got + ", wanted " + want + ")"); }
}
function dom(name, message) { const e = new Error(message); e.name = name; return e; }

check("a closed port picker is the person's own", userSide(dom("NotFoundError", "Failed to execute 'requestPort' on 'Serial': No port selected by the user.")), true);
check("a declined permission prompt is the person's own", userSide(dom("NotAllowedError", "Permission denied")), true);
check("no sync on the cable is the person's own", userSide(new Error("Failed to connect with the device")), true);
check("a port another tab holds is the person's own", userSide(new Error("Failed to open serial port.")), true);
check("a thrown string is read too", userSide("No port selected by the user"), true);
check("a flasher exception is still an error", userSide(new TypeError("Cannot read properties of undefined (reading 'length')")), false);
check("a download failure is still an error", userSide(new Error("HTTP 502 fetching firmware.bin")), false);
check("a checksum mismatch is still an error", userSide(new Error("Image verification failed")), false);
check("nothing at all is still an error", userSide(undefined), false);

// And the catch actually uses it: the level is the decision, not a constant.
check("the install's catch posts by that decision",
  /tellBoard\(userSide\(err\) \? "info" : "error"/.test(src), true);

// The write skips the NVS partition. The release image carries NVS as 0xFF
// padding between the partition table and the app, and writing it erased the
// secret behind the device's id (and its settings and Wi-Fi) on every
// install: 98 installs and 51 device ids in one week. The function reads the
// table the image itself carries; a synthetic image with a real-shaped table
// is enough to pin it, and a table it cannot read means the whole image, as
// before. otadata must still be written: blank otadata is what boots app0.
const pm = src.match(/function partsToWrite\(bytes\) \{[\s\S]*?\n  \}\n/);
if (!pm) {
  console.log("  FAIL install.js has no partsToWrite(bytes); every install erases the device's id again");
  process.exit(1);
}
const partsToWrite = new Function(pm[0] + "\nreturn partsToWrite;")();
function entry(type, sub, off, size, label) {
  const e = new Uint8Array(32);
  e[0] = 0xaa; e[1] = 0x50; e[2] = type; e[3] = sub;
  new DataView(e.buffer).setUint32(4, off, true);
  new DataView(e.buffer).setUint32(8, size, true);
  for (let i = 0; i < label.length && i < 16; i++) e[12 + i] = label.charCodeAt(i);
  return e;
}
function image(entries, length) {
  const b = new Uint8Array(length).fill(0x11);
  b[0] = 0xe9;
  let off = 0x8000;
  for (const e of entries) { b.set(e, off); off += 32; }
  b.fill(0xff, off, 0x10000);
  b[0x10000] = 0xe9;
  return b;
}
const TABLE = [entry(1, 2, 0x9000, 0x5000, "nvs"), entry(1, 0, 0xe000, 0x2000, "otadata"), entry(0, 0x10, 0x10000, 0x7f0000, "app0")];
let parts = partsToWrite(image(TABLE, 0x10000 + 0x400));
check("a real table: the image is written in two parts", parts.length, 2);
check("the first part is the bootloader and the table, up to NVS", parts[0].address === 0 && parts[0].data.length === 0x9000, true);
check("the second part starts at otadata, so blank otadata still boots app0", parts[1].address, 0xe000);
check("and runs to the end of the image", parts[1].data.length, 0x10000 + 0x400 - 0xe000);
check("exactly the NVS partition is what is not written", parts[0].data.length + parts[1].data.length, 0x10000 + 0x400 - 0x5000);
parts = partsToWrite(image([entry(1, 0, 0xe000, 0x2000, "otadata"), entry(0, 0x10, 0x10000, 0x7f0000, "app0")], 0x10400));
check("a table with no nvs entry: the whole image, as before", parts.length === 1 && parts[0].address === 0 && parts[0].data.length === 0x10400, true);
const noMagic = image(TABLE, 0x10400); noMagic[0x8000] = 0x00;
check("a table that cannot be read: the whole image", partsToWrite(noMagic).length, 1);
check("an nvs entry past the end of the image: the whole image", partsToWrite(image(TABLE, 0x9800)).length, 1);
check("the flash writes those parts, not the raw image", /fileArray: parts\.map\(/.test(src) && /var parts = partsToWrite\(bytes\)/.test(src), true);

process.exit(0);
