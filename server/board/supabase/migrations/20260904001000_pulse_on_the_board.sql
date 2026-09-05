-- The pulse runs on the board. GitHub disables scheduled workflows in forks
-- and this repository is one; the board's own scheduler (pg_cron) has run
-- every night it was asked to, and pg_net lets it make HTTP requests. So the
-- board probes every host itself, every 30 minutes, and reads the answers
-- three minutes later: an info probe event when a host answers with a status
-- its row allows, an error otherwise (one card per host, closed by the next
-- ok). The daily upstream sync is checked the same way through GitHub's
-- public compare API: upstream commits xteink lacks, older than 30 hours,
-- with no sync/upstream-* pull request open, is late.

create extension if not exists pg_net with schema extensions;

create table if not exists pulse_targets (
  host text primary key,
  method text not null default 'GET' check (method in ('GET', 'POST')),
  url text not null,
  alive text not null default '2xx',      -- comma list: 200, 401, 2xx, 3xx, 4xx
  app text not null default 'tooling',    -- whose card an outage is
  enabled boolean not null default true
);
alter table pulse_targets enable row level security;
drop policy if exists pulse_targets_read on pulse_targets;
create policy pulse_targets_read on pulse_targets for select to authenticated using (is_allowed());

insert into pulse_targets (host, method, url, alive, app) values
  ('site',   'GET',  'https://crossplay.ma-r-s.com/',          '200',         'site'),
  ('report', 'GET',  'https://crossplay.ma-r-s.com/report/',   '200',         'site'),
  ('inbox',  'POST', 'https://crossplay.ma-r-s.com/api/inbox', '401',         'site'),
  ('books',  'GET',  'https://books.ma-r-s.com/',              '2xx,401',     'getbooks'),
  ('anki',   'GET',  'https://sync.ma-r-s.com/',               '2xx,3xx,4xx', 'anki'),
  ('read',   'GET',  'https://read.ma-r-s.com/',               '2xx,3xx,4xx', 'instapaper')
on conflict (host) do nothing;

-- Requests in flight between fire and collect.
create table if not exists pulse_pending (
  request_id bigint primary key,
  kind text not null,          -- host | upstream_compare | upstream_prs
  host text,
  fired_at timestamptz not null default now()
);

create or replace function pulse_alive(p_status int, p_alive text) returns boolean
language sql immutable as $$
  select bool_or(
    case trim(w)
      when '2xx' then p_status between 200 and 299
      when '3xx' then p_status between 300 and 399
      when '4xx' then p_status between 400 and 499
      else p_status::text = trim(w)
    end)
  from unnest(string_to_array(coalesce(p_alive, ''), ',')) as w
$$;

create or replace function pulse_fire() returns void
language plpgsql security definer set search_path = public, extensions, net as $$
declare
  t record;
  rid bigint;
begin
  -- Anything still pending from last time never answered: count it as down.
  perform pulse_collect();
  for t in select * from pulse_targets where enabled loop
    if t.method = 'POST' then
      select net.http_post(url := t.url, body := '{}'::jsonb,
                           headers := '{"Content-Type":"application/json"}'::jsonb,
                           timeout_milliseconds := 15000) into rid;
    else
      select net.http_get(url := t.url, timeout_milliseconds := 15000) into rid;
    end if;
    insert into pulse_pending (request_id, kind, host) values (rid, 'host', t.host);
  end loop;
  select net.http_get(url := 'https://api.github.com/repos/ma-r-s/crossplay/compare/xteink...crosspoint-reader:develop',
                      headers := '{"User-Agent":"crossplay-board-pulse","Accept":"application/vnd.github+json"}'::jsonb,
                      timeout_milliseconds := 20000) into rid;
  insert into pulse_pending (request_id, kind, host) values (rid, 'upstream_compare', 'upstream-sync');
  select net.http_get(url := 'https://api.github.com/repos/ma-r-s/crossplay/pulls?state=open&per_page=50',
                      headers := '{"User-Agent":"crossplay-board-pulse","Accept":"application/vnd.github+json"}'::jsonb,
                      timeout_milliseconds := 20000) into rid;
  insert into pulse_pending (request_id, kind, host) values (rid, 'upstream_prs', 'upstream-sync');
end
$$;

create or replace function pulse_collect() returns void
language plpgsql security definer set search_path = public, extensions, net as $$
declare
  p record;
  r record;
  t record;
  st int;
  ok boolean;
  why text;
  cmp jsonb;
  prs jsonb;
  ahead int;
  oldest timestamptz;
  oldest_h int;
  open_prs int;
begin
  -- Hosts.
  for p in select * from pulse_pending where kind = 'host' loop
    select * into r from net._http_response where id = p.request_id;
    select * into t from pulse_targets where host = p.host;
    if r.id is null then
      if p.fired_at > now() - interval '2 minutes' then continue; end if;  -- not answered yet, and young
      st := 0; ok := false; why := 'no answer';
    elsif r.timed_out or r.status_code is null then
      st := 0; ok := false; why := coalesce('no answer in 15s: ' || r.error_msg, 'no answer in 15s');
    else
      st := r.status_code; ok := pulse_alive(st, t.alive); why := 'answered ' || st;
    end if;
    if ok then
      insert into events (service, event, level, fingerprint, props)
        values ('pulse', 'probe', 'info', 'pulse|' || p.host,
                jsonb_build_object('host', p.host, 'app', t.app, 'status', st::text, 'from', 'board'));
    else
      insert into events (service, event, level, fingerprint, props)
        values ('pulse', 'probe', 'error', 'pulse|' || p.host,
                jsonb_build_object('message', p.host || ' is down: ' || why || ' from ' || t.url,
                                   'host', p.host, 'app', t.app, 'url', t.url, 'status', st::text, 'from', 'board'));
    end if;
    delete from pulse_pending where request_id = p.request_id;
    delete from net._http_response where id = p.request_id;
  end loop;

  -- Upstream: needs both answers.
  select r1.content::jsonb, r2.content::jsonb into cmp, prs
    from pulse_pending a join pulse_pending b on b.kind = 'upstream_prs' and a.kind = 'upstream_compare'
    join net._http_response r1 on r1.id = a.request_id and r1.status_code = 200
    join net._http_response r2 on r2.id = b.request_id and r2.status_code = 200
    limit 1;
  if cmp is not null then
    ahead := coalesce((cmp ->> 'ahead_by')::int, 0);
    select min((c -> 'commit' -> 'committer' ->> 'date')::timestamptz) into oldest
      from jsonb_array_elements(coalesce(cmp -> 'commits', '[]'::jsonb)) c;
    oldest_h := coalesce(extract(epoch from now() - oldest) / 3600, 0)::int;
    select count(*) into open_prs from jsonb_array_elements(coalesce(prs, '[]'::jsonb)) pr
      where (pr -> 'head' ->> 'ref') like 'sync/upstream-%';
    if ahead > 0 and open_prs = 0 and oldest_h >= 30 then
      insert into events (service, event, level, fingerprint, props)
        values ('pulse', 'probe', 'error', 'pulse|upstream-sync',
                jsonb_build_object('message', 'upstream sync late: ' || ahead || ' commits not merged, oldest ' || oldest_h || 'h, no sync pull request open',
                                   'host', 'upstream-sync', 'app', 'tooling', 'behind', ahead, 'oldest_h', oldest_h, 'from', 'board'));
    else
      insert into events (service, event, level, fingerprint, props)
        values ('pulse', 'probe', 'info', 'pulse|upstream-sync',
                jsonb_build_object('host', 'upstream-sync', 'app', 'tooling', 'behind', ahead, 'oldest_h', oldest_h, 'open_prs', open_prs, 'from', 'board'));
    end if;
    delete from net._http_response where id in (select request_id from pulse_pending where kind like 'upstream_%');
    delete from pulse_pending where kind like 'upstream_%';
  elsif exists (select 1 from pulse_pending where kind like 'upstream_%' and fired_at < now() - interval '10 minutes') then
    -- GitHub did not answer both; drop them and try next time rather than call the sync late.
    delete from net._http_response where id in (select request_id from pulse_pending where kind like 'upstream_%');
    delete from pulse_pending where kind like 'upstream_%';
  end if;
end
$$;

select cron.schedule('pulse-fire', '*/30 * * * *', $$select public.pulse_fire()$$);
select cron.schedule('pulse-collect', '3,33 * * * *', $$select public.pulse_collect()$$);
