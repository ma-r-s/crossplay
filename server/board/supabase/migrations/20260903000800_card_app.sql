-- Two additions to the 000200 trigger. An error event may name the app whose
-- owner gets the card (props.app): the pulse posts as "pulse" but a dead
-- books host is Get Books' problem. And an info event carrying a fingerprint
-- closes that fingerprint's open error card: the outage was a card exactly
-- as long as it lasted.
create or replace function events_before_insert() returns trigger
language plpgsql security definer set search_path = public as $$
declare
  fp text;
  msg text;
  open_card bigint;
  new_card bigint;
  app text;
begin
  -- An info event that carries a fingerprint says that problem is gone: the
  -- open card for it closes by itself, with a line saying what answered.
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
