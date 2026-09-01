#!/bin/bash
# device-build-needed.sh, against every shape of change it can meet.
#
# This script decides whether four cross-compiled device builds are SKIPPED, so
# a wrong "not needed" ships a firmware nothing compiled. Every assertion below
# is therefore written from the skip's point of view: the interesting direction
# is not "does it build when it should", it is "does it ever skip when it must
# not". Hence the unknown-directory case, which is the one that protects the
# rule against the next person adding a top-level directory that feeds the
# image.
#
#   host-tests/gatepath/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="$HERE/../../scripts_local/device-build-needed.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -x "$TOOL" ] || { echo "FAIL cannot find or execute $TOOL"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
q()   { "$@" >/dev/null 2>&1; }
cdd() { cd "$1" || { echo "FAIL cannot enter $1 -- refusing to run outside the fixture"; exit 1; }; }

# A repo with an origin/xteink to diff against. Same sandbox discipline as
# host-tests/wtbase: an earlier suite of ours fell through a failed cd and drove
# the real workspace, so the fixture is checked before anything is asserted.
q git init --bare -b xteink "$WORK/origin.git"
q git clone "$WORK/origin.git" "$WORK/repo"
cdd "$WORK/repo"
q git config user.email t@t; q git config user.name t
mkdir -p src lib docs site scripts host-tests tools_local
echo x > src/main.cpp; echo x > lib/thing.h; echo x > docs/a.md
echo x > site/index.html; echo x > scripts/build_html.py; echo x > platformio.ini
q git add -A; q git commit -m base
q git push -u origin xteink

case "$PWD" in "$WORK"/*) ;; *) echo "FAIL fixture not in place (cwd $PWD)"; exit 1 ;; esac

# needed <label> <expect: yes|no> -- runs the tool and checks the direction.
#
# CHECK_BUILD_RELEASE_ENVS is unset for the call, deliberately. check.sh exports
# it under --committed, and the tool correctly answers "needed" whenever it is
# set -- so inheriting it made every skip case fail inside exactly the mode this
# suite most needs to be trusted in, while passing standalone where its author
# ran it. The environment a check runs in is part of the check. The release-envs
# behaviour is asserted below by setting the variable ON PURPOSE.
needed() {
  local label="$1" expect="$2" out rc
  out="$(env -u CHECK_BUILD_RELEASE_ENVS "$TOOL" 2>&1)"; rc=$?
  if [ "$expect" = "yes" ]; then
    [ "$rc" -eq 0 ] && ok "$label -> builds run" || bad "$label -> SKIPPED, must not ($out)"
  else
    [ "$rc" -eq 1 ] && ok "$label -> builds skipped" || bad "$label -> ran, expected skip ($out)"
  fi
}

reset_tree() { q git checkout -q "$MAIN" 2>/dev/null; q git reset --hard origin/xteink; q git clean -fdq; }
MAIN="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo xteink)"

echo "device-build-needed"

reset_tree; needed "no change at all" no

reset_tree; echo edit >> docs/a.md;      q git add -A; q git commit -m d; needed "docs only" no
reset_tree; echo edit >> site/index.html; q git add -A; q git commit -m s; needed "site only" no

reset_tree; echo edit >> src/main.cpp;   q git add -A; q git commit -m c; needed "src/" yes
reset_tree; echo edit >> lib/thing.h;    q git add -A; q git commit -m l; needed "lib/" yes
reset_tree; echo edit >> platformio.ini; q git add -A; q git commit -m p; needed "platformio.ini" yes
reset_tree; echo edit >> scripts/build_html.py; q git add -A; q git commit -m b; needed "scripts/ (a pre-build generator)" yes

# The mixed case: one firmware path among many inert ones must still build.
reset_tree
echo edit >> docs/a.md; echo edit >> site/index.html; echo edit >> src/main.cpp
q git add -A; q git commit -m mixed; needed "docs + site + one src file" yes

# The submodule pointer. Tonight's real firmware bug was exactly this: a
# freeink-sdk bump that left four users with a mirrored, unresponsive panel.
# A rule that skipped device builds for it would be worse than no rule.
reset_tree; echo x > freeink-sdk; q git add -A; q git commit -m sdk
needed "the freeink-sdk pointer" yes

# The case the allowlist direction exists for. A directory the rule has never
# heard of must build, not skip -- this is what stops the rule going stale the
# day somebody adds a new source root.
reset_tree; mkdir -p newthing; echo x > newthing/x.c
q git add -A; q git commit -m n; needed "an unrecognised top-level directory" yes

# Uncommitted and untracked work counts. A new file under src/ is untracked
# until it is added, and that is the worst moment to skip.
reset_tree; echo dirty >> src/main.cpp;  needed "uncommitted edit to src/" yes
reset_tree; echo x > src/brand_new.cpp;  needed "untracked new file under src/" yes
reset_tree; echo dirty >> docs/a.md;     needed "uncommitted edit to docs/" no

# A release gate builds what it is about to publish, whatever the diff says.
reset_tree; echo edit >> docs/a.md; q git add -A; q git commit -m r
CHECK_BUILD_RELEASE_ENVS=1 "$TOOL" >/dev/null 2>&1
[ $? -eq 0 ] && ok "release gate builds even for a docs-only diff" \
             || bad "release gate skipped its own device builds"

# Fail-safe: if the base cannot be resolved, build.
reset_tree
env -u CHECK_BUILD_RELEASE_ENVS "$TOOL" --base refs/heads/does-not-exist >/dev/null 2>&1
[ $? -eq 0 ] && ok "unresolvable base -> builds run" || bad "unresolvable base -> skipped"

# The unlocked-build guard's own suite. It runs inside pio, so a bug in it
# fails every device build in the workspace rather than one.
echo
if python3 "$HERE/test_build_lock.py"; then :; else FAIL=$((FAIL+1)); fi

# --device-only: the mirror question. Host-green is NO evidence for code the
# simulator target never compiles, so such a branch keeps its device build even
# when the workspace is otherwise landing on host-green.
dev() {
  local label="$1" expect="$2" rc
  env -u CHECK_BUILD_RELEASE_ENVS "$TOOL" --device-only >/dev/null 2>&1; rc=$?
  if [ "$expect" = "yes" ]; then
    [ "$rc" -eq 0 ] && ok "$label -> device gate required" || bad "$label -> would land on host-green, must not"
  else
    [ "$rc" -eq 1 ] && ok "$label -> host-green is enough" || bad "$label -> demanded a device gate needlessly"
  fi
}

# --range must answer about the commits B ADDS, not about the difference between
# two tips. `git diff A B` is symmetric, so a caller asking "do the commits I am
# MISSING touch firmware?" would get back ITS OWN firmware changes. The case
# below is built so a symmetric implementation gives the opposite answer: the
# side branch changes src/, the range's other end changes only docs/.
echo
echo "device-build-needed --range"

reset_tree
q git checkout -q -b sidebranch
printf 'int f(){return 2;}\n' > src/plain.cpp
q git add -A; q git commit -m "firmware only on this side"
q git checkout -q "$MAIN"
echo inert >> docs/a.md
q git add -A; q git commit -m "docs only on the other side"
out="$(env -u CHECK_BUILD_RELEASE_ENVS "$TOOL" --range "sidebranch..$MAIN" 2>&1)"; rc=$?
if [ "$rc" -eq 1 ]; then
  ok "--range ignores the OTHER side's firmware changes"
else
  bad "--range saw the other side's firmware: $out"
fi

# And it must still see firmware that the range genuinely adds.
q git checkout -q sidebranch
q git checkout -q "$MAIN"
printf '#include <esp_sleep.h>\n' > src/added.cpp
q git add -A; q git commit -m "firmware added by this side"
out="$(env -u CHECK_BUILD_RELEASE_ENVS "$TOOL" --range "sidebranch..$MAIN" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ]; then
  ok "--range still sees firmware the range really adds"
else
  bad "--range missed firmware it should have seen: $out"
fi
q git checkout -q "$MAIN"

echo
echo "device-build-needed --device-only"

reset_tree; echo edit >> docs/a.md; q git add -A; q git commit -m d
dev "docs only" no
reset_tree; echo x > freeink-sdk; q git add -A; q git commit -m s
dev "the freeink-sdk pointer" yes
reset_tree; echo edit >> platformio.ini; q git add -A; q git commit -m p
dev "platformio.ini" yes

# An ordinary app-layer source file: the host target compiles it, so host-green
# is real evidence and it can land without the lock.
reset_tree; printf 'int f(){return 1;}\n' > src/plain.cpp; q git add -A; q git commit -m a
dev "a plain src/ file the host target compiles" no

# The same file once it reaches for something the host target does not have.
reset_tree; printf '#include <esp_sleep.h>\nint f(){return 1;}\n' > src/plain.cpp
q git add -A; q git commit -m e
dev "a src/ file including an ESP-IDF header" yes
reset_tree; printf '#if FREEINK_DEVICE_X4\nint f(){return 1;}\n#endif\n' > src/plain.cpp
q git add -A; q git commit -m g
dev "a src/ file behind a FREEINK_DEVICE_ guard" yes

# CHECK_BUILD_RELEASE_ENVS must not answer for the other two modes. --committed
# exports it, so while its short-circuit sat above them, every --range staleness
# check inside a committed gate reported FIRMWARE whatever the commits touched,
# and --device-only would have demanded a device gate for a docs change. The
# variable means "this run builds release images"; it does not mean "the commits
# you are asking about touch firmware" or "the host target cannot see this".
echo
echo "CHECK_BUILD_RELEASE_ENVS does not answer for --range or --device-only"

reset_tree; echo edit >> docs/a.md; q git add -A; q git commit -m d
if CHECK_BUILD_RELEASE_ENVS=1 "$TOOL" --device-only >/dev/null 2>&1; then
  bad "--device-only demanded a device gate for docs, because release envs were requested"
else
  ok "--device-only ignores CHECK_BUILD_RELEASE_ENVS"
fi
if CHECK_BUILD_RELEASE_ENVS=1 "$TOOL" >/dev/null 2>&1; then
  ok "the DEFAULT question still honours CHECK_BUILD_RELEASE_ENVS"
else
  bad "the default question stopped building for a release gate"
fi

reset_tree
q git checkout -q -b relside
echo edit >> docs/a.md; q git add -A; q git commit -m "docs on the far side"
q git checkout -q "$MAIN"
if CHECK_BUILD_RELEASE_ENVS=1 "$TOOL" --range "$MAIN..relside" >/dev/null 2>&1; then
  bad "--range called a docs-only range firmware, because release envs were requested"
else
  ok "--range ignores CHECK_BUILD_RELEASE_ENVS"
fi
q git checkout -q "$MAIN"

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]
