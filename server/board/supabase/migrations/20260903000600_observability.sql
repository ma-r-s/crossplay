-- Three things the numbers page could not say: whether a service is up
-- (the pulse), how the workflow itself is doing (asks, waits, refusals),
-- and what happens when the events table grows forever (a nightly rollup
-- and a 90-day cut). Views run as the caller so the public key sees nothing.

-- The pulse: server/pulse/pulse.sh posts one probe per host every 30 minutes.
create or replace view pulse_hosts as
  with p as (
    select at, level, props ->> 'host' as host, nullif(props ->> 'ms', '')::int as ms
    from events
    where service = 'pulse' and event = 'probe' and at > now() - interval '7 days'
  )
  select host,
         max(at) as last_at,
         (array_agg(level order by at desc))[1] = 'info' as up_now,
         round(100.0 * count(*) filter (where level = 'info') / count(*), 1) as uptime_7d,
         percentile_cont(0.5) within group (order by ms) filter (where level = 'info') as ms_median,
         count(*) filter (where level = 'error') as failures_7d
  from p
  where host is not null
  group by host
  order by host;

-- The workflow, by week: what opened, what closed, how often Mario was
-- asked and how often he answered, how many errors became cards, how often a
-- hook refused something, how many releases went out.
create or replace view workflow_weekly as
  with weeks as (
    select generate_series(date_trunc('week', now()) - interval '7 weeks',
                           date_trunc('week', now()), interval '1 week') as week
  )
  select w.week::date as week,
    (select count(*) from cards c where c.created_at >= w.week and c.created_at < w.week + interval '7 days') as cards_opened,
    (select count(*) from history h where h.what in ('state done', 'state released')
       and h.at >= w.week and h.at < w.week + interval '7 days') as cards_closed,
    (select count(*) from blockers b where b.need = 'mario'
       and b.created_at >= w.week and b.created_at < w.week + interval '7 days') as asks_to_mario,
    (select count(*) from blockers b where b.need = 'mario' and b.answered_at is not null
       and b.answered_at >= w.week and b.answered_at < w.week + interval '7 days') as answers_from_mario,
    (select count(*) from cards c where c.source = 'error'
       and c.created_at >= w.week and c.created_at < w.week + interval '7 days') as error_cards,
    (select count(*) from events e where e.service = 'workflow' and e.event = 'refusal'
       and e.at >= w.week and e.at < w.week + interval '7 days') as refusals,
    (select count(*) from events e where e.service = 'release'
       and e.at >= w.week and e.at < w.week + interval '7 days') as releases
  from weeks w
  order by week desc;

-- Hours a card sits in each state, over the moves that ended in the last 30
-- days. A state nobody leaves (done) has no row, by construction.
create or replace view state_dwell as
  with moves as (
    select card_id, at, substr(what, 7) as state,
           lead(at) over (partition by card_id order by at, id) as left_at
    from history
    where what like 'state %'
  )
  select state,
         count(*) as n,
         round((extract(epoch from percentile_cont(0.5) within group (order by left_at - at)) / 3600)::numeric, 1) as median_h,
         round((extract(epoch from max(left_at - at)) / 3600)::numeric, 1) as max_h
  from moves
  where left_at is not null and left_at > now() - interval '30 days'
  group by state
  order by state;

-- Mario's inbox: what waits on him now, and how long an answer takes.
create or replace view inbox_latency as
  select count(*) filter (where open) as open_now,
         round((extract(epoch from max(now() - created_at) filter (where open)) / 3600)::numeric, 1) as oldest_open_h,
         round((extract(epoch from percentile_cont(0.5) within group (order by answered_at - created_at)
                  filter (where answered_at is not null and created_at > now() - interval '30 days')) / 3600)::numeric, 1) as median_answer_h,
         count(*) filter (where answered_at > now() - interval '7 days') as answered_7d
  from blockers
  where need = 'mario';

create or replace view open_cards_by_app as
  select app,
         count(*) as open,
         count(*) filter (where state = 'reported') as untriaged,
         round((extract(epoch from max(now() - created_at)) / 86400)::numeric, 1) as oldest_d
  from cards
  where state not in ('done', 'released', 'parked')
  group by app
  order by open desc, app;

alter view pulse_hosts set (security_invoker = true);
alter view workflow_weekly set (security_invoker = true);
alter view state_dwell set (security_invoker = true);
alter view inbox_latency set (security_invoker = true);
alter view open_cards_by_app set (security_invoker = true);

-- Retention. Raw events are kept 90 days; every night the days before today
-- are folded into events_rollup (re-folding the last three, so late arrivals
-- are counted), and rows older than 90 days go. error_fingerprints keeps its
-- counts either way.
create table if not exists events_rollup (
  day date not null,
  service text not null,
  event text not null,
  level text not null,
  n integer not null,
  devices integer not null,
  primary key (day, service, event, level)
);
alter table events_rollup enable row level security;
drop policy if exists rollup_read_allowed on events_rollup;
create policy rollup_read_allowed on events_rollup for select to authenticated using (is_allowed());

create or replace function rollup_events() returns void
language plpgsql security definer set search_path = public as $$
begin
  insert into events_rollup (day, service, event, level, n, devices)
  select at::date, service, event, level, count(*), count(distinct device)
  from events
  where at < date_trunc('day', now()) and at >= date_trunc('day', now()) - interval '3 days'
  group by 1, 2, 3, 4
  on conflict (day, service, event, level) do update
    set n = excluded.n, devices = excluded.devices;
  delete from events where at < now() - interval '90 days';
end
$$;

create extension if not exists pg_cron with schema pg_catalog;
grant usage on schema cron to postgres;
select cron.schedule('events-rollup', '17 4 * * *', $$select public.rollup_events()$$);
