-- A card addressed to Mario is an item in his inbox, by construction.
--
-- The inbox view is the open `mario` blockers and nothing else, and a card is
-- not a blocker. So a card filed on app `mario` -- the app that by convention
-- already means "only Mario can decide this" -- reached him only if somebody
-- also remembered to block on it. Twice nobody did: cards 75 and 84 were his
-- decisions and aged a day in `reported` while his inbox said nothing needs
-- you. That is a dropped message, not a delay.
--
-- The rule lives here as well as in the CLI because the CLI is not the only
-- writer: the site's report function, the inbox page and a hand-typed UPDATE
-- all reach `cards` directly, and a rule that only one writer obeys is prose
-- with extra steps.
--
-- The blocker carries a default. One with no stated "what happens if you
-- never answer" forces him to engage before he can safely ignore it, which is
-- exactly how an inbox becomes noise and stops being read. This wording is a
-- placeholder the filer's own words replace: `board new --default` and
-- `board app <id> mario --default` overwrite it on the blocker this opens.
--
-- Idempotent on the one thing that matters, an open `mario` blocker: a card
-- that has one gets no second one, so moving a card to `mario` twice files
-- one blocker in total.

create or replace function cards_mario_inbox() returns trigger
language plpgsql security definer set search_path = public as $$
declare
  next_n integer;
begin
  -- A decision already taken is not one to ask again. Without this the two
  -- halves of this file disagree: the backfill below skips settled cards, but
  -- a board restored by INSERTing a dump would fire this trigger for every
  -- settled decision it ever held, which is the flood the backfill avoids.
  if new.state in ('done', 'released', 'parked') then
    return null;
  end if;
  if exists (select 1 from blockers where card_id = new.id and open and need = 'mario') then
    return null;
  end if;
  select coalesce(max(n), 0) + 1 into next_n from blockers where card_id = new.id;
  insert into blockers (card_id, n, need, ask, "default", by_session)
    values (new.id, next_n, 'mario', new.title,
            'nothing happens until he answers', 'board');
  insert into history (card_id, what)
    values (new.id, 'blocked (mario): ' || new.title);
  return null;
end
$$;

-- Filed there.
drop trigger if exists cards_mario_inbox_ins on cards;
create trigger cards_mario_inbox_ins after insert on cards
  for each row when (new.app = 'mario')
  execute function cards_mario_inbox();

-- Moved there. `update of app` plus the distinct-from guard so that editing
-- the title or state of a card already on his desk is not a second delivery.
drop trigger if exists cards_mario_inbox_upd on cards;
create trigger cards_mario_inbox_upd after update of app on cards
  for each row when (new.app = 'mario' and old.app is distinct from new.app)
  execute function cards_mario_inbox();

-- The cards that were already dropped. Only the ones still open: a decision
-- that has since been taken does not need asking again, and an inbox that
-- opens with a pile of settled questions is one nobody reads. On the board as
-- it stands every card on app `mario` is `done`, so this backfills nothing
-- today and is here because the next board restored from a dump will not be.
with filed as (
  insert into blockers (card_id, n, need, ask, "default", by_session)
  select c.id,
         coalesce((select max(b.n) from blockers b where b.card_id = c.id), 0) + 1,
         'mario', c.title, 'nothing happens until he answers', 'board'
  from cards c
  where c.app = 'mario'
    and c.state not in ('done', 'released', 'parked')
    and not exists (
      select 1 from blockers b where b.card_id = c.id and b.open and b.need = 'mario'
    )
  returning card_id
)
insert into history (card_id, what)
select f.card_id, 'blocked (mario): ' || c.title
from filed f join cards c on c.id = f.card_id;
