#!/bin/sh
# Keep the shared PlatformIO object cache from filling the disk.
#
#   ./scripts_local/cache-guard.sh --status    what is there now
#   ./scripts_local/cache-guard.sh --prune     trim to the cap, oldest first
#
# check.sh sources this and calls cache_guard_check once it holds the firmware
# build lock. Standalone use is for looking, and for a manual trim.
#
# WHY THIS EXISTS
#
# PlatformIO's build_cache_dir is content-addressed with no eviction and no size
# limit -- upstream behaviour, not our configuration. Every tree shares one
# cache through PLATFORMIO_BUILD_CACHE_DIR, and every code change in every tree
# mints new object hashes that nothing ever removes. Measured 2026-08-30: 66GB
# in 252,825 files, and NOT ONE of them older than seven days. This is not stale
# junk accumulating; it is churn with no floor.
#
# That measurement is the whole design. Pruning by age frees nothing, because
# nothing is old. The cap has to be on SIZE, oldest-first.
#
# It fails as a compiler error, never as a disk warning: `[Errno 28] No space
# left on device` arrives from inside the espressif32 builder, naming no file of
# ours, while extracting framework libs the fork does not even use. It reads
# exactly like the concurrent-build corruption and is not that.

# Trim the cache to this. Big enough that a warm tree stays warm; small enough
# that thirty trees cannot fill a disk between releases.
: "${CACHE_CAP_GB:=25}"
# Refuse to start device builds below this much FREE SPACE. Deliberately
# generous: a cold build writes into the cache as it runs, so the headroom you
# check is not the headroom you keep. That is how "tight" becomes Errno 28
# mid-build rather than a warning up front.
: "${AVAIL_FLOOR_GB:=10}"

# APFS reports / and /System/Volumes/Data as two volumes sharing one container,
# so they show wildly different percentages while having the SAME space free.
# Read the Avail column; a percentage here is meaningless in both directions.
cache_guard_avail_gb() {
  df -g / 2>/dev/null | awk 'NR==2 {print $4}'
}

# KB internally, GB only at the edges. Integer GB truncation is lossy in
# exactly the wrong direction: a 66.9GB cache reports 66, and comparing a cap
# at GB granularity silently does nothing for anything under a gigabyte.
cache_guard_size_kb() {
  [ -d "$1" ] || { echo 0; return; }
  du -sk "$1" 2>/dev/null | awk '{print $1}'
}

cache_guard_size_gb() {
  kb=$(cache_guard_size_kb "$1")
  awk -v k="$kb" 'BEGIN {printf "%.1f", k/1048576}'
}

cache_guard_status() {
  dir="${1:-${PLATFORMIO_BUILD_CACHE_DIR:-}}"
  [ -n "$dir" ] || { echo "cache: no PLATFORMIO_BUILD_CACHE_DIR set"; return 0; }
  size=$(cache_guard_size_gb "$dir")
  avail=$(cache_guard_avail_gb)
  files=$(find "$dir" -type f 2>/dev/null | wc -l | tr -d ' ')
  printf "cache: %sGB in %s files at %s\n" "$size" "$files" "$dir"
  printf "disk:  %sGB free (cap %sGB, floor %sGB)\n" "$avail" "$CACHE_CAP_GB" "$AVAIL_FLOOR_GB"
}

# Delete oldest-by-mtime until the cache is under the cap.
#
# CALLER MUST HOLD THE FIRMWARE BUILD LOCK. These objects are inputs to whatever
# device build is running in any tree; deleting them underneath one turns a
# healthy build into a link error naming no file of ours, which is the exact
# failure this script exists to stop being mysterious.
cache_guard_prune() {
  dir="${1:-${PLATFORMIO_BUILD_CACHE_DIR:-}}"
  [ -n "$dir" ] && [ -d "$dir" ] || return 0
  now_kb=$(cache_guard_size_kb "$dir")
  target_kb=$(awk -v g="$CACHE_CAP_GB" 'BEGIN {printf "%d", g*1048576}')
  [ "$now_kb" -le "$target_kb" ] && return 0
  echo "  cache is $(cache_guard_size_gb "$dir")GB, over the ${CACHE_CAP_GB}GB cap -- trimming oldest first"

  # Oldest first, by mtime. -print0/-r0 so a path with a space cannot split.
  # The .lock directories are skipped: one of them is how we got here.
  find "$dir" -type f ! -name '*.lock' -print0 2>/dev/null \
    | xargs -0 stat -f '%m %z %N' 2>/dev/null \
    | sort -n \
    | while IFS=' ' read -r _mtime bytes path; do
        [ "$now_kb" -le "$target_kb" ] && break
        rm -f "$path" 2>/dev/null || continue
        now_kb=$(( now_kb - (bytes / 1024) - 1 ))
        printf '%s\n' "$now_kb" > "$dir/.guard-progress"
      done
  rm -f "$dir/.guard-progress"

  # Empty directories left behind cost inodes and slow every later find.
  #
  # Two exclusions, and both were found by the test rather than by thinking:
  # -mindepth 1 keeps the cache ROOT, and -name '*.lock' keeps the build locks.
  # A lock is an EMPTY DIRECTORY by construction -- that is how mkdir gives an
  # atomic mutex -- so an unqualified empty-dir sweep deletes the very lock the
  # caller is holding, and the next tree's mkdir then succeeds while a device
  # build is already running. That is the collision the lock exists to prevent,
  # reintroduced by the cleanup meant to be harmless.
  find "$dir" -mindepth 1 -type d -empty ! -name '*.lock' -delete 2>/dev/null
  echo "  cache now $(cache_guard_size_gb "$dir")GB, $(cache_guard_avail_gb)GB free"
}

# The gate. Prints what it sees, trims if over, and refuses only when trimming
# could not get the disk above the floor -- so the failure names the disk
# instead of arriving later as a compiler error.
cache_guard_check() {
  dir="${1:-${PLATFORMIO_BUILD_CACHE_DIR:-}}"
  [ -n "$dir" ] || return 0
  size_kb=$(cache_guard_size_kb "$dir")
  cap_kb=$(awk -v g="$CACHE_CAP_GB" 'BEGIN {printf "%d", g*1048576}')
  avail=$(cache_guard_avail_gb)
  printf "cache: %sGB, disk: %sGB free\n" "$(cache_guard_size_gb "$dir")" "$avail"

  if [ "$size_kb" -gt "$cap_kb" ] || [ "$avail" -lt "$AVAIL_FLOOR_GB" ]; then
    cache_guard_prune "$dir"
    avail=$(cache_guard_avail_gb)
  fi

  if [ "$avail" -lt "$AVAIL_FLOOR_GB" ]; then
    cat >&2 <<MSG

REFUSING TO BUILD: ${avail}GB free, floor is ${AVAIL_FLOOR_GB}GB.

  Trimming the object cache did not recover enough. Something else is filling
  the disk, and a device build started now would most likely die inside the
  espressif32 builder on [Errno 28] while extracting framework libs -- an error
  naming no file of ours, which reads exactly like a broken commit and is not.

  Look at what is large before building again:
      du -sh ~/Library/Application\\ Support/* | sort -rh | head
      git worktree list        # merged trees still holding build dirs

  Override once you have looked:  AVAIL_FLOOR_GB=4 ./scripts_local/check.sh
MSG
    return 1
  fi
  return 0
}

case "${1:-}" in
  --status) cache_guard_status "${2:-}" ;;
  --prune)  cache_guard_status "${2:-}"; cache_guard_prune "${2:-}" ;;
  --check)  cache_guard_check "${2:-}" ;;
  "")       : ;;   # sourced
  *)        echo "usage: cache-guard.sh [--status|--prune|--check] [dir]" >&2; exit 2 ;;
esac
