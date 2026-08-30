#!/usr/bin/env bash
# Rebuild site/assets/esptool.bundle.js from the esptool-js npm package.
#
# The Install button on the site talks to the device with esptool-js, the same
# library the `esptool` command line is a Python cousin of. It has to arrive as
# ONE same-origin ES module, for two reasons that both fail silently otherwise:
#
#   * vercel.json sets Cross-Origin-Embedder-Policy: require-corp, so a <script
#     src> pointing at unpkg or jsDelivr is refused by the browser rather than
#     warned about. Everything the page loads is same-origin, and this is no
#     exception.
#   * esptool-js loads its flasher stubs with dynamic import() of JSON files.
#     Left unbundled that is a dozen extra requests to paths nothing on this
#     site serves; the failure appears only at the moment someone flashes,
#     which is the worst possible time. --bundle inlines them (they are 92KB of
#     the 175KB output, and the ESP32-S3 one is the only one this fork needs).
#
# The output is committed, because the site has no build step and is not about
# to grow one for a file that changes when esptool-js releases.
#
#     bash tools_local/site/build_esptool.sh
#
# It needs bun (the workspace package manager) and nothing else; esbuild comes
# from bunx and neither it nor esptool-js is installed into the repo.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$REPO/site/assets/esptool.bundle.js"

# Pinned rather than floating: this file writes a bootloader to offset 0 of
# somebody else's device, and "whatever npm had that morning" is not a thing to
# find out about from a bricked X4 Pro. Bump it deliberately, then flash a real
# device before committing.
VERSION="0.5.7"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cd "$WORK"
printf '{"name":"esptool-bundle","private":true,"type":"module"}\n' > package.json
bun add "esptool-js@$VERSION" > /dev/null 2>&1
echo 'export { ESPLoader, Transport } from "esptool-js";' > entry.js
bunx esbuild entry.js --bundle --format=esm --minify --target=es2020 \
  --outfile=bundle.js > /dev/null 2>&1

# The banner is the only thing that survives minification to say what this is;
# a 175KB wall of minified JS with no provenance is how a vendored file becomes
# unmaintainable.
{
  echo "// esptool-js $VERSION, bundled by tools_local/site/build_esptool.sh."
  echo "// Apache-2.0, (c) Espressif Systems. https://github.com/espressif/esptool-js"
  echo "// Generated file -- edit the build script, never this."
  cat bundle.js
} > "$OUT"

echo "wrote $OUT ($(wc -c < "$OUT") bytes, esptool-js $VERSION)"
