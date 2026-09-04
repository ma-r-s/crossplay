#!/bin/bash
# Recapture the fixtures from the real GitHub API.
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
get "$G/actions/workflows/crossplay-release.yml/runs?per_page=6"     | strip runs     > "$HERE/release-runs.json"
get "$G/actions/workflows/crossplay-autorelease.yml/runs?per_page=6" | strip runs     > "$HERE/autorelease-runs.json"
get "$G/releases/latest"                                             | strip release  > "$HERE/latest-release.json"
get "$G/commits?sha=xteink&per_page=12"                              | strip commits  > "$HERE/xteink-commits.json"
wc -c "$HERE"/*.json
