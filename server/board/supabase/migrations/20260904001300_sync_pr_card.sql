-- A sync run that opened a pull request is work waiting for a reviewer, and
-- the orchestrator moves states from cards, so the run's info event opens a
-- task card in review (one per pull request). Before this, PR #43 sat on
-- GitHub with its URL in an event nobody reads.
alter table cards drop constraint if exists cards_source_check;
alter table cards add constraint cards_source_check
  check (source in ('session', 'site', 'import', 'github', 'error', 'sync'));

create or replace function events_before_insert() returns trigger
language plpgsql security definer set search_path = public as $$
declare
  fp text;
  msg text;
  open_card bigint;
  new_card bigint;
  app text;
  pr text;
begin
  if new.level <> 'error' then
    -- An info event that carries a fingerprint says that problem is gone: the
    -- open card for it closes by itself, with a line saying what answered.
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
    -- A sync run whose result is a pull request: a card in review, once.
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
$$;
