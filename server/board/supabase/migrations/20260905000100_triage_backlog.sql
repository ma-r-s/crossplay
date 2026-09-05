-- How far behind triage is, in one row, for the top of the inbox. On
-- 2026-09-05 the answer to "is triage up to date?" (37 cards in reported, 8
-- older than 36 hours, the last one triaged 24 hours earlier) took a
-- database query; the inbox front showed only what needed Mario, and the
-- Numbers page buries untriaged counts per app. "Last triage" is the last
-- card anyone marked triaged, which is the verb the orchestrator's runbook
-- uses; a card leaving reported another way is not counted.
create or replace view triage_backlog as
  select count(*)::int as waiting,
         coalesce(round(extract(epoch from max(now() - created_at)) / 3600)::int, 0) as oldest_h,
         (select max(at) from history where what = 'state triaged') as last_triaged_at,
         (select round(extract(epoch from now() - max(at)) / 3600)::int from history where what = 'state triaged') as since_triage_h
  from cards
  where state = 'reported';
alter view triage_backlog set (security_invoker = true);
