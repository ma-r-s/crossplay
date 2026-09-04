#!/bin/bash
# release_notes.py and release-needed.sh, against repositories built for the purpose.
#
# The script decides the next version and rewrites the notes block that
# host-tests/release then insists on, so a wrong answer here ships a release
# announcing the previous one, which is exactly what v1.6.2 did by hand. Every
# input the script reads is replaced: the git history, the pull requests (a
# JSON file instead of gh), the workflow file and platformio.ini.
#
# TWO FILES since 2026-09-04, and the split is the point of half of this suite:
#
#   docs/release-body.md   what a tag publishes. This release only.
#   docs/release-notes.md  the history. Every release, newest first, never
#                          published.
#
# They were one file, and so every release page carried every earlier release:
# v1.12.21's body was 20,402 characters and opened with a standing catalogue,
# then "What is new in 1.12.21", then what WAS new in 1.12.12, 1.12.11,
# 1.12.10, 1.12.9, 1.12.8 and 1.12.7.
#
# The second block is v1.12.17 rebuilt commit by commit. That release announced
# seven changes, four of which no device could see, and the one line that DID
# reach the firmware named the operation -- "Sync CrossPoint develop (6
# commits) and FreeInk SDK" -- rather than the fonts, translations and
# glyph-arena fix inside it. Mario got the update prompt on his device, read
# that version's notes, and asked why a release had happened at all. The
# fixture carries the real titles, the real sync body and the real path sets,
# because the bug was invisible to every check that existed and the only thing
# that could have caught it is the actual release it shipped in.
#
# The last block is v1.12.21 itself, from the real repository rather than a
# fixture: a release whose only device-reaching change was the version number
# the release wrote. It asserts the range does NOT warrant a release.
#
# The notes are read on the GitHub release page and nowhere else: the firmware
# parses tag_name, the asset name, its url and its size (ReleaseJsonParser.cpp)
# and draws two version numbers (OtaUpdateActivity.cpp). Checked, because the
# first version of this comment said the panel showed them.
#
#   host-tests/autorelease/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
TOOL="$REPO_ROOT/scripts_local/release_notes.py"
RULE="$REPO_ROOT/scripts_local/device-build-needed.sh"
NEEDED="$REPO_ROOT/scripts_local/release-needed.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
[ -f "$TOOL" ] || { echo "FAIL cannot find $TOOL"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
q()   { "$@" >/dev/null 2>&1; }

# Both files, in the shape release_notes.py writes them. Used by every fixture
# so the layout is stated once.
lay_out_docs() {  # <repo dir> <version> <one old bullet>
  mkdir -p "$1/docs"
  cat > "$1/docs/release-body.md" <<MD
Games and small tools for the device.

### What is new in $2

- $3
- Another old note

### Installing

Flash it.
MD
  cat > "$1/docs/release-notes.md" <<MD
# CrossPlay release history

<!-- releases, newest first -->

### $2

- $3
MD
}

R="$WORK/repo"; mkdir -p "$R/.github/workflows"; cd "$R" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
cat > platformio.ini <<'INI'
[env]
version = 1.5.0

[crossplay]
version = 1.12.9
INI
lay_out_docs "$R" 1.12.9 "The old note"
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
Y=docs/release-body.md
H=docs/release-notes.md
grep -q "### What is new in 1.12.10" "$Y" && ok "the body's heading names the new version" || bad "heading not rewritten"
git diff --quiet --exit-code -- .github/ && ok "no workflow file is touched by the bump" || bad "the bump touched a workflow file, which the default token may not push"
grep -q "The old note" "$Y" && bad "the old notes survived in the body" || ok "the body carries this release only"
grep -q "### Installing" "$Y" && grep -q "Flash it." "$Y" && ok "the rest of the body is intact" || bad "the body lost text outside the block"
grep -q "Games and small tools" "$Y" && ok "the preamble is intact" || bad "preamble lost"
# THE HISTORY, which is the half that stops a release page growing. The new
# release goes on top and the previous one is still there -- a prepend that
# quietly replaced would look identical on the release page and lose the archive.
grep -q "^### 1.12.10$" "$H" && ok "the history gained the new release" || bad "the history did not gain 1.12.10"
grep -q "^### 1.12.9$" "$H" && ok "the history kept the previous release" || bad "the history dropped 1.12.9"
python3 - "$H" <<'PY' && ok "the newest release is first in the history" || bad "the new block was appended, not prepended"
import re, sys
heads = re.findall(r"^### (\S+)$", open(sys.argv[1]).read(), re.M)
sys.exit(0 if heads[:2] == ["1.12.10", "1.12.9"] else 1)
PY
grep -q "The old note" "$H" && ok "the previous release's text lives on in the history" || bad "the history lost the previous release's bullets"
# Writing the same version twice must not stack two blocks. A failed push
# followed by a hand re-run is the obvious way it happens and a doubled entry
# is silent.
q python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --last-tag v1.12.9 --write
[ "$(grep -c '^### 1.12.10$' "$H")" = "1" ] && ok "re-writing a version does not stack a second history block" || bad "the history doubled the release"

# a minor label on any merged pull request bumps the minor instead
q git checkout -- platformio.ini "$Y" "$H"
sed -i.bak 's/"labels": \[\]/"labels": [{"name": "release:minor"}]/' "$WORK/prs.json"
python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --dry-run 2>&1 | grep -q "NEXT_VERSION=1.13.0" && ok "release:minor bumps the minor" || bad "minor bump missing"

# nothing merged: no version, nothing written
q git checkout -q v1.12.9 2>/dev/null; q git checkout -q -b quiet
python3 "$TOOL" --repo-dir "$R" --pr-json "$WORK/prs.json" --dry-run 2>&1 | grep -q "NEXT_VERSION=$" && ok "nothing since the tag means no version" || bad "released with nothing merged"

echo
echo "v1.12.17: only what a person can receive becomes a note"
# The seven landings of v1.12.16..v1.12.17, with their real path sets. The
# table that classifies them is scripts_local/device-build-needed.sh, asked by
# release_notes.py rather than copied into it -- so this suite is also the
# place a change to that table shows up in the notes.
R2="$WORK/v11217"; mkdir -p "$R2/docs"; cd "$R2" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
printf '[crossplay]\nversion = 1.12.16\n' > platformio.ini
lay_out_docs "$R2" 1.12.16 "The previous release"
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
N=docs/release-body.md
grep -q "### What is new in 1.12.17" "$N" && ok "the release is still 1.12.17" || bad "version: $out"

# CARD #190, CLOSED HERE. "Build both devices in one pio run" (#42) is the fix
# for the defect that shipped v1.12.14 and v1.12.15 with bootloader.bin,
# firmware.bin and partitions.bin missing from the published -full.bin, and it
# lives in .github/workflows/crossplay-release.yml. While one predicate
# answered both questions, .github/ was inert and so the repair of the most
# user-visible release defect in this fork's history was filed as a change no
# device could see: it cut no release and appeared in none.
#
# The table's `ships` column has one row for that one file -- the only workflow
# in the tree that builds and uploads what a person downloads. Every other
# workflow verifies, schedules or rebuilds the website, and stays invisible to
# the release. #46 rides on the same row, and #50 does not: it changed
# crossplay-ci.yml, which publishes nothing, and earns its line from
# src/apps_local/link/LinkPlay.cpp instead.
for kept in \
  "Build both devices in one pio run" \
  "One release build per tag"
do
  if grep -qi "$kept" "$N"; then
    ok "a change to what gets PUBLISHED is a note: $kept"
  else
    bad "the release workflow's own fix is still invisible to its notes: $kept ($out)"
  fi
done

# The three that reach nobody. Each is asserted TWICE, and the pair is the
# point: absent from the body, AND present in the run's own output as an
# excluded title. A one-sided "not in the notes" check passes for any reason a
# title is missing -- a pull request the lookup failed to match produces no
# bullet either, and would have read as the filter working.
for gone in \
  "The release watcher" \
  "Readbridge: the running service" \
  "Quotes are stripped before the command is split"
do
  if grep -qi "$gone" "$N"; then
    bad "a change nobody can receive is still a note: $gone"
  elif ! echo "$out" | grep -qi "(not a note, reaches no user).*$gone"; then
    bad "missing from the notes but never excluded either, so nothing found it: $gone"
  else
    ok "excluded, and said so: $gone"
  fi
done

# scripts_local/ is the row that moved, and it moved on ONE axis. It stays live
# for the build -- two of its files are `pre:` extra_scripts that run inside
# every device build, asserted by host-tests/gatepath -- and it does not ship,
# because a build lock and an SCons signature shard can refuse a build or slow
# it and cannot put different source into one. #47 is the guard hook, and a
# player receives nothing different for it.
grep -qi "quotes are stripped" "$N" && bad "the workspace's own machinery became a release note" \
                                    || ok "scripts_local/ builds but does not ship"

# The sync is one of the lines of v1.12.17 that did reach the firmware, and it
# said nothing. Its body says what came in, so the body is what the notes carry.
grep -q "Sync CrossPoint develop" "$N" && bad "the sync still announces itself by the operation" \
                                       || ok "the sync's title is replaced by what came in"
for kept in "Language-specific fonts" "Update translations" "Keep the glyph arena usable under heap pressure" "Add icons to tabs"; do
  grep -q "$kept" "$N" && ok "from the sync body: $kept" || bad "the sync body lost: $kept"
done
grep -qi "bump version" "$N" && bad "the sync's own version bump became a note" || ok "a version bump is not a note"
grep -q "(#3126)" "$N" && bad "upstream's pull request number reads as one of ours" || ok "upstream's PR numbers are stripped"

# A src/ change is a note whatever its title says: #50's title describes only
# the CI half, which is a title problem and not this rule's to solve.
grep -q "clang-format, unit tests and cppcheck" "$N" && ok "a src/ change is a note whatever its title says" || bad "a src/ change was filtered out"

# THE COUNT LINE IS NOT A RELEASE NOTE. "Plus 3 changes nothing on the device
# can see." is itself a line a player cannot act on, on a page written for
# players. The excluded landings are named on stdout, where the autorelease job
# log keeps them, and the count goes with them.
grep -qi "^- Plus " "$N" && bad "the excluded count is still a bullet on the release page" || ok "the excluded count is not a bullet"
echo "$out" | grep -q "3 landings excluded from the notes, named above." \
  && ok "the count is on stdout, where the job log keeps it" \
  || bad "the excluded landings were not counted anywhere: $out"

echo
echo "the range, the gates and the fail-safe"
# A cold critic mutated release_notes.py 21 ways against the block above and 9
# mutants lived. The fixture's own shape was why: land() always branches from
# the current tip and merges at once, so `sha^1..sha`, `sha^1..sha^2` and
# `sha^2^..sha^2` are the SAME diff and nothing could tell which parent the
# code had picked. These are the cases where they differ.
R4="$WORK/ranges"; mkdir -p "$R4/docs"; cd "$R4" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
printf '[crossplay]\nversion = 3.0.0\n' > platformio.ini
lay_out_docs "$R4" 3.0.0 "old"
q git add -A; q git commit -qm base; q git tag v3.0.0

# AN EVIL MERGE: the branch touches docs only, the MERGE COMMIT adds a source
# file. `sha^1..sha` is the only range that sees it; the branch's own diff says
# inert, and shipping that answer would drop a real firmware change from the
# notes while every check stayed green.
q git checkout -qb app/evil; mkdir -p docs; echo w > docs/evil.md; q git add -A; q git commit -qm "docs: a note"
q git checkout -q xteink
q git merge --no-ff --no-commit app/evil >/dev/null 2>&1
mkdir -p src; echo 'int evil;' > src/evil.cpp; q git add -A
q git commit -qm "Merge pull request #90 from ma-r-s/app/evil"
EVIL="$(git rev-parse HEAD)"

# A merge that brings in nothing. `-s ours` makes the merge's tree its first
# parent's by construction, which is the shape a branch already landed by
# another route leaves behind: the first-parent diff is empty, and only that
# range says so. Every other range here reports the branch's own src/ file.
q git checkout -qb app/dup
mkdir -p src; echo 'int dup;' > src/dup.cpp; q git add -A; q git commit -qm "fix: also landed elsewhere"
q git checkout -q xteink
q git merge -q --no-ff -s ours app/dup -m "Merge pull request #91 from ma-r-s/app/dup"
DUP="$(git rev-parse HEAD)"

# A third landing that reaches a user and says the SAME thing as #90. Two
# pull requests can fix one bug from both ends and write one line for it; the
# notes must carry it once. Nothing else here produces a repeated bullet, and a
# mutant that removed the de-duplication passed the whole suite.
q git checkout -qb app/twin
mkdir -p src; echo 'int twin;' > src/twin.cpp; q git add -A; q git commit -qm "fix: the other end of it"
q git checkout -q xteink
q git merge -q --no-ff app/twin -m "Merge pull request #92 from ma-r-s/app/twin"
TWIN="$(git rev-parse HEAD)"

python3 - "$WORK/ranges.json" "$EVIL" "$DUP" "$TWIN" <<'JSN'
import json, sys
out, evil, dup, twin = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
same = "What is new: One bug, fixed from both ends."
json.dump([
    {"number": 90, "title": "docs: a note that carries a source file", "labels": [], "body": same,
     "mergeCommit": {"oid": evil}},
    {"number": 91, "title": "fix: also landed elsewhere", "labels": [{"name": "release:minor"}], "body": "",
     "mergeCommit": {"oid": dup}},
    {"number": 92, "title": "fix: the other end of it", "labels": [], "body": same,
     "mergeCommit": {"oid": twin}},
], open(out, "w"))
JSN
out="$(python3 "$TOOL" --repo-dir "$R4" --pr-json "$WORK/ranges.json" --dry-run 2>&1)"
echo "$out" | grep -q -- "- One bug, fixed from both ends." \
  && ok "an evil merge is judged by the merge, not by the branch it merged" \
  || bad "the wrong parent was asked: $out"
[ "$(echo "$out" | grep -c -- "- One bug, fixed from both ends.")" = "1" ] \
  && ok "two landings that say one thing are one line" \
  || bad "a repeated line was printed twice: $out"
echo "$out" | grep -q "(not a note, reaches no user) Also landed elsewhere" \
  && ok "a merge that brings in nothing is not a note" \
  || bad "an empty merge became a note: $out"
# The count is DERIVED, and this is the only place that can say so: the block
# above always excludes three, so a hardcoded 3 passes it. One here, and
# singular.
echo "$out" | grep -q "1 landing excluded from the notes, named above." \
  && ok "the excluded count is the number excluded, and reads as one" \
  || bad "the count is not derived from what was excluded: $out"
# #91 is EXCLUDED from the notes and still carries release:minor. The label is
# about the version, not about the notes, and nothing else asserts it: a mutant
# that only honoured labels on kept pull requests passed the whole suite.
echo "$out" | grep -q "NEXT_VERSION=3.1.0" \
  && ok "release:minor on an excluded pull request still bumps the minor" \
  || bad "the version followed the notes filter: $out"

# THE FAIL-SAFE. device-build-needed.sh absent means the question cannot be
# asked, and the answer must be "print the bullet". Nothing else in this suite
# exercises it, and a mutant flipping it to False passed everything.
FS="$WORK/nofilter"; mkdir -p "$FS"
cp "$TOOL" "$FS/release_notes.py"
out="$(python3 "$FS/release_notes.py" --repo-dir "$R2" --pr-json "$WORK/v11217.json" --dry-run 2>&1)"
echo "$out" | grep -q -- "- The release watcher" \
  && ok "no rule to ask means every merge is a note" \
  || bad "a missing device-build-needed.sh silently filtered: $out"
echo "$out" | grep -q "excluded from the notes" && bad "the fail-safe still counted exclusions" || ok "the fail-safe excludes nothing"
# AND it must arrive BY the fail-safe, not by the stand-down. Reversing the
# fail-safe drops every merge, which empties the bullets, which trips the
# stand-down, which lists every merge again: the same output by the opposite
# route, and it passed both checks above. The stand-down announces itself on
# stdout; the fail-safe says nothing. That line is the difference.
echo "$out" | grep -q "nothing since the tag reaches a user" \
  && bad "every merge was dropped and the stand-down covered for it" \
  || ok "the fail-safe included them, rather than the stand-down re-adding them"

# A commit whose parent cannot be resolved. No fixture can put one on a
# first-parent line after a tag, so this asks reaches_a_user() directly. The
# repository's root commit touches docs/ and platformio.ini only, so an answer
# derived from its diff would be "inert"; only the fail-safe says otherwise.
python3 - "$TOOL" "$R2" "$(git -C "$R2" rev-list --max-parents=0 HEAD)" <<'ROOT'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("rn", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
sys.exit(0 if m.reaches_a_user(sys.argv[2], sys.argv[3]) else 1)
ROOT
[ $? -eq 0 ] && ok "a commit with no parent to diff against is a note" \
             || bad "an unanswerable question was read as 'no'"

# AND THE REFUSAL. A landing that touches a path in no row of the table cannot
# be classified, and the table REFUSES rather than guessing (exit 2). For the
# notes the refusal must still print the bullet -- hiding a change is the bug
# this filter exists to fix -- and must say on stdout that it could not
# classify it, or a reader cannot tell an included bullet from an unclassified
# one.
RU="$WORK/unclassified"; mkdir -p "$RU/docs"; cd "$RU" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
printf '[crossplay]\nversion = 4.0.0\n' > platformio.ini
lay_out_docs "$RU" 4.0.0 "old"
q git add -A; q git commit -qm base; q git tag v4.0.0
q git checkout -qb app/newroot; mkdir -p brandnew; echo x > brandnew/x.c; q git add -A
q git commit -qm "feat: a directory the table has never heard of"
q git checkout -q xteink; q git merge -q --no-ff app/newroot -m "Merge pull request #1 from ma-r-s/app/newroot"
echo "[]" > "$WORK/none.json"
out="$(python3 "$TOOL" --repo-dir "$RU" --pr-json "$WORK/none.json" --dry-run 2>&1)"
echo "$out" | grep -q -- "- A directory the table has never heard of" \
  && ok "an unclassified path still gets its bullet" \
  || bad "a refusal was read as 'no': $out"
echo "$out" | grep -qi "unclassified path" \
  && ok "and the run says it could not classify it" \
  || bad "the refusal was swallowed and reads as an ordinary note: $out"

# THE THREE GATES on reading a sync's body, each of which replaces a pull
# request's own title with lines lifted out of its body.
cd "$R2" || exit 1
python3 - "$WORK/gates.json" "$(git -C "$R2" rev-parse HEAD~1)" <<'PY'
import json, sys
out, sha = sys.argv[1], sys.argv[2]
json.dump([{"number": 1, "title": "TITLE", "labels": [], "body": "BODY", "mergeCommit": {"oid": sha}}], open(out, "w"))
PY
gate() { # gate <title> <body> ; prints the notes
  python3 - "$WORK/gates.json" "$1" "$2" <<'PY'
import json, sys
f = sys.argv[1]; d = json.load(open(f)); d[0]["title"] = sys.argv[2]; d[0]["body"] = sys.argv[3]
json.dump(d, open(f, "w"))
PY
  python3 "$TOOL" --repo-dir "$R2" --pr-json "$WORK/gates.json" --last-tag "$(git -C "$R2" describe --tags --abbrev=0 --match 'v*' 2>/dev/null || echo v1.12.16)" --dry-run 2>&1
}
SYNCBODY='- `aaaaaaa` feat: language-specific fonts (#3146)
- `bbbbbbb` fix: keep the glyph arena usable'
o="$(gate 'fix: the deck reopens after sync; review no longer panics' "$SYNCBODY")"
echo "$o" | grep -q "Language-specific fonts" \
  && bad "a firmware pull request lost its title to sha-shaped lines in its body" \
  || ok "only the sync run's own title is replaced by its body"
o="$(gate 'chore: sync CrossPoint develop (2 commits)' "$SYNCBODY")"
echo "$o" | grep -q "Language-specific fonts" && ok "the sync run's own title is replaced" || bad "the sync gate is too tight: $o"
o="$(gate 'chore: sync CrossPoint develop (1 commit)' '- `aaaaaaa` feat: only one')"
echo "$o" | grep -q "Only one" && bad "one sha-shaped line was enough to replace a title" || ok "one bullet is not a commit list"
o="$(gate 'chore: sync CrossPoint develop (2 commits)' "$SYNCBODY"'
```
- `ccccccc` feat: DO NOT SHIP THIS LINE
```')"
echo "$o" | grep -q "DO NOT SHIP" && bad "a fenced code block became a release note" || ok "fenced blocks are not the commit list"
o="$(gate 'chore: sync CrossPoint develop (2 commits)' 'What is new: the fork says it in its own words
'"$SYNCBODY")"
echo "$o" | grep -q "the fork says it in its own words" && ok "an explicit What is new beats the body scan" || bad "the body scan overrode a written line: $o"
o="$(gate 'chore: sync CrossPoint develop (2 commits)' '- `aaaaaaa` feat: bump version
- `bbbbbbb` feat: 1.5x zoom on the page view
- `ccccccc` feat: reader menu '$'—'' an overlay over the page')"
echo "$o" | grep -qi "bump version" && bad "the sync version bump became a note" || ok "a version bump is not a note"
echo "$o" | grep -q "1.5x zoom on the page view" && ok "a subject that starts with a version number survives" || bad "1.5x was read as a version bump: $o"
echo "$o" | grep -q $'—' && bad "an em-dash reached the release page" || ok "upstream's em-dash becomes a comma"

# Nothing reaching a user at all. release-needed.sh gates the automatic path on
# exactly this, so only a hand-cut release arrives here -- and an empty "What is
# new" block would be worse than a noisy one.
R3="$WORK/inert"; mkdir -p "$R3/docs"; cd "$R3" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t
printf '[crossplay]\nversion = 2.0.0\n' > platformio.ini
lay_out_docs "$R3" 2.0.0 "old"
q git add -A; q git commit -qm base; q git tag v2.0.0
q git checkout -qb app/docsonly; mkdir -p docs; echo w > docs/note.md; q git add -A; q git commit -qm "docs: a note"
q git checkout -q xteink; q git merge -q --no-ff app/docsonly -m "Merge pull request #1 from ma-r-s/app/docsonly"
out="$(python3 "$TOOL" --repo-dir "$R3" --pr-json "$WORK/none.json" --write 2>&1)"
grep -q "A note" docs/release-body.md && ok "a hand-cut release with nothing device-reaching still says something" || bad "the notes block came out empty: $out"
# "0 landings excluded" is unreachable by construction, so asserting its absence
# asserted nothing. What is worth asserting is that the fallback prints NO count
# line at all and says on stdout why, since a reader seeing every merge listed
# has to be able to tell the filter stood down from the filter never running.
echo "$out" | grep -q "excluded from the notes" && bad "the fallback still counted exclusions" || ok "the fallback adds no count line"
echo "$out" | grep -q "nothing since the tag reaches a user" && ok "the fallback says why it listed everything" || bad "the fallback stood down silently: $out"

echo
echo "release-needed.sh"
mkdir -p "$R/scripts_local"
cp "$RULE" "$NEEDED" "$R/scripts_local/"
chmod +x "$R"/scripts_local/*.sh
cd "$R" || exit 1
q git checkout -q xteink
q git add -A; q git commit -qm "chore: scripts"
q git tag v1.12.10
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok "nothing merged since the tag: no release" || bad "released with nothing since the tag"
mkdir -p docs; echo words > docs/note.md; q git add -A; q git commit -qm "docs: a note"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok "a docs-only merge since the tag is not a release" || bad "a docs-only change would release"
# THE ONE THAT CUT v1.12.21. `.gitignore` is live for a build -- a --committed
# gate materialises its trial worktree from git, so what is ignored decides what
# exists there to compile -- and cannot alter a byte of a published image,
# because a clean clone checks out every tracked file whatever the ignore rules
# say. One predicate answered both questions and this released.
echo "ignored" >> .gitignore; q git add -A; q git commit -qm "chore: ignore a build artefact"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok ".gitignore builds but does not release" || bad ".gitignore would still cut a release"
# And the fork's own machinery, which is the other half of the same shape.
mkdir -p scripts_local; echo "# tweak" >> scripts_local/check.sh; q git add -A; q git commit -qm "gate: a tweak"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok "scripts_local/ builds but does not release" || bad "the gate's own machinery would cut a release"
# CARD #190's half, on the release decision rather than the notes: a fix to what
# gets published must cut a release, even though no local device build can see it.
mkdir -p .github/workflows; echo "# fix" >> .github/workflows/crossplay-release.yml
q git add -A; q git commit -qm "ci: publish the merged image, not the app image"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 0 ] && ok "a fix to what the release publishes releases" || bad "a packaging fix is still invisible to the release decision"
q git reset -q --hard HEAD~1
# ...and a CI tweak, in the same directory, does not.
mkdir -p .github/workflows; echo "# ci" >> .github/workflows/crossplay-ci.yml
q git add -A; q git commit -qm "ci: a job tweak"
git diff --quiet HEAD~1 HEAD -- .github/workflows/crossplay-ci.yml && bad "the CI-tweak fixture committed nothing, so the check below proves nothing" || ok "the CI-tweak fixture really changed a workflow file"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 1 ] && ok "a CI tweak in the same directory does not release" || bad ".github was added wholesale; every CI tweak now cuts a release"
mkdir -p src; echo 'int x;' > src/x.cpp; q git add -A; q git commit -qm "fix: something a device can see"
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 0 ] && ok "a src change since the tag releases" || bad "a src change did not release"
# THE REFUSAL, which is the property that stops the next unrecognised path
# doing what .gitignore did. Neither answer is safe, so it declines and names
# the path; the autorelease job turns exit 2 into a red run.
q git reset -q --hard v1.12.10
mkdir -p brandnew; echo x > brandnew/x.c; q git add -A; q git commit -qm "feat: a new top-level directory"
refusal="$(bash scripts_local/release-needed.sh 2>&1)"; rc=$?
[ "$rc" -eq 2 ] && ok "an unclassified path refuses, rather than guessing (exit 2)" || bad "an unclassified path was answered with a guess (exit $rc)"
echo "$refusal" | grep -q "brandnew/x.c" && ok "and the refusal names the path" || bad "the refusal did not say which path: $refusal"
q git reset -q --hard v1.12.10
q git tag -d v1.12.10; q git tag -d v1.12.9
bash scripts_local/release-needed.sh >/dev/null 2>&1; [ $? -eq 0 ] && ok "no tag at all means release" || bad "no tag refused to release"

echo
echo "v1.12.21, from the real repository"
# THE RELEASE THAT SHOULD NOT HAVE HAPPENED. Every device was offered an update
# whose only device-reaching change was the version number the release itself
# wrote. This is the real range, in the real repository, asked as the gate would
# have asked it: BEFORE the bump, since the autorelease decides and then writes.
#
# Skipped rather than failed when the tags are missing, because a shallow clone
# legitimately lacks them -- but the skip says so, so a suite that quietly never
# ran this is visible.
if git -C "$REPO_ROOT" rev-parse -q --verify v1.12.20 >/dev/null 2>&1 &&
   git -C "$REPO_ROOT" rev-parse -q --verify v1.12.21 >/dev/null 2>&1; then
  PRE="$(git -C "$REPO_ROOT" rev-parse 'v1.12.21^')"
  if ( cd "$REPO_ROOT" && bash "$RULE" --range "v1.12.20..$PRE" --ships --quiet ); then
    bad "v1.12.20..v1.12.21 still warrants a release"
  else
    ok "v1.12.20..v1.12.21 does not warrant a release"
  fi
  # And the same range on the OTHER column still says build, because that
  # answer was never wrong -- .gitignore really can change what a --committed
  # worktree compiles. If this ever flips, the build axis was loosened by a
  # change that was only supposed to add the second one.
  if ( cd "$REPO_ROOT" && bash "$RULE" --range "v1.12.20..$PRE" --quiet ); then
    ok "the same range still needs its device builds: two answers, not one"
  else
    bad "the build column changed too; this was meant to add an axis, not move one"
  fi
  # v1.12.18 was a real release for a real reason. A rule that answers 'no' to
  # everything would pass both checks above.
  if git -C "$REPO_ROOT" rev-parse -q --verify v1.12.17 >/dev/null 2>&1; then
    PRE18="$(git -C "$REPO_ROOT" rev-parse 'v1.12.18^')"
    if ( cd "$REPO_ROOT" && bash "$RULE" --range "v1.12.17..$PRE18" --ships --quiet ); then
      ok "v1.12.17..v1.12.18 does warrant one"
    else
      bad "a release that was right is now refused; the ships column says no to everything"
    fi
  fi
else
  echo "  SKIP v1.12.20/v1.12.21 not in this clone; the real-range checks did not run"
fi

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]
