#!/usr/bin/env python3
"""Publish site/emulator/ as GitHub release assets and write the manifest.

WHY THIS EXISTS. site/emulator/ used to be committed. It is 3.7MB that changes
on every firmware merge, and crossplay-emulator.yml committed a fresh copy after
each one: 111 blob revisions of crossplay.wasm alone, ~357MB of history, 56 of
those revisions in one week. The repository reached ~497MB on GitHub for a
~104MB checkout and grew about 20MB a day, forever, which every clone and every
CI checkout in the fork pays for.

Ignoring the directory is not on its own an option, and .gitignore said so for
months: Vercel has no Emscripten, so a git-connected deploy that cannot see
these files ships a site whose headline feature 404s. The browser demo is the
first thing the README offers a stranger.

So the bytes go somewhere a clone does not follow -- a GitHub release asset --
and the repository keeps a pointer instead:

    site/emulator-manifest.json     ~1KB, one commit per rebuild

`site/fetch-emulator.mjs` reads that manifest during Vercel's build and puts the
files back before the site is served. A download that fails or arrives with the
wrong bytes fails the build, so a broken fetch leaves the PREVIOUS deployment
serving rather than shipping a 404.

    python3 tools_local/site/publish_emulator.py                # upload + write
    python3 tools_local/site/publish_emulator.py --dry-run      # write only

THREE THINGS ABOUT THE DESIGN, each of which is load-bearing:

  * Asset names are CONTENT-ADDRESSED (`<sha256[:16]>-crossplay.wasm`). A
    rolling name updated in place would break every redeploy of an older
    commit -- Vercel's Redeploy button and its rollbacks both rebuild an old
    tree, whose manifest names bytes that no longer exist under that name. With
    a content address, an old manifest keeps resolving as long as the asset is
    there, and a rebuild that changes nothing uploads nothing.

  * The manifest records `built_from`, the source revision, and it is what makes
    the commit non-empty. scripts_local/emulator-stale.sh compares the last
    commit touching the artefact against the last one touching its sources, and
    that comparison is unsatisfiable when a rebuild produces identical bytes --
    the same reason site/emulator/BUILT_FROM was invented. The revision always
    changes, so the manifest always changes, so the gate can always be met.

  * The files are verified to BE brotli before they are published. They are
    served with `Content-Encoding: br` (see site/vercel.json and
    tools_local/site/precompress.py), and publishing raw bytes behind that
    header ships a wasm no browser can decode -- with no error anywhere,
    because nothing on the wire is wrong until the decoder gives up.
"""

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SITE = REPO / "site"
EMU = SITE / "emulator"
MANIFEST = SITE / "emulator-manifest.json"

# The rolling release the assets hang off. Not `v*`, deliberately:
# crossplay-release.yml triggers on `push: tags: v*` and a version-shaped tag
# here would build and publish a whole firmware release for a wasm rebuild.
TAG = "emulator"
RELEASE_TITLE = "Browser emulator assets"
RELEASE_NOTES = (
    "Build outputs for the browser demo on crossplay.ma-r-s.com, published here "
    "rather than committed so the repository stops growing ~20MB a day.\n\n"
    "Every asset is named by the sha256 of its contents. `site/emulator-manifest.json` "
    "on `xteink` says which ones the live site is currently built from, and "
    "`site/fetch-emulator.mjs` is what downloads them during Vercel's build.\n\n"
    "Nothing here is firmware. Do not flash any of it."
)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def brotli_or_die():
    try:
        import brotli  # noqa: F401

        return brotli
    except ImportError:
        sys.exit(
            "the brotli module is missing: uv pip install --python"
            " .venv-study/bin/python brotli"
        )


def sources():
    """Every file in site/emulator/, whatever it is called.

    Deliberately not a list of three names. build.py decides what it writes,
    and a fourth output added there would otherwise be published by nobody and
    404 in the browser -- the exact shape of bug this repository keeps paying
    for. Whatever the build leaves in the directory is what ships.
    """
    if not EMU.is_dir():
        sys.exit(
            f"{EMU} does not exist -- build the emulator first:\n"
            "  pio run -e simulator_x4_pro -t compiledb\n"
            "  source ../.emsdk/emsdk_env.sh && python3 tools_local/wasm/build.py"
        )
    files = sorted(p for p in EMU.rglob("*") if p.is_file())
    if not files:
        sys.exit(f"{EMU} is empty -- nothing to publish")
    return files


def describe(path, brotli):
    """One manifest entry: what it is called, what it hashes to, how big it is.

    Two hashes, not one. `sha256` is of the bytes as stored and as uploaded --
    brotli -- and is what the Vercel-side fetch verifies, because that is what
    it downloads. `sha256_raw` is of the decoded bytes, and is what the
    live-site check falls back to: `curl --compressed` hands back a decoded
    body, and a check that only knew the encoded hash would report the live
    site broken on a detail of its own request headers.
    """
    data = path.read_bytes()
    try:
        raw = brotli.decompress(data)
    except Exception:
        sys.exit(
            f"{path.relative_to(SITE)} is not brotli, and site/vercel.json serves\n"
            "site/emulator/ with Content-Encoding: br. Publishing it would ship bytes\n"
            "no browser can decode, silently. Run:\n"
            "  python3 tools_local/site/precompress.py"
        )
    return {
        "name": str(path.relative_to(EMU)),
        "asset": f"{hashlib.sha256(data).hexdigest()[:16]}-{path.name}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "sha256_raw": hashlib.sha256(raw).hexdigest(),
        "bytes": len(data),
        "bytes_raw": len(raw),
    }


def built_from(brotli):
    """The revision the artefact was built from, taken from the artefact.

    site/emulator/BUILT_FROM is written by tools_local/wasm/build.py at link
    time and says what it compiled. `git rev-parse HEAD` here says only when
    somebody ran this script, which is the same answer in CI (build and publish
    are one job) and a wrong one anywhere else -- publishing an unchanged
    artefact from a feature branch would stamp it with that branch's commit and
    claim a rebuild that never happened.

    It is also what makes the manifest change on every rebuild, which is what
    lets scripts_local/emulator-stale.sh ever be satisfied. HEAD is the fallback
    and nothing more.
    """
    stamp = EMU / "BUILT_FROM"
    if stamp.is_file():
        data = stamp.read_bytes()
        try:
            data = brotli.decompress(data)
        except Exception:
            pass  # not compressed yet; the file is plain text either way
        text = data.decode("utf-8", "replace").strip()
        if text:
            return text
    return run(["git", "rev-parse", "HEAD"], cwd=REPO).stdout.strip() or "unknown"


def repo_slug(explicit):
    if explicit:
        return explicit
    p = run(
        ["gh", "repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"],
        cwd=REPO,
    )
    slug = p.stdout.strip()
    if not slug:
        sys.exit("could not work out the repository; pass --repo owner/name")
    return slug


def ensure_release(slug):
    """Make the rolling release if it is not there, and never not as a prerelease.

    `--prerelease` is not tidiness. GitHub defines /releases/latest as the most
    recent release that is neither a prerelease nor a draft, and three separate
    things ask exactly that question -- checked, not assumed, over every
    api.github.com call in the tree:

      * src/network/OtaUpdater.cpp -- every device in the field, looking for a
        firmware update;
      * site/assets/install.js -- the Install button's "Latest release";
      * the board's release watcher (server/board/supabase/migrations).

    Published as an ordinary release this one becomes "latest" for all three,
    and it contains no firmware at all. Nothing downstream would say so: a
    device would be offered an update whose asset does not exist, and the site
    would print a tag its own validator then refuses.
    """
    if run(["gh", "release", "view", TAG, "--repo", slug]).returncode == 0:
        return
    print(f"creating the {TAG} release on {slug}")
    p = run(
        [
            "gh",
            "release",
            "create",
            TAG,
            "--repo",
            slug,
            "--title",
            RELEASE_TITLE,
            "--notes",
            RELEASE_NOTES,
            "--prerelease",
        ]
    )
    if p.returncode != 0:
        sys.exit(f"could not create the {TAG} release:\n{p.stdout}{p.stderr}")


def existing_assets(slug):
    p = run(["gh", "release", "view", TAG, "--repo", slug, "--json", "assets"])
    if p.returncode != 0:
        return set()
    try:
        return {a["name"] for a in json.loads(p.stdout).get("assets", [])}
    except (json.JSONDecodeError, KeyError):
        return set()


def upload(slug, path, asset_name, already, workdir):
    """Upload one file under its content-addressed name.

    gh takes the name from the file, so the file is linked under the asset name
    first. Skipped when the release already carries that name: the name IS the
    content, so a byte-identical rebuild uploads nothing and an existing asset
    is never clobbered -- which is what keeps older manifests resolving.
    """
    if asset_name in already:
        print(f"  {asset_name}  already published")
        return
    staged = workdir / asset_name
    staged.write_bytes(path.read_bytes())
    p = run(["gh", "release", "upload", TAG, str(staged), "--repo", slug])
    if p.returncode != 0:
        sys.exit(f"upload of {asset_name} failed:\n{p.stdout}{p.stderr}")
    print(f"  {asset_name}  uploaded ({path.stat().st_size // 1024} KB)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", help="owner/name; asked of gh when omitted")
    ap.add_argument(
        "--server",
        default="https://github.com",
        help="GitHub server URL (github.server_url in Actions)",
    )
    ap.add_argument(
        "--dry-run", action="store_true", help="write the manifest, upload nothing"
    )
    args = ap.parse_args()

    brotli = brotli_or_die()
    # Listed ONCE and carried. Globbing a second time to pair with the entries
    # would let a directory that changed in between silently misalign the zip,
    # and the result -- an asset uploaded under another file's content hash --
    # is exactly the failure the hashes exist to prevent.
    paths = sources()
    files = [describe(p, brotli) for p in paths]
    slug = repo_slug(args.repo)

    if not args.dry_run:
        ensure_release(slug)
        already = existing_assets(slug)
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            work = pathlib.Path(tmp)
            for entry, path in zip(files, paths):
                upload(slug, path, entry["asset"], already, work)

    manifest = {
        "_comment": (
            "Written by tools_local/site/publish_emulator.py; read by "
            "site/fetch-emulator.mjs during Vercel's build. The bytes live on the "
            "GitHub release below rather than in git. Do not hand-edit."
        ),
        # Derived, never typed: the same fetch that would break if this were
        # wrong is the site's headline feature.
        "base": f"{args.server.rstrip('/')}/{slug}/releases/download/{TAG}/",
        "built_from": built_from(brotli),
        "files": files,
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n")
    total = sum(f["bytes"] for f in files)
    print(
        f"wrote {MANIFEST.relative_to(REPO)} -- {len(files)} files, {total // 1024} KB"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
