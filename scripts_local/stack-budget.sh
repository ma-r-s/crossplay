#!/bin/sh
# Prove every task's deepest call path fits the stack it was given.
#
#   ./scripts_local/stack-budget.sh            # build if needed, then check
#   ./scripts_local/stack-budget.sh --verbose  # print the deepest path too
#
# Runs from inside its own tree, like the rest of scripts_local/, so it measures
# this worktree's code and not firmware-next's.
#
# THE BUILD CACHE HAS TO BE BYPASSED, and this is the whole reason the script
# exists rather than a line in the README. `-fstack-usage` and
# `-fcallgraph-info=su` write .su and .ci files *beside* each object. Those are
# compiler side-outputs, not build artifacts, so `build_cache_dir` neither stores
# nor restores them -- but it does restore the objects, and then nothing
# recompiles and nothing is emitted.
#
# It lies convincingly. `rm -rf .pio/build/x4pro` refilled 537 objects from the
# cache in 18 seconds and reported SUCCESS with zero .ci files. Touching sources
# does not help either: the cache key is content, not mtime. Setting
# PLATFORMIO_BUILD_CACHE_DIR to "" does not help: the empty value is ignored and
# platformio.ini wins.
#
# Pointing it at a directory that is empty does work. Same build then takes ~60s
# and emits all 533. The throwaway is deleted afterwards so the tree's real cache
# is left alone.
set -e
cd "$(dirname "$0")/.."

CACHE=$(mktemp -d)
trap 'rm -rf "$CACHE"' EXIT INT TERM

rm -rf .pio/build/x4pro
PLATFORMIO_BUILD_CACHE_DIR="$CACHE" \
PLATFORMIO_BUILD_FLAGS="-fstack-usage -fcallgraph-info=su" \
  pio run -e x4pro

# The objects left behind have no counterpart in the tree's own cache, so the
# next ordinary `pio run` recompiles them. That is the price of a real
# measurement, and it is paid once per run of this script rather than silently.
exec python3 scripts_local/stack_budget.py --build-dir .pio/build/x4pro "$@"
