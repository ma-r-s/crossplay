#!/bin/sh
# Which tree does each running build belong to?
#
#   ./scripts_local/whose-gate.sh          every running gate, with its tree
#   ./scripts_local/whose-gate.sh --mine   only the ones in THIS tree
#
# WHY THIS EXISTS (card #314)
#
# The workspace rule is "kill by PID, never by pattern", and on 2026-09-05 that
# was not enough. An agent ran
#
#     pgrep -f "check.sh --committed"
#
# and got four pids. Every session runs an identically named script from an
# identically named path inside its own worktree, so the command line cannot
# tell them apart -- it is the same string for all of them. The agent nearly
# killed two siblings' gates, and the only thing that distinguished them was
# each pid's WORKING DIRECTORY.
#
# So the answer to "is that my build" is not a pattern and not a name. It is
# `lsof -a -d cwd`, which is awkward enough by hand that nobody does it, which
# is why it lives here instead of in a rule nobody can follow.
#
# Prints, one line per process:  PID  ELAPSED  TREE  COMMAND
# Exit 0 always; an empty list is an answer, not an error.
set -u

# Overridable so host-tests/checksh can drive this against a process it started
# itself. The bracket is the self-exclusion from app/checkrace: `[c]heck` is a
# regex that matches "check" while the pattern's own text does not contain it,
# so this listing can never include the pgrep that produced it.
PATTERN="${GATE_PATTERN:-[c]heck\.sh|[p]io run|[c]make --build}"

MINE=""
if [ "${1:-}" = "--mine" ]; then
  MINE="$(pwd -P)"
fi

# /proc where it exists (Linux, and the CI runner host-tests/checksh runs on),
# lsof elsewhere (macOS, which has no /proc). host-tests/portguard already
# guards `command -v lsof` for the same reason: the tool is not everywhere, and
# a resolver that silently returns nothing would make this script answer "no
# build running" while four of them are.
pid_cwd() {  # pid -> absolute working directory, or empty
  if [ -d /proc/"$1" ]; then
    readlink -f /proc/"$1"/cwd 2>/dev/null
    return 0
  fi
  lsof -a -d cwd -p "$1" -Fn 2>/dev/null | sed -n 's/^n//p' | head -1
}

# The tree a working directory belongs to: the nearest ancestor holding a
# scripts_local/, which is what makes a directory one of ours. Falls back to the
# raw path rather than guessing, because a wrong tree name here would be read as
# fact, and this whole file exists because a wrong answer got a sibling
# session's build killed.
tree_of() {  # cwd -> tree path
  _wg_d="$1"
  while [ -n "$_wg_d" ] && [ "$_wg_d" != "/" ]; do
    if [ -d "$_wg_d/scripts_local" ]; then
      # A --committed run does its work in a throwaway worktree named
      # "xteink-committed-<tag>-<owner pid>" in TMPDIR, so most of the pids in
      # this listing resolve to a temp path and NOT to the tree whose code is
      # being verified. That is the answer nobody can act on: the question is
      # always "whose work is this", and four of the five lines would name a
      # directory belonging to no session. The owner's pid is in the name, so
      # ask IT where it lives.
      case "$(basename "$_wg_d")" in
        xteink-committed-*-*)
          _wg_owner="$(pid_cwd "$(basename "$_wg_d" | sed 's/.*-//')")"
          if [ -n "$_wg_owner" ]; then
            printf '%s\n' "$_wg_owner"
            return 0
          fi
          ;;
      esac
      printf '%s\n' "$_wg_d"
      return 0
    fi
    _wg_d="$(dirname "$_wg_d")"
  done
  printf '%s\n' "$1"
}

SELF=$$
found=0
for pid in $(pgrep -f "$PATTERN" 2>/dev/null); do
  [ "$pid" = "$SELF" ] && continue
  cwd="$(pid_cwd "$pid")"
  [ -n "$cwd" ] || continue
  tree="$(tree_of "$cwd")"
  if [ -n "$MINE" ] && [ "$tree" != "$MINE" ]; then
    continue
  fi
  elapsed="$(ps -o etime= -p "$pid" 2>/dev/null | tr -d ' ')"
  cmd="$(ps -o command= -p "$pid" 2>/dev/null | cut -c1-70)"
  printf '%s\t%s\t%s\t%s\n' "$pid" "${elapsed:-?}" "$tree" "$cmd"
  found=$((found + 1))
done

if [ "$found" -eq 0 ]; then
  if [ -n "$MINE" ]; then
    echo "no build running in $MINE"
  else
    echo "no build running anywhere in this workspace"
  fi
fi
exit 0
