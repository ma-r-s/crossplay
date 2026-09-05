-- A third claim: the dispatcher, the one session Mario talks to. It may
-- message any session (to hand a card to its owner) and may end a turn on a
-- question (its one clarifying question to Mario).
alter table claims drop constraint if exists claims_name_check;
alter table claims add constraint claims_name_check
  check (name in ('orchestrator', 'integrator', 'dispatcher'));
