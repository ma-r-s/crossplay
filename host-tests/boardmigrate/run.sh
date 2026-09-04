#!/bin/bash
# server/board/migrate.sh on a real postgres, against fixture migrations.
#
# The board had no migration tracking until 2026-09-04; this is the script
# that gives it one, so what it records and what it refuses is asserted here
# rather than trusted: files apply in name order and are recorded; a second
# run applies nothing; a new file applies alone; a failing file rolls back,
# is not recorded, and stops the run; two files with one version are refused
# before anything runs; --mark-through records without running.
#
# Docker provides the postgres, as in host-tests/relwatch: a skip on a laptop
# without it, a failure in CI.
#
#   host-tests/boardmigrate/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SCRIPT="$ROOT/server/board/migrate.sh"
IMAGE="${RELWATCH_PG_IMAGE:-postgres:16-alpine}"

if ! docker info >/dev/null 2>&1; then
  if [ -n "${CI:-}" ]; then
    echo "FAIL boardmigrate  docker is not available and this is CI: the suite must run here"; exit 1
  fi
  echo "SKIP boardmigrate  docker is not running; start it to run this suite (in CI it is a failure)"; exit 0
fi

WORK="$(mktemp -d)"
CID="$(docker run --rm -d -e POSTGRES_PASSWORD=x -e POSTGRES_DB=board "$IMAGE" -c fsync=off 2>/dev/null)"
[ -n "$CID" ] || { echo "FAIL boardmigrate  could not start $IMAGE"; exit 1; }
trap 'docker rm -f "$CID" >/dev/null 2>&1; rm -rf "$WORK"' EXIT
for _ in $(seq 1 60); do docker exec "$CID" pg_isready -U postgres -d board >/dev/null 2>&1 && break; sleep 0.5; done
docker exec "$CID" pg_isready -U postgres -d board >/dev/null 2>&1 || { echo "FAIL boardmigrate  postgres never came up"; exit 1; }
# The image answers pg_isready during its first-run initialisation, then
# restarts the server once; the first real connection can land in that gap.
for _ in $(seq 1 40); do docker exec -i "$CID" psql -U postgres -d board -qtA -c 'select 1' >/dev/null 2>&1 && break; sleep 0.5; done

export MIGRATE_PSQL="docker exec -i $CID psql -U postgres -d board"
export MIGRATE_DIR="$WORK/mig"; mkdir -p "$MIGRATE_DIR"
q() { docker exec -i "$CID" psql -U postgres -d board -qtA -c "$1"; }
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
migrate() { bash "$SCRIPT" "$@" >"$WORK/out" 2>&1; echo $?; }

echo 'create table a (id int);' > "$MIGRATE_DIR/20260101000000_a.sql"
echo 'alter table a add column b int;' > "$MIGRATE_DIR/20260101000100_b.sql"
echo 'insert into a (id, b) values (1, 2);' > "$MIGRATE_DIR/20260101000200_c.sql"

code=$(migrate --list)
[ "$code" = 0 ] && grep -q 'pending' "$WORK/out" && [ "$(grep -c '_.\.sql' "$WORK/out")" = 3 ] && ok "--list names the three pending files" || bad "--list: exit $code, $(cat "$WORK/out")"
[ "$(q 'select count(*) from a' 2>/dev/null)" = "" ] && ok "--list applied nothing" || bad "--list applied something"

code=$(migrate)
[ "$code" = 0 ] && [ "$(grep -o 'applied [0-9_a-z]*\.sql' "$WORK/out" | tr '\n' ' ')" = "applied 20260101000000_a.sql applied 20260101000100_b.sql applied 20260101000200_c.sql " ] && ok "three files applied in name order" || bad "apply: exit $code, $(cat "$WORK/out")"
[ "$(q 'select b from a')" = 2 ] && ok "the files ran (a has b = 2)" || bad "the files did not run"
[ "$(q 'select count(*) from board_migrations')" = 3 ] && ok "three rows recorded" || bad "rows recorded: $(q 'select count(*) from board_migrations')"
[ "$(q "select string_agg(name, ',' order by version) from board_migrations")" = "20260101000000_a.sql,20260101000100_b.sql,20260101000200_c.sql" ] && ok "rows carry the file names" || bad "row names wrong"

code=$(migrate)
[ "$code" = 0 ] && grep -q 'nothing pending' "$WORK/out" && ! grep -q '^applied' "$WORK/out" && ok "a second run applies nothing" || bad "second run: exit $code, $(cat "$WORK/out")"

echo 'create table d (x int);' > "$MIGRATE_DIR/20260101000300_d.sql"
code=$(migrate)
[ "$code" = 0 ] && [ "$(grep -c '^applied' "$WORK/out")" = 1 ] && grep -q 'applied 20260101000300_d.sql' "$WORK/out" && ok "a new file applies alone" || bad "new file: exit $code, $(cat "$WORK/out")"

printf 'create table e (x int);\nselect no_such_function();\n' > "$MIGRATE_DIR/20260101000400_bad.sql"
echo 'create table f (x int);' > "$MIGRATE_DIR/20260101000500_f.sql"
code=$(migrate)
[ "$code" != 0 ] && grep -q 'FAILED  20260101000400_bad.sql' "$WORK/out" && ok "a failing file fails the run and is named" || bad "failing file: exit $code, $(cat "$WORK/out")"
[ "$(q "select count(*) from information_schema.tables where table_name in ('e','f')")" = 0 ] && ok "the failing file rolled back and the file after it did not run" || bad "e or f exists"
[ "$(q 'select count(*) from board_migrations')" = 4 ] && ok "the failing file is not recorded" || bad "rows: $(q 'select count(*) from board_migrations')"
rm "$MIGRATE_DIR/20260101000400_bad.sql" "$MIGRATE_DIR/20260101000500_f.sql"

echo 'create table g (x int);' > "$MIGRATE_DIR/20260101000300_dd.sql"
code=$(migrate)
[ "$code" != 0 ] && grep -q 'share a version' "$WORK/out" && grep -q '20260101000300_dd.sql' "$WORK/out" && ok "two files with one version are refused and named" || bad "duplicate: exit $code, $(cat "$WORK/out")"
[ "$(q "select count(*) from information_schema.tables where table_name = 'g'")" = 0 ] && ok "the refusal ran nothing" || bad "g exists"
rm "$MIGRATE_DIR/20260101000300_dd.sql"

q 'drop table board_migrations, a, d' >/dev/null
code=$(migrate --mark-through 20260101000100)
[ "$code" = 0 ] && [ "$(q 'select count(*) from board_migrations')" = 2 ] && ok "--mark-through records the files up to the version" || bad "mark: exit $code, rows $(q 'select count(*) from board_migrations'), $(cat "$WORK/out")"
[ "$(q "select count(*) from information_schema.tables where table_name = 'a'")" = 0 ] && ok "--mark-through runs nothing" || bad "a exists after mark"
code=$(migrate --list)
[ "$(grep -c '_.\.sql' "$WORK/out")" = 2 ] && grep -q '20260101000200_c.sql' "$WORK/out" && ok "after the mark, only the later files are pending" || bad "list after mark: $(cat "$WORK/out")"

echo "$((PASS + FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]
