#!/usr/bin/env python3
"""Does the LIVE site serve the emulator this commit says it should?

The emulator is no longer committed. It is published as a GitHub release asset
(tools_local/site/publish_emulator.py), pointed at by site/emulator-manifest.json,
and pulled back in by site/fetch-emulator.mjs during Vercel's build. Three moving
parts where there used to be one, and every one of them can fail in a way that
looks like nothing:

  * the manifest lands but the assets were never uploaded;
  * Vercel skips the build step entirely (a dashboard Output Directory override
    left on and empty does exactly that), so the fetch never runs and whatever
    is in git is served instead;
  * the fetch runs, downloads something, and the page still gets last week's.

Nothing about the site's own HTML changes in any of those cases. The homepage
renders, the hero sits there, and the demo 404s or runs old firmware. So this
asks the live origin directly, and it is the only check in the fork that can
tell the difference.

    python3 tools_local/site/verify_live_emulator.py                 # host from index.html
    python3 tools_local/site/verify_live_emulator.py https://host    # or say it
    python3 tools_local/site/verify_live_emulator.py --timeout 720   # wait for a deploy

Exit 0 only when every file in the manifest is served and hashes correctly.
Anything else is exit 1 with the reason named.

TWO HASHES, AND WHY. The files are stored and served already brotli-compressed
with `Content-Encoding: br` (see site/README.md -- edge compression on a
multi-megabyte binary streams at 35 KB/s and it is not optional). So the bytes
that come back depend on what the client asked for and on whether an
intermediary decoded them. Comparing against only one of the two hashes would
report a healthy site broken on a detail of this script's own request headers.
Both are in the manifest; either one matching is a pass, and neither matching
is a real failure that no amount of header negotiation explains.
"""

import argparse
import hashlib
import json
import pathlib
import re
import sys
import time
import urllib.error
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
SITE = REPO / "site"
MANIFEST = SITE / "emulator-manifest.json"
INDEX = SITE / "index.html"
UA = "crossplay-live-check"


def canonical_host():
    """The site's own og:url, rather than a hostname typed in here twice.

    site/set-host.py writes it and the page publishes it; a check with its own
    copy of the address is a check that keeps passing against the wrong site
    after a move.
    """
    try:
        text = INDEX.read_text(encoding="utf-8", errors="replace")
    except OSError as err:
        sys.exit(f"cannot read {INDEX}: {err}")
    m = re.search(r'property="og:url"\s+content="([^"]+)"', text)
    if not m:
        sys.exit(
            "site/index.html carries no og:url, so this check cannot work out "
            "which site to ask. Pass the origin as an argument."
        )
    return m.group(1).rstrip("/")


def get(url, method="GET", timeout=60):
    """status, headers, body, and the URL it ENDED at.

    The last one matters: urllib follows redirects silently, and a preview
    deployment redirects every path to a login page. Without the final URL the
    only evidence of that is a body that hashes wrong.
    """
    req = urllib.request.Request(url, method=method, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as res:
        body = res.read() if method == "GET" else b""
        return res.status, dict(res.headers), body, res.url


def looks_deployed(url, entry):
    """A cheap HEAD: is a body of the right SIZE there yet?

    Only ever used to decide whether it is worth downloading megabytes to hash
    them. It is not the verdict -- a right-sized wrong file passes this and
    fails the real check below, which is the correct order for a probe that
    must not become the answer.
    """
    try:
        status, headers, _, _ = get(url, method="HEAD", timeout=30)
    except (urllib.error.URLError, OSError):
        return False
    if status != 200:
        return False
    length = headers.get("Content-Length")
    if length is None:
        return True  # no length to judge by; let the hash decide
    return int(length) in (entry["bytes"], entry["bytes_raw"])


def verify(url, entry):
    """Download it and hash it. Returns None on success, else why not."""
    try:
        status, headers, body, final = get(url)
    except urllib.error.HTTPError as err:
        return f"HTTP {err.code}"
    except (urllib.error.URLError, OSError) as err:
        return f"unreachable ({err})"
    if status != 200:
        return f"HTTP {status}"
    if not body:
        return "empty body"

    # A protected preview answers every path with a login page, 200 and all.
    # Reported as a hash mismatch that reads exactly like a broken emulator --
    # which is what it did the first time this was pointed at one, over four
    # files, convincingly. Name the real cause instead: this is not a verdict
    # about the artefact and must not be mistaken for one.
    ctype = headers.get("Content-Type", "").lower()
    if ctype.startswith("text/html"):
        via = f" (ended at {final})" if final and final != url else ""
        return (
            f"an HTML page came back instead of the file{via}. This origin is "
            "behind Vercel's deployment protection, so nothing here can read it; "
            "point this at production, or check the deployment's build log instead."
        )

    got = hashlib.sha256(body).hexdigest()
    if got == entry["sha256"]:
        return None  # served exactly the bytes that were published
    if got == entry["sha256_raw"]:
        return None  # something decoded it on the way; still the right file

    # Encoded on the wire and we were handed it undecoded by a client that did
    # not ask: decode and compare, so the verdict is about the FILE and not
    # about content negotiation.
    if headers.get("Content-Encoding", "").lower() == "br":
        try:
            import brotli

            if (
                hashlib.sha256(brotli.decompress(body)).hexdigest()
                == entry["sha256_raw"]
            ):
                return None
        except ImportError:
            pass
        except Exception:
            return (
                f"served with Content-Encoding: br and is NOT brotli "
                f"({len(body)} bytes). The page will fail to decode it."
            )
    return (
        f"wrong contents ({len(body)} bytes, sha256 {got[:16]}...; "
        f"expected {entry['sha256'][:16]}... encoded or "
        f"{entry['sha256_raw'][:16]}... decoded)"
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "origin", nargs="?", help="https://host; taken from index.html when omitted"
    )
    ap.add_argument(
        "--manifest",
        type=pathlib.Path,
        default=MANIFEST,
        help="the manifest to hold the site to",
    )
    ap.add_argument(
        "--timeout",
        type=int,
        default=0,
        help="seconds to wait for a deploy to appear (default: do not wait)",
    )
    ap.add_argument("--interval", type=int, default=20, help="seconds between polls")
    args = ap.parse_args()

    try:
        manifest = json.loads(args.manifest.read_text())
    except (OSError, json.JSONDecodeError) as err:
        sys.exit(f"cannot read {args.manifest}: {err}")
    files = manifest.get("files") or []
    if not files:
        sys.exit(
            f"{args.manifest} names no files; there is nothing to verify and "
            "that is itself the bug."
        )

    origin = (args.origin or canonical_host()).rstrip("/")
    print(
        f"asking {origin} for the emulator built from {manifest.get('built_from', '?')}"
    )
    urls = {f["name"]: f"{origin}/emulator/{f['name']}" for f in files}

    # Wait for the deploy, judged on the biggest file only. Polling every file
    # would download ~4MB a round for no more information.
    if args.timeout > 0:
        biggest = max(files, key=lambda f: f["bytes"])
        deadline = time.time() + args.timeout
        while not looks_deployed(urls[biggest["name"]], biggest):
            if time.time() >= deadline:
                print(
                    f"\n{biggest['name']} never appeared at the published size "
                    f"within {args.timeout}s."
                )
                break
            print(f"  waiting for {biggest['name']} ...")
            time.sleep(args.interval)

    bad = []
    for f in files:
        why = verify(urls[f["name"]], f)
        if why is None:
            print(f"  ok   {f['name']}")
        else:
            print(f"  FAIL {f['name']}: {why}")
            bad.append(f["name"])

    if bad:
        print(
            f"\n{origin} is NOT serving the emulator this commit publishes "
            f"({', '.join(bad)}).\n"
            "The demo on the front page is the first thing a stranger is offered, "
            "so this is a live outage of the headline feature.\n"
            "Look at, in order:\n"
            "  1. the newest Vercel deployment's build log -- did "
            "`node fetch-emulator.mjs` run, and did it succeed?\n"
            "  2. the `emulator` release on GitHub -- are the assets "
            "site/emulator-manifest.json names actually there?\n"
            "  3. Vercel Project Settings -> Build -- an Output Directory "
            "override left on and empty SKIPS the build step entirely."
        )
        return 1
    print(
        f"\n{origin} serves the emulator from the manifest. {len(files)} file(s) checked."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
