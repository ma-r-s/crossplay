-- What the public report form adds to a card: where it came from, so the
-- function can refuse a flood from one address without a CAPTCHA. The address
-- is stored hashed; the function never stores or shows the address itself.

alter table cards add column if not exists reporter_hash text;
create index if not exists cards_reporter_recent on cards (reporter_hash, created_at desc)
  where reporter_hash is not null;
