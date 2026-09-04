-- The release watcher: it looks at the release pipeline from outside, the way
-- the pulse looks at the hosts, and it says so when a release was started and
-- did not finish.
--
-- Why it exists. On 2026-09-04 two releases failed, four workflow runs over
-- five hours, and nothing said anything. The autorelease reported success --
-- correctly: its own job is to bump, tag and dispatch, and all three worked.
-- The build it dispatched is a different workflow with no `needs:` binding it
-- to anything, so its failure was reported to nobody. Tags appeared, the board
-- stayed clean, and a session read a bump commit and told Mario 1.12.15 had
-- shipped. The only detector in the system was Mario's e-reader saying there
-- was no update. Automating the release automated away its only observer: when
-- a person cut releases by hand, a failure was visible in the same breath as
-- making it.
--
-- Where it runs. On the board, for the reason the pulse runs here: this
-- repository is a fork and GitHub does not run `schedule:` workflows in forks.
-- It also has to survive the thing it watches -- a watcher living inside the
-- release chain is asleep exactly when the chain is broken -- so it reaches
-- GitHub's public API through pg_net and needs no token, no runner and no
-- workflow file.
--
-- What it decides, and the numbers behind the two clocks. Both are measured
-- from the 53 runs of crossplay-release.yml and the 48 releases they
-- published, not guessed:
--
--   * A run of the release chain that ENDED anything but success is a fault
--     immediately, with no clock at all. That is the common case and it needs
--     no boundary: 33868277061 would have opened a card at 11:42 this morning,
--     thirteen minutes after the tag, and the other three within the hour.
--   * A version tagged with NO run building it: 15 minutes. The two automated
--     releases so far show tag-to-run at 2 and 3 seconds (bump b7e9c49e at
--     13:08:06, run 33876388600 created 13:08:09). Fifteen minutes is three
--     hundred times the observed latency.
--   * A version tagged, building or built, and still not published: 60
--     minutes. All 48 successful releases published within 19.9 minutes of
--     their run being created; the longest any run has ever occupied,
--     including a 16.9-minute wait for a runner, is 29.4 minutes. Sixty is
--     twice the worst ever seen. A generous margin costs little here because
--     it is only the backstop: a failed build is reported without waiting.
--   * No answer from GitHub for 3 hours (six missed passes) is itself a fault.
--     A detector that has quietly gone blind looks exactly like a green one,
--     which is the failure this file exists to stop repeating.
--
-- What it cannot see, said here so a clean board is not read as more than it
-- is. It watches releases that were STARTED. If crossplay-autorelease.yml runs
-- green and its gate decides go=false every time -- a rule in
-- release-needed.sh going wrong, RELEASE_HOLD left at 1 -- there is no bump
-- commit, nothing is owed, and this watcher is silent while merges pile up
-- unreleased. Its own failing runs are caught, which is why both workflows'
-- runs are read; a gate that quietly always says no is not.
--
-- What it does NOT do is collapse. Four failed runs are four cards, not one
-- summary: the fault was that it failed four times and said nothing four
-- times, and a watcher that answers with a single tidy notification has
-- reproduced it. One card per run, one card per owed version, each keyed on
-- its own id, each closed on its own.

create extension if not exists pg_net with schema extensions;

-- Every fault key this watcher has already adjudicated. A key in here has had
-- its say; a key not in here has not. That distinction is the whole of "an old
-- failure is not a new one", and it is per run id and per version, never per
-- "the release is broken", so a second failure is a second card.
create table if not exists release_seen (
  key text primary key,
  first_seen timestamptz not null default now(),
  note text
);
alter table release_seen enable row level security;
drop policy if exists release_seen_read on release_seen;
create policy release_seen_read on release_seen for select to authenticated using (is_allowed());

-- A version the pipeline said it was releasing (its `chore: crossplay X` bump
-- commit on xteink) and has not published. Remembered here rather than
-- rediscovered every pass, so a release that stays owed while merges keep
-- landing does not fall out of the commit window and go quiet.
create table if not exists release_pending (
  version text primary key,
  at timestamptz,                    -- the bump commit's own date, when it was in the window
  sha text,
  first_seen timestamptz not null default now()
);
alter table release_pending enable row level security;
drop policy if exists release_pending_read on release_pending;
create policy release_pending_read on release_pending for select to authenticated using (is_allowed());

create table if not exists release_state (
  id int primary key default 1 check (id = 1),
  armed_at timestamptz not null default now(),
  last_ok_at timestamptz not null default now(),
  seeded boolean not null default false
);
insert into release_state (id) values (1) on conflict (id) do nothing;
alter table release_state enable row level security;
drop policy if exists release_state_read on release_state;
create policy release_state_read on release_state for select to authenticated using (is_allowed());

-- Requests in flight between fire and collect, as the pulse does it.
create table if not exists relwatch_http (
  request_id bigint primary key,
  kind text not null,                -- release_runs | autorelease_runs | latest | commits
  fired_at timestamptz not null default now()
);

-- 1.12.9 < 1.12.10. Null for anything that is not a plain dotted number, so a
-- tag like xkcd-pack compares with nothing instead of comparing wrongly.
create or replace function semver_key(p text) returns int[]
language sql immutable as $$
  select case
    when regexp_replace(coalesce(p, ''), '^v', '') ~ '^[0-9]+(\.[0-9]+)*$'
    then string_to_array(regexp_replace(p, '^v', ''), '.')::int[]
  end
$$;

-- The whole decision, as one function of its inputs: no table reads, no HTTP,
-- no clock of its own. host-tests/relwatch drives it with the real API
-- payloads of 2026-09-04 and with the pairs that have fooled people before --
-- a run that is merely running against one that failed, an old failure against
-- a new one -- because a probe that cannot tell those apart reports
-- confidently and wrongly.
--
-- Returns {"verdicts": [...], "pending": [...]}. A verdict is
-- {key, level, message, props}: error opens the card its key names, info
-- closes it. Only keys absent from p_seen come back as errors, and only keys
-- present in it come back as info, so the caller neither repeats itself nor
-- announces a recovery from a fault it never reported.
create or replace function release_verdicts(
  p_runs      jsonb,          -- the workflow_runs of both release workflows, concatenated
  p_latest    jsonb,          -- /releases/latest
  p_commits   jsonb,          -- /commits?sha=xteink
  p_pending   jsonb,          -- release_pending as [{version, at, sha, first_seen}]
  p_seen      jsonb,          -- release_seen keys as ["key", ...]
  p_now       timestamptz,
  p_last_ok   timestamptz,
  p_stall     interval,
  p_overdue   interval,
  p_blind     interval
) returns jsonb
language plpgsql stable as $$
declare
  r          jsonb;
  c          jsonb;
  v          jsonb;
  latest     text;
  latest_key int[];
  pend       jsonb := '{}'::jsonb;      -- version -> {version, at, sha, first_seen}
  out_v      jsonb := '[]'::jsonb;
  seen       jsonb := coalesce(p_seen, '[]'::jsonb);
  key        text;
  ver        text;
  status     text;
  concl      text;
  created    timestamptz;
  at         timestamptz;
  mins       int;
  n_runs     int;
  outcomes   text;
begin
  latest     := nullif(p_latest ->> 'tag_name', '');
  latest_key := semver_key(latest);

  -- The versions the pipeline said it was releasing: what we already
  -- remembered, plus every bump commit still in the window. The commit wins
  -- when both have it, because the commit carries the exact minute and the
  -- remembered row may only carry the minute we first looked.
  for v in select * from jsonb_array_elements(coalesce(p_pending, '[]'::jsonb)) loop
    pend := pend || jsonb_build_object(v ->> 'version', v);
  end loop;
  for c in select * from jsonb_array_elements(coalesce(p_commits, '[]'::jsonb)) loop
    ver := substring(split_part(coalesce(c -> 'commit' ->> 'message', ''), E'\n', 1)
                     from '^chore: crossplay ([0-9]+\.[0-9]+\.[0-9]+)$');
    if ver is not null then
      pend := pend || jsonb_build_object(ver, jsonb_build_object(
        'version', ver,
        'at',      c -> 'commit' -> 'committer' ->> 'date',
        'sha',     c ->> 'sha',
        'first_seen', coalesce(pend -> ver ->> 'first_seen', to_char(p_now, 'YYYY-MM-DD"T"HH24:MI:SSOF'))));
    end if;
  end loop;

  -- A version at or below the newest published release has shipped. If we had
  -- said it was owed, say it is not any more: the card closes itself, exactly
  -- as a host coming back closes its outage.
  for v in select value from jsonb_each(pend) loop
    if latest_key is not null and semver_key(v ->> 'version') is not null
       and semver_key(v ->> 'version') <= latest_key then
      key := 'release|owed|' || (v ->> 'version');
      if seen ? key then
        out_v := out_v || jsonb_build_array(jsonb_build_object(
          'key', key, 'level', 'info',
          'message', 'crossplay ' || (v ->> 'version') || ' is published',
          'props', jsonb_build_object('version', v ->> 'version', 'released', latest)));
      end if;
      pend := pend - (v ->> 'version');
    end if;
  end loop;

  -- Every run of the release chain. A run says three different things and only
  -- one of them is "fine".
  for r in select * from jsonb_array_elements(coalesce(p_runs, '[]'::jsonb)) loop
    key     := 'release|run|' || (r ->> 'id');
    status  := r ->> 'status';
    concl   := r ->> 'conclusion';      -- ->> is NULL for JSON null, which is the point
    created := (r ->> 'created_at')::timestamptz;
    mins    := (extract(epoch from p_now - created) / 60)::int;
    if status = 'completed' and concl is not null
       and concl not in ('success', 'skipped', 'neutral') then
      -- Ended, and not well. No clock: this is a fault the moment it is true.
      if not (seen ? key) then
        out_v := out_v || jsonb_build_array(jsonb_build_object(
          'key', key, 'level', 'error',
          'message', coalesce(r ->> 'name', 'the release build') || ' for ' ||
                     coalesce(r ->> 'head_branch', '?') || ' ended ' || concl ||
                     ' after ' || mins || ' minutes; nothing else was going to say so',
          'props', jsonb_build_object('run', r ->> 'id', 'workflow', r ->> 'name',
                                      'ref', r ->> 'head_branch', 'conclusion', concl,
                                      'url', r ->> 'html_url')));
      end if;
    elsif status is distinct from 'completed' or concl is null then
      -- Still going, or completed carrying no conclusion at all. An empty
      -- conclusion is not a success and it is not a failure: it is not knowing,
      -- and not knowing for longer than a release has ever taken is a fault.
      if p_now - created > p_overdue and not (seen ? key) then
        out_v := out_v || jsonb_build_array(jsonb_build_object(
          'key', key, 'level', 'error',
          'message', coalesce(r ->> 'name', 'a release build') || ' for ' ||
                     coalesce(r ->> 'head_branch', '?') || ' has been ' ||
                     case when status = 'completed' then 'completed with no conclusion'
                          else coalesce(status, 'in an unknown state') end ||
                     ' for ' || mins || ' minutes; the longest healthy one took 20',
          'props', jsonb_build_object('run', r ->> 'id', 'workflow', r ->> 'name',
                                      'ref', r ->> 'head_branch',
                                      'status', status, 'conclusion', coalesce(concl, 'none'),
                                      'url', r ->> 'html_url')));
      end if;
    end if;
  end loop;

  -- Versions the pipeline owes. This is the check that answers from outside:
  -- it does not care whether a run failed, was never dispatched, or vanished.
  -- It survives the tag being deleted afterwards, which is what happened to
  -- v1.12.15, because the bump commit is what it counts.
  for v in select value from jsonb_each(pend) loop
    ver  := v ->> 'version';
    key  := 'release|owed|' || ver;
    at   := coalesce((v ->> 'at')::timestamptz, (v ->> 'first_seen')::timestamptz, p_now);
    mins := (extract(epoch from p_now - at) / 60)::int;
    select count(*), string_agg(distinct coalesce(x ->> 'conclusion', x ->> 'status'), ', ')
      into n_runs, outcomes
      from jsonb_array_elements(coalesce(p_runs, '[]'::jsonb)) x
      where x ->> 'head_branch' = 'v' || ver;
    if seen ? key then
      continue;
    elsif n_runs = 0 and p_now - at > p_stall then
      out_v := out_v || jsonb_build_array(jsonb_build_object(
        'key', key, 'level', 'error',
        'message', 'crossplay ' || ver || ' was tagged ' || mins ||
                   ' minutes ago and no release build has started; a build starts within seconds',
        'props', jsonb_build_object('version', ver, 'sha', v ->> 'sha',
                                    'age_min', mins, 'builds', 0,
                                    'published', coalesce(latest, 'none'))));
    elsif n_runs > 0 and p_now - at > p_overdue then
      out_v := out_v || jsonb_build_array(jsonb_build_object(
        'key', key, 'level', 'error',
        'message', 'crossplay ' || ver || ' was tagged ' || mins ||
                   ' minutes ago and is still not published; ' || n_runs ||
                   ' build(s), ' || coalesce(outcomes, 'no outcome') ||
                   '. Newest published is ' || coalesce(latest, 'none'),
        'props', jsonb_build_object('version', ver, 'sha', v ->> 'sha',
                                    'age_min', mins, 'builds', n_runs,
                                    'outcomes', outcomes,
                                    'published', coalesce(latest, 'none'))));
    end if;
  end loop;

  -- The watcher's own eyes. Silence from GitHub is not health.
  key := 'release|blind';
  if p_last_ok is null or p_now - p_last_ok > p_blind then
    if not (seen ? key) then
      out_v := out_v || jsonb_build_array(jsonb_build_object(
        'key', key, 'level', 'error',
        'message', 'the release watcher has had no answer from GitHub since ' ||
                   coalesce(p_last_ok::text, 'ever') || '; it can see nothing and is not evidence of health',
        'props', jsonb_build_object('last_ok', p_last_ok)));
    end if;
  elsif seen ? key then
    out_v := out_v || jsonb_build_array(jsonb_build_object(
      'key', key, 'level', 'info',
      'message', 'the release watcher can see GitHub again',
      'props', jsonb_build_object('last_ok', p_last_ok)));
  end if;

  return jsonb_build_object(
    'verdicts', out_v,
    'pending', coalesce((select jsonb_agg(value) from jsonb_each(pend)), '[]'::jsonb));
end
$$;

create or replace function relwatch_fire() returns void
language plpgsql security definer set search_path = public, extensions, net as $$
declare
  rid bigint;
  h jsonb := '{"User-Agent":"crossplay-board-relwatch","Accept":"application/vnd.github+json"}'::jsonb;
  g text := 'https://api.github.com/repos/ma-r-s/crossplay';
begin
  perform relwatch_collect();
  delete from relwatch_http where fired_at < now() - interval '20 minutes';
  select net.http_get(url := g || '/actions/workflows/crossplay-release.yml/runs?per_page=6',
                      headers := h, timeout_milliseconds := 20000) into rid;
  insert into relwatch_http (request_id, kind) values (rid, 'release_runs');
  select net.http_get(url := g || '/actions/workflows/crossplay-autorelease.yml/runs?per_page=6',
                      headers := h, timeout_milliseconds := 20000) into rid;
  insert into relwatch_http (request_id, kind) values (rid, 'autorelease_runs');
  select net.http_get(url := g || '/releases/latest',
                      headers := h, timeout_milliseconds := 20000) into rid;
  insert into relwatch_http (request_id, kind) values (rid, 'latest');
  select net.http_get(url := g || '/commits?sha=xteink&per_page=12',
                      headers := h, timeout_milliseconds := 20000) into rid;
  insert into relwatch_http (request_id, kind) values (rid, 'commits');
end
$$;

-- Take whatever answered, decide, and post. Split out from the deciding so the
-- decision can be tested without a network: relwatch_apply is what the tests
-- drive, relwatch_collect is only the plumbing that feeds it.
create or replace function relwatch_apply(
  p_runs jsonb, p_latest jsonb, p_commits jsonb, p_now timestamptz, p_saw_github boolean
) returns jsonb
language plpgsql security definer set search_path = public as $$
declare
  res     jsonb;
  v       jsonb;
  seeding boolean;
  posted  int := 0;
begin
  if p_saw_github then
    update release_state set last_ok_at = p_now where id = 1;
  end if;
  select seeded into seeding from release_state where id = 1;
  seeding := not coalesce(seeding, false);

  -- Arming is what makes the history old news, so it may only happen on a pass
  -- that actually saw the history. An empty pass that arms is worse than no
  -- watcher: the next pass then reads every fault on record as brand new and
  -- opens the whole backlog at once. That is not hypothetical -- the first
  -- relwatch_fire() on the live board armed on its own opening
  -- relwatch_collect(), when nothing had answered yet, and the pass after it
  -- opened six cards for a morning two other cards already covered.
  if seeding and not p_saw_github then
    return jsonb_build_object('posted', 0, 'seeding', true, 'verdicts', 0,
                              'armed', false);
  end if;

  select release_verdicts(
      p_runs, p_latest, p_commits,
      coalesce((select jsonb_agg(to_jsonb(rp)) from release_pending rp), '[]'::jsonb),
      coalesce((select jsonb_agg(key) from release_seen), '[]'::jsonb),
      p_now,
      (select last_ok_at from release_state where id = 1),
      interval '15 minutes', interval '60 minutes', interval '3 hours')
    into res;

  if p_saw_github then
    delete from release_pending where version not in (
      select x ->> 'version' from jsonb_array_elements(res -> 'pending') x);
    insert into release_pending (version, at, sha, first_seen)
      select x ->> 'version', (x ->> 'at')::timestamptz, x ->> 'sha',
             coalesce((x ->> 'first_seen')::timestamptz, p_now)
        from jsonb_array_elements(res -> 'pending') x
      on conflict (version) do update
        set at = coalesce(excluded.at, release_pending.at), sha = coalesce(excluded.sha, release_pending.sha);
  end if;

  for v in select * from jsonb_array_elements(res -> 'verdicts') loop
    if v ->> 'level' = 'error' then
      -- The first pass after this migration lands adjudicates the history and
      -- posts none of it: every run and every owed version already on record
      -- belongs to cards #155 and #158, and a watcher that opens its whole
      -- backlog on arrival is the trap that caught two sessions before this
      -- one. Everything that happens AFTER arming is new.
      insert into release_seen (key, note)
        values (v ->> 'key', case when seeding then 'seeded at install' else v ->> 'message' end)
        on conflict (key) do nothing;
      if not seeding then
        insert into events (service, event, level, fingerprint, props)
          values ('release', 'run', 'error', v ->> 'key',
                  jsonb_build_object('app', 'tooling', 'message', v ->> 'message', 'from', 'board')
                  || coalesce(v -> 'props', '{}'::jsonb));
        posted := posted + 1;
      end if;
    else
      delete from release_seen where key = v ->> 'key';
      if not seeding then
        insert into events (service, event, level, fingerprint, props)
          values ('release', 'run', 'info', v ->> 'key',
                  jsonb_build_object('app', 'tooling', 'host', 'release', 'message', v ->> 'message')
                  || coalesce(v -> 'props', '{}'::jsonb));
        posted := posted + 1;
      end if;
    end if;
  end loop;

  update release_state set seeded = true where id = 1;
  return jsonb_build_object('posted', posted, 'seeding', seeding, 'armed', true,
                            'verdicts', jsonb_array_length(res -> 'verdicts'));
end
$$;

create or replace function relwatch_collect() returns void
language plpgsql security definer set search_path = public, extensions, net as $$
declare
  runs jsonb := '[]'::jsonb;
  latest jsonb;
  commits jsonb;
  body jsonb;
  p record;
  r record;
  got int := 0;
  young boolean := false;
begin
  for p in select * from relwatch_http loop
    select * into r from net._http_response where id = p.request_id;
    if r.id is null then
      if p.fired_at > now() - interval '2 minutes' then young := true; end if;
      continue;
    end if;
    if r.status_code = 200 and r.content is not null then
      begin
        body := r.content::jsonb;
      exception when others then body := null;
      end;
      if body is not null then
        got := got + 1;
        if p.kind in ('release_runs', 'autorelease_runs') then
          runs := runs || coalesce(body -> 'workflow_runs', '[]'::jsonb);
        elsif p.kind = 'latest' then latest := body;
        elsif p.kind = 'commits' then commits := body;
        end if;
      end if;
    end if;
    delete from relwatch_http where request_id = p.request_id;
    delete from net._http_response where id = p.request_id;
  end loop;

  -- Nothing has come back yet and the requests are young: this pass has no
  -- opinion. Saying "healthy" here is the mistake this whole file is about.
  if got = 0 and young then
    return;
  end if;
  -- Four answers or none: a verdict on half the evidence is a guess. With
  -- fewer than four, only the blind rule runs, off last_ok_at.
  perform relwatch_apply(runs, latest, commits, now(), got = 4);
end
$$;

select cron.schedule('relwatch-fire',    '10,40 * * * *', $$select public.relwatch_fire()$$);
select cron.schedule('relwatch-collect', '13,43 * * * *', $$select public.relwatch_collect()$$);
