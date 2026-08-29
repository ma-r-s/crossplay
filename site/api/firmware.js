// GET /api/firmware?device=x4pro|sticky&tag=v1.8.0
//
// Streams one release image back to the browser, same-origin. The Install
// button cannot fetch it from GitHub itself: release assets are served from
// release-assets.githubusercontent.com, which sends no Access-Control-Allow-
// Origin at all, so a cross-origin fetch() from the page is refused before a
// single byte arrives. (COEP require-corp, set for the whole site in
// vercel.json, would refuse it a second time.) This function is the only
// moving part on an otherwise static site, and it exists for exactly that
// reason: to put those bytes on our own origin.
//
// It is a pipe, not a media server. It resolves nothing, decides nothing, and
// caches nothing.
//
// WHY THE CLIENT PASSES THE TAG. The obvious shape is /api/firmware?device=x
// with the function asking the GitHub API which release is latest. That fails
// under exactly the traffic this button was built for: unauthenticated API
// calls are rate limited per IP, and every visitor's request would come from
// the same handful of Vercel egress addresses. Sixty installs an hour, site
// wide, and the button starts answering 403. The page already asks the API for
// the latest tag from the visitor's own browser -- it did so before this
// feature existed, to print "Latest release: ..." -- so the tag is free there
// and the limit is per visitor, which nobody will ever reach.
//
// That makes the tag client-supplied, which is the SSRF question: never build
// a URL out of it without proving what it is first. Hence the two regexes
// below. They admit release tags of this repository and nothing else -- no
// slashes, no scheme, no host, no traversal -- so the worst a hostile caller
// gets is a different version of our own firmware.

const { Readable } = require("node:stream");
const { pipeline } = require("node:stream/promises");

const REPO = "ma-r-s/crossplay";

// Must match the artefact names in .github/workflows/crossplay-release.yml
// ("Name the x4pro artefacts" / "Name the sticky artefacts"). Renaming them
// there without changing this line breaks the Install button and nothing else
// -- no build fails, no test goes red on its own -- so host-tests/release
// asserts the two spellings against each other.
const DEVICES = {
  x4pro: "crossplay-{tag}-x4pro-full.bin",
  sticky: "crossplay-{tag}-sticky-full.bin",
};

// v1.8.0 and nothing else. Anchored, bounded, digits only.
const TAG = /^v\d{1,3}\.\d{1,3}\.\d{1,3}$/;

module.exports = async function handler(req, res) {
  const url = new URL(req.url, "http://localhost");
  const device = url.searchParams.get("device") || "";
  const tag = url.searchParams.get("tag") || "";

  const template = Object.prototype.hasOwnProperty.call(DEVICES, device)
    ? DEVICES[device]
    : null;
  if (!template) {
    res.statusCode = 400;
    res.setHeader("Content-Type", "application/json");
    res.end(JSON.stringify({ error: "Unknown device. Use x4pro or sticky." }));
    return;
  }
  if (!TAG.test(tag)) {
    res.statusCode = 400;
    res.setHeader("Content-Type", "application/json");
    res.end(JSON.stringify({ error: "Malformed release tag." }));
    return;
  }

  const name = template.replace("{tag}", tag);
  const source = `https://github.com/${REPO}/releases/download/${tag}/${name}`;

  let upstream;
  try {
    upstream = await fetch(source, { redirect: "follow" });
  } catch (err) {
    res.statusCode = 502;
    res.setHeader("Content-Type", "application/json");
    res.end(
      JSON.stringify({ error: `Could not reach GitHub: ${err.message}` }),
    );
    return;
  }
  if (!upstream.ok) {
    // 404 here is the normal shape of "that release has no image for this
    // board" -- a tag from before the board existed, or a release whose
    // upload failed. Pass the status through rather than inventing one.
    res.statusCode = upstream.status === 404 ? 404 : 502;
    res.setHeader("Content-Type", "application/json");
    res.end(
      JSON.stringify({
        error: `GitHub returned ${upstream.status} for ${name}.`,
      }),
    );
    return;
  }

  // No Content-Length. Vercel caps a BUFFERED function response at 4.5MB and
  // these images are ~6.3MB; a streamed response is exempt, and declaring a
  // length is the difference between the two. The size the progress bar needs
  // rides in its own header instead, which is same-origin and so readable
  // without any CORS exposure dance.
  const size = upstream.headers.get("content-length");
  res.statusCode = 200;
  res.setHeader("Content-Type", "application/octet-stream");
  res.setHeader("Content-Disposition", `attachment; filename="${name}"`);
  if (size) res.setHeader("X-Firmware-Size", size);
  res.setHeader("X-Firmware-Name", name);
  // Cached nowhere, on purpose. The bytes behind a tag never change, so an
  // s-maxage would be correct in principle -- but what a CDN does when asked to
  // cache a 6MB STREAMED function response is the one thing here that cannot be
  // tried before it is live, and a install that breaks for everyone is a worse
  // trade than re-fetching a file people install once. Six megabytes is also
  // not something a browser should keep. Revisit with a small s-maxage if a
  // spike ever makes it worth measuring.
  res.setHeader("Cache-Control", "no-store");

  try {
    await pipeline(Readable.fromWeb(upstream.body), res);
  } catch (err) {
    // Headers are long gone by now, so there is no status left to send. Ending
    // the response short is the signal; the client checks the byte count it
    // received against X-Firmware-Size and refuses to flash a short image.
    res.destroy(err);
  }
};
