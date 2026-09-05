-- The triage line counts what the board can vouch for. A reported card that
-- a session has bound (session or branch set) is claimed work, not a triage
-- backlog, so it is counted apart; and a reported card on app mario is a
-- decision only he can take, so the line says how many of the waiting are
-- his. What the board cannot see, a card finished and never marked, stays a
-- person's job to correct (the orchestrator's sweep does).
-- Columns are added in the middle, which create-or-replace refuses; the view
-- has no dependants.
drop view if exists triage_backlog;
create view triage_backlog as
  select count(*) filter (where session is null and branch is null)::int as waiting,
         count(*) filter (where session is not null or branch is not null)::int as claimed,
         count(*) filter (where app = 'mario')::int as for_mario,
         coalesce(round(extract(epoch from max(now() - created_at) filter (where session is null and branch is null)) / 3600)::int, 0) as oldest_h,
         (select max(at) from history where what = 'state triaged') as last_triaged_at,
         (select round(extract(epoch from now() - max(at)) / 3600)::int from history where what = 'state triaged') as since_triage_h
  from cards
  where state = 'reported';
alter view triage_backlog set (security_invoker = true);
