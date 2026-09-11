#!/usr/bin/env bash
# Publish a built Wikipedia pack: copy it beside the ones already there, point
# the stable name at it, and purge Cloudflare's copy of the old shards.
#
#   server/packs/scripts/publish_pack.sh <pack-dir> <lang> <snapshot>
#   server/packs/scripts/publish_pack.sh ~/packs/wikipedia en 2026-05
#
# The install page reads https://packs.ma-r-s.com/wikipedia/<lang>/, a symlink
# to <lang>-<snapshot>; the flip is atomic and the old pack stays on disk until
# it is removed by hand.
set -euo pipefail
DIR="${1:?pack directory (holds manifest.json)}"
LANG_="${2:?language, e.g. en}"
SNAP="${3:?snapshot, e.g. 2026-05}"
test -f "$DIR/manifest.json" || { echo "no manifest.json in $DIR" >&2; exit 1; }
DEST="/srv/packs/data/wikipedia/$LANG_-$SNAP"
ssh orange "mkdir -p '$DEST'"
rsync -a "$DIR/" "orange:$DEST/"
# The published copy of the manifest names the directory its parts live in,
# so a page that loaded it keeps fetching THIS build after the stable name
# moves to the next one (the old directory stays until removed by hand).
# The builder's own manifest is untouched; the reader ignores the field.
ssh orange python3 - "$DEST/manifest.json" "https://packs.ma-r-s.com/wikipedia/$LANG_-$SNAP/" <<'PY'
import json, sys
path, base = sys.argv[1], sys.argv[2]
with open(path, encoding="utf-8") as f:
    m = json.load(f)
m["base"] = base
with open(path, "w", encoding="utf-8") as f:
    json.dump(m, f, separators=(",", ":"))
print("manifest base:", base)
PY
ssh orange "cd /srv/packs/data/wikipedia && ln -sfn '$LANG_-$SNAP' '$LANG_.new' && mv -T '$LANG_.new' '$LANG_' && ls -l '$LANG_'"
echo "published https://packs.ma-r-s.com/wikipedia/$LANG_/ -> $LANG_-$SNAP"
echo "parts are fetched from $LANG_-$SNAP/ by checksum-keyed URL; nothing to purge. Old directories can go after a day."
