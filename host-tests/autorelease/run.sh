#!/bin/bash
# release_notes.py, against a repository built for the purpose.
#
# The script decides the next version and rewrites the notes block that
# host-tests/release then insists on, so a wrong answer here ships a release
# announcing the previous one, which is exactly what v1.6.2 did by hand. Every
# input the script reads is replaced: the git history, the pull requests (a
# JSON file instead of gh), the workflow file and platformio.ini.
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
echo "release-needed.sh"
mkdir -p "$R/scripts_local"
cp "$HERE/../../scripts_local/device-build-needed.sh" "$HERE/../../scripts_local/release-needed.sh" "$R/scripts_local/"
chmod +x "$R"/scripts_local/*.sh
cdd "$R"
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
