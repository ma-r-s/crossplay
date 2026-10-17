-- The board stops being a pile. Mario, 2026-09-20: "everyone is happy to
-- create cards and no one ever remembers to close them. I have 500. Most have
-- been fixed or never existed. We are never getting through 500."
--
-- Measured that day: 547 cards in 17 days, 203 open, 145 of those filed by
-- sessions, 113 older than two weeks and never claimed by anyone; 24 whose
-- pull request had merged and whose card nobody closed; 22 of 29 field alarms
-- silent for days. Filing was mandatory and closing was memory, and a card
-- filed by a session had no way out: nothing on the board is worked without
-- Mario's word, and he does not read the board.
--
-- So: a card exists only while someone is on the hook for it (work in
-- progress, something a person said, an alarm still ringing), and every
-- closing that used to be somebody's memory is done here.

-- 1. What a session notices is a NOTICE, not a card: one line that expires by
-- itself. Seen again, it counts up and lives longer; seen three times it is
-- shown to Mario as one line. Nothing else ever has to be done about it.
create table if not exists notices (
  id bigint generated always as identity primary key,
  app text not null default 'unknown',
  what text not null,
  detail text not null default '',
  seen int not null default 1,
  sessions text[] not null default '{}',
  first_seen timestamptz not null default now(),
  last_seen timestamptz not null default now(),
  expires_at timestamptz not null default now() + interval '14 days',
  promoted_card bigint references cards (id)
);
alter table notices enable row level security;
drop policy if exists notices_read_allowed on notices;
create policy notices_read_allowed on notices for select to authenticated using (is_allowed());

create or replace view notices_recurring as
  select id, app, what, seen, first_seen, last_seen
  from notices
  where seen >= 3 and promoted_card is null and expires_at > now()
  order by seen desc, last_seen desc;
alter view notices_recurring set (security_invoker = true);

-- 2. The board in one row, for the top of the inbox: what people asked for
-- and is still open, what is being worked on, which alarms are ringing.
create or replace view board_now as
  select count(*) filter (where reporter in ('mario', 'user') and state in ('reported', 'triaged'))::int as from_people,
         count(*) filter (where state in ('working', 'review', 'merged'))::int as in_progress,
         count(*) filter (where source = 'error')::int as alarms,
         (select count(*)::int from notices where expires_at > now() and promoted_card is null) as notices
  from cards
  where state not in ('done', 'released', 'parked');
alter view board_now set (security_invoker = true);

-- 3. Closing, by the clock instead of by memory. Runs daily; the numbers are
-- arguments so the one-off clean-up of 2026-09-20 could use shorter ones.
-- Nothing here touches a card a person filed, a card with an open question
-- on it, or the child of an open card a person filed.
create or replace function board_expire(unclaimed_days int default 14, silent_days int default 7,
                                        merged_days int default 2, stalled_days int default 14)
returns table (rule text, closed int)
language plpgsql security definer set search_path = public as $$
declare
  r record;
  c int;
begin
  c := 0;
  for r in
    select cd.id from cards cd join error_fingerprints f on f.card_id = cd.id
    where cd.source = 'error' and cd.state in ('reported', 'triaged') and cd.session is null
      and f.last_seen < now() - make_interval(days => silent_days)
  loop
    update cards set state = 'done', updated_at = now() where id = r.id;
    insert into history (card_id, what) values (r.id,
      format('closed by itself: the error has not been seen for %s days. If it comes back, a new card opens.', silent_days));
    c := c + 1;
  end loop;
  rule := 'alarm gone quiet'; closed := c; return next;

  c := 0;
  for r in
    select cd.id from cards cd
    where cd.state = 'merged' and cd.updated_at < now() - make_interval(days => merged_days)
  loop
    update cards set state = 'done', updated_at = now() where id = r.id;
    insert into history (card_id, what) values (r.id, 'closed by itself: merged, and nothing else was ever going to move it on.');
    c := c + 1;
  end loop;
  rule := 'merged and left open'; closed := c; return next;

  c := 0;
  for r in
    select cd.id from cards cd
    where cd.reporter in ('session', 'unknown') and cd.source not in ('error', 'site', 'github')
      and cd.state in ('reported', 'triaged') and cd.session is null
      and cd.created_at < now() - make_interval(days => unclaimed_days)
      and not exists (select 1 from blockers b where b.card_id = cd.id and b.open)
      and not exists (select 1 from cards p where p.id = cd.parent
                        and p.state not in ('done', 'released', 'parked') and p.reporter in ('mario', 'user'))
  loop
    update cards set state = 'parked', updated_at = now() where id = r.id;
    insert into history (card_id, what) values (r.id,
      format('expired: a session filed it and nobody claimed it in %s days. Still searchable; if it is real it will be noticed again.', unclaimed_days));
    c := c + 1;
  end loop;
  rule := 'filed by a session, never claimed'; closed := c; return next;

  c := 0;
  for r in
    select cd.id from cards cd
    where cd.reporter in ('session', 'unknown') and cd.state in ('working', 'review')
      and cd.updated_at < now() - make_interval(days => stalled_days)
      and not exists (select 1 from blockers b where b.card_id = cd.id and b.open)
  loop
    update cards set state = 'parked', updated_at = now() where id = r.id;
    insert into history (card_id, what) values (r.id,
      format('expired: a session''s own work, untouched for %s days.', stalled_days));
    c := c + 1;
  end loop;
  rule := 'a session''s work gone stale'; closed := c; return next;

  delete from notices where expires_at < now() and promoted_card is null;
  get diagnostics c = row_count;
  rule := 'notices expired'; closed := c; return next;
end $$;

do $$ begin
  if exists (select 1 from cron.job where jobname = 'board-expire') then
    perform cron.unschedule('board-expire');
  end if;
end $$;
select cron.schedule('board-expire', '23 5 * * *', $$select * from public.board_expire()$$);

-- 4. The events trigger, two changes:
--    a. A MERGED pull request closes its card. crossplay-board.yml posts
--       workflow/merged with the branch; the card bound to that branch is done.
--    b. One error from one device is an event, not a bug. A service's error
--       opens a card when it has been seen three times, or on two devices, in
--       seven days ("The device has been lost." was somebody unplugging a
--       cable, and it was a card). The infrastructure alarms (release, pulse,
--       upstream-sync, workflow) still open at once: one failed release is
--       already the whole story.
create or replace function public.events_before_insert()
 returns trigger
 language plpgsql
 security definer
 set search_path to 'public'
as $function$
declare
  fp text;
  msg text;
  open_card bigint;
  new_card bigint;
  app text;
  pr text;
  br text;
  r record;
  seen7 int;
  devs7 int;
begin
  if new.level <> 'error' then
    if new.fingerprint is not null then
      select card_id into open_card from error_fingerprints where fingerprint = new.fingerprint;
      if open_card is not null and exists (
        select 1 from cards where id = open_card and source = 'error' and state not in ('done', 'released', 'parked')
      ) then
        update cards set state = 'done', updated_at = now() where id = open_card;
        insert into history (card_id, what)
          values (open_card, 'recovered: ' || coalesce(new.props ->> 'host', new.service) || ' answers again');
        new.card_id := open_card;
      end if;
    end if;
    br := coalesce(new.props ->> 'branch', '');
    if new.service = 'workflow' and new.event = 'merged' and br <> '' and br <> 'xteink' then
      for r in select id from cards where branch = br and state in ('working', 'review', 'merged') loop
        update cards set state = 'done', updated_at = now() where id = r.id;
        insert into history (card_id, what) values (r.id,
          'closed by itself: pull request ' || coalesce('#' || nullif(new.props ->> 'pr', ''), 'for ' || br) || ' merged');
        new.card_id := r.id;
      end loop;
    end if;
    pr := coalesce(new.props ->> 'result', '');
    if new.service = 'upstream-sync' and new.event = 'run' and pr like 'https://github.com/%/pull/%' then
      select id into open_card from cards
        where source = 'sync' and body like '%' || pr || '%'
          and state not in ('done', 'released', 'parked', 'merged')
        limit 1;
      if open_card is null then
        insert into cards (title, app, kind, body, state, source)
          values (
            left('sync: ' || coalesce(nullif(new.props ->> 'title', ''),
                                      'pull request ' || regexp_replace(pr, '.*/pull/', '#')), 120),
            'tooling', 'task',
            'Opened by the upstream-sync routine: ' || pr ||
              E'\n' || left(coalesce(new.props ->> 'summary', ''), 1500) ||
              E'\nThe critic reviews it; it merges on green like any other pull request (docs/workflow/upstream-sync.md).',
            'review', 'sync')
          returning id into new_card;
        insert into history (card_id, what) values (new_card, 'opened from the sync run: ' || pr);
        new.card_id := new_card;
      else
        new.card_id := open_card;
      end if;
    end if;
    return new;
  end if;
  msg := coalesce(new.props ->> 'message', '');
  fp := coalesce(new.fingerprint, event_fingerprint(new.service, new.event, msg));
  new.fingerprint := fp;
  app := coalesce(nullif(new.props ->> 'app', ''), new.service);

  insert into error_fingerprints (fingerprint, service, message, count, last_seen)
    values (fp, new.service, left(msg, 400), 1, now())
    on conflict (fingerprint) do update
      set count = error_fingerprints.count + 1, last_seen = now()
    returning card_id into open_card;

  if open_card is not null then
    if exists (select 1 from cards where id = open_card and state not in ('done', 'released', 'parked')) then
      new.card_id := open_card;
      return new;
    end if;
  end if;

  if new.service not in ('release', 'pulse', 'upstream-sync', 'workflow') then
    select count(*) + 1,
           count(distinct device) filter (where device is not null and device is distinct from new.device)
             + (case when new.device is not null then 1 else 0 end)
      into seen7, devs7
      from events where fingerprint = fp and level = 'error' and at > now() - interval '7 days';
    if seen7 < 3 and devs7 < 2 then
      return new;  -- counted, not carded
    end if;
  end if;

  insert into cards (title, app, kind, body, state, source, fingerprint)
    values (
      left(new.service || ': ' || coalesce(nullif(msg, ''), new.event), 120),
      app, 'bug',
      'Seen by the ' || new.service || ' service. First message: ' || left(msg, 1000) ||
        E'\nEvent: ' || new.event || E'\nProps: ' || left(new.props::text, 1500),
      'triaged', 'error', fp)
    returning id into new_card;
  insert into history (card_id, what) values (new_card, 'opened from an error event (' || new.service || ')');
  update error_fingerprints set card_id = new_card where fingerprint = fp;
  new.card_id := new_card;
  return new;
end
$function$;
