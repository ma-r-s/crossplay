#!/bin/bash
# Recapture the fixtures from the real GitHub API.
#
#   capture.sh healthy   refresh the "a release shipped" pair (the default)
#   capture.sh morning   refuses: those four are frozen evidence of 2026-09-04
#
# Every field the watcher reads is kept exactly as GitHub sends it: that is the
# point of capturing rather than hand-writing them, because a fixture written
# from memory agrees with a function that reads the wrong field name. What is
# stripped is only whole sub-objects the watcher never looks at (a run's
# `repository` and `head_repository`, a release's `author` and its assets'
# `uploader`, a commit's GitHub-user `author`/`committer` -- note the dates
# live in `commit.committer.date`, which stays). They are half the bytes.
#
#   host-tests/relwatch/fixtures/capture.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
G=https://api.github.com/repos/ma-r-s/crossplay
get() { curl -sS -H 'Accept: application/vnd.github+json' -H 'User-Agent: crossplay-board-pulse' "$1"; }
strip() { python3 -c 'import json,sys;
d=json.load(sys.stdin)
def run(r):
    for k in ("repository","head_repository"): r.pop(k,None)
    return r
def rel(r):
    r.pop("author",None)
    for a in r.get("assets",[]): a.pop("uploader",None)
    return r
def com(c):
    for k in ("author","committer"): c.pop(k,None)
    return c
f={"runs":lambda d:(d.update(workflow_runs=[run(x) for x in d["workflow_runs"]]) or d),
   "release":rel,"commits":lambda d:[com(x) for x in d]}[sys.argv[1]]
json.dump(f(d),sys.stdout,indent=2)' "$1"; }
case "${1:-}" in
  morning)
    # FROZEN EVIDENCE. These four are the state of the pipeline on 2026-09-04
    # between 11:30 and 13:08, when two releases failed four runs in five hours
    # with nothing published since v1.12.13. The API window has long since moved
    # past them, so this branch CANNOT reproduce them -- it exists to say so
    # rather than to let a plain re-run quietly overwrite the evidence with
    # today's healthy pipeline and turn the suite green on nothing.
    echo "the morning fixtures are frozen history and cannot be recaptured:" >&2
    echo "  release-runs.json autorelease-runs.json latest-release.json xteink-commits.json" >&2
    echo "recover them from git if they are lost." >&2
    exit 1;;
  healthy|"")
    # Whatever the pipeline looks like now. The suite reads these as "a release
    # that shipped", so recapture them only when the newest release is healthy.
    get "$G/actions/workflows/crossplay-release.yml/runs?per_page=2" | strip runs    > "$HERE/healthy-release-runs.json"
    get "$G/releases/latest"                                        | strip release > "$HERE/healthy-latest-release.json"
    wc -c "$HERE"/healthy-*.json;;
  *)
    echo "usage: capture.sh [healthy|morning]" >&2; exit 2;;
esac
