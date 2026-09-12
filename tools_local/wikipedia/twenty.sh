#!/bin/bash
# Twenty random articles, photographed in the simulator, one after another:
# the gate that showed the padded possessives, the dead links and the
# respellings nothing on the fixture could.
#
#   tools_local/wikipedia/twenty.sh <random-tap-y> <out-dir>
#   tools_local/wikipedia/twenty.sh 318 qa-artifacts/twenty
#
# Opens the app from Home (Apps, then WIKIPEDIA), then twenty times: taps
# RANDOM ARTICLE, waits for the first page, photographs it, presses Back.
# 318 is the RANDOM bar's row under a one- or two-line CONTINUE title (296
# hits the card once a two-line title is the last article read, and every
# shot is that article again). The pack on fs_agent/wikipedia is whatever is
# there; copy the current build in first and reset state.json from
# state_seed.json so the CONTINUE card starts short. The simulator's first
# RANDOM is always the same article (stub RNG); the rest move.
set -euo pipefail
Y="${1:?y of the RANDOM ARTICLE bar; 318 under a one- or two-line CONTINUE title}"
OUT="${2:?output dir}"
cd "$(dirname "$0")/../.."
input="1500:TAP:120,725;3000:TAP:240,546"
shots=""
t=5000
for i in $(seq 1 20); do
  input="$input;$t:TAP:240,$Y"
  shots="$shots;$((t + 3500)):$OUT/random-$(printf %02d "$i").bmp"
  input="$input;$((t + 4000)):BACK"
  t=$((t + 5000))
done
input="$input;$t:QUIT"
shots="${shots#;}"
./scripts_local/sim-shot.sh "$input" "$shots" "$OUT"
# One sheet, five by four at half size, so one look covers all twenty.
python3 - "$OUT" <<'PY'
import glob, sys
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
out = sys.argv[1]
files = sorted(glob.glob(out + "/random-*.png"))[:20]
if not files:
    sys.exit(0)
w, h = 240, 400
sheet = Image.new("L", (w * 5, h * 4), 255)
for i, f in enumerate(files):
    sheet.paste(Image.open(f).convert("L").resize((w, h)), ((i % 5) * w, (i // 5) * h))
sheet.save(out + "/twenty-sheet.png")
print("sheet:", out + "/twenty-sheet.png")
PY
