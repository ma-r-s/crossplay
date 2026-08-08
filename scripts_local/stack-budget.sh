#!/bin/sh
# Prove every task's deepest call path fits the stack it was given.
#
#   ./scripts_local/stack-budget.sh            # build if needed, then check
#   ./scripts_local/stack-budget.sh --verbose  # print the deepest path too
#
# Runs from inside its own tree, like the other scripts_local/ entries, so it
# measures this worktree's code rather than firmware-next's.
#
# The instrumented objects are kept in their own build dir. Sharing .pio/build/
# x4pro with a normal build means the next `pio run` silently reuses objects
# compiled with different flags, and the flags are the entire point here.
set -e
cd "$(dirname "$0")/.."

BUILD_DIR=".pio/build/x4pro-stack"

# PlatformIO's build cache is keyed on content, so a rebuild with new flags is
# not enough on its own; a separate build dir is what keeps the two apart.
PLATFORMIO_BUILD_FLAGS="-fstack-usage -fcallgraph-info=su" \
PLATFORMIO_BUILD_DIR=".pio/build" \
  pio run -e x4pro --build-dir "$BUILD_DIR" >/dev/null 2>&1 ||
  PLATFORMIO_BUILD_FLAGS="-fstack-usage -fcallgraph-info=su" pio run -e x4pro

# Fall back to the default location when --build-dir is not supported by this
# PlatformIO, so the check still runs rather than reporting an empty graph.
[ -d "$BUILD_DIR" ] || BUILD_DIR=".pio/build/x4pro"

exec python3 scripts_local/stack_budget.py --build-dir "$BUILD_DIR" "$@"
