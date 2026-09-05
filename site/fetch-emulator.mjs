// Put site/emulator/ back before the site is served.
//
// This is site/vercel.json's buildCommand. It runs on Vercel, in this
// directory, with nothing installed -- so it uses only what the build image
// guarantees: node, and node's own fetch and crypto. (curl is not on Vercel's
// documented package list for the build image, and python3 is not guaranteed
// either. This is the one thing that is.)
//
// WHY. The emulator is 3.7MB of generated wasm that changes on every firmware
// merge, and it used to be committed -- 111 revisions of crossplay.wasm, ~357MB
// of history, ~20MB a day and rising, paid for by every clone and every CI
// checkout. It cannot simply be ignored either: Vercel has no Emscripten, so a
// deploy that cannot see these files ships a site whose headline feature 404s,
// which is the reason .gitignore spared them for months. So the bytes live on a
// GitHub release, the repository keeps ./emulator-manifest.json, and this puts
// the two back together during the build.
// tools_local/site/publish_emulator.py is the other half.
//
// THE FAILURE MODE THIS IS WRITTEN AROUND. If a fetch quietly did nothing, the
// deploy would succeed and the demo would 404, and nothing anywhere would
// notice. So every path out of here that is not "the right bytes are on disk"
// exits non-zero, which fails the Vercel build, which leaves the PREVIOUS
// deployment serving. A broken emulator publish must cost a stale site, never
// a broken one.
//
//   node fetch-emulator.mjs           # from site/
//
// Verified end to end by host-tests/site/fetch_emulator.js, which includes the
// case that matters: bytes that arrive corrupted must be REFUSED, not written.

import { createHash } from "node:crypto";
import { readFile, writeFile, mkdir, rename, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const MANIFEST = path.join(HERE, "emulator-manifest.json");
const OUT = path.join(HERE, "emulator");
const ATTEMPTS = 3;

const sha256 = (buf) => createHash("sha256").update(buf).digest("hex");

async function hashOnDisk(file) {
  try {
    return sha256(await readFile(file));
  } catch {
    return null; // absent, unreadable: both mean "fetch it"
  }
}

async function download(url) {
  let last;
  for (let attempt = 1; attempt <= ATTEMPTS; attempt++) {
    try {
      const res = await fetch(url, { redirect: "follow" });
      if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
      return Buffer.from(await res.arrayBuffer());
    } catch (err) {
      last = err;
      // A release asset 404s permanently; a network blip does not. Retrying a
      // 404 costs ten seconds and tells the log the same thing three times,
      // which is cheaper than distinguishing them wrongly.
      if (attempt < ATTEMPTS) {
        console.warn(`    attempt ${attempt} failed (${err.message}); retrying`);
        await new Promise((r) => setTimeout(r, 2000 * attempt));
      }
    }
  }
  throw last;
}

async function main() {
  let manifest;
  try {
    manifest = JSON.parse(await readFile(MANIFEST, "utf8"));
  } catch (err) {
    throw new Error(
      `cannot read ${path.basename(MANIFEST)}: ${err.message}\n` +
        "It is written by tools_local/site/publish_emulator.py and committed. " +
        "Without it there is no way to know which emulator this commit wants.",
    );
  }
  const { base, files, built_from: builtFrom } = manifest;
  if (!base) {
    throw new Error("the manifest has no `base`, so there is nowhere to fetch from.");
  }
  if (!Array.isArray(files) || files.length === 0) {
    throw new Error(
      "the manifest names no files. Nothing to fetch is not success: it would " +
        "leave the site with no emulator and this build green. Refusing.",
    );
  }

  console.log(`emulator: ${files.length} file(s), built from ${builtFrom ?? "unknown"}`);
  await mkdir(OUT, { recursive: true });

  let fetched = 0;
  let kept = 0;
  for (const file of files) {
    const { name, asset, sha256: want, bytes } = file;
    if (!name || !asset || !want) {
      throw new Error(`manifest entry is incomplete: ${JSON.stringify(file)}`);
    }
    const target = path.join(OUT, name);
    // Never let a name out of the manifest escape the directory it names.
    if (path.relative(OUT, target).startsWith("..") || path.isAbsolute(name)) {
      throw new Error(`manifest names a file outside emulator/: ${name}`);
    }

    // A copy already on disk with the right hash needs nothing. That is the
    // normal case locally (someone just built it) and, until the committed
    // copies are removed, the fast path here too.
    if ((await hashOnDisk(target)) === want) {
      console.log(`  ${name}  already correct`);
      kept++;
      continue;
    }

    const url = new URL(asset, base).toString();
    console.log(`  ${name}  <- ${url}`);
    const body = await download(url);
    const got = sha256(body);
    if (got !== want) {
      // The whole point. Wrong bytes are worse than no bytes: they deploy
      // green and break in a browser, which no check here would ever see.
      throw new Error(
        `${name} downloaded with the wrong contents.\n` +
          `      expected sha256 ${want}\n` +
          `      got               ${got}\n` +
          `      (${body.length} bytes; the manifest says ${bytes})`,
      );
    }
    // Written aside and moved, so a build killed mid-download cannot leave a
    // truncated wasm that hashes to nothing and is served anyway.
    const tmp = `${target}.partial`;
    await mkdir(path.dirname(target), { recursive: true });
    await writeFile(tmp, body);
    await rename(tmp, target);
    await rm(tmp, { force: true });
    fetched++;
  }

  console.log(`emulator ready: ${fetched} downloaded, ${kept} already present`);
}

main().catch((err) => {
  console.error("\nEMULATOR FETCH FAILED -- refusing to deploy a site without it.");
  console.error(`  ${err.message}`);
  console.error(
    "\nThe previous deployment keeps serving. Fix the publish " +
      "(tools_local/site/publish_emulator.py) or the manifest, then redeploy.",
  );
  process.exit(1);
});
