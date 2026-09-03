-- An ask that is a thing to do carries its steps, numbered lines the owner
-- writes, shown under the message. Without them Mario went back to old
-- conversations to find out how; that is the failure this removes.
alter table blockers add column if not exists steps text;
create or replace view inbox as
  select b.id as blocker_id, b.n, c.id as card_id, c.title, c.app, c.kind, c.body,
         c.state, b.ask, b."default", b.steps, b.created_at
  from blockers b join cards c on c.id = b.card_id
  where b.open and b.need = 'mario'
  order by b.created_at;
alter view inbox set (security_invoker = true);
