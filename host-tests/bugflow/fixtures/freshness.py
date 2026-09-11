#!/usr/bin/env python3
"""board.py's staleness check: <board.py> <repo> <trunk ref> [--through-main]

Default prints "<commits missing> <hours old> <the warning, or ->".

`--through-main` instead drives main() with an argument the CLI does not know
and prints its stderr. That path is the whole point of the check and it is the
one a direct call to stale_warning() cannot reach: the incident was `board list
--reporter user` against a CLI 332 commits behind, argparse answers an
unrecognised argument with sys.exit(2) from inside parse_args, and a check
placed after parse_args therefore prints nothing in exactly the case it exists
for.

Separate from run.sh because the thing under test compares against a git ref
and the suite has to choose that ref: the real one is origin/xteink, and
asserting against it would make the result a fact about when this tree was
last pulled.
"""

import importlib.util
import io
import contextlib
import pathlib
import sys

spec = importlib.util.spec_from_file_location("board", sys.argv[1])
board = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board)

repo, trunk = sys.argv[2], sys.argv[3]

if "--through-main" in sys.argv[4:]:
    board.cli_repo = lambda: pathlib.Path(repo)
    board.TRUNK = trunk
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        try:
            board.main(["list", "--reporter", "user", "--no-such-flag"])
        except SystemExit:
            pass
    sys.stdout.write(err.getvalue())
else:
    n, oldest = board.missing_commits(repo, trunk)
    print(n, int(oldest), board.stale_warning(repo, trunk) or "-")
