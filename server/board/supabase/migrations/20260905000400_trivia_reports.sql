-- Player reports about individual trivia questions, from board card #257.
--
-- TWO TABLES, AND THEY MUST NOT BE ONE. `trivia_reports` holds what was
-- reported; `trivia_rate` holds who has been posting. Keeping the address hash
-- off the report row is the whole point: joined, they would let anyone with
-- read access list every question one reader reported, which is a reading
-- history of a personal device. Split, the rate table holds no report content
-- and the report table holds nothing about the reporter.
--
-- WHY report_key IS THE PRIMARY KEY. It is
--     sha256(server_secret || device_id || pack_id || index)
-- computed in the function, and it is the ONLY thing derived from the device.
-- As a primary key it makes a repeat report of the SAME question by the same
-- device collide, which is the de-duplication the queue needs -- forty reports
-- of one question from one annoyed player must not read as forty players
-- agreeing. Because the hash is per-question, two reports of DIFFERENT
-- questions by one device share nothing, so the rows cannot be joined into a
-- history. The secret never leaves the server, so the keys cannot be probed
-- offline either.
--
-- The device id itself is NEVER stored, in any column, in either table.

create table if not exists trivia_reports (
  report_key text primary key,
  at timestamptz not null default now(),
  pack text not null,                    -- the pack id the report was filed against
  idx integer not null,                  -- index within THAT pack; meaningless without it
  pack_count integer,                    -- the count the device held, for the binding check
  reason text not null default 'none',
  version text,
  board text,
  -- Filled in later by the resolver, which joins (pack, idx) through that
  -- build's published index map. Null means "not resolved yet", which is a
  -- normal state, not an error: a report can outlive the manifest's arrival.
  corpus_id text,
  resolved_at timestamptz,
  -- What the resolver concluded. 'open' until it runs; then 'matched',
  -- 'already_removed' (an earlier report won -- still counts, as confirmation),
  -- 'repaired_since' (the text moved under it; a human must see it) or
  -- 'unknown_pack'.
  outcome text not null default 'open'
    check (outcome in ('open', 'matched', 'already_removed', 'repaired_since', 'unknown_pack'))
);

create index if not exists trivia_reports_pack_idx on trivia_reports (pack, idx);
create index if not exists trivia_reports_at on trivia_reports (at desc);
create index if not exists trivia_reports_open on trivia_reports (outcome) where outcome = 'open';

-- Rate limiting, deliberately in its own table with no report content.
--
-- The bridges' limiters live in process memory and reset when the process
-- restarts (server/read-bridge/bridge/app.py). A Vercel function has no process
-- to hold a counter in, so the durable form is the only one available -- and it
-- is the better one anyway. site/api/report.js counts rows in `cards` via its
-- reporter_hash column; `events` has no such column and is written to by every
-- heartbeat and download, so counting there would mean scanning a hot table.
-- Hence this: one row per address hash per window, and nothing else.
create table if not exists trivia_rate (
  ip_hash text primary key,
  window_start timestamptz not null default now(),
  count integer not null default 0
);

alter table trivia_reports enable row level security;
alter table trivia_rate enable row level security;
-- No policies: both tables are reachable only with the service key, which the
-- site function holds and no browser ever sees. The public anon key can insert
-- into `events` and must not reach these.
