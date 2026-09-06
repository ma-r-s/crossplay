#!/bin/bash
# One worktree per piece of work in flight.
#
#   ./scripts/wt.sh new <name> [--from <branch>]   # make one
#   ./scripts/wt.sh list                           # what exists, and what state
#   ./scripts/wt.sh drop <name>                    # remove it once merged
#   ./scripts/wt.sh prune [--dry-run]              # drop every tree that is merged and clean
#
# Why this exists: everything used to happen in firmware-next/. Several apps
# were built at once, so that one tree held four efforts' uncommitted work at
# the same time, nothing could be committed atomically, agent screenshot runs
# shared one SD card and one qa-artifacts/ directory, every save restarted
# Mario's simulator, and check.sh reported green for the *union* of everyone's
# code. That last one is not theoretical: Dungeon, Insider and Hacker News all
# shipped against Toybox symbols that were never committed, and xteink HEAD did
# not compile for three commits, because every check ran against a dirty tree.
#
# A worktree per effort fixes all of it at once: own branch, own build output,
# own SD card, own screenshots, own build lock.
set -uo pipefail

SCRIPTS_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
INTEGRATION="$(cd "$SCRIPTS_DIR/.." && pwd)"

find_workspace() {
  local d="$1"
  while [ "$d" != "/" ]; do
    [ -e "$d/.xteink-workspace" ] && { printf '%s' "$d"; return 0; }
    d="$(dirname "$d")"
  done
  printf '%s' "$(dirname "$1")"
}
WORKSPACE="$(find_workspace "$SCRIPTS_DIR")"
WT_ROOT="$WORKSPACE/wt"

die() {
  echo "error: $*" >&2
  exit 1
}

# The base is origin/xteink, never the local branch. firmware-next's local
# xteink carries a train's unpushed commits for the whole bump-to-push window,
# so branching from it silently adopts somebody else's release in progress. On
# 2026-08-30 a tree cut that way gated another session's unpushed release notes
# and reported that session's failure as its own discovery.
cmd_new() {
  local name="${1:-}" from=""
  [ -n "$name" ] || die "usage: wt.sh new <name> [--from <branch>]"
  shift
  while [ $# -gt 0 ]; do
    case "$1" in
      --from)
        from="${2:-}"
        [ -n "$from" ] || die "--from needs a branch"
        shift 2
        ;;
      *) die "unknown option: $1" ;;
    esac
  done

  local dir="$WT_ROOT/$name" branch="app/$name"
  [ -e "$dir" ] && die "$dir already exists"
  git -C "$INTEGRATION" show-ref --verify --quiet "refs/heads/$branch" &&
    die "branch $branch already exists (drop the old worktree first, or pick another name)"

  # Fetch before reading origin/xteink: a stale remote ref bases the tree on
  # work that landed hours ago, which is the same bug one indirection further.
  if [ -z "$from" ]; then
    git -C "$INTEGRATION" fetch origin --quiet 2>/dev/null || true
    from="origin/xteink"
  fi

  mkdir -p "$WT_ROOT"
  echo "creating $dir on $branch (from $from)"
  git -C "$INTEGRATION" worktree add -b "$branch" "$dir" "$from" || die "worktree add failed"

  # A fresh worktree does NOT populate submodules, and the host tests compile
  # FreeInkUI out of freeink-sdk/. Without this the new tree looks fine until
  # the first check.sh fails to find headers that are plainly there in the tree
  # next door. sync.sh learned this the same way.
  echo "initialising submodules ..."
  git -C "$dir" submodule update --init --recursive || die "submodule init failed"

  # Its own card and its own artifacts. Both are gitignored and both are derived
  # from the tree path by lib-sim.sh, so they need only to exist.
  mkdir -p "$dir/fs_agent/books" "$dir/fs_agent/.crosspoint" "$dir/qa-artifacts"

  cat <<EOF

ready: $dir

  cd $dir
  ./scripts_local/check.sh            # host tests + both builds, this tree only
  ./scripts_local/sim-shot.sh '...'   # screenshots, this tree's own SD card

Use scripts_local/ from inside the tree, NOT the workspace-root scripts/ --
those resolve to firmware-next and would test the wrong code. The scripts
refuse rather than let that happen quietly.

Mario can watch this tree with:  ./scripts/dev.sh $name
EOF
}

cmd_list() {
  printf "%-16s %-22s %-8s %s\n" TREE BRANCH DIRTY STATE
  local d
  for d in "$INTEGRATION" "$WT_ROOT"/*/; do
    [ -d "$d" ] || continue
    d="${d%/}"
    local name branch dirty state ahead
    name="$(basename "$d")"
    branch="$(git -C "$d" branch --show-current 2>/dev/null || echo '?')"
    # --ignore-submodules=untracked: the icon tools drop a __pycache__/ inside
    # freeink-sdk, which is untracked content in a submodule and not your work.
    # Without it every clean tree reports one dirty file, which trains you to
    # ignore the column. sync.sh and check.sh do the same.
    dirty="$(git -C "$d" status --porcelain --ignore-submodules=untracked 2>/dev/null | grep -c '' || true)"
    [ "$dirty" -eq 0 ] && dirty="clean" || dirty="$dirty files"
    # Against origin/xteink, not the local branch, for the reason in cmd_new:
    # during a train the local branch is ahead of what everyone else can see,
    # so every tree's count reads high and the column stops meaning anything.
    ahead="$(git -C "$d" rev-list --count origin/xteink.."$branch" 2>/dev/null || echo 0)"
    state=""
    [ "$ahead" != "0" ] && state="$ahead commit(s) ahead of origin/xteink"
    # A simulator running out of this tree means somebody is using it.
    pgrep -f "^$d/.pio/build/simulator_x4_pro/program" >/dev/null 2>&1 &&
      state="${state:+$state, }sim running"
    printf "%-16s %-22s %-8s %s\n" "$name" "$branch" "$dirty" "$state"
  done
}

cmd_drop() {
  local name="${1:-}"
  [ -n "$name" ] || die "usage: wt.sh drop <name>"
  local dir="$WT_ROOT/$name" branch="app/$name"
  [ -d "$dir" ] || die "no worktree at $dir"

  # Refuse to throw away work that is not in xteink yet. Removing a worktree
  # deletes its build output and its screenshots too, so a wrong call here is
  # not a small mistake.
  local unmerged dirty
  dirty="$(git -C "$dir" status --porcelain --ignore-submodules=untracked | grep -c '' | tr -d ' ')"
  unmerged="$(git -C "$INTEGRATION" rev-list --count origin/xteink.."$branch" 2>/dev/null || echo 0)"
  if [ "$dirty" != "0" ] || [ "$unmerged" != "0" ]; then
    echo "refusing to drop $name:" >&2
    [ "$dirty" != "0" ] && echo "  $dirty uncommitted file(s)" >&2
    [ "$unmerged" != "0" ] && echo "  $unmerged commit(s) not in xteink" >&2
    echo >&2
    echo "merge it first, or pass --force if you mean to lose it." >&2
    [ "${2:-}" != "--force" ] && exit 1
    echo "--force given, dropping anyway." >&2
  fi

  git -C "$INTEGRATION" worktree remove --force "$dir" || die "worktree remove failed"
  git -C "$INTEGRATION" branch -D "$branch" 2>/dev/null
  echo "dropped $name"
}

# Closing is the part nobody does. A tree whose branch is entirely in
# origin/xteink and whose working copy is clean holds nothing: no commits, no
# edits, and its build output and screenshots can be made again. Ninety-odd
# such trees once sat under wt/ because dropping them was a decision per tree.
# prune makes it one decision for all of them and never touches a tree that
# has anything in it, is detached (wt/_land), or has a simulator running.
cmd_prune() {
  local dry=0
  [ "${1:-}" = "--dry-run" ] && dry=1
  git -C "$INTEGRATION" fetch -q origin 2>/dev/null
  local d name branch dirty unmerged kept=0 dropped=0
  for d in "$WT_ROOT"/*/; do
    [ -d "$d" ] || continue
    d="${d%/}"; name="$(basename "$d")"
    branch="$(git -C "$d" branch --show-current 2>/dev/null)"
    [ -n "$branch" ] || { kept=$((kept + 1)); continue; }
    dirty="$(git -C "$d" status --porcelain --ignore-submodules=untracked 2>/dev/null | grep -c '' | tr -d ' ')"
    unmerged="$(git -C "$INTEGRATION" rev-list --count origin/xteink.."$branch" 2>/dev/null || echo 1)"
    # A tree in ACTIVE USE can be clean and merged at the same time, and
    # dropping it destroys work that was never going to be committed.
    #
    # 2026-09-04: a user-test session lost wt/usertest and wt/usertest2
    # mid-run, twice, taking their screenshots with them. A QA or review tree
    # never commits anything -- reading, building and screenshotting is the
    # whole job -- so it is clean by definition and merged by definition, and
    # every one of the three tests above says "safe to delete" while somebody
    # is working in it. The simulator check only covers the seconds a shot is
    # actually being taken.
    #
    # So: recent write activity keeps a tree. Cheap, and it needs nothing from
    # the board -- a tree nobody has touched in two hours is abandoned, and one
    # written to since then is not. .git and .pio are excluded because a fetch
    # or a build cache touches them without anyone being there.
    local touched
    touched="$(find "$d" -mindepth 1 -maxdepth 3 \
                 -not -path "$d/.git/*" -not -path "$d/.git" \
                 -not -path "$d/.pio/*" -not -path "$d/.pio" \
                 -newermt '-120 minutes' -print -quit 2>/dev/null)"
    if [ "$dirty" != "0" ] || [ "$unmerged" != "0" ] || [ -n "$touched" ] || pgrep -f "^$d/.pio/build/simulator_x4_pro/program" >/dev/null 2>&1; then
      kept=$((kept + 1)); continue
    fi
    # And the board's word: a tree whose holder's lease is live, or with a gate
    # running on it, is in use whatever the files say. Two sessions once shared
    # one tree for an hour; a sweep once committed another worker's diff. The
    # record is the only thing that may call a tree abandoned.
    local boardpy="$INTEGRATION/tools_local/board/board.py"
    if [ -f "$boardpy" ] && ! python3 "$boardpy" tree "$name" >/dev/null 2>&1; then
      kept=$((kept + 1)); continue
    fi
    if [ "$dry" = 1 ]; then
      echo "would drop $name ($branch: merged, clean)"
    else
      git -C "$INTEGRATION" worktree remove --force "$d" >/dev/null 2>&1 || { echo "could not remove $name" >&2; kept=$((kept + 1)); continue; }
      git -C "$INTEGRATION" branch -D "$branch" >/dev/null 2>&1
      echo "dropped $name ($branch: merged, clean)"
    fi
    dropped=$((dropped + 1))
  done
  [ "$dry" = 1 ] && echo "prune: $dropped would go, $kept kept" || echo "prune: $dropped dropped, $kept kept"
}

case "${1:-}" in
  new)
    shift
    cmd_new "$@"
    ;;
  list | ls | "")
    cmd_list
    ;;
  drop | rm)
    shift
    cmd_drop "$@"
    ;;
  prune)
    shift
    cmd_prune "$@"
    ;;
  *)
    echo "usage: wt.sh {new <name> [--from <branch>] | list | drop <name> | prune [--dry-run]}" >&2
    exit 2
    ;;
esac
