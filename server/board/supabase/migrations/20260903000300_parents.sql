-- Subtasks: a card may sit under another card. `board list` shows children
-- indented under their parent; a parent's state is its own, not derived.
alter table cards add column if not exists parent bigint references cards (id) on delete set null;
create index if not exists cards_parent on cards (parent) where parent is not null;
