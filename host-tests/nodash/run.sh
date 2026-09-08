#!/bin/sh
# No em-dashes in anything this fork writes.
#
# Mario's standing rule, in his global CLAUDE.md: the unicode em-dash never
# appears in prose, docs, comments, commit messages or anything else with his
# name on it. Use a period, comma, colon, semicolon or parentheses. Double
# hyphens are fine and the codebase uses them everywhere.
#
# It was a remembered rule and got broken repeatedly, most expensively in a
# public pull request comment posted under his GitHub identity on 2026-09-07.
# A remembered rule is not a rule, so this is the gate.
#
# Scoped to FORK-OWNED files. 149 upstream-owned files carry em-dashes and are
# not ours to rewrite; a gate that flagged those would go red on every sync and
# be switched off inside a week.
#
#   host-tests/nodash/run.sh
set -e
cd "$(dirname "$0")"
python3 check_nodash.py
