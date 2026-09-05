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

process.exit(0);
