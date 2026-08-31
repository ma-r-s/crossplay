#!/bin/sh
# The cache guard, against a synthetic cache. Never touches the real one: the
# whole point of the guard is that deleting objects underneath a running build
# breaks it, and a test that pruned the shared cache would do exactly that.
#
#   host-tests/cacheguard/run.sh
set -e
cd "$(dirname "$0")"
GUARD="$(cd ../.. && pwd)/scripts_local/cache-guard.sh"
WORK="${TMPDIR:-/tmp}/cacheguard-test-$$"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/cache"

checks=0
failures=0
check() {
  checks=$((checks + 1))
  if [ "$2" != "$3" ]; then
    failures=$((failures + 1))
    echo "FAIL $1: expected '$3', got '$2'"
  fi
}

# 40 files of 1MB, mtimes one day apart, oldest first.
#
# NESTED IN SUBDIRECTORIES, exactly like the real cache, which stores its
# content-addressed objects under 00/ 01/ 02/ ... A flat fixture is why the
# first version of these tests passed while the cap was a no-op on the real
# 88GB cache: `find -maxdepth 1` is a GLOBAL option, so the prune only ever
# saw the two files at the root and never descended. Measured on the real
# cache: 2 files at depth 1, 336,164 at any depth.
i=0
while [ $i -lt 40 ]; do
  mkdir -p "$WORK/cache/$(printf '%02d' $((i % 8)))"
  f="$WORK/cache/$(printf '%02d' $((i % 8)))/obj$(printf '%02d' $i).o"
  dd if=/dev/zero of="$f" bs=1048576 count=1 2>/dev/null
  touch -t "$(date -v-${i}d +%Y%m%d%H%M 2>/dev/null || date -d "-${i} days" +%Y%m%d%H%M)" "$f"
  i=$((i + 1))
done
mkdir -p "$WORK/cache/x4pro.lock"

# Under the cap: nothing is touched, however old the files are.
before=$(find "$WORK/cache" -name '*.o' | wc -l | tr -d ' ')
CACHE_CAP_GB=1 AVAIL_FLOOR_GB=0 "$GUARD" --prune "$WORK/cache" >/dev/null 2>&1
after=$(find "$WORK/cache" -name '*.o' | wc -l | tr -d ' ')
check "under the cap prunes nothing" "$after" "$before"

# Age alone must never trigger a prune. This is the measured fact the whole
# design rests on: the real cache had 252,825 files and none older than 7 days,
# so an age-based policy would have freed exactly zero.
check "age alone does not prune" "$after" "40"

# Over the cap: oldest go first, newest survive.
big="$WORK/big"
mkdir -p "$big"
i=0
while [ $i -lt 60 ]; do
  mkdir -p "$big/$(printf '%02d' $((i % 8)))"
  f="$big/$(printf '%02d' $((i % 8)))/obj$(printf '%02d' $i).o"
  dd if=/dev/zero of="$f" bs=1048576 count=1 2>/dev/null
  touch -t "$(date -v-${i}d +%Y%m%d%H%M 2>/dev/null || date -d "-${i} days" +%Y%m%d%H%M)" "$f"
  i=$((i + 1))
done
# ~30MB cap against 60MB of files: about half must go, and the survivors must
# be the newest half.
mkdir -p "$big/x4pro.lock"
CACHE_CAP_GB=0.03 AVAIL_FLOOR_GB=0 "$GUARD" --prune "$big" >/dev/null 2>&1
survivors=$(find "$big" -name '*.o' | wc -l | tr -d ' ')
check "over the cap trims" "$([ "$survivors" -lt 60 ] && echo yes || echo no)" "yes"
check "over the cap keeps some" "$([ "$survivors" -gt 0 ] && echo yes || echo no)" "yes"

# The cache ROOT must survive. The firmware build lock lives in it, and a tree
# waiting on that lock would mkdir into a directory that no longer means
# anything -- two device builds at once, which is what the lock prevents.
check "cache root survives" "$([ -d "$big" ] && echo yes || echo no)" "yes"
check "lock in a trimmed cache survives" "$([ -d "$big/x4pro.lock" ] && echo yes || echo no)" "yes"

# The lock's CONTENTS, not just the directory. A lock is a directory holding an
# owner file, so a filter that only skips names ending .lock steps into it and
# deletes the owner -- reintroducing the ownership bug this script documents,
# via its own cleanup. -maxdepth used to hide this by never descending at all.
printf '99999 sometree\n' > "$big/x4pro.lock/owner"
touch -t 202001010000 "$big/x4pro.lock/owner"
CACHE_CAP_GB=0.001 AVAIL_FLOOR_GB=0 "$GUARD" --prune "$big" >/dev/null 2>&1
check "the lock OWNER file survives a deep prune" \
  "$([ -f "$big/x4pro.lock/owner" ] && echo yes || echo no)" "yes"

# Whatever survived must be the NEWEST, never an arbitrary subset.
oldest_gone=yes
for n in 59 58 57 56 55; do
  [ -f "$big/$(printf '%02d' $((n % 8)))/obj$n.o" ] && oldest_gone=no
done
check "oldest evicted first" "$oldest_gone" "yes"

# The build lock must survive a prune. Deleting it would free a lock another
# tree is holding, and two device builds would then run at once -- the exact
# collision the lock exists to prevent.
check "lock dir survives" "$([ -d "$WORK/cache/x4pro.lock" ] && echo yes || echo no)" "yes"

# A path with a space must not split.
spacey="$WORK/spacey"
mkdir -p "$spacey/some dir"
dd if=/dev/zero of="$spacey/some dir/a b.o" bs=1024 count=4 2>/dev/null
CACHE_CAP_GB=999 AVAIL_FLOOR_GB=0 "$GUARD" --prune "$spacey" >/dev/null 2>&1
check "space in path survives an under-cap run" \
  "$([ -f "$spacey/some dir/a b.o" ] && echo yes || echo no)" "yes"

# A missing directory is not an error: a fresh workspace has no cache yet.
CACHE_CAP_GB=1 AVAIL_FLOOR_GB=0 "$GUARD" --prune "$WORK/nope" >/dev/null 2>&1
check "missing cache dir is not an error" "$?" "0"

# --status must report without changing anything.
n1=$(find "$big" -type f | wc -l | tr -d ' ')
"$GUARD" --status "$big" >/dev/null 2>&1
n2=$(find "$big" -type f | wc -l | tr -d ' ')
check "--status is read-only" "$n2" "$n1"

# SCons state must survive a prune, however old and however large.
#
# .sconsign*.dblite is the signature database every build reads and REWRITES,
# and a running build holds it open for its whole run. Deleting it mid-build
# gives FileNotFoundError renaming .sconsign314.tmp onto it; deleting it between
# builds is quieter and worse, because every tree cold-rebuilds and nothing says
# why. It is 523MB in the real cache, so an oldest-first sweep would reach it.
state="$WORK/state"
mkdir -p "$state"
i=0
while [ $i -lt 40 ]; do
  mkdir -p "$state/$(printf '%02d' $((i % 8)))"
  f="$state/$(printf '%02d' $((i % 8)))/obj$(printf '%02d' $i).o"
  dd if=/dev/zero of="$f" bs=1048576 count=1 2>/dev/null
  touch -t "$(date -v-${i}d +%Y%m%d%H%M 2>/dev/null || date -d "-${i} days" +%Y%m%d%H%M)" "$f"
  i=$((i + 1))
done
# Deliberately the OLDEST and among the largest, so an unqualified sweep takes
# it first. That is exactly the real cache's shape.
dd if=/dev/zero of="$state/.sconsign314.dblite" bs=1048576 count=5 2>/dev/null
touch -t "$(date -v-400d +%Y%m%d%H%M 2>/dev/null || date -d '-400 days' +%Y%m%d%H%M)" "$state/.sconsign314.dblite"
mkdir -p "$state/.cache-meta"
touch -t "$(date -v-400d +%Y%m%d%H%M 2>/dev/null || date -d '-400 days' +%Y%m%d%H%M)" "$state/.cache-meta"

CACHE_CAP_GB=0.005 AVAIL_FLOOR_GB=0 "$GUARD" --prune "$state" >/dev/null 2>&1
check "sconsign database survives a deep prune" \
  "$([ -f "$state/.sconsign314.dblite" ] && echo yes || echo no)" "yes"
check "sconsign keeps its bytes" \
  "$(wc -c < "$state/.sconsign314.dblite" | tr -d ' ')" "5242880"
check "dot-directories at the root survive" \
  "$([ -d "$state/.cache-meta" ] && echo yes || echo no)" "yes"
check "the prune still did its job" \
  "$([ "$(find "$state" -name '*.o' | wc -l | tr -d ' ')" -lt 40 ] && echo yes || echo no)" "yes"

# Sourcing must be inert, whatever the caller's arguments are.
#
# check.sh sources this from inside its build loop, where $1 is the caller's own
# first argument -- "--committed" on every release gate. A sourced script
# inherits those positional parameters, so an unguarded `case "${1:-}"` reached
# its usage branch and called exit 2, killing the gate from inside a helper that
# had not been asked to do anything. It broke every --committed run and every
# lock test at once, and neither the executed forms nor a bare source revealed
# it, because both have an empty or expected $1.
for arg in --committed --tests --unknown "-x" ""; do
  out=$(sh -c "set -- $arg; . '$GUARD' >/dev/null 2>&1; echo SURVIVED" 2>/dev/null || true)
  check "sourcing is inert with \$1='$arg'" "$out" "SURVIVED"
done

# ...while the executed forms still dispatch, including the usage error.
# `|| rc=$?` on both, because this file runs under `set -e`: a bare call whose
# exit code is the thing being TESTED aborts the suite before it can print its
# summary, and an aborted suite prints nothing at all -- which reads exactly
# like a pass when the caller only looks at the last line. That happened here.
rc=0; "$GUARD" --status "$WORK/cache" >/dev/null 2>&1 || rc=$?
check "executed --status still works" "$rc" "0"
rc=0; "$GUARD" --nonsense >/dev/null 2>&1 || rc=$?
check "executed with a bad flag still errors" "$rc" "2"

echo "$checks checks, $failures failed"
[ "$failures" -eq 0 ]
