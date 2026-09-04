#!/bin/bash
# Apply the board's migrations the board has not seen, in name order, and
# record each one, so the board can answer which files it has.
#
# Until 2026-09-04 nothing tracked this: every file under supabase/migrations
# was applied by hand with psql -f, "applied" was a fact about someone's shell
# history, and two files once shared a version prefix with no defined order
# between them. The table board_migrations is the answer, and this script is
# the only thing that writes it.
#
#   server/board/migrate.sh                    apply what is pending, in order, then
#                                              reload PostgREST
#   server/board/migrate.sh --list             say what is pending, apply nothing
#   server/board/migrate.sh --mark-through V   record every file up to version V as
#                                              applied WITHOUT running it: once, for
#                                              a board that was migrated by hand
#
# Each file runs in one transaction: a file that fails leaves nothing of itself
# behind and is not recorded, and nothing after it runs. Two files with one
# version are refused before anything runs.
#
# Connection: .board/supabase.env in the workspace (found by walking up from
# this file), or MIGRATE_PSQL set to a psql command that already connects,
# which is what host-tests/boardmigrate does against a throwaway postgres.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MIG="${MIGRATE_DIR:-$HERE/supabase/migrations}"
MODE=apply
THROUGH=""
case "${1:-}" in
  "") ;;
  --list) MODE=list ;;
  --mark-through)
    MODE=mark; THROUGH="${2:-}"
    [ -n "$THROUGH" ] || { echo "migrate: --mark-through needs a version (14 digits)"; exit 2; } ;;
  *) echo "migrate: unknown argument '$1' (--list, --mark-through V, or nothing)"; exit 2 ;;
esac

if [ -n "${MIGRATE_PSQL:-}" ]; then
  run_psql() { $MIGRATE_PSQL -v ON_ERROR_STOP=1 -qtA "$@"; }
else
  ENV=""; d="$HERE"
  while [ "$d" != "/" ]; do
    [ -f "$d/.board/supabase.env" ] && { ENV="$d/.board/supabase.env"; break; }
    d="$(dirname "$d")"
  done
  [ -n "$ENV" ] || { echo "migrate: no .board/supabase.env above $HERE, and MIGRATE_PSQL is not set"; exit 2; }
  set -a; . "$ENV"; set +a
  run_psql() {
    PGPASSWORD="$SUPABASE_DB_PASSWORD" psql \
      "host=aws-0-us-east-1.pooler.supabase.com port=5432 dbname=postgres user=postgres.$SUPABASE_PROJECT_REF sslmode=require" \
      -v ON_ERROR_STOP=1 -qtA "$@"
  }
fi

files=()
while IFS= read -r f; do [ -n "$f" ] && files+=("$f"); done < <(ls "$MIG"/*.sql 2>/dev/null | sort)
[ ${#files[@]} -gt 0 ] || { echo "migrate: no .sql files under $MIG"; exit 2; }

version_of() { basename "$1" | cut -d_ -f1; }
for f in "${files[@]}"; do
  v="$(version_of "$f")"
  [[ "$v" =~ ^[0-9]{14}$ ]] || { echo "migrate: $(basename "$f") does not start with a 14-digit version"; exit 1; }
done
dups="$(for f in "${files[@]}"; do version_of "$f"; done | sort | uniq -d)"
if [ -n "$dups" ]; then
  echo "migrate: two files share a version, which has no defined order between them; rename one:"
  for v in $dups; do ls "$MIG"/"$v"_*.sql | xargs -n1 basename | sed 's/^/  /'; done
  exit 1
fi

if ! run_psql -c "create table if not exists board_migrations (
      version text primary key, name text not null,
      applied_at timestamptz not null default now())" >/dev/null 2>&1; then
  echo "migrate: cannot reach the board (or cannot create board_migrations)"; exit 1
fi
applied="$(run_psql -c "select version from board_migrations order by version")"
pending=()
for f in "${files[@]}"; do
  grep -qx "$(version_of "$f")" <<<"$applied" || pending+=("$f")
done
count() { run_psql -c "select count(*) from board_migrations"; }

case "$MODE" in
  list)
    if [ ${#pending[@]} -eq 0 ]; then echo "up to date: $(count) applied, nothing pending"
    else echo "pending, in this order:"; for f in "${pending[@]}"; do echo "  $(basename "$f")"; done; fi
    exit 0 ;;
  mark)
    n=0
    for f in "${pending[@]}"; do
      v="$(version_of "$f")"
      [ "$v" \> "$THROUGH" ] && continue
      run_psql -c "insert into board_migrations (version, name) values ('$v', '$(basename "$f")') on conflict do nothing" >/dev/null || exit 1
      echo "marked  $(basename "$f")"; n=$((n + 1))
    done
    echo "marked $n file(s) as applied without running them; $(count) applied"
    exit 0 ;;
esac

if [ ${#pending[@]} -eq 0 ]; then echo "up to date: $(count) applied, nothing pending"; exit 0; fi
OUT="$(mktemp)"; trap 'rm -f "$OUT"' EXIT
for f in "${pending[@]}"; do
  n="$(basename "$f")"; v="$(version_of "$f")"
  if run_psql --single-transaction <"$f" >"$OUT" 2>&1; then
    run_psql -c "insert into board_migrations (version, name) values ('$v', '$n')" >/dev/null || { echo "applied $n but could not record it"; exit 1; }
    echo "applied $n"
  else
    echo "FAILED  $n: rolled back, not recorded, nothing after it was run"
    sed 's/^/  /' "$OUT"
    exit 1
  fi
done
run_psql -c "notify pgrst, 'reload schema'" >/dev/null 2>&1
echo "up to date: $(count) applied"
