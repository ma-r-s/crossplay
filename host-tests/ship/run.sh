#!/bin/bash
# What scripts_local/ship.sh must keep true, because it is now the only thing
# between a green gate and a public release.
#
# The GitHub pipeline used to be the second opinion: whatever a local script
# did, crossplay-release.yml rebuilt the tag and published from that. It does
# not any more, so every property that used to be guaranteed by "CI builds the
# tag" is now guaranteed by this file or by nothing.
#
# Each check below names the failure it prevents, and every one of them has
# already happened once on this repository:
#
#   1. v1.0.1 published the asset under a new name and every device's "Check
#      for updates" reported nothing, forever, because the OTA updater matches
#      the literal "firmware.bin" and nothing else.
#   2. v1.12.14 and v1.12.15 shipped with bootloader.bin, partitions.bin and
#      firmware.bin missing from the merged image.
#   3. Two agents in one evening read a red gate as a pass, once from `tail -1`
#      returning a background wrapper's "[exited with code 0]" and once from $?.
#   4. Card #572: the release brake compared RELEASE_HOLD against the literal
#      "1" while everybody was told to write "<card>:<session>:<why>", so the
#      documented format sailed through and two releases shipped under a hold.
#
# And one that has not happened yet only because the pipeline rebuilt after
# the bump: platformio.ini compiles the version into both release envs and
# OtaUpdater.cpp:119 compares a release's tag against that compiled string. A
# binary built BEFORE the bump, published under the tag AFTER it, reports the
# old version from the new firmware, so the update it just installed stays on
# offer. That is check 5, and it is an ordering check, which is the only kind
# that can catch it.
#
#   host-tests/ship/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SHIP="$ROOT/scripts_local/ship.sh"
PARSER="$ROOT/lib/JsonParser/ReleaseJsonParser.cpp"

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL ship  $1"; }
# Locally a SKIP is information: a scratch checkout may legitimately not be a
# git repository. In CI every input is supposed to be there, so a check that
# did not run is a FAILURE -- otherwise the suite guarding the publisher can
# quietly stop running and still exit 0, which is the shape of bug this whole
# file exists to catch. host-tests/checksh asserts every suite does this, and
# it caught this one not doing it.
skip() {
  if [ -n "${CI:-}" ]; then
    bad "$1 (a skip is a failure in CI: the inputs should be present here)"
  else
    echo "SKIP ship  $1"
  fi
}

for f in "$SHIP" "$PARSER"; do
  [ -f "$f" ] || { echo "FAIL ship  missing $f"; exit 1; }
done
[ -x "$SHIP" ] || bad "scripts_local/ship.sh is not executable"

# Comments describe; only code ships. Every textual check below reads the
# script with comments stripped, or a comment saying the right thing would
# pass for the code doing it -- which is the exact shape of card #572.
CODE="$(sed 's/#.*//' "$SHIP")"

# -- 1. the OTA updater's literal, on both sides ----------------------------
#
# Asserted against the parser rather than against a remembered string, so the
# day somebody renames it in the firmware this fails here rather than in the
# field six weeks later.
# The first draft of these two checks passed a mutation that renamed the
# published asset to crossplay-firmware.bin. Both were satisfied by the string
# "firmware.bin" occurring SOMEWHERE -- and it occurs in every
# .pio/build/<env>/firmware.bin path in the file, so they could never have
# failed. What matters is the DESTINATION of the copy and the name the
# existence check uses, so match those, anchored to the end of the path.
LITERAL="$(grep -o '"firmware\.bin"' "$PARSER" | head -1 | tr -d '"')"
if [ -z "$LITERAL" ]; then
  bad "ReleaseJsonParser.cpp no longer contains a firmware asset literal; this suite cannot tell what ship.sh must publish"
elif printf '%s' "$CODE" | grep -qE "cp +\\\$[{]?IMAGES[}]?/gh_release_x4pro/firmware\.bin +'?\"?\\\$[{]?DIST[}]?/${LITERAL//./\\.}'?\"?"; then
  ok
else
  bad "ship.sh does not copy the x4pro image to dist/$LITERAL. That is the only asset name the OTA updater matches (ReleaseJsonParser.cpp), and under any other name every device's Check for updates reports nothing, forever, and says nothing about why"
fi

# And the existence check, separately: the copy could be right and the guard
# missing, which is how an asset list that silently lost a file reads exactly
# like a working release.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -qE '\[ +-f +"?\$[{]?DIST[}]?/firmware\.bin"? +\]'; then
  ok_=1
else
  failed=$((failed + 1)); ok_=0
  echo "FAIL ship  ship.sh does not verify dist/firmware.bin is on disk before publishing"
fi

# -- 1b. the images come from the gate, never from this worktree ------------
#
# The bug this exists for, found by a cold review on 2026-09-21: ship.sh read
# $REPO/.pio/build, and check.sh --committed builds in a throwaway worktree
# under TMPDIR whose own trap deletes it. On a clean tree that path does not
# exist, so ship.sh simply could not work. On a tree where somebody had run
# `check.sh --flash gh_release_x4pro` it held a PRE-BUMP image, which would
# have passed the existence check, passed the magic numbers (they are real
# images), passed the tag-versus-version guard (that reads platformio.ini in
# the working tree, not the binary), and published firmware reporting the old
# version under the new tag.
#
# So: every read of a built artefact must come from the handover directory,
# and a bare .pio/build read is the defect itself.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -qE '(^|[^A-Za-z_/])\.pio/build/'; then
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh reads .pio/build directly. That directory belongs to whatever last built in THIS worktree, and the gate does not build here -- it builds in a throwaway worktree it then deletes. Package from the gate's handover (CHECKSH-IMAGES) instead."
else
  ok
fi

checks=$((checks + 1))
if printf '%s' "$CODE" | grep -q 'CHECKSH-IMAGES' && printf '%s' "$CODE" | grep -q 'CHECK_KEEP_RELEASE_IMAGES'; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not ask the gate to hand its images over (CHECK_KEEP_RELEASE_IMAGES) and read back where they went (CHECKSH-IMAGES), so whatever it packages did not come from the build it just verified"
fi

# And the handover must be checked against HEAD, or a directory left by an
# earlier commit's run is indistinguishable from this one's.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -q 'basename "$IMAGES"' && printf '%s' "$CODE" | grep -q 'rev-parse HEAD'; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not compare the handover directory against HEAD, so images built from a different commit would publish under this tag"
fi

# -- 1c. the binary must report the version the tag names ------------------
#
# The only check in ship.sh that reads the FIRMWARE rather than a fact about
# the file. Names, lengths and magic numbers are all satisfied by a stale
# image: it is a real image, of the wrong commit. This asks what
# OtaUpdater.cpp:119 asks on every device, and getting it wrong is not a
# build error -- it is an update prompt that never goes away, on every unit
# in the field, with nothing red anywhere.
#
# The anchor is the User-Agent BridgeHttp.cpp and StudySync.cpp build from
# CROSSPOINT_VERSION, so it is in every release image by construction rather
# than a debug line a LOG_LEVEL could compile out.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -q 'strings -a' && printf '%s' "$CODE" | grep -q 'CrossPlay-ESP32-\$NEXT'; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not read the version out of the images it is about to publish. Every other check here passes for a stale image built from a different commit, because a stale image is a real image; this is the one that cannot."
fi

# -- 2. each merged image gets all three parts, at the ROM's offsets --------
#
# Offset-then-file pairs rather than one literal line, so reformatting does
# not fail a correct script and reordering does not pass a broken one.
for board in x4pro sticky; do
  merge="$(printf '%s' "$CODE" | tr '\n' ' ' | grep -o "merge-bin[^;]*gh_release_$board/firmware\.bin" || true)"
  if [ -z "$merge" ]; then
    bad "ship.sh never calls esptool merge-bin for $board"
    continue
  fi
  for pair in "0x0:bootloader.bin" "0x8000:partitions.bin" "0x10000:firmware.bin"; do
    off="${pair%%:*}"; part="${pair#*:}"
    if printf '%s' "$merge" | grep -q "$off *[^ ]*gh_release_$board/$part"; then
      ok
    else
      bad "$board's merged image does not place $part at $off; an image missing a part is indistinguishable from a good one until a device is bricked with it"
    fi
  done
done

# -- 3. the magic numbers are checked, not assumed --------------------------
#
# esptool exits 0 on a merge that produced nothing useful. These three bytes
# are the only probe here that can fail on the real condition.
for probe in e903 aa50 e907; do
  if printf '%s' "$CODE" | grep -q "$probe"; then
    ok
  else
    bad "ship.sh does not check for the $probe magic number; it would publish an unmerged image as a full one"
  fi
done

# -- 3b. every guard must REFUSE, not merely notice -------------------------
#
# A cold review broke ship.sh eleven ways and this suite caught two. Most of
# the misses were the same edit: turn a `die` into a `say`. The guard is
# still there, its message is still there, every text check still passes, and
# the release goes out anyway. That is the guard-shaped hole this file exists
# to close, so the checks below read the ARM rather than the mention: the
# lines between each detector and the end of its branch must reach a die.
#
# Still textual, and it does not prove reachability -- only a real publish
# does that, and a real publish is not something a suite may perform. It does
# make "noticed but did not stop" impossible to write by accident, which is
# how every one of those mutations was spelled.
# DIE_TEXT is every die() payload in the script and nothing else: from each
# `die "` to the line that closes its quote. Asking whether a guard's own
# MESSAGE is inside one is exact -- turning that die into a say removes the
# message from this set and the check fails, which is the whole point.
#
# The first version instead looked for any `die` within twelve lines of the
# detector, and two of six mutations walked straight through it by finding a
# NEIGHBOURING guard's die. A proximity test is not a test of the arm.
# DIE_TEXT is every die() payload and nothing else. Built in python rather
# than awk: the first attempt was an awk state machine that never reset, so
# once it saw one die it captured the rest of the file and two mutations
# passed against a message that was no longer in any die at all. A detector
# that over-captures is indistinguishable from one that works.
DIE_TEXT="$(python3 - "$SHIP" <<'PYEOF'
import re, sys
src = re.sub(r'(?m)^\s*#.*$', '', open(sys.argv[1]).read())
# each die "..." payload, quotes balanced, newlines allowed inside
print("\n".join(m.group(1) for m in re.finditer(r'(?:^|[\s;&|])die\s+"((?:[^"\\]|\\.)*)"', src, re.S)))
PYEOF
)"

guard_refuses() {  # <label> <a distinctive fragment of the guard message>
  checks=$((checks + 1))
  if printf '%s' "$DIE_TEXT" | grep -qE "$2"; then
    ok
  else
    failed=$((failed + 1))
    echo "FAIL ship  the $1 guard does not refuse: its message is not inside any die(), so it notices and carries on. A guard that prints and shrugs passes every other check in this file and publishes anyway, which is how nine of eleven mutations got through the first version of this suite."
  fi
}
guard_refuses "magic-number"         'image that is not merged'
guard_refuses "RELEASE_HOLD"         'RELEASE_HOLD is set'
guard_refuses "up-to-date"           'has moved since this branch left it'
guard_refuses "handover-versus-HEAD" 'images are from'
guard_refuses "binary version"       'reports version'
guard_refuses "dirty tree"           'working tree is dirty'
guard_refuses "missing OTA asset"    'firmware\.bin is missing'
guard_refuses "incomplete handover"  'no images to package'

# -- 4. the gate's verdict is grepped, and its exit code is not trusted -----
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -q "grep -o 'CHECKSH-VERDICT"; then
  ok_=1
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not grep for CHECKSH-VERDICT. Both 'VERDICT WITHHELD' and 'SOMETHING FAILED' have exited 0 in this workspace, and under a background wrapper tail -1 returns the wrapper's own status line"
fi

# An empty verdict is not a pass either: a run that never reached its verdict
# has to be refused explicitly, or the `case` falls through to whatever the
# default arm does.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -A8 'CHECKSH-VERDICT' | grep -q '""[)]'; then
  ok_=1
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not refuse an EMPTY verdict. A gate killed before it printed one is not a pass, and it is the case that looks most like success"
fi

# host-green-device-skipped is a PASS for check.sh and a REFUSAL here: it
# means no device image was built, so there is nothing to publish.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -q 'host-green-device-skipped'; then
  ok_=1
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not handle the host-green-device-skipped verdict, which passes check.sh while leaving .pio/build without the images this publishes"
fi

# -- 5. the bump happens BEFORE the build ------------------------------------
#
# The ordering check, and the reason this suite exists at all. See the header.
#
# The gate is matched by its INVOCATION, not by the string "check.sh
# --committed" appearing anywhere. The first draft of this check matched the
# latter and failed a correct script, because ship.sh's up-to-date refusal
# prints "rebase and re-gate: ./scripts_local/check.sh --committed" as advice
# forty lines above the real call. A detector satisfied by a mention of the
# thing is the bug it is supposed to catch, one level up.
# THE VERSION, NOT THE NOTES. An earlier version of this check matched
# `release_notes.py --write` and was right only while that one call did both
# jobs. It does not any more: the VERSION is compiled into the firmware so it
# must precede the build, and the NOTES are not, so they are written after
# the squash -- which is the only point at which the pull request is merged
# and release_notes.py can map a commit to it at all. Matching the notes here
# would forbid the correct order.
BUMP_LINE="$(printf '%s' "$CODE" | grep -nE "s\\^version \*= \*|git commit -q -m 'chore: crossplay \\\$NEXT'" | head -1 | cut -d: -f1)"
GATE_LINE="$(printf '%s' "$CODE" | grep -nE 'CHECK_FORCE_DEVICE_BUILDS=1 +\./scripts_local/check\.sh --committed' | head -1 | cut -d: -f1)"
checks=$((checks + 1))
if [ -z "$BUMP_LINE" ] || [ -z "$GATE_LINE" ]; then
  failed=$((failed + 1))
  echo "FAIL ship  cannot find both the version write (platformio.ini's [crossplay] version) and the gate (check.sh --committed) in ship.sh; the ordering they must keep cannot be checked"
elif [ "$BUMP_LINE" -lt "$GATE_LINE" ]; then
  ok_=1
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh gates at line $GATE_LINE and writes the version at line $BUMP_LINE, so it publishes images compiled with the PREVIOUS version. platformio.ini compiles the version in (-DCROSSPOINT_VERSION) and OtaUpdater.cpp:119 compares the tag against it, so every device would keep offering an update it already installed"
fi

# -- 6. current with trunk, or the squash lands a tree nobody built ---------
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -q 'merge-base' && printf '%s' "$CODE" | grep -q 'rev-parse origin/xteink'; then
  ok_=1
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not compare the merge base against origin/xteink. Without it a merge commit can be published, and a merge commit's tree is not the tree the gate built"
fi

# -- 5b. the tag is pushed after everything that can still refuse ----------
#
# Pushing a tag is the first irreversible thing this script does in public,
# so it must come after the last check that can say no: the eight-file
# handover, the three magic numbers, and the probe that reads the version out
# of the firmware. Pushed earlier -- as it was until this check existed -- a
# packaging failure leaves a tag in the history with no release against it
# and a delete-and-retag to clear it.
#
# Line order is the whole assertion here, so it is read the same way as the
# bump-before-build check above.
TAGPUSH_LINE="$(printf '%s' "$CODE" | grep -nE 'run "git push [^"]*\$TAG' | head -1 | cut -d: -f1)"
PROBE_LINE="$(printf '%s' "$CODE" | grep -n 'CrossPlay-ESP32-\$NEXT' | head -1 | cut -d: -f1)"
checks=$((checks + 1))
if [ -z "$TAGPUSH_LINE" ] || [ -z "$PROBE_LINE" ]; then
  failed=$((failed + 1))
  echo "FAIL ship  cannot find both the tag push and the firmware version probe, so the order they must keep cannot be checked"
elif [ "$PROBE_LINE" -lt "$TAGPUSH_LINE" ]; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh pushes the tag at line $TAGPUSH_LINE and only checks the firmware's version at line $PROBE_LINE. A tag pushed before the last check that can refuse leaves a version in the history with no release against it whenever packaging fails."
fi

# -- 6b. the squash goes through GitHub, and the trees are compared --------
#
# Two halves of one invariant, and neither works without the other.
#
# THROUGH GITHUB, because only a merge GitHub performs leaves the pull
# request MERGED with its mergeCommit set, and release_notes.py maps commits
# to pull requests by that oid and nothing else. A local squash-and-push
# leaves the pull request open with its head unreachable, the mapping empty,
# every note line falling back to a raw commit subject, and the release:minor
# label never seen -- so every release silently becomes a patch bump.
#
# AND THE TREES COMPARED, because "a squash of an up-to-date branch has the
# same tree" is true and is exactly the kind of true-by-argument the last
# defect hid behind. The images in the handover were built from the branch
# tip; if what landed differs by a byte they are the wrong bytes.
checks=$((checks + 1))
# MATCHED AS AN INVOCATION, not as the string appearing anywhere. The first
# version of this grepped for "gh pr merge .*--squash" and passed every
# mutation, because ship.sh's own --dry-run branch PRINTS that text: `say "
# would: gh pr merge $PR_NUMBER --squash"`. Three checks in this file have
# now been written that way and all three could never fail. A detector
# satisfied by a mention of the thing is the bug it is meant to catch.
if printf '%s' "$CODE" | grep -qE 'run "gh pr merge [^"]*--squash'; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not squash through 'gh pr merge --squash'. A local squash leaves the pull request open with its head unreachable, so release_notes.py maps no commit to it and the release page becomes a list of raw commit subjects with every release a patch bump."
fi

checks=$((checks + 1))
# The COMPARISON, not the variables: both names appear in the die message
# that reports a mismatch, so grepping for them passed with the comparison
# deleted.
if printf '%s' "$CODE" | grep -qE '\[ "\$BRANCH_TREE" != "\$TRUNK_TREE" \]'; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh does not compare the landed tree against the tree the gate built. The handover's images came from the branch tip; if the squash resolved a merge, they are bytes nobody built."
fi

# -- 7. RELEASE_HOLD: any non-empty value holds -----------------------------
#
# Card #572 exactly. A comparison against the literal 1 lets the documented
# format through.
checks=$((checks + 1))
if printf '%s' "$CODE" | grep -qE '\[ *"?\$\{?HOLD' && printf '%s' "$CODE" | grep -q '= *"1"'; then
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh compares RELEASE_HOLD against the literal 1. CLAUDE.md tells every session to write '<card>:<session>:<why>', so the documented format would not be recognised and the brake would do nothing (card #572)"
elif printf '%s' "$CODE" | grep -q 'HOLD'; then
  ok_=1
else
  failed=$((failed + 1))
  echo "FAIL ship  ship.sh never reads RELEASE_HOLD; the one global brake on releasing would not exist on the path that releases"
fi

# -- 8. it actually refuses, rather than describing a refusal ---------------
#
# The checks above read text. This one runs the script, because a guard that
# is present and unreachable reads exactly like a guard that works. Both cases
# are driven in a scratch clone so nothing here can touch a real branch, and
# --dry-run is deliberately NOT used: the refusals must fire before it.
SCRATCH="$(mktemp -d -t ship-suite)"
trap 'rm -rf "$SCRATCH"' EXIT
if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
  q() { "$@" >/dev/null 2>&1; }
  q git init -q "$SCRATCH/repo"
  mkdir -p "$SCRATCH/repo/scripts_local"
  cp "$SHIP" "$SCRATCH/repo/scripts_local/ship.sh"
  q git -C "$SCRATCH/repo" config user.email s@e; q git -C "$SCRATCH/repo" config user.name s
  q git -C "$SCRATCH/repo" checkout -q -b app/scratch
  q git -C "$SCRATCH/repo" add -A
  q git -C "$SCRATCH/repo" commit -q -m init

  # dirty tree
  echo dirt > "$SCRATCH/repo/dirt.txt"
  out="$(cd "$SCRATCH/repo" && ./scripts_local/ship.sh --dry-run 2>&1)"; rc=$?
  checks=$((checks + 1))
  if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -qi "dirty"; then
    ok_=1
  else
    failed=$((failed + 1))
    echo "FAIL ship  ship.sh did not refuse a dirty working tree (exit $rc). An uncommitted file is not in the commit the gate verified, so the images would not be the ones this tree describes"
  fi
  rm -f "$SCRATCH/repo/dirt.txt"

  # on trunk
  q git -C "$SCRATCH/repo" checkout -q -b xteink
  out="$(cd "$SCRATCH/repo" && ./scripts_local/ship.sh --dry-run 2>&1)"; rc=$?
  checks=$((checks + 1))
  if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -qi "xteink"; then
    ok_=1
  else
    failed=$((failed + 1))
    echo "FAIL ship  ship.sh did not refuse being run on xteink itself (exit $rc)"
  fi
else
  skip "not a git checkout; the live refusal checks need one"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
