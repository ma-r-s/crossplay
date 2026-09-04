#!/bin/bash
# The release watcher, on a real postgres running the board's real migrations.
#
# On 2026-09-04 two releases failed, four workflow runs over five hours, and
# every visible signal said healthy: the autorelease reported success, tags
# appeared, the board was clean. The only detector in the system was Mario's
# e-reader saying there was no update. This suite exists so the detector that
# replaces the e-reader is watched firing, on the real payloads of that
# morning, rather than argued about.
#
# It applies server/board/supabase/migrations verbatim -- only the two
# `create extension` lines for pg_net and pg_cron are commented out, because
# neither exists outside Supabase and prelude.sql stands in for both -- so the
# functions and the events trigger under test are the ones that run on the
# board, not a description of them.
#
#   host-tests/relwatch/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MIG="$ROOT/server/board/supabase/migrations"
IMAGE="${RELWATCH_PG_IMAGE:-postgres:16-alpine}"

# A skip is information on a laptop with Docker closed. In CI every input is
# meant to be there, so a check that did not run is a failure: otherwise the
# suite that proves the watcher works reports green by never running.
if ! docker info >/dev/null 2>&1; then
  if [ -n "${CI:-}" ]; then
    echo "FAIL relwatch  docker is not available and this is CI: the suite must run here"
    exit 1
  fi
  echo "SKIP relwatch  docker is not running; start it to run this suite (in CI it is a failure)"
  exit 0
fi

# Several trees run in this workspace at once; fixed /tmp names would have two
# of them reading each other's output.
WORK="$(mktemp -d)"

CID="$(docker run --rm -d -e POSTGRES_PASSWORD=relwatch -e POSTGRES_DB=board "$IMAGE" \
        -c fsync=off -c full_page_writes=off 2>/dev/null)"
if [ -z "$CID" ]; then
  echo "FAIL relwatch  could not start $IMAGE"
  exit 1
fi
trap 'docker rm -f "$CID" >/dev/null 2>&1; rm -rf "$WORK"' EXIT

psql() { docker exec -i "$CID" psql -U postgres -d board -v ON_ERROR_STOP=1 -qtA "$@"; }

for _ in $(seq 1 60); do
  docker exec "$CID" pg_isready -U postgres -d board >/dev/null 2>&1 && break
  sleep 1
done
if ! docker exec "$CID" pg_isready -U postgres -d board >/dev/null 2>&1; then
  echo "FAIL relwatch  postgres never came up"
  exit 1
fi

docker exec "$CID" mkdir -p /tmp/fx >/dev/null 2>&1
for f in "$HERE"/fixtures/*.json; do docker cp "$f" "$CID:/tmp/fx/" >/dev/null; done

if ! psql < "$HERE/prelude.sql" > "$WORK/prelude.out" 2>&1; then
  echo "FAIL relwatch  the prelude did not apply"; cat "$WORK/prelude.out"; exit 1
fi

for m in "$MIG"/*.sql; do
  if ! sed -e '/^create extension if not exists pg_net/s/^/-- test: /' \
           -e '/^create extension if not exists pg_cron/s/^/-- test: /' "$m" \
       | psql > "$WORK/migration.out" 2>&1; then
    echo "FAIL relwatch  migration $(basename "$m") did not apply"
    tail -20 "$WORK/migration.out"
    exit 1
  fi
done

if ! psql < "$HERE/checks.sql" > "$WORK/checks.out" 2>&1; then
  echo "FAIL relwatch  the checks did not run to the end"
  tail -30 "$WORK/checks.out"
  exit 1
fi

# The keys under test contain '|', which is psql's own column separator, so the
# report is formatted in SQL and read back as whole lines.
psql -c "select case when ok then '  ok   ' || label
                     else '  FAIL ' || label || E'\\n         got  [' || got || E']\\n         want [' || want || ']' end
         from results order by n"
PASS="$(psql -c 'select count(*) from results where ok')"
FAIL="$(psql -c 'select count(*) from results where not ok')"

echo "relwatch: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
