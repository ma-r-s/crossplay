-- Who reported each card.
--
-- Mario asked "what have I reported?" and the board could not answer: every
-- card was source 'session', 'error', 'import' or 'site', and reporter_email
-- was null on all but one. `source` says by what MECHANISM a card arrived;
-- nothing said WHO the observation belonged to, so a bug he hit on his own
-- device and a bug an audit found by reading code were indistinguishable.
--
-- The column is deliberately NOT defaulted to 'session'. A path that forgets
-- to stamp must be visible as 'unknown' rather than silently claim one of our
-- own sessions found it, because the value of the answer is that he can trust
-- the list.
--
--   mario    he experienced it, asked for it, or ruled on it, and said so
--   user     a person who is not Mario: the public report form, a GitHub issue
--   session  our own side found it: an audit, a gate, a cold review, a probe,
--            or the board's own error trigger
--   unknown  the card carries no evidence either way

alter table cards add column if not exists reporter text not null default 'unknown'
  check (reporter in ('mario', 'user', 'session', 'unknown'));
create index if not exists cards_reporter on cards (reporter, created_at desc);

-- The recovery, applied once. Every 'mario' row below was established from the
-- card's own text or from a verbatim run of eight or more words matched against
-- a real user message in the session transcripts; the evidence for each is in
-- docs/workflow/what-mario-reported.md. Cards filed after this migration carry
-- what the board stamps at creation, so this backfill is a one-off and reruns
-- as a no-op on ids that no longer exist.

-- mario: 57 cards
update cards set reporter = 'mario' where id in (
  49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 93, 107,
  108, 112, 125, 127, 130, 131, 138, 166, 179, 191, 201, 236, 240, 241,
  243, 244, 246, 247, 248, 249, 250, 252, 253, 257, 260, 261, 262, 264,
  266, 293, 295, 302, 305, 311, 313, 327, 328, 329, 348, 349, 351, 365,
  370
);

-- user: 2 cards
update cards set reporter = 'user' where id in (
  13, 207
);

-- session: 295 cards
update cards set reporter = 'session' where id in (
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 27, 28, 29, 30,
  31, 32, 33, 34, 35, 37, 38, 39, 41, 42, 44, 45, 46, 47,
  48, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
  74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87,
  88, 89, 90, 91, 92, 94, 95, 96, 97, 98, 99, 101, 102, 103,
  104, 105, 106, 109, 110, 113, 114, 115, 116, 118, 119, 120, 121, 122,
  123, 124, 126, 128, 129, 133, 134, 135, 136, 137, 139, 140, 141, 142,
  143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156,
  157, 158, 167, 168, 169, 170, 171, 172, 174, 175, 176, 177, 178, 180,
  181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 192, 193, 194, 195,
  196, 197, 198, 199, 200, 202, 203, 204, 205, 206, 208, 209, 210, 211,
  212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
  226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 237, 238, 239, 242,
  245, 251, 254, 255, 256, 258, 259, 263, 265, 267, 268, 269, 270, 271,
  272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285,
  286, 287, 288, 289, 290, 291, 292, 294, 296, 297, 298, 299, 300, 301,
  303, 304, 306, 307, 308, 309, 310, 312, 314, 315, 316, 317, 318, 319,
  320, 321, 322, 323, 324, 325, 326, 330, 331, 332, 333, 334, 335, 336,
  337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 350, 352, 353,
  354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 366, 367, 368,
  369
);

-- unknown: 6 cards, left at the column default on purpose:
--   #25, #36, #40, #43, #100, #111

-- Two mechanisms answer the question by themselves, so they are derived rather
-- than left for a caller to remember. A card the error trigger opens from a
-- probe or a crash is our own side by construction; a card `board issues` opens
-- from a GitHub issue was written by a person who is not Mario. Everything else
-- keeps what the writer stamped, including an explicit 'unknown' -- the whole
-- point of the default is that a path which forgets stays visible.
create or replace function cards_reporter_from_source() returns trigger
language plpgsql as $$
begin
  if new.reporter = 'unknown' then
    if new.source in ('error', 'sync') then
      new.reporter := 'session';
    elsif new.source = 'github' then
      new.reporter := 'user';
    end if;
  end if;
  return new;
end;
$$;

drop trigger if exists cards_reporter_from_source on cards;
create trigger cards_reporter_from_source
  before insert on cards
  for each row execute function cards_reporter_from_source();
