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

echo "the integration tree"
expect "worker edit in firmware-next refused"   2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/src/a.cpp\"}}"
expect "worker write in firmware-next refused"  2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/docs/x.md\"}}"
expect "integrator edit in firmware-next allowed" 0 pretool "{\"session_id\":\"$INTEG\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/firmware-next/src/a.cpp\"}}"
expect "worker edit in its own tree allowed"    0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Edit\",\"tool_input\":{\"file_path\":\"$ROOT/wt/x/src/a.cpp\"}}"
expect "worker bash write into firmware-next refused" 2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && git merge app/x\"}}"
expect "worker bash read of firmware-next allowed"    0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"git -C $ROOT/firmware-next log --oneline -5\"}}"
expect "integrator bash merge allowed"          0 pretool "{\"session_id\":\"$INTEG\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd $ROOT/firmware-next && git merge app/x\"}}"

echo "the build lock"
expect "raw pio run refused"                    2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd wt/x && pio run -e x4pro\"}}"
expect "check.sh allowed"                       0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"cd wt/x && ./scripts_local/check.sh --tests\"}}"
expect "pio in a word is not pio run"           0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"grep -rn 'pio run' docs\"}}"

echo "who may talk to whom"
expect "worker to orchestrator allowed"         0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"Main\",\"message\":\"blocked\"}}"
expect "worker to orchestrator with ref allowed" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"Main [1a2b3c]\",\"message\":\"blocked\"}}"
expect "worker to a peer refused"               2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"xteink-ff\",\"message\":\"who owns 1.12.5\"}}"
expect "worker to a peer via the app refused"   2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_dddd-peer\",\"message\":\"hi\"}}"
expect "worker to orchestrator via the app allowed" 0 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"mcp__ccd_session_mgmt__send_message\",\"tool_input\":{\"session_id\":\"local_$ORCH\",\"message\":\"hi\"}}"
expect "orchestrator to anyone allowed"         0 pretool "{\"session_id\":\"$ORCH\",\"tool_name\":\"SendMessage\",\"tool_input\":{\"to\":\"xteink-ff\",\"message\":\"take it\"}}"
expect "worker asking Mario refused"            2 pretool "{\"session_id\":\"$WORKER\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"board ask 3 --ask 'ship?' --default hold\"}}"
expect "orchestrator asking Mario allowed"      0 pretool "{\"session_id\":\"$ORCH\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"board ask 3 --ask 'ship?' --default hold\"}}"

echo "ending a turn"
T="$WORK/transcript.jsonl"
mk_transcript() { # mk_transcript "<last assistant text>"
  printf '{"type":"user","message":{"content":"go"}}\n' > "$T"
  printf '{"type":"assistant","message":{"content":[{"type":"text","text":%s}]}}\n' "$(python3 -c 'import json,sys;print(json.dumps(sys.argv[1]))' "$1")" >> "$T"
}
mk_transcript "Fixed and gated. Want me to also port the picker fix, or leave it?"
expect "hand-back with no card refused"          2 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"
grep -q "board bind" "$WORK/err" && ok "refusal tells it to bind" || bad "refusal does not tell it to bind"
expect "stop_hook_active never loops"            0 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":true}"
expect "orchestrator may hand back"              0 stop "{\"session_id\":\"$ORCH\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"
mk_transcript "Fixed, gated, pushed. PR open; card moved to review."
expect "a finished turn passes"                  0 stop "{\"session_id\":\"$WORKER\",\"transcript_path\":\"$T\",\"stop_hook_active\":false}"

CID=$(board new "Sudoku loses the puzzle from the difficulty menu" --from sudoku --kind bug | sed 's/^#\([0-9]*\).*/\1/')
board bind "$CID" --session "$WORKER" --tree wt/x --branch app/x >/dev/null
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
board answer "$CID" "delete it" --note "less code" >/dev/null
board inbox | grep -q "Nothing needs you" && ok "an answer clears the inbox" || bad "answer did not clear"
board show "$CID" | grep -q "closed: delete it" && ok "the answer is on the card" || bad "answer not on card"
board state "$CID" review >/dev/null
board list | grep -q "review" && ok "state moves" || bad "state did not move"
printf '## Trivia: play the new build\nbody one\n\n## Hacker News: keep anything?\nbody two\n' > "$WORK/import.md"
board import "$WORK/import.md" | grep -q "imported 2 cards" && ok "import makes one card per heading" || bad "import failed"
board list | grep -q "trivia" && ok "import derives the app from the heading" || bad "app not derived"
board integrator --session "$WORKER" >/dev/null 2>&1 && bad "a second integrator claim succeeded" || ok "a held integration claim refuses a second claimant"
board integrator --session "$WORKER" --release >/dev/null 2>&1 && bad "a stranger released the claim" || ok "only the holder releases the claim"
board integrator --session "$INTEG" --release >/dev/null && ok "the holder releases the claim" || bad "holder cannot release"

echo
echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]
