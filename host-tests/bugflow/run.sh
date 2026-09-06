#!/bin/bash
# The hooks that make the workspace rules physical, and the board they read.
#
# Every rule below is asserted in both directions: the thing that must be
# refused is refused, and the thing that must be allowed is allowed. A guard
# that blocks everything passes a one-sided test as easily as a guard that
# blocks nothing, and this suite exists because the previous enforcement was
# prose that nobody could test at all.
#
#   host-tests/bugflow/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GUARD="$HERE/../../scripts_local/hooks/guard.py"
BOARD="$HERE/../../tools_local/board/board.py"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$GUARD" ] || { echo "FAIL cannot find $GUARD"; exit 1; }
[ -f "$BOARD" ] || { echo "FAIL cannot find $BOARD"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }

# A fake workspace: the two directories the root is recognised by, a worker
# tree, and an armed board. Nothing here touches the real .board.
ROOT="$WORK/ws"
mkdir -p "$ROOT/firmware-next/src" "$ROOT/wt/x/src" "$ROOT/.board"
export BOARD_ROOT="$ROOT"
board() { python3 "$BOARD" "$@"; }

WORKER="aaaa-worker"; ORCH="bbbb-orch"; INTEG="cccc-integ"

# guard <mode> <json>  -> prints the exit code
guard() { printf '%s' "$2" | python3 "$GUARD" "$1" >"$WORK/out" 2>"$WORK/err"; echo $?; }
expect() { # expect <label> <want-exit> <mode> <json>
  local got; got=$(guard "$3" "$4")
  if [ "$got" = "$2" ]; then ok "$1"; else bad "$1 (exit $got, wanted $2; stderr: $(head -c 160 "$WORK/err"))"; fi
}

echo "hooks are inert until armed"
expect "unarmed: firmware-next edit passes" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/src/a.cpp\"}}"
touch "$ROOT/.board/enabled"
board init >/dev/null
board orchestrator --name Main --session "$ORCH" >/dev/null
board integrator --session "$INTEG" >/dev/null

echo "the guard fails open on its own trouble"
printf 'not json' | python3 "$GUARD" pretool >/dev/null 2>&1; [ $? -eq 0 ] && ok "unreadable input is no opinion" || bad "unreadable input blocked"
printf '{"session_id":"x","tool_name":"Bash","tool_input":{"command":"ls"}}' | BOARD_ROOT=/nonexistent python3 "$GUARD" pretool >/dev/null 2>&1; [ $? -eq 0 ] && ok "a missing board is no opinion" || bad "a missing board blocked"

board pulse 2>&1 | grep -q "needs the Supabase store" && ok "pulse on the file store says what it needs" || bad "pulse on the file store did not explain itself"
echo "the integration tree"
expect "worker edit in firmware-next refused"   2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/src/a.cpp\"}}"
grep -q "integrator --session $WORKER" "$WORK/err" && ok "the refusal carries the remedy with the session id filled in" || bad "refusal lacks the substituted remedy: $(head -c 200 "$WORK/err")"
expect "worker write in firmware-next refused"  2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/docs/x.md\"}}"
expect "integrator edit in firmware-next allowed" 0 pretool "{\"session_id\":\"$INTEG\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/src/a.cpp\"}}"
expect "worker edit in its own tree allowed"    0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/wt/x/src/a.cpp\"}}"
expect "worker bash write into firmware-next refused" 2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && git merge app/x\"}}"
expect "worker bash read of firmware-next allowed"    0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"git -C $ROOT/firmware-next log --oneline -5\"}}"
expect "reading the tree with 2>&1 is not a write"  0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && git fetch -q origin 2>&1 | tail -3\"}}"
expect "git log -C the tree to /dev/null is fine"   0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"git -C $ROOT/firmware-next log --oneline -5 >/dev/null 2>&1\"}}"
expect "a redirect into the tree is a write"        2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"echo x > $ROOT/firmware-next/docs/x.md\"}}"
expect "a relative redirect after cd into the tree is a write" 2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && cat a > docs/x.md\"}}"
expect "cp into the tree is a write"                2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cp /tmp/a.h $ROOT/firmware-next/src/a.h\"}}"
expect "a heredoc that merely mentions the tree is data" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"python3 - <<'EOF'\\ncmd = 'cd firmware-next && git merge app/x'\\nprint(cmd)\\nEOF\"}}"
expect "cd out of the tree ends the tree context"   0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && git status && cd $ROOT/wt/x && git commit -am x\"}}"
expect "integrator bash merge allowed"          0 pretool "{\"session_id\":\"$INTEG\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && git merge app/x\"}}"

echo "the build lock"
expect "grep -ln on the tree is a read, not ln"     0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"grep -ln schedule: $ROOT/firmware-next/.github/workflows/x.yml\"}}"
expect "a quoted 'sed -i' pattern is a read"         0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"grep -n \\\"sed -i\\\" $ROOT/firmware-next/scripts/x.sh\"}}"
expect "sed -i with a quoted tree path is a write"   2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"sed -i '' \\\"$ROOT/firmware-next/src/a.cpp\\\"\"}}"
expect "raw pio run refused"                    2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd wt/x && pio run -e x4pro\"}}"
expect "check.sh allowed"                       0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd wt/x && ./scripts_local/check.sh --tests\"}}"
expect "pio in a word is not pio run"           0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"grep -rn 'pio run' docs\"}}"

echo "quotes are stripped before the command is split"
bashjson() { python3 -c 'import json,sys; print(json.dumps({"session_id": sys.argv[1], "tool_name": "Bash", "tool_input": {"command": sys.argv[2]}}))' "$WORKER" "$1"; }
expect "a pipe inside quotes does not cut the quotes"     0 pretool "$(bashjson "cd $ROOT/firmware-next && echo \"in: \$(git tag --contains abc | tr '\\n' ' ')\"")"
expect "git tag --contains is a read"                     0 pretool "$(bashjson "cd $ROOT/firmware-next && git tag --contains abc")"
expect "git tag <name> is still a write"                  2 pretool "$(bashjson "cd $ROOT/firmware-next && git tag v9")"
expect "a quoted redirect target in the tree is a write"  2 pretool "$(bashjson "echo x > \"$ROOT/firmware-next/docs/x.md\"")"

echo "who may talk to whom"
expect "worker to orchestrator allowed"         0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"Main\",\"message\":\"blocked\"}}"
expect "worker to orchestrator with ref allowed" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"Main [1a2b3c]\",\"message\":\"blocked\"}}"
expect "worker to a peer refused"               2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"xteink-ff\",\"message\":\"who owns 1.12.5\"}}"
expect "worker to a peer via the app refused"   2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_dddd-peer\",\"message\":\"hi\"}}"
expect "worker to its own subagent allowed"        0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"aaab5d61709dbe8a4\",\"message\":\"apply the review\"}}"
expect "worker to the orchestrator's app id, unregistered, refused" 2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_bbbb-app\",\"message\":\"hi\"}}"
board orchestrator --name Main --session "$ORCH" --app-id local_bbbb-app >/dev/null
expect "worker to the orchestrator's app id, registered, allowed" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_bbbb-app\",\"message\":\"hi\"}}"
board orchestrator --name Main --session "bbbb-orch-restarted" >/dev/null
expect "a re-registration without --app-id keeps the app id" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_bbbb-app\",\"message\":\"hi\"}}"
board orchestrator --name Main --session "$ORCH" --app-id local_bbbb-app >/dev/null
expect "the orchestrator is still known by its hook id" 0 pretool "{\"session_id\":\"$ORCH\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"xteink-ff\",\"message\":\"card #3 is yours\"}}"
expect "worker to orchestrator via the app allowed" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_$ORCH\",\"message\":\"hi\"}}"
DISP="dddd-dispatch"; board dispatcher --name Dispatch --session "$DISP" >/dev/null
expect "the dispatcher may message an owner"      0 pretool "{\"session_id\":\"$DISP\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"xteink-ff\",\"message\":\"card #3 is yours\"}}"
expect "the dispatcher may still not ask Mario itself" 2 pretool "{\"session_id\":\"$DISP\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"board ask 3 --ask 'ship?' --default hold\"}}"
expect "a heredoc mentioning the ask verb is data"  0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"python3 - <<'EOF'\\nprint('board ask 3')\\nEOF\"}}"
expect "orchestrator to anyone allowed"         0 pretool "{\"session_id\":\"$ORCH\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"xteink-ff\",\"message\":\"take it\"}}"
expect "worker asking Mario refused"            2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"board ask 3 --ask 'ship?' --default hold\"}}"
expect "orchestrator asking Mario allowed"      0 pretool "{\"session_id\":\"$ORCH\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"board ask 3 --ask 'ship?' --default hold\"}}"

echo "two open pull requests that touch one file"
OVERLAP="$ROOT/tools_local/board/overlap.py"
[ -f "$OVERLAP" ] || OVERLAP="$(dirname "$BOARD")/overlap.py"
python3 "$OVERLAP" --from-json "$HERE/fixtures/prs.json" >"$WORK/overlap.out" 2>&1
grep -q "#10 (app/render) and #11 (app/browser) both touch:" "$WORK/overlap.out" && ok "the overlapping pair is named with its branches" || bad "overlap: pair not named: $(cat "$WORK/overlap.out")"
grep -q "    src/apps_local/link/LinkPlay.cpp" "$WORK/overlap.out" && ok "and the shared file" || bad "overlap: shared file missing"
grep -q "crossplay.wasm" "$WORK/overlap.out" && bad "overlap: CI's emulator artefact counted as a shared file" || ok "the emulator artefact both carry is not an overlap"
grep -q "#12" "$WORK/overlap.out" && bad "overlap: a pull request sharing nothing was named" || ok "a pull request sharing nothing is not named"
grep -q "1 overlapping pair(s) among 3 open" "$WORK/overlap.out" && ok "the count is right" || bad "overlap: count line wrong: $(tail -1 "$WORK/overlap.out")"
printf '[]' >"$WORK/none.json"; python3 "$OVERLAP" --from-json "$WORK/none.json" | grep -q "no two touch the same file" && ok "no pull requests is said plainly" || bad "overlap: empty input not handled"

echo "ending a turn"
T="$WORK/transcript.jsonl"
mk_transcript() { # mk_transcript "<last assistant text>"
  printf '{"type":"user","message":{"content":"go"}}\n' > "$T"
  printf '{"type":"assistant","message":{"content":[{"type":"text","text":%s}]}}\n' "$(python3 -c 'import json,sys;print(json.dumps(sys.argv[1]))' "$1")" >> "$T"
}
mk_transcript "Fixed and gated. Want me to also port the picker fix, or leave it?"
expect "hand-back with no card refused"          2 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"
[ -s "$ROOT/.board/refusals.log" ] && grep -q " $WORKER Bash " "$ROOT/.board/refusals.log" && grep -q " $WORKER stop " "$ROOT/.board/refusals.log" && ok "every refusal leaves a line in refusals.log with the session and the tool" || bad "refusals.log is missing a line for a Bash or a Stop refusal"
grep -q "board.py bind" "$WORK/err" && ok "refusal tells it to bind" || bad "refusal does not tell it to bind"
expect "stop_hook_active never loops"            0 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":true}"
expect "the dispatcher may end on its one question"  0 stop "{\"session_id\":\"$DISP\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"
expect "orchestrator may hand back"              0 stop "{\"session_id\":\"$ORCH\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"
mk_transcript "Fixed, gated, pushed. PR open; card moved to review."
expect "a finished turn passes"                  0 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"

CID=$(board new "Sudoku loses the puzzle from the difficulty menu" --from sudoku --kind bug | sed 's/^#\([0-9]*\).*/\1/')
board bind "$CID" --session "$WORKER" --tree wt/x --branch app/x >/dev/null
echo "a tree another session holds refuses writes"
expect "another session editing wt/x is refused"        2 pretool "{\"session_id\":\"other-session\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/wt/x/src/a.cpp\"}}"
grep -q "wt/x is bound to session $WORKER (card #$CID)" "$WORK/err" && ok "the refusal names the tree, its holder and the card" || bad "refusal lacks the holder: $(head -c 200 "$WORK/err")"
grep -q "wt.sh new" "$WORK/err" && ok "and says to cut a tree of its own" || bad "refusal lacks the remedy"
expect "the holder still edits its tree"                0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/wt/x/src/a.cpp\"}}"
expect "the orchestrator may edit any tree"             0 pretool "{\"session_id\":\"$ORCH\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/wt/x/src/a.cpp\"}}"
expect "an unbound tree is anyone's"                    0 pretool "{\"session_id\":\"other-session\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/wt/free/src/a.cpp\"}}"
expect "a write from inside the tree by another session is refused" 2 pretool "{\"session_id\":\"other-session\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"sed -i '' src/a.cpp\"},\"cwd\":\"$ROOT/wt/x\"}"
expect "a read from inside the tree by another session is fine"     0 pretool "{\"session_id\":\"other-session\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"grep -rn foo src\"},\"cwd\":\"$ROOT/wt/x\"}"
expect "a write naming the tree from elsewhere is refused"          2 pretool "{\"session_id\":\"other-session\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cp /tmp/a.h $ROOT/wt/x/src/a.h\"},\"cwd\":\"$ROOT\"}"
expect "the holder writes from inside its tree"                     0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"git commit -am x\"},\"cwd\":\"$ROOT/wt/x\"}"
mk_transcript "I cannot see the panel from here. Let me know when you have flashed it."
expect "hand-back with a card but no blocker refused" 2 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"
grep -q "card #$CID" "$WORK/err" && ok "refusal names the card" || bad "refusal does not name the card"
board block "$CID" --session "$WORKER" --need desk --ask "flash and look at the door" --default "stays unverified" >/dev/null
expect "hand-back with a blocker recorded passes" 0 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"

echo "session start"
guard session-start "{\"session_id\":\"$WORKER\",\"cwd\":\"$ROOT\"}" >/dev/null
grep -q "session id is $WORKER" "$WORK/out" && ok "prints the session id" || bad "no session id printed"
grep -q "orchestrator is: Main" "$WORK/out" && ok "names the orchestrator" || bad "does not name the orchestrator"
grep -q "Your card: #$CID" "$WORK/out" && ok "names the bound card" || bad "does not name the bound card"
grep -q "worker contract" "$WORK/out" && ok "prints the contract" || bad "does not print the contract"
guard session-start "{\"session_id\":\"$ORCH\",\"cwd\":\"$ROOT\"}" >/dev/null
grep -q "ORCHESTRATOR" "$WORK/out" && ok "the orchestrator is told it is one" || bad "orchestrator not told"

echo "the board"
board inbox | grep -q "Nothing needs you" && ok "inbox empty when nothing needs Mario" || bad "inbox not empty"
board ask "$CID" --ask "Keep the latch or delete it?" --default "deleted" >/dev/null
board inbox | grep -q "Need from you: Keep the latch" && ok "an ask reaches the inbox" || bad "ask missing from inbox"
board inbox | grep -q "If you do nothing: deleted" && ok "the default is shown" || bad "default missing"
board answer "$CID" "keep" >/dev/null
board ask "$CID" --ask "Flash it and look at the door" --default "unverified" --steps "1. Flash v1.12.10 over Wi-Fi
2. Open Sudoku
3. Tap DIFFICULTY four times" >/dev/null
board inbox | grep -q "How: 2. Open Sudoku" && ok "an ask carries its steps into the inbox" || bad "steps missing from the inbox"
board show "$CID" | grep -q "how: 1. Flash" && ok "show prints the steps" || bad "show lacks steps"
board answer "$CID" "delete it" --note "less code" >/dev/null
board inbox | grep -q "Nothing needs you" && ok "an answer clears the inbox" || bad "answer did not clear"
board show "$CID" | grep -q "closed: delete it" && ok "the answer is on the card" || bad "answer not on card"
board state "$CID" review >/dev/null
board list | grep -q "review" && ok "state moves" || bad "state did not move"
printf '## Trivia: play the new build\nbody one\n\n## Hacker News: keep anything?\nbody two\n' > "$WORK/import.md"
board import "$WORK/import.md" | grep -q "imported 2 cards" && ok "import makes one card per heading" || bad "import failed"
board list | grep -q "trivia" && ok "import derives the app from the heading" || bad "app not derived"
printf '## Anki: a card with a labelled body\nYou were building: sync.\nSince then: proven end to end.\n' > "$WORK/import2.md"
board import "$WORK/import2.md" >/dev/null
board ask 4 --ask "use it once" --default "unverified" >/dev/null
board inbox | grep -q "Since: proven end to end" && ok "inbox strips the since label" || bad "inbox repeats the since label"
board inbox | grep -q "Since: Since" && bad "inbox doubled the label" || ok "no doubled label"
board owner sudoku --session "$WORKER" --tree wt/x >/dev/null
board route "$CID" | grep -q "owner session $WORKER" && ok "a card routes to its app's owner" || bad "route did not find the owner"
board route 2 | grep -q "no owner" && ok "an app with no owner says so" || bad "no-owner case wrong"
board owner sudoku | grep -q "session $WORKER" && ok "owner lookup without flags" || bad "owner lookup failed"
cat > "$WORK/issues.json" <<'JSON'
[{"number":7,"title":"sometimes Slow Reader (especially page turning)","body":"4.2 s per page turn","labels":[],"url":"https://github.com/ma-r-s/crossplay/issues/7","author":{"login":"gitlias"}},
 {"number":9,"title":"Sudoku loses my puzzle","body":"","labels":[{"name":"bug"}],"url":"https://github.com/ma-r-s/crossplay/issues/9","author":{"login":"x"}}]
JSON
board issues --from-json "$WORK/issues.json" | grep -q "2 new card" && ok "open issues become cards" || bad "issues did not become cards"
board issues --from-json "$WORK/issues.json" | grep -q "0 new card" && ok "a second sweep makes no duplicates" || bad "issues sweep duplicated cards"
board list | grep -q "reader .*Slow Reader" && ok "an issue about page turns lands on the reader" || bad "reader issue not routed to reader"

echo "what agents learn stays on the card"
board note "$CID" "repro: open the menu, press back, open it again" | grep -q "#$CID noted" && ok "a note is accepted" || bad "note refused"
board show "$CID" | grep -q "note: repro: open the menu" && ok "the note is a history line on the card" || bad "note missing from show"
board note "$CID" "Seen on unit B as well." --body >/dev/null
board show "$CID" | grep -q "Seen on unit B as well" && ok "--body appends the note to the card body" || bad "--body did not append"
board list --state review | grep -q "#$CID" && ok "list --state filters to the state" || bad "list --state missed the card"
board list --state parked | grep -q "no cards" && ok "list --state on an empty state says so" || bad "list --state parked printed cards"
if board new "Sudoku: the puzzle is lost from the difficulty menu" --from sudoku --kind bug >"$WORK/dup.out" 2>&1; then bad "a reworded duplicate was filed"; else grep -q "looks like an open card" "$WORK/dup.out" && grep -q "#$CID" "$WORK/dup.out" && ok "a reworded duplicate is stopped and the open card named" || bad "duplicate refusal lacks the card: $(cat "$WORK/dup.out")"; fi
grep -q "board note $CID" "$WORK/dup.out" && ok "the refusal says how to add to the existing card" || bad "refusal lacks the note remedy"
board new "Sudoku: the puzzle is lost from the difficulty menu" --from sudoku --kind bug --anyway | grep -q "^#" && ok "--anyway files it regardless" || bad "--anyway did not file"
board new "Chess clock drifts by a second every minute" --from chess --kind bug | grep -q "^#" && ok "a different title is filed without ceremony" || bad "an unrelated title was stopped"
board block "$CID" --session "$WORKER" --need desk --ask "Does the fix hold on unit B?" --default "ships unverified on B" >/dev/null
if board state "$CID" released >"$WORK/rel.out" 2>&1; then bad "a card with an open desk blocker was released"; else grep -q "open blocker" "$WORK/rel.out" && grep -q "Does the fix hold on unit B" "$WORK/rel.out" && ok "settling a card with an open desk blocker is refused and the blocker named" || bad "refusal lacks the blocker: $(cat "$WORK/rel.out")"; fi
board show "$CID" | grep -q "^#$CID *review" && ok "the card stayed in review" || bad "the card moved anyway"
board state "$CID" released --with-blockers | grep -q "#$CID released" && ok "--with-blockers settles it on purpose" || bad "--with-blockers refused"
board state "$CID" review >/dev/null
board list | grep -q "sudoku .*Sudoku loses my puzzle" && ok "an issue names its app from the owners" || bad "sudoku issue not routed to sudoku"

# Every assertion above hands the sweep a file, so the branch that actually
# runs in production -- shelling out to `gh` -- had never been executed by a
# test. A stub `gh` on PATH exercises it and records what it was asked.
echo "the github sweep, through gh itself"
mkdir -p "$WORK/bin"
cat > "$WORK/bin/gh" <<'SH'
#!/bin/bash
printf '%s\n' "$*" >> "$GH_LOG"
[ "$2" = "list" ] && cat "$GH_ISSUES" || echo "closed"
SH
chmod +x "$WORK/bin/gh"
export GH_LOG="$WORK/gh.log" GH_ISSUES="$WORK/live.json"
# A subshell, not a `PATH=... board ...` prefix: bash 3.2 (which is /bin/bash
# here) leaves an assignment made in front of a *function* call set afterwards,
# and the stub gh would then serve the rest of the suite.
ghboard() { ( PATH="$WORK/bin:$PATH"; python3 "$BOARD" "$@" ); }
cat > "$WORK/live.json" <<'JSON'
[{"number":31,"title":"Checkers drops the ninth capture in a chain","body":"uint8_t[3]","labels":[{"name":"bug"}],"url":"https://github.com/ma-r-s/crossplay/issues/31","author":{"login":"stranger"}}]
JSON
SWEEP=$(ghboard issues)
grep -q "issue #31" <<< "$SWEEP" && ok "a sweep with no --from-json shells out to gh" || bad "the live gh path made no card: $SWEEP"
GID=$(sed -n 's/^#\([0-9]*\) <- issue #31.*/\1/p' <<< "$SWEEP")
grep -q -- "--state open" "$GH_LOG" && ok "it asks gh for open issues only" || bad "gh was not asked for open issues: $(cat "$GH_LOG")"
ghboard issues | grep -q "0 new card" && ok "the live path dedupes on a second sweep" || bad "the live path duplicated a card"
ghboard tick | grep -q "0 new card" && ok "tick sweeps and then lists" || bad "tick did not sweep"
ghboard tick | grep -q "#$GID " && ok "tick prints the open board after the sweep" || bad "tick printed no board"

# --close-released had no test at all: the half of the flow that reaches out
# and changes something on GitHub was the untested half.
echo "a released card closes its issue"
board state "$GID" released >/dev/null
: > "$GH_LOG"
ghboard issues --close-released | grep -q "1 issue(s) closed" && ok "a released card closes its GitHub issue" || bad "close-released closed nothing"
grep -q "issue close 31 " "$GH_LOG" && ok "it closes the issue its card came from" || bad "close-released named the wrong issue: $(cat "$GH_LOG")"
grep -q -- "--comment" "$GH_LOG" && ok "the close carries a comment" || bad "the issue was closed silently"
board show "$GID" | grep -q "closed GitHub issue #31" && ok "the close is recorded on the card" || bad "the close left no history"
: > "$GH_LOG"
ghboard tick | grep -q "close 31" && bad "tick closed an issue" || ok "tick never closes anything"
board state "$GID" triaged >/dev/null

PID=$(board new "Analytics everywhere" --from tooling | sed 's/^#\([0-9]*\).*/\1/')
KID=$(board new "Firmware heartbeat" --from firmware --parent "$PID" | sed 's/^#\([0-9]*\).*/\1/')
board parent "$CID" --of "$PID" >/dev/null
board list | grep -q "^    #$KID " && ok "a child lists indented under its parent" || bad "child not indented"
board list | grep -q "^    #$CID " && ok "board parent moves an existing card under one" || bad "parent command failed"
board show "$PID" | grep -q "#$KID " && ok "show lists the children" || bad "show lacks children"
board parent "$PID" --of "$PID" >/dev/null 2>&1 && bad "a card became its own parent" || ok "a card cannot be its own parent"
board integrator --session "$WORKER" >/dev/null 2>&1 && bad "a second integrator claim succeeded" || ok "a held integration claim refuses a second claimant"
board integrator --session "$WORKER" --release >/dev/null 2>&1 && bad "a stranger released the claim" || ok "only the holder releases the claim"
board integrator --session "$INTEG" --release >/dev/null && ok "the holder releases the claim" || bad "holder cannot release"

# Mario reads his inbox and nothing else, the inbox is the open `mario`
# blockers and nothing else, and a card is not a blocker. So a card filed on
# app `mario` -- the app that by convention already means "only Mario can
# decide this" -- reached him only if somebody also remembered to block on it.
# Twice nobody did: cards 75 and 84 were his decisions and aged a day in
# `reported` while his inbox said nothing needs you. Card #209 made the rule
# physical, and this is where it is watched holding.
echo "a card addressed to Mario is an inbox item by construction"
MID=$(board new "Retire Main and open a fresh orchestrator" --from mario | sed 's/^#\([0-9]*\).*/\1/')
board inbox | grep -q "Need from you: Retire Main and open a fresh orchestrator" \
  && ok "a card filed on app mario reaches the inbox, asking its title" || bad "a card filed on app mario never reached the inbox"
board inbox | grep -q "If you do nothing: nothing happens until he answers" \
  && ok "the blocker it opens says what happens if he never answers" || bad "the auto blocker states no default"
board new "Archive the four dead apps" --from mario --default "they stay on the shelf" >/dev/null
board inbox | grep -q "If you do nothing: they stay on the shelf" \
  && ok "a filer-supplied default wins over the honest fallback" || bad "the filer's default was dropped"
OID=$(board new "Sudoku keeps its own puzzle" --from sudoku | sed 's/^#\([0-9]*\).*/\1/')
board show "$OID" | grep -q "BLOCKED(mario)" && bad "a card on another app opened a mario blocker" || ok "only app mario opens one"

# Moved there, not only filed there: the orchestrator retargets cards, and a
# decision that becomes Mario's on Tuesday is as invisible as one that was his
# on Monday.
board app "$OID" mario --default "the puzzle stays where it is" >/dev/null
board inbox | grep -q "Need from you: Sudoku keeps its own puzzle" \
  && ok "moving a card to app mario reaches the inbox" || bad "a move to app mario never reached the inbox"
board show "$OID" | grep -q "moved to app mario" && ok "the move is on the card" || bad "the move left no history"
board app "$OID" mario >/dev/null
board app "$OID" mario >/dev/null
[ "$(board show "$OID" | grep -c '^  blocker ')" = 1 ] \
  && ok "moving it there again files no second blocker" || bad "repeated moves multiplied the blocker"
board inbox | grep -q "If you do nothing: the puzzle stays where it is" \
  && ok "a repeat move keeps the default the filer gave" || bad "a repeat move overwrote the default"
board app "$OID" sudoku >/dev/null
board app "$OID" mario >/dev/null
[ "$(board show "$OID" | grep -c '^  blocker ')" = 1 ] \
  && ok "a round trip through another app files no second blocker" || bad "a round trip multiplied the blocker"
board answer "$MID" "retire it" >/dev/null
board show "$MID" | grep -q "closed: retire it" && ok "he answers the auto blocker like any other" || bad "the auto blocker cannot be answered"

# A decision already taken is not one to ask again. The rule and the backfill
# in the migration have to agree about this, or a board restored by INSERTing
# a dump opens one blocker per settled decision it ever held.
DID=$(board new "A decision he already took" --from tooling | sed 's/^#\([0-9]*\).*/\1/')
board state "$DID" done >/dev/null
board app "$DID" mario >/dev/null
board show "$DID" | grep -q "BLOCKED(mario)" && bad "a done card was put back in the inbox" || ok "a settled card moved to app mario opens nothing"
board new "A decision long since parked" --from mario >/dev/null
SID2=$(board list | grep "A decision long since parked" | sed 's/^#\([0-9]*\).*/\1/')
board state "$SID2" parked >/dev/null

# The words the filer typed must not vanish in silence. This is the one case
# where they cannot be used: the card is already asking him something else, and
# overwriting THAT blocker's default would be worse than not applying these.
EID=$(board new "Archive the empty duplicate" --from tooling | sed 's/^#\([0-9]*\).*/\1/')
board block "$EID" --session "$WORKER" --need mario --ask "Archive it or keep it?" --default "it stays" >/dev/null
board app "$EID" mario --default "THESE WORDS SHOULD MATTER" 2>&1 | grep -q -- "--default not applied" \
  && ok "a --default that cannot be used says so out loud" || bad "a --default was dropped in silence"
board show "$EID" | grep -q "if nothing: it stays" \
  && ok "and the blocker already there keeps its own words" || bad "the existing blocker's default was overwritten"

# Two open mario blockers on one card. Rare before this rule; routine once
# every card on app mario carries one of its own. The inbox prints two lines,
# and an answer typed against one of them must not land on the other -- which
# is what `board answer` did, silently, by keeping the last match.
FID=$(board new "Wavelength retail deck" --from mario --default "the deck ships" | sed 's/^#\([0-9]*\).*/\1/')
board block "$FID" --session "$WORKER" --need mario --ask "Do we have permission for the retail deck?" --default "we assume not" >/dev/null
board inbox | grep -q -- "board answer $FID '<choice>' --n 2" \
  && ok "the inbox names the blocker in the command it prints" || bad "the inbox prints an ambiguous answer command"
# Into a file, not a pipe: an ambiguous answer is REFUSED, so `board` exits 1,
# and under `set -o pipefail` that non-zero status is the pipeline's however
# well grep matched. A refusal read as a missing message is the one shape this
# assertion must not have.
board answer "$FID" "yes" > "$WORK/amb" 2>&1
grep -q "say which with --n" "$WORK/amb" \
  && ok "an ambiguous answer is refused rather than guessed" || bad "an ambiguous answer picked one silently: $(head -c 120 "$WORK/amb")"
board answer "$FID" "we have it" --n 2 >/dev/null
board show "$FID" | grep -q "blocker 2 \[mario, closed: we have it\]" \
  && ok "--n answers the blocker it names" || bad "--n answered the wrong blocker"
board show "$FID" | grep -q "blocker 1 \[mario, open\]" \
  && ok "and leaves the other one open" || bad "--n closed a blocker it did not name"

# Moving a card off his desk does not withdraw what he was asked: taking an
# item out of his inbox with no answer is the dropped message this whole rule
# is about. It is said out loud and left for a person.
board app "$FID" tooling 2>&1 | grep -q "still in Mario.s inbox" \
  && ok "moving a card off app mario says what it leaves in his inbox" || bad "a card left the app and its inbox item went unmentioned"
board inbox | grep -q "Need from you: Wavelength retail deck" \
  && ok "and does not silently withdraw it" || bad "the move withdrew an unanswered question"

# Filed by heading, and by the GitHub sweep: same rule, same wording.
printf '## mario: Which of the three layouts ships\nbody\n' > "$WORK/import3.md"
board import "$WORK/import3.md" >/dev/null
board inbox | grep -q "Need from you: Which of the three layouts ships" \
  && ok "an imported card on app mario reaches the inbox" || bad "import skipped the rule"
board new "Capital App" --from MARIO >/dev/null
board list | grep -q "^#[0-9]* *reported *mario *Capital App" \
  && ok "the app name is lowercased on the way in" || bad "board new stored a mixed-case app the SQL trigger would miss"

# Filing on app mario is not the orchestrator-only `board ask`: a worker
# already records `--need mario` blockers on its own card by the contract, so
# the same worker may file the decision as a card. The gate that stays shut is
# `board ask`, asserted above.
expect "a worker may file a card on app mario" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"board new 'Which layout ships' --from mario\"}}"

echo "emulator staleness, one answer for check.sh and CI"
STALE="$HERE/../../scripts_local/emulator-stale.sh"
E="$WORK/emu"; mkdir -p "$E/src" "$E/site/emulator"; ( cd "$E" && git init -q -b xteink && git config user.email t@t && git config user.name t \
  && echo a > src/a.cpp && echo w > site/emulator/crossplay.wasm && git add -A && git commit -qm "both" \
  && sleep 1 && echo b > src/a.cpp && git add -A && git commit -qm "source moved" )
bash "$STALE" "$E" >/dev/null && ok "a newer source makes the emulator stale" || bad "stale not detected"
( cd "$E" && sleep 1 && echo w2 > site/emulator/crossplay.wasm && git add -A && git commit -qm "chore: emulator rebuilt" )
bash "$STALE" "$E" >/dev/null && bad "a rebuilt emulator still reads stale" || ok "a rebuilt emulator reads fresh"
# A herestring, not a pipe. Under `set -o pipefail`, `grep -q` exits on its
# first match and the producer's remaining write gets EPIPE, so the pipeline
# returns 141 and an intact list reads as a missing one. It only loses that
# race when the machine is busy, which is exactly inside check.sh. A
# herestring has no producer process to kill, and keeps the -x exact match.
PATHS="$(bash "$STALE" --paths)"
grep -qx "src" <<< "$PATHS" && grep -qx "tools_local/wasm" <<< "$PATHS" \
  && ok "--paths names the source list" || bad "--paths is missing sources"
grep -q 'emulator-stale.sh' "$HERE/../../scripts_local/check.sh" && ok "check.sh asks the shared script" || bad "check.sh still carries its own staleness test"

# The artefact is now a POINTER, not the bytes. crossplay-emulator.yml publishes
# the wasm to a GitHub release and commits site/emulator-manifest.json; the bytes
# under site/emulator/ are frozen at the last revision that was ever committed
# and never move again. A staleness test that only watched the directory would
# therefore call every rebuild stale forever, and check.sh fails on stale on the
# deploy branch -- a permanently red gate on the branch that matters most.
( cd "$E" && sleep 1 && echo c > src/a.cpp && git add -A && git commit -qm "source moved again" )
bash "$STALE" "$E" >/dev/null && ok "a source change after a rebuild reads stale again" || bad "stale not detected after a rebuild"
( cd "$E" && sleep 1 && echo '{"files":[]}' > site/emulator-manifest.json && git add -A && git commit -qm "chore: emulator rebuilt" )
bash "$STALE" "$E" >/dev/null && bad "a manifest-only rebuild still reads stale, so the deploy branch's gate is permanently red" || ok "a manifest-only rebuild reads fresh"

echo "the emulator rebuild's commit subject, spelled in two workflows"
# crossplay-autorelease.yml tells an emulator rebuild from a real merge that
# moved the tip past what CI verified by MATCHING THE SUBJECT. Reword it in
# crossplay-emulator.yml and every release silently stops: the gate decides the
# tip moved, declines, and says so in a log nobody reads. Nothing links the two
# files, so this is the link.
AR="$HERE/../../.github/workflows/crossplay-autorelease.yml"
EM="$HERE/../../.github/workflows/crossplay-emulator.yml"
# COMMENT LINES DROPPED FIRST. A subject match that has been replaced by
# something better is usually left in the file as the comment explaining what it
# replaced, and a check that reads it there goes on passing while guarding a
# dead line -- which is worse than going red, because it looks like coverage.
subject="$(grep -vE '^[[:space:]]*#' "$AR" | grep -oE "grep -vq '\^[^']+'" | sed -E "s/.*'\^//; s/'$//")"
if [ -z "$subject" ]; then
  # Not a failure. It means the gate stopped settling a question about content
  # by reading a title, which is the right direction; there is then no subject
  # for crossplay-emulator.yml to keep in step with.
  ok "crossplay-autorelease.yml no longer keys off the commit subject, so there is nothing here to keep in step"
else
  ok "autorelease matches the subject '$subject'"
  grep -q -- "-m \"$subject" "$EM" \
    && ok "crossplay-emulator.yml still commits under that subject" \
    || bad "crossplay-emulator.yml's commit subject no longer starts with '$subject', so autorelease will read every rebuild as a moved tip and stop releasing"
fi

echo "what the rebuild commits, against what CI ignores"
# crossplay-ci.yml's paths-ignore exists so the rebuild's own push does not
# start a second CrossPlay run -- one that CANCELS the merge's run and takes
# the autorelease with it, because a cancelled run is not a success. That cost
# two full builds per merge on 2026-09-04. The filter names paths; the rebuild
# picks them. Nothing links the two, and the failure is a doubled build and a
# skipped release, neither of which says why.
CI="$HERE/../../.github/workflows/crossplay-ci.yml"
added="$(grep -oE '^ *git add [^|&;]+' "$EM" | sed -E 's/^ *git add //' | tr -s ' ' '\n' | grep -v '^$' | sort -u)"
if [ -z "$added" ]; then
  bad "crossplay-emulator.yml stages nothing; the check cannot tell what CI must ignore"
else
  ok "the rebuild stages: $(printf '%s' "$added" | tr '\n' ' ')"
  ignored="$(sed -n '/paths-ignore:/,/^  [a-z_]*:/p' "$CI" | grep -oE "'[^']+'" | tr -d "'")"
  for path in $added; do
    match=no
    for pat in $ignored; do
      case "$path" in ${pat%/\*\*}|${pat%/\*\*}/*|$pat) match=yes;; esac
    done
    [ "$match" = yes ] \
      && ok "crossplay-ci.yml ignores $path" \
      || bad "crossplay-emulator.yml commits $path and crossplay-ci.yml's paths-ignore does not cover it, so every rebuild starts a second CI run that cancels the merge's own and skips the release"
  done
fi

echo "the shared scratchpad"
# Card #314. The agent scratchpad is described as session-specific and is not:
# several agents run under one session id, and every one of them independently
# reaches for gate.log, pr.md, out.txt, check.log. Three runs were corrupted in
# one evening and one reached GitHub -- an agent wrote its pull request body to
# scratchpad/pr.md, another session overwrote that exact path, and the first
# pushed the second's text into PR #117.
#
# A convention cannot fix this, because the failure mode IS every agent
# independently choosing the same obvious name. So the flat top level is
# refused and the refusal names the subdirectory to use instead. Asserted in
# both directions throughout: a guard that refuses the whole scratchpad would
# pass a one-sided test exactly as well, and would make the remedy unusable.
SP="$WORK/scratchpad"
mkdir -p "$SP/x"
WT="$ROOT/wt/x"

expect "a write to the flat scratchpad root is refused" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"$SP/pr.md\"}}"
grep -q "$SP/x" "$WORK/err" \
  && ok "the refusal names this tree's own subdirectory" \
  || bad "refusal does not name the namespaced path: $(head -c 200 "$WORK/err")"
grep -q "pr.md" "$WORK/err" \
  && ok "the refusal keeps the filename the agent chose" \
  || bad "refusal drops the filename, so the remedy has to be reconstructed"

expect "a write INSIDE the namespaced subdirectory is allowed" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"$SP/x/pr.md\"}}"
expect "and so is anything deeper" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"$SP/x/notes/pr.md\"}}"
expect "an Edit of the flat root is refused too" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$SP/gate.log\"}}"

# The three incidents were all shell redirects, not Write calls: the gate was
# backgrounded with `> scratchpad/gate.log`, and the PR body was a heredoc.
expect "a redirect into the flat root is refused" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"./scripts_local/check.sh --committed > $SP/gate.log 2>&1\"}}"
expect "a tee into the flat root is refused" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"./scripts_local/check.sh | tee $SP/out.txt\"}}"
expect "a cd into the scratchpad then a heredoc is refused" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $SP && cat > pr.md\"}}"
expect "a redirect into the namespaced subdirectory is allowed" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"./scripts_local/check.sh --committed > $SP/x/gate.log 2>&1\"}}"
# A redirect that sits AFTER the `<<` on a heredoc's opening line. Dropping the
# whole construct as data -- which is what the firmware-next guard does -- loses
# exactly this, and it is one of the two spellings an agent writes a PR body in.
expect "a redirect on a heredoc's opening line is refused" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"python3 - <<'PY' > $SP/out.json\\nprint(1)\\nPY\"}}"
# ...and the body itself stays data. A path named inside a heredoc is text being
# written, not a file being opened, and refusing it would make the guard fire on
# documents that merely describe the rule.
expect "a path named inside a heredoc body is not a write" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cat <<'EOF'\\nnever write to $SP/gate.log\\nEOF\"}}"

# The other direction, which is the half a blocking guard passes for free.
expect "an ordinary redirect in the tree is untouched" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"ls > $WT/out.txt\"}}"
expect "> /dev/null is not a write into anything" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"./scripts_local/check.sh > /dev/null 2>&1\"}}"
expect "reading a scratchpad path is not a write" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"grep -c CHECKSH-VERDICT $SP/gate.log\"}}"
expect "a repo path that merely contains the word is untouched" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"$WT/src/scratchpad_notes.md\"}}"

# The namespace has to come from something that actually differs between the
# colliding agents. It is the worktree, because one card, one branch, one
# worktree is the workflow's own rule -- and because on 2026-09-05 the working
# directory was the ONLY thing that told four concurrently running gates apart.
guard session-start "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\"}" >/dev/null
grep -q "scratchpad is SHARED" "$WORK/out" \
  && ok "session start says the scratchpad is shared" \
  || bad "session start does not warn that the scratchpad is shared"
grep -q "<scratchpad>/x/" "$WORK/out" \
  && ok "session start names this tree's subdirectory" \
  || bad "session start does not name the subdirectory: $(grep -i scratchpad "$WORK/out" | head -2)"

# A session with no worktree still gets a name of its own rather than sharing a
# fallback with every other one. Asserted on the NAME, never on "<scratchpad>/":
# that prefix is constant text in the message and matches with the namespace
# empty, which is the one outcome this has to catch.
guard session-start "{\"session_id\":\"$ORCH\",\"cwd\":\"$ROOT\"}" >/dev/null
grep -qE "<scratchpad>/[A-Za-z0-9_.-]+/" "$WORK/out" \
  && ok "a session outside any worktree still gets a namespace of its own" \
  || bad "a session outside a worktree got an EMPTY namespace: $(grep -o '<scratchpad>[^ ]*' "$WORK/out")"

# The two directions that matter for the guard's false-positive risk: a `>`
# inside a quoted string is TEXT. writes_into_tree refused four read-only
# commands in 2026-09 for exactly this, and a wrong refusal here blocks every
# session in the workspace, which is worse than a missed write.
expect "a > inside a quoted grep pattern is not a redirect" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"git log --grep='check.sh > $SP/gate.log'\"}}"
expect "a > inside an echo argument is not a redirect either" 0 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"echo 'never write to $SP/gate.log' >> $WT/notes.md\"}}"
# ...but a QUOTED target is still a target. Deleting quoted strings outright --
# which is how the firmware-next guard solves the same problem -- would lose
# this, and quoting a path is the ordinary way to write one.
expect "a quoted scratchpad target is still refused" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"./scripts_local/check.sh > '$SP/gate.log' 2>&1\"}}"
# A subshell or a brace group is the same cd.
expect "a cd inside a subshell carries" 2 pretool \
  "{\"session_id\":\"$WORKER\",\"cwd\":\"$WT\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"(cd $SP && cat > pr.md)\"}}"
grep -q "$SP/pr.md is at the top" "$WORK/err" \
  && ok "and the refusal prints the path without the shell's closing paren" \
  || bad "the refusal named a path the reader cannot paste: $(grep -o "$SP[^ ]*" "$WORK/err" | head -1)"

echo
echo "a held card is not bound twice"
CID2=$(board new "Sudoku: a second card that wants the same tree" --from sudoku --kind task --anyway | sed 's/^#\([0-9]*\).*/\1/')
if board bind "$CID2" --session "other-session" --tree wt/x >"$WORK/tree.out" 2>&1; then bad "a second card was bound to a held tree"; else grep -q "already the tree of #$CID" "$WORK/tree.out" && ok "binding a second card to a held tree is refused, naming the card" || bad "tree refusal lacks the card: $(cat "$WORK/tree.out")"; fi
board bind "$CID2" --session "other-session" --tree wt/y | grep -q "bound to other-session" && ok "a tree of its own binds" || bad "a free tree was refused"
HELD=$(board new "Trivia: the timer keeps running on the score screen" --from trivia --kind bug | sed 's/^#\([0-9]*\).*/\1/')
board bind "$HELD" --session "held-a" --tree wt/one --branch app/one >/dev/null
if board bind "$HELD" --session "other-session" --tree wt/two >"$WORK/bind.out" 2>&1; then bad "a second session bound a held card"; else grep -q "held by session held-a" "$WORK/bind.out" && grep -q "wt/one" "$WORK/bind.out" && ok "the second bind is refused and the holder, its tree and branch are named" || bad "bind refusal lacks the holder: $(cat "$WORK/bind.out")"; fi
grep -q -- "--take" "$WORK/bind.out" && ok "the refusal says how to take the card over on purpose" || bad "refusal lacks the --take remedy"
board show "$HELD" | grep -q "session held-a" && ok "the card stayed with its holder" || bad "the card changed hands anyway"
board bind "$HELD" --session "held-a" --tree wt/one >/dev/null 2>&1 && ok "the holder may bind its own card again" || bad "the holder was refused its own card"
board bind "$HELD" --session "other-session" --tree wt/two --take | grep -q "bound to other-session" && ok "--take hands the card over" || bad "--take did not bind"
board show "$HELD" | grep -q "taken over from session held-a" && ok "the takeover is a history line" || bad "no takeover line"
board state "$HELD" done >/dev/null
board bind "$HELD" --session "held-a" >/dev/null 2>&1 && ok "a settled card can be re-bound without --take" || bad "a settled card was treated as held"

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]
