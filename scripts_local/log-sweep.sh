#!/bin/sh
# Keep check.sh's per-tree log directories from accumulating in TMPDIR.
#
#   ./scripts_local/log-sweep.sh --status        what is there now
#   ./scripts_local/log-sweep.sh --prune [base]  remove stale ones, provably safe
#
# check.sh sources this and calls log_sweep_prune_stale once, at the top, before
# it writes anything. Standalone use is for looking, and for a manual sweep.
#
# WHY THIS EXISTS (card #144, corrected by #210)
#
# check.sh names its log directory "$LOGS" = "$TMPDIR/xteink-check-$TAG", where
# TAG is a shasum of the tree's absolute path. That is ONE directory per tree
# path, reused forever: nothing anywhere removed it, and the EXIT trap in
# check.sh's --committed path frees only the throwaway worktree. Measured
# 2026-09-05: 503 xteink-check-* directories totalling 5.1GB in one TMPDIR, one
# per worktree that has EVER run the gate, the oldest from a tree deleted a week
# earlier. A full run writes ~900 files into its dir (a whole cmake-build tree
# lives inside it), so the bytes are real, not noise.
#
# TWO cleanups, together, bound it:
#   1. On a GREEN verdict, check.sh drops THIS run's own dir (a one-line rm at
#      the foot of the file -- a passing run's logs record only passing suites).
#   2. This sweep removes SIBLING dirs that no running gate could still own, so
#      the dirs left by trees since deleted, and by runs that FAILED long enough
#      ago to be past debugging, cannot pile up without limit.
#
# THE SAFETY ARGUMENT is the whole design. Several trees run check.sh at once,
# and deleting a live gate's log dir out from under it turns a healthy run into
# a mystery. So a dir is removed ONLY when nothing anywhere in its subtree has
# been touched for LOG_SWEEP_MAX_AGE_HOURS -- a window far longer than any run.
# The freshness signal is the newest mtime in the SUBTREE, never the directory's
# own mtime: a directory's mtime bumps only when a direct child is added or
# removed, so a gate that is mid-build -- appending to a log file it created
# minutes ago, or writing object files deep under cmake-build -- has a directory
# mtime that stopped advancing while the run is very much alive. Reading the dir
# mtime would delete exactly the busy trees. The subtree walk cannot.
#
# 24h is not "recent enough to probably be safe", it is "no check.sh run has
# ever taken remotely this long". A cold full run with both device builds is
# well under an hour; the floor exists so a stuck-and-abandoned run is swept
# eventually, not so a live one is swept ever.

# The keep window, in hours. Anything whose subtree has been silent for longer
# is provably not a running gate. Generous on purpose (see above).
: "${LOG_SWEEP_MAX_AGE_HOURS:=24}"

# Print a temp marker file whose mtime is (now - hours). The caller removes it.
# BSD and GNU date spell relative time differently, and the BSD form is not
# merely unsupported on GNU -- `date -v` there is "set the date", which would
# fail loudly, but 2>/dev/null would eat it and leave an unstamped marker at the
# current time, against which EVERYTHING reads as older and the sweep deletes
# live trees. So detect the flavour by `date --version` (GNU has it, BSD does
# not), exactly as cache-guard.sh detects stat, and on any failure to produce a
# stamp leave the marker at "now" so the comparison finds everything NEWER and
# the sweep removes NOTHING. Failing closed here means keeping dirs, never
# deleting them.
log_sweep_cutoff_marker() {
  _ls_hours="${1:-$LOG_SWEEP_MAX_AGE_HOURS}"
  _ls_marker="$(mktemp "${TMPDIR:-/tmp}/xteink-logsweep-mark.XXXXXX")" || return 1
  if date --version >/dev/null 2>&1; then
    _ls_stamp="$(date -d "-${_ls_hours} hours" +%Y%m%d%H%M.%S 2>/dev/null)"
  else
    _ls_stamp="$(date -v-"${_ls_hours}"H +%Y%m%d%H%M.%S 2>/dev/null)"
  fi
  if [ -n "$_ls_stamp" ]; then
    touch -t "$_ls_stamp" "$_ls_marker" 2>/dev/null || true
  fi
  printf '%s\n' "$_ls_marker"
}

# True when nothing in $1's subtree is newer than the marker $2, i.e. the dir is
# safe to remove. `find -newer | head -1` short-circuits on the first fresh file
# for a live tree; for a genuinely stale one it walks the (small) subtree to
# prove nothing is fresh. No -newermt / -quit: -newer against a reference file
# and a head pipe are portable to both BSD and GNU find.
log_sweep_is_stale() {
  _ls_dir="$1"; _ls_mark="$2"
  [ -d "$_ls_dir" ] || return 1
  _ls_recent="$(find "$_ls_dir" -newer "$_ls_mark" 2>/dev/null | head -1)"
  [ -z "$_ls_recent" ]
}

# Remove every "$base"/xteink-check-* directory whose subtree is older than the
# window, except $self (this run's own, passed so a fresh-but-not-yet-written
# dir is never a candidate). Never fatal, and prints one line only if it removed
# something.
log_sweep_prune_stale() {
  _ls_base="${1:-${TMPDIR:-/tmp}}"
  _ls_self="${2:-}"
  _ls_hours="${3:-$LOG_SWEEP_MAX_AGE_HOURS}"
  _ls_marker="$(log_sweep_cutoff_marker "$_ls_hours")" || return 0
  _ls_removed=0
  _ls_freed_kb=0
  for _ls_d in "$_ls_base"/xteink-check-*; do
    [ -d "$_ls_d" ] || continue
    if [ -n "$_ls_self" ] && [ "$_ls_d" = "$_ls_self" ]; then
      continue
    fi
    if log_sweep_is_stale "$_ls_d" "$_ls_marker"; then
      _ls_kb="$(du -sk "$_ls_d" 2>/dev/null | awk '{print $1}')"
      if rm -rf "$_ls_d" 2>/dev/null; then
        _ls_removed=$((_ls_removed + 1))
        _ls_freed_kb=$((_ls_freed_kb + ${_ls_kb:-0}))
      fi
    fi
  done
  rm -f "$_ls_marker" 2>/dev/null || true
  if [ "$_ls_removed" -gt 0 ]; then
    printf '  swept %s stale check.sh log dir(s), freed %sMB (subtree idle >%sh)\n' \
      "$_ls_removed" "$((_ls_freed_kb / 1024))" "$_ls_hours"
  fi
  return 0
}

# What is there now: count, total size, and how many the sweep would take.
log_sweep_status() {
  _ls_base="${1:-${TMPDIR:-/tmp}}"
  _ls_hours="${2:-$LOG_SWEEP_MAX_AGE_HOURS}"
  _ls_n=0
  for _ls_d in "$_ls_base"/xteink-check-*; do
    [ -d "$_ls_d" ] || continue
    _ls_n=$((_ls_n + 1))
  done
  _ls_total="$(du -sch "$_ls_base"/xteink-check-* 2>/dev/null | awk 'END{print $1}')"
  printf 'log dirs: %s in %s (total %s)\n' "$_ls_n" "$_ls_base" "${_ls_total:-0B}"
  _ls_marker="$(log_sweep_cutoff_marker "$_ls_hours")" || return 0
  _ls_stale=0
  for _ls_d in "$_ls_base"/xteink-check-*; do
    [ -d "$_ls_d" ] || continue
    if log_sweep_is_stale "$_ls_d" "$_ls_marker"; then
      _ls_stale=$((_ls_stale + 1))
    fi
  done
  rm -f "$_ls_marker" 2>/dev/null || true
  printf 'a sweep would remove %s of them (subtree idle >%sh)\n' "$_ls_stale" "$_ls_hours"
}

# Dispatch ONLY when executed, never when sourced. check.sh sources this; a
# sourced script inherits the caller's $0 (the sourcing script's name), so
# comparing basenames tells "run me" from "give me the functions" in both bash
# and sh. Same guard, same reason, as cache-guard.sh.
case "$(basename "${0:-}")" in
  log-sweep.sh)
    case "${1:-}" in
      --prune)  log_sweep_prune_stale "${2:-${TMPDIR:-/tmp}}" "" "${3:-$LOG_SWEEP_MAX_AGE_HOURS}" ;;
      --status) log_sweep_status "${2:-${TMPDIR:-/tmp}}" "${3:-$LOG_SWEEP_MAX_AGE_HOURS}" ;;
      "")       log_sweep_status "${TMPDIR:-/tmp}" ;;
      *)        echo "usage: log-sweep.sh [--status|--prune] [base] [max_age_hours]" >&2; exit 2 ;;
    esac
    ;;
esac
