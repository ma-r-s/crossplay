-- Enough of Supabase for the board's own migrations to run on a stock
-- postgres. Everything here is a stand-in for a platform service, never for a
-- line of this repository's logic: the migrations themselves are applied
-- verbatim (run.sh comments out only the two `create extension` lines for
-- pg_net and pg_cron, which are not available outside Supabase), so the
-- triggers, functions and policies under test are the ones that run on the
-- board.

create schema if not exists auth;
create schema if not exists extensions;
create schema if not exists storage;
create schema if not exists cron;
create schema if not exists net;
do $$ begin
  if not exists (select 1 from pg_roles where rolname = 'anon') then create role anon; end if;
  if not exists (select 1 from pg_roles where rolname = 'authenticated') then create role authenticated; end if;
end $$;

-- Nobody is signed in during the tests; is_allowed() is therefore false, which
-- is what row security is for and has nothing to do with what is asserted here
-- (the suite connects as the owner, who bypasses it, exactly as the service
-- key does on the board).
create or replace function auth.jwt() returns jsonb language sql stable as $$ select '{}'::jsonb $$;
create or replace function auth.uid() returns uuid language sql stable as $$ select null::uuid $$;

create table if not exists storage.buckets (id text primary key, name text, public boolean);
create table if not exists storage.objects (id bigserial primary key, bucket_id text, name text);
alter table storage.objects enable row level security;

create table if not exists cron.job (jobid bigserial primary key, jobname text, schedule text, command text);
create or replace function cron.schedule(p_name text, p_sched text, p_cmd text) returns bigint
language sql as $$ insert into cron.job (jobname, schedule, command) values (p_name, p_sched, p_cmd) returning jobid $$;
create or replace function cron.unschedule(p_name text) returns boolean
language sql as $$ delete from cron.job where jobname = p_name returning true $$;

create table if not exists net._http_response (
  id bigint primary key, status_code int, content text, headers jsonb,
  timed_out boolean default false, error_msg text);
create sequence if not exists net.request_id_seq;
create or replace function net.http_get(url text, params jsonb default '{}', headers jsonb default '{}',
                                        timeout_milliseconds int default 5000) returns bigint
language sql as $$ select nextval('net.request_id_seq') $$;
create or replace function net.http_post(url text, body jsonb default '{}', params jsonb default '{}',
                                         headers jsonb default '{}', timeout_milliseconds int default 5000) returns bigint
language sql as $$ select nextval('net.request_id_seq') $$;
