// site/fetch-emulator.mjs, run for real against a local server.
//
// This script is the only thing standing between "the emulator was published"
// and "the emulator is on the site". It runs on Vercel, once per deploy, in a
// place nobody watches, and the deploy is green whatever it prints. So the
// case worth testing is not the happy one -- it is every way it can come back
// WITHOUT the right bytes, because each of those, if it exited zero, would
// publish a homepage whose headline feature 404s with no check anywhere red.
//
// Six cases, five of which are failures:
//   * a correct download lands the bytes
//   * a file already on disk with the right hash is not downloaded again
//   * an asset that comes back corrupted is REFUSED and not written
//   * an asset that 404s is refused
//   * a manifest naming a path outside emulator/ is refused
//   * a manifest with no files at all is refused, rather than "0 files, done"
//
// The server is in this process and the script under test is a child, so
// everything here is async: spawnSync would block this process's event loop,
// the server would never answer the request the child had already made, and
// the whole suite would hang until a timeout with no output. (It did. Twice is
// the number of times that costs a session, so it is written down here.)
//
//   node host-tests/site/fetch_emulator.js <repo root>

const { createHash } = require("node:crypto");
const { spawn } = require("node:child_process");
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const ROOT = process.argv[2] || path.join(__dirname, "..", "..");
const SCRIPT = path.join(ROOT, "site", "fetch-emulator.mjs");

let failed = 0;
const ok = (m) => console.log(`  ok   ${m}`);
const bad = (m) => {
  failed++;
  console.log(`  FAIL ${m}`);
};

const sha256 = (b) => createHash("sha256").update(b).digest("hex");

// The assets the server hands out, and the manifest entries describing them.
// Derived from the bytes rather than typed, so a test cannot assert a hash the
// script was never going to compute.
const BODIES = {
  "aaa-crossplay.js": Buffer.from("// pretend module\n"),
  "bbb-crossplay.wasm": Buffer.from([0x00, 0x61, 0x73, 0x6d, 1, 0, 0, 0]),
};
const entry = (name, asset) => ({
  name,
  asset,
  sha256: sha256(BODIES[asset]),
  sha256_raw: sha256(BODIES[asset]),
  bytes: BODIES[asset].length,
  bytes_raw: BODIES[asset].length,
});

let served = 0;
let corrupt = new Set();
let missing = new Set();

const server = http.createServer((req, res) => {
  const name = decodeURIComponent(req.url.replace(/^\//, ""));
  served++;
  if (missing.has(name) || !(name in BODIES)) {
    res.writeHead(404).end("no such asset");
    return;
  }
  const body = corrupt.has(name)
    ? Buffer.concat([BODIES[name], Buffer.from("tampered")])
    : BODIES[name];
  res.writeHead(200, { "Content-Length": body.length }).end(body);
});

function run(manifest, { seed } = {}) {
  // The script resolves the manifest and the output directory against its own
  // location, so a copy in a scratch directory is a complete, isolated run.
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fetchemu-"));
  fs.copyFileSync(SCRIPT, path.join(dir, "fetch-emulator.mjs"));
  fs.writeFileSync(
    path.join(dir, "emulator-manifest.json"),
    JSON.stringify(manifest, null, 2),
  );
  if (seed) {
    fs.mkdirSync(path.join(dir, "emulator"), { recursive: true });
    for (const [name, body] of Object.entries(seed)) {
      fs.writeFileSync(path.join(dir, "emulator", name), body);
    }
  }
  served = 0;
  return new Promise((resolve) => {
    const child = spawn("node", [path.join(dir, "fetch-emulator.mjs")], {
      stdio: ["ignore", "pipe", "pipe"],
    });
    let out = "";
    child.stdout.on("data", (d) => (out += d));
    child.stderr.on("data", (d) => (out += d));
    child.on("close", (code) =>
      resolve({
        code,
        out,
        requests: served,
        read: (name) => {
          try {
            return fs.readFileSync(path.join(dir, "emulator", name));
          } catch {
            return null;
          }
        },
      }),
    );
  });
}

async function main() {
  const base = `http://127.0.0.1:${server.address().port}/`;
  const good = {
    base,
    built_from: "0000000000000000000000000000000000000000",
    files: [
      entry("crossplay.js", "aaa-crossplay.js"),
      entry("crossplay.wasm", "bbb-crossplay.wasm"),
    ],
  };

  // 1. the happy path really does land the bytes
  let r = await run(good);
  r.code === 0
    ? ok("a correct manifest fetches and exits 0")
    : bad(`a correct manifest exited ${r.code}: ${r.out}`);
  const landed = r.read("crossplay.wasm");
  landed && sha256(landed) === good.files[1].sha256
    ? ok("the wasm on disk is the wasm the manifest names")
    : bad("the wasm was not written, or was written wrong");

  // 2. a file already correct is not downloaded again. This is the path the
  //    committed fallback takes today, and it is also what lets a local
  //    serve.py work offline after one build, so it is asserted rather than
  //    assumed.
  r = await run(good, {
    seed: {
      "crossplay.js": BODIES["aaa-crossplay.js"],
      "crossplay.wasm": BODIES["bbb-crossplay.wasm"],
    },
  });
  r.code === 0 && r.requests === 0
    ? ok("files already correct on disk are not re-downloaded")
    : bad(
        `expected 0 requests and exit 0, got ${r.requests} requests and exit ${r.code}`,
      );

  // 3. THE ONE THAT MATTERS. Wrong bytes must not be written and must not exit
  //    0: a corrupted wasm deploys green and dies in the browser, where no
  //    check in this repository can see it.
  corrupt = new Set(["bbb-crossplay.wasm"]);
  r = await run(good);
  r.code !== 0
    ? ok("a corrupted download exits non-zero")
    : bad(
        "a corrupted download exited 0 -- the site would deploy a broken emulator",
      );
  r.read("crossplay.wasm") === null
    ? ok("a corrupted download writes nothing")
    : bad("a corrupted download left bytes on disk");
  /wrong contents/.test(r.out)
    ? ok("it says which file and what it expected")
    : bad(`the failure does not name the problem: ${r.out}`);
  corrupt = new Set();

  // 4. a missing asset -- the manifest landed but the upload did not
  missing = new Set(["bbb-crossplay.wasm"]);
  r = await run(good);
  r.code !== 0
    ? ok("an asset that 404s exits non-zero")
    : bad("a missing asset exited 0");
  missing = new Set();

  // 5. the manifest comes from git, not from a stranger, but it still names
  //    filesystem paths and may not name one outside emulator/.
  const escape = JSON.parse(JSON.stringify(good));
  escape.files[0].name = "../../index.html";
  r = await run(escape);
  r.code !== 0 && /outside emulator/.test(r.out)
    ? ok("a manifest naming a path outside emulator/ is refused")
    : bad(`a path escape was not refused (exit ${r.code}): ${r.out}`);

  // 6. an empty list is the quiet catastrophe: nothing to do, nothing fetched,
  //    exit 0, and a site with no emulator at all.
  r = await run({ base, built_from: "x", files: [] });
  r.code !== 0
    ? ok("a manifest with no files is refused rather than trivially satisfied")
    : bad(
        "an empty manifest exited 0, which would deploy a site with no emulator",
      );
}

server.listen(0, "127.0.0.1", () => {
  main()
    .catch((err) => bad(`the harness itself failed: ${err.stack}`))
    .finally(() => {
      server.close();
      console.log(`${failed} failed`);
      process.exit(0);
    });
});
