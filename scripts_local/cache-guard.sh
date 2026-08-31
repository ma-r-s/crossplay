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
# ours, while extracting framework libs the fork does not even use.
#
# THREE different causes produce that same signature, which is why one is so
# easily mistaken for another:
#
#   1. the disk is full                       -> this guard's floor
#   2. two device builds ran at once          -> check.sh's firmware lock
#   3. something mutated the cache mid-build  -> the exclusions in prune below
#
# All three arrive minutes into a run, from inside the espressif32 builder,
# naming no file of ours. Anyone debugging one is looking at three
# indistinguishable candidates and needs to be told the other two exist.

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

  # Oldest first, by mtime. -print0 so a path with a space cannot split.
  #
  # What is EXCLUDED matters more than what is deleted. The cache directory
  # holds two unrelated kinds of thing:
  #
  #   * content-addressed object files -- disposable, that is the whole point
  #   * SCons and PlatformIO STATE at the root: .sconsign*.dblite (523MB here),
  #     lock directories, and anything else dotted.
  #
  # The state is not cache. .sconsign*.dblite is the signature database every
  # build reads and REWRITES, and a running build holds it open for its whole
  # run -- so deleting it mid-build produces
  #     FileNotFoundError: '.sconsign314.tmp' -> '.sconsign314.dblite'
  # as SCons renames its temp file into place and finds the target gone. Losing
  # it outside a build is quieter and worse: every tree cold-rebuilds and
  # nothing says why.
  #
  # Holding the firmware lock does NOT make this safe. That lock serialises
  # DEVICE builds only; the native simulator build runs unlocked in every tree
  # and uses the same SCons database. So the exclusion has to be structural,
  # not a timing argument.
  # Progress every ~2GB. Trimming a content-addressed cache is hundreds of
  # thousands of small unlinks and a large sweep runs for minutes; silence for
  # that long is the exact signature this workspace keeps misreading as a wedge,
  # and this is the branch that exists to stop failures being ambiguous. It
  # costs one printf per 2GB and turns "still working" into evidence.
  start_kb="$now_kb"
  next_report=$(( now_kb - 2097152 ))
  removed=0
  # -path, NOT -maxdepth. `-maxdepth` is a GLOBAL option in find: writing
  # `-maxdepth 1 -name '.*' -prune -o -type f -print` does not scope the depth
  # to the prune clause, it caps the ENTIRE walk at depth 1 -- and the cache
  # stores its objects under 00/ 01/ 02/, so the prune saw 2 files out of
  # 336,164 and deleted nothing while announcing that it was trimming. The
  # tests passed because their fixture was flat; they are nested now.
  # Prune whole SUBTREES, not just matching files. A lock is a DIRECTORY
  # containing an owner file, so `! -name '*.lock'` skips the directory and
  # then happily deletes the owner inside it -- which is the ownership bug
  # this script documents, reintroduced by its own cleanup. -maxdepth used to
  # hide that by never descending at all.
  find "$dir" \( -path "$dir/.*" -o -name '*.lock' \) -prune -o -type f -print0 2>/dev/null \
    | xargs -0 stat -f '%m %z %N' 2>/dev/null \
    | sort -n \
    | while IFS=' ' read -r _mtime bytes path; do
        [ "$now_kb" -le "$target_kb" ] && break
        rm -f "$path" 2>/dev/null || continue
        now_kb=$(( now_kb - (bytes / 1024) - 1 ))
        removed=$(( removed + 1 ))
        if [ "$now_kb" -le "$next_report" ]; then
          printf '    trimmed %sGB so far (%s files), %sGB left to go, %sGB free\n' \
            "$(awk -v a="$start_kb" -v b="$now_kb" 'BEGIN{printf "%.1f",(a-b)/1048576}')" \
            "$removed" \
            "$(awk -v a="$now_kb" -v b="$target_kb" 'BEGIN{printf "%.1f",(a-b)/1048576}')" \
            "$(cache_guard_avail_gb)"
          next_report=$(( now_kb - 2097152 ))
        fi
      done

  # Empty directories left behind cost inodes and slow every later find.
  #
  # Three exclusions now, none of them found by thinking:
  # -mindepth 1 keeps the cache ROOT, and -name '*.lock' keeps the build locks.
  # A lock is an EMPTY DIRECTORY by construction -- that is how mkdir gives an
  # atomic mutex -- so an unqualified empty-dir sweep deletes the very lock the
  # caller is holding, and the next tree's mkdir then succeeds while a device
  # build is already running. That is the collision the lock exists to prevent,
  # reintroduced by the cleanup meant to be harmless.
  find "$dir" -mindepth 1 -name '.*' -prune -o -type d -empty ! -name '*.lock' -print0 2>/dev/null \
    | xargs -0 rmdir 2>/dev/null || true
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

# Dispatch ONLY when executed, never when sourced.
#
# A sourced script inherits the caller's positional parameters. check.sh sources
# this from inside its build loop, where $1 is the caller's own first argument --
# "--committed" on every release gate. The old unguarded `case "${1:-}"` then
# fell through to its usage branch and called `exit 2`, killing the gate from
# inside a helper that had not been asked to do anything. It also aborted
# host-tests/checksh before its owner line ran, which is how it was found.
#
# $0 is the sourcing script's name when sourced, and this file's own path when
# executed, so comparing basenames tells the two apart in both bash and sh.
case "$(basename "${0:-}")" in
  cache-guard.sh)
    case "${1:-}" in
      --status) cache_guard_status "${2:-}" ;;
      --prune)  cache_guard_status "${2:-}"; cache_guard_prune "${2:-}" ;;
      --check)  cache_guard_check "${2:-}" ;;
      "")       cache_guard_status "" ;;
      *)        echo "usage: cache-guard.sh [--status|--prune|--check] [dir]" >&2; exit 2 ;;
    esac
    ;;
esac
