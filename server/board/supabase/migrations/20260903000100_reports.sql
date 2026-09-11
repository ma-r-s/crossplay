-- What the public report form adds to a card: where it came from, so the
-- function can refuse a flood without a CAPTCHA. The hash is of the CLIENT IP,
-- not of the address. The address itself is stored, in reporter_email, so a
-- reply can reach the sender; board.py shows it. This comment used to say the
-- opposite of both.

alter table cards add column if not exists reporter_hash text;
create index if not exists cards_reporter_recent on cards (reporter_hash, created_at desc)
  where reporter_hash is not null;
