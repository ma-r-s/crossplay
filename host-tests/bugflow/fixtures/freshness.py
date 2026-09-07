#!/usr/bin/env python3
"""Drive board.py's staleness check directly: <board.py> <repo> <trunk ref>.

Prints "<commits missing> <hours old> <the warning, or ->". It is a separate
file rather than a heredoc inside run.sh because the thing under test is a
comparison against a git ref, and the suite needs to choose that ref: the
real one is origin/xteink, and asserting against it would make the result a
fact about when this tree was last pulled.
"""

import importlib.util
import sys

spec = importlib.util.spec_from_file_location("board", sys.argv[1])
board = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board)

repo, trunk = sys.argv[2], sys.argv[3]
n, oldest = board.missing_commits(repo, trunk)
print(n, int(oldest), board.stale_warning(repo, trunk) or "-")
