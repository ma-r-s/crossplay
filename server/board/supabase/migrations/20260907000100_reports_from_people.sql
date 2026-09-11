-- Reports from people, as their own thing in Mario's inbox.
--
-- The inbox is `open blockers that need mario` and nothing else. A blocker
-- means a session cannot proceed, so a stranger's report had no way into the
-- inbox at all: three sat in `reported` for a day while the page he reads said
-- the only things needing him were two sessions' asks. `board list --reporter
-- user` found them, but a filter he has to remember to apply is exactly the
-- thing he said was missing.
--
-- Two decisions are recorded here because they are the whole design.
--
-- 1. NOT a blocker. Turning every report into a `mario` blocker would have
--    reused the answer path for free, and it would have cost the meaning of
--    the inbox: `inbox_latency` and `workflow_weekly.asks_to_mario` measure how
--    long a SESSION waits on him, and nobody is blocked on "nice firmware,
--    thanks". A report also has no honest `default` and no steps, so two thirds
--    of the blocker shape would have been filler. It gets its own section
--    above the asks instead: unmissable without being longer.
--
-- 2. Read once, not open forever. `state` alone cannot carry this. A report
--    triaged by a session an hour after it arrives leaves `reported` and would
--    vanish before he ever saw it; a report nobody triages would sit in his
--    face forever and become wallpaper, which is how an inbox stops being
--    read. `mario_seen_at` is the mechanism: each report interrupts him
--    exactly once, and marking it read is his act, not a session's.
--
-- Nothing is backfilled. Settled cards are excluded by state, so #13 (released,
-- the slow page turns from a GitHub issue) never appears, and #389, #425 and
-- #426 are unread because he has not read them.

alter table cards add column if not exists mario_seen_at timestamptz;
create index if not exists cards_unread_reports on cards (created_at)
  where reporter = 'user' and mario_seen_at is null;

-- What a person reported and nobody has shown him yet. `unread` is left to the
-- caller (mario_seen_at is null) so the same view can also answer "what did
-- people report" over the ones he has already read.
--
-- The address is here because replying is the point: the report form asks for
-- one and stores it, and until now nothing ever offered it to him. It is a
-- private view behind the inbox's passphrase and row security, the same door
-- as every other card field.
create or replace view reports_from_people as
  select c.id, c.title, c.body, c.app, c.kind, c.state, c.device, c.version,
         c.reporter_email, c.photo_path, c.created_at, c.mario_seen_at,
         round(extract(epoch from now() - c.created_at) / 3600)::int as age_h
  from cards c
  where c.reporter = 'user'
    and c.state not in ('done', 'released', 'parked')
  order by c.created_at;
alter view reports_from_people set (security_invoker = true);
