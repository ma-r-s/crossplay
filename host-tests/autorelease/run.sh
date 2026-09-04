#!/bin/bash
# release_notes.py, against a repository built for the purpose.
#
# The script decides the next version and rewrites the notes block that
# host-tests/release then insists on, so a wrong answer here ships a release
# announcing the previous one, which is exactly what v1.6.2 did by hand. Every
# input the script reads is replaced: the git history, the pull requests (a
# JSON file instead of gh), the workflow file and platformio.ini.
#
# The second half of this suite is v1.12.17 rebuilt commit by commit. That
# release announced seven changes, four of which no device could see (a board
# watcher, a server-side bridge and two release-pipeline fixes), and the one
# line that DID reach the firmware named the operation -- "Sync CrossPoint
# develop (6 commits) and FreeInk SDK" -- rather than the fonts, translations
# and glyph-arena fix inside it. Mario read that on his device and asked why a
# release had happened at all. The fixture carries the real titles, the real
# sync body and the real path sets, because the bug was invisible to every
# check that existed and the only thing that could have caught it is the actual
# release it shipped in.
#
#   host-tests/autorelease/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="$HERE/../../scripts_local/release_notes.py"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
[ -f "$TOOL" ] || { echo "FAIL cannot find $TOOL"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
q()   { "$@" >/dev/null 2>&1; }

R="$WORK/repo"; mkdir -p "$R/.github/workflows"; cd "$R" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
cat > platformio.ini <<'INI'
[env]
version = 1.5.0

[crossplay]
version = 1.12.9
INI
mkdir -p docs
cat > docs/release-notes.md <<'MD'
Games and small tools for the device.

### What is new in 1.12.9

- The old note
- Another old note

### Installing

Flash it.
MD
echo base > a.txt; q git add -A; q git commit -qm "base"; q git tag v1.12.9
# two landings: one through a pull request with a What is new line, one plain merge
q git checkout -qb app/one; echo one > one.txt; q git add -A; q git commit -qm "feat(trivia): options that do not give the answer away"
q git checkout -q xteink; q git merge -q --no-ff app/one -m "Merge branch 'app/one' into xteink"
ONE_SHA="$(git rev-parse HEAD)"
q git checkout -qb app/two; echo two > two.txt; q git add -A; q git commit -qm "fix(shelf): pages step on the right axis"
q git checkout -q xteink; q git merge -q --no-ff app/two -m "Merge branch 'app/two' into xteink"
TWO_SHA="$(git rev-parse HEAD)"
echo "chore" > c.txt; q git add -A; q git commit -qm "chore: emulator rebuilt for the shelf train"

cat > "$WORK/prs.json" <<JSON
[{"number": 41, "title": "feat(trivia): options that do not give the answer away", "labels": [],
  "body": "## What\\n\\nThe picker.\\n\\nWhat is new: Trivia's wrong answers no longer give the right one away.\\n\\n## Proof\\n\\n42 checks.",
  "mergeCommit": {"oid": "$ONE_SHA"}}]
JSON

out="$(python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --dry-run 2>&1)"
echo "$out" | grep -q "NEXT_VERSION=1.12.10" && ok "one patch up from the last tag" || bad "version: $out"
echo "$out" | grep -q -- "- Trivia's wrong answers no longer give the right one away." && ok "the pull request's own line is the note" || bad "PR line missing: $out"
echo "$out" | grep -q -- "- Pages step on the right axis" && ok "a merge without a pull request falls back to its subject, humanised" || bad "fallback missing: $out"
echo "$out" | grep -q "emulator rebuilt" && bad "an emulator rebuild became a note" || ok "emulator rebuilds are not notes"
grep -q "version = 1.12.9" platformio.ini && ok "dry run writes nothing" || bad "dry run wrote"

q python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --write
grep -q "^version = 1.12.10" platformio.ini && ok "platformio.ini bumped" || bad "platformio.ini not bumped"
grep -q "^version = 1.5.0" platformio.ini && ok "upstream's version key untouched" || bad "upstream's version key changed"
Y=docs/release-notes.md
grep -q "### What is new in 1.12.10" "$Y" && ok "the notes heading names the new version" || bad "heading not rewritten"
git diff --quiet --exit-code -- .github/ && ok "no workflow file is touched by the bump" || bad "the bump touched a workflow file, which the default token may not push"
grep -q "The old note" "$Y" && bad "the old notes survived" || ok "the old notes are gone"
grep -q "### Installing" "$Y" && grep -q "Flash it." "$Y" && ok "the rest of the body is intact" || bad "the body lost text outside the block"
grep -q "Games and small tools" "$Y" && ok "the preamble is intact" || bad "preamble lost"
python3 - "$Y" <<'PY' && ok "the block starts at column 0" || bad "the block picked up an indent"
import sys
t=open(sys.argv[1]).read()
i=t.index("### What is new in 1.12.10"); blk=t[t.rfind("\n", 0, i) + 1:t.index("### Installing")]
lines=[l for l in blk.splitlines() if l.strip()]
sys.exit(0 if all(not l.startswith(" ") for l in lines) else 1)
PY

# a minor label on any merged pull request bumps the minor instead
q git checkout -- platformio.ini "$Y"
sed -i.bak 's/"labels": \[\]/"labels": [{"name": "release:minor"}]/' "$WORK/prs.json"
python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --dry-run 2>&1 | grep -q "NEXT_VERSION=1.13.0" && ok "release:minor bumps the minor" || bad "minor bump missing"

# nothing merged: no version, nothing written
q git checkout -q v1.12.9 2>/dev/null; q git checkout -q -b quiet
python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --dry-run 2>&1 | grep -q "NEXT_VERSION=$" && ok "nothing since the tag means no version" || bad "released with nothing merged"

echo
echo "v1.12.17: only what a device can see becomes a note"
# The seven landings of v1.12.16..v1.12.17, with their real path sets. The
# rule that classifies them is scripts_local/device-build-needed.sh, asked by
# release_notes.py rather than copied into it -- so this suite is also the
# place a change to that allowlist shows up in the notes.
R2="$WORK/v11217"; mkdir -p "$R2/docs"; cd "$R2" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
printf '[crossplay]\nversion = 1.12.16\n' > platformio.ini
printf 'CrossPlay.\n\n### What is new in 1.12.16\n\n- The previous release\n\n### Installing\n\nFlash it.\n' > docs/release-notes.md
q git add -A; q git commit -qm base; q git tag v1.12.16

land() { # land <branch> <merge subject> <path>...
  local br="$1" msg="$2"; shift 2
  q git checkout -qb "$br"
  local f
  for f in "$@"; do mkdir -p "$(dirname "$f")"; echo "$br" >> "$f"; done
  q git add -A; q git commit -qm "work on $br"
  q git checkout -q xteink
  q git merge -q --no-ff "$br" -m "$msg"
  git rev-parse HEAD
}

SHA42="$(land app/onepio  'Merge pull request #42 from ma-r-s/app/onepio' \
  .github/workflows/crossplay-release.yml host-tests/release/run.sh)"
SHA43="$(land sync/upstream-20260904 'Merge pull request #43 from ma-r-s/sync/upstream-20260904' \
  freeink-sdk lib/EpdFont/SdCardFont.cpp lib/EpdFont/scripts/sd-fonts.yaml \
  lib/I18n/translations/bulgarian.yaml USER_GUIDE.md docs/i18n.md)"
SHA47="$(land app/guardpipe 'Merge pull request #47 from ma-r-s/app/guardpipe' \
  host-tests/bugflow/run.sh scripts_local/hooks/guard.py)"
SHA46="$(land app/onedispatch 'Merge pull request #46 from ma-r-s/app/onedispatch' \
  .github/workflows/crossplay-autorelease.yml .github/workflows/crossplay-release.yml host-tests/release/run.sh)"
SHA45="$(land app/instalist 'Merge pull request #45 from ma-r-s/app/instalist' \
  server/read-bridge/bridge/app.py server/read-bridge/scripts/deploy.sh)"
SHA44="$(land app/relwatch 'Merge pull request #44 from ma-r-s/app/relwatch' \
  docs/workflow/events.md host-tests/relwatch/run.sh server/board/supabase/migrations/20260904001200_release_watch.sql)"
SHA50="$(land app/cijobs 'Merge pull request #50 from ma-r-s/app/cijobs' \
  .github/workflows/crossplay-ci.yml src/apps_local/link/LinkPlay.cpp)"
mkdir -p site; echo rebuilt > site/emulator.txt
q git add -A; q git commit -qm "chore: emulator rebuilt for the cijobs merge"

# The real pull requests. None of the seven carried a "What is new" line, which
# is why every bullet in v1.12.17 came from a title; #43's body is its real
# "What came in" list, verbatim.
python3 - "$WORK/v11217.json" "$SHA42" "$SHA43" "$SHA44" "$SHA45" "$SHA46" "$SHA47" "$SHA50" <<'PY'
import json, sys
out, (p42, p43, p44, p45, p46, p47, p50) = sys.argv[1], sys.argv[2:9]
sync_body = """Automated upstream sync, per docs/workflow/upstream-sync.md.

## What came in

**CrossPoint `develop` -> `xteink` (6 commits)**

- `3b203264` bump version
- `320a97a9` fix: duplicate key 'STR_KEYBOARD_LAYOUTS'
- `1f9523eb` feat: language-specific fonts (#3146)
- `365618e4` chore: update translations (#2946)
- `20d67cb3` refactor: convert compile-time settings lookup tables to constexpr (#3364)
- `93b6fe11` fix: keep the glyph arena usable under heap pressure (#3126)

**FreeInk SDK (1 commit)**

- `5927393` feat: add icons to tabs
"""
prs = [
    (42, "fix: build both devices in one pio run, and stop misdescribing why", p42, "One invocation, both envs."),
    (43, "chore: sync CrossPoint develop (6 commits) and FreeInk SDK", p43, sync_body),
    (44, "The release watcher: the thing that would have noticed", p44, "The board watches the release chain from outside."),
    (45, "readbridge: the running service can be asked which commit it is", p45, "/healthz answers with the build sha."),
    (46, "ci: one release build per tag, and assert both halves of why", p46, "The dispatch becomes conditional on the secret."),
    (47, "guard: quotes are stripped before the command is split", p47, "Quoted strings go before the split."),
    (50, "ci: clang-format, unit tests and cppcheck run in the fork's own workflow", p50, "The three jobs live in crossplay-ci.yml now."),
]
json.dump(
    [{"number": n, "title": t, "labels": [], "body": b, "mergeCommit": {"oid": s}} for n, t, s, b in prs],
    open(out, "w"),
)
PY

out="$(python3 "$TOOL" --repo-dir "$R2" --pr-json "$WORK/v11217.json" --write 2>&1)"
N=docs/release-notes.md
grep -q "### What is new in 1.12.17" "$N" && ok "the release is still 1.12.17" || bad "version: $out"

# The four that reach nothing a device runs. Asserted against the NOTES FILE,
# not the run's output: the titles are still printed to stdout on purpose.
for gone in \
  "The release watcher" \
  "Readbridge: the running service" \
  "one release build per tag" \
  "Build both devices in one pio run"
do
  grep -qi "$gone" "$N" && bad "a change no device can see is still a note: $gone" \
                        || ok "not a note: $gone"
done

# The sync is the one line of v1.12.17 that did reach the firmware, and it said
# nothing. Its body says what came in, so the body is what the notes carry.
grep -q "Sync CrossPoint develop" "$N" && bad "the sync still announces itself by the operation" \
                                       || ok "the sync's title is replaced by what came in"
for kept in "Language-specific fonts" "Update translations" "Keep the glyph arena usable under heap pressure" "Add icons to tabs"; do
  grep -q "$kept" "$N" && ok "from the sync body: $kept" || bad "the sync body lost: $kept"
done
grep -qi "bump version" "$N" && bad "the sync's own version bump became a note" || ok "a version bump is not a note"
grep -q "(#3126)" "$N" && bad "upstream's pull request number reads as one of ours" || ok "upstream's PR numbers are stripped"

# Two that DO reach a device image, and are notes for reasons worth stating.
# #50 rewrote src/apps_local/link/LinkPlay.cpp (a ternary cppcheck misreads);
# its title describes only the CI half, which is a title problem and not this
# rule's to solve. #47 touched scripts_local/, which device-build-needed.sh
# holds on the live side because two files there are `pre:` extra_scripts that
# run inside every device build. Both are the rule answering honestly; if
# either line disappears, the rule was narrowed here instead of there.
grep -q "clang-format, unit tests and cppcheck" "$N" && ok "a src/ change is a note whatever its title says" || bad "a src/ change was filtered out"
grep -q "quotes are stripped before the command is split" "$N" && ok "scripts_local/ stays on the live side, as the build rule has it" || bad "the notes narrowed the rule that device-build-needed.sh owns"

grep -q "Plus 4 changes nothing on the device can see" "$N" && ok "the four excluded ones are counted, not hidden" || bad "the excluded count is missing"
echo "$out" | grep -q "(not a note, reaches no device image) The release watcher" && ok "the excluded titles are printed for the release log" || bad "the excluded titles vanished from the run's output"

# Nothing device-reaching at all. release-needed.sh gates the automatic path on
# exactly this, so only a hand-cut release arrives here -- and an empty "What is
# new" block would be worse than a noisy one.
echo "[]" > "$WORK/none.json"
R3="$WORK/inert"; mkdir -p "$R3/docs"; cd "$R3" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
printf '[crossplay]\nversion = 2.0.0\n' > platformio.ini
printf 'X\n\n### What is new in 2.0.0\n\n- old\n\n### Installing\n\ny\n' > docs/release-notes.md
q git add -A; q git commit -qm base; q git tag v2.0.0
q git checkout -qb app/docsonly; mkdir -p docs; echo w > docs/note.md; q git add -A; q git commit -qm "docs: a note"
q git checkout -q xteink; q git merge -q --no-ff app/docsonly -m "Merge pull request #1 from ma-r-s/app/docsonly"
out="$(python3 "$TOOL" --repo-dir "$R3" --pr-json "$WORK/none.json" --write 2>&1)"
grep -q "A note" docs/release-notes.md && ok "a hand-cut release with nothing device-reaching still says something" || bad "the notes block came out empty: $out"
grep -q "Plus 0 changes" docs/release-notes.md && bad "the fallback counted its own lines as excluded" || ok "no excluded count when the filter stood down"
cd "$R" || exit 1

echo
echo "release-needed.sh"
mkdir -p "$R/scripts_local"
cp "$HERE/../../scripts_local/device-build-needed.sh" "$HERE/../../scripts_local/release-needed.sh" "$R/scripts_local/"
chmod +x "$R"/scripts_local/*.sh
cd "$R" || exit 1
q git add -A; q git commit -qm "chore: scripts"
q git tag v1.12.10
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok "nothing merged since the tag: no release" || bad "released with nothing since the tag"
mkdir -p docs; echo words > docs/note.md; q git add -A; q git commit -qm "docs: a note"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok "a docs-only merge since the tag is not a release" || bad "a docs-only change would release"
mkdir -p src; echo 'int x;' > src/x.cpp; q git add -A; q git commit -qm "fix: something a device can see"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 0 ] && ok "a src change since the tag releases" || bad "a src change did not release"
q git tag -d v1.12.10; q git tag -d v1.12.9
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 0 ] && ok "no tag at all means release" || bad "no tag refused to release"

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]
