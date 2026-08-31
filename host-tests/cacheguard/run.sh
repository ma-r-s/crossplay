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
i=0
while [ $i -lt 40 ]; do
  f="$WORK/cache/obj$(printf '%02d' $i).o"
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
  f="$big/obj$(printf '%02d' $i).o"
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

# Whatever survived must be the NEWEST, never an arbitrary subset.
oldest_gone=yes
for n in 59 58 57 56 55; do
  [ -f "$big/obj$n.o" ] && oldest_gone=no
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

echo "$checks checks, $failures failed"
[ "$failures" -eq 0 ]
