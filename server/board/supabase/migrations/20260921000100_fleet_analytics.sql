-- Fleet analytics: what an owner asks about a device, a version and a service.
--
-- Mario, 2026-09-21: the inbox "allows none of this and gives really useless
-- info and omits the good info". Four things he named, and one this file adds
-- because the other three are not readable without it:
--
--   1. The versions table double-listed every version, once per board, because
--      versions_now was keyed (version, board). One row per version now, with
--      the boards as a jsonb map so a board nobody has built yet appears by
--      itself rather than waiting for a column to be added here.
--   2. Per device: which versions it has run and when it moved between them.
--   3. Per device: which services it has used.
--   4. Per service: how many devices, and that service's own numbers.
--   5. Ordering. "1.13.9" sorts AFTER "1.13.15" as text, which put the newest
--      release in the middle of the table. version_key() makes the comparison
--      numeric.
--
-- Every view here reads `events` and nothing else, so it inherits the one
-- honest limit the page has to keep saying out loud: a device is counted when
-- it uses a service or checks for an update, and a reader that only reads is
-- never heard from. These are floors, not totals.

-- "1.13.15" -> {1,13,15}, for ordering that is not alphabetical. Immutable so
-- an index could use it later; null for anything that is not a dotted number
-- ("unknown", a branch build), which then sorts last rather than throwing.
create or replace function version_key(v text) returns int[]
  language sql immutable as $$
  select case
    when v ~ '^[0-9]+(\.[0-9]+)*'
      then string_to_array(substring(v from '^[0-9]+(?:\.[0-9]+)*'), '.')::int[]
    else null
  end;
$$;

-- What the field runs right now: ONE ROW PER VERSION, newest first, with the
-- count of each board on it. The sum of `devices` is still exactly the number
-- of devices, because devices_now holds each device once.
--
-- Replaces the (version, board, devices) shape, which repeated 1.13.15 once
-- per board and made a reader count the rows to answer "how many are on the
-- latest". Dropped rather than replaced because the column list changes.
drop view if exists versions_now;
create view versions_now as
  with per_board as (
    select version, coalesce(board, 'unknown') as board, count(*) as devices
    from devices_now group by 1, 2)
  select version,
         sum(devices)::bigint as devices,
         jsonb_object_agg(board, devices) as boards
  from per_board
  group by 1
  order by version_key(version) desc nulls last, version desc;
alter view versions_now set (security_invoker = true);

-- Every version a device has run, and when it was on it. One row per
-- (device, version): first_at is when that device first reported the version,
-- last_at the most recent. The page reads this as an update history, newest
-- first, and it is the only place that answers "has this reader ever updated".
--
-- No window: a device's history is the point, and events older than 90 days
-- are rolled up and deleted (20260903000600_observability.sql), so this is
-- already bounded by retention rather than by a clause here.
create or replace view device_versions as
  select device, version,
         min(at) as first_at, max(at) as last_at, count(*) as events
  from events
  where device is not null and device <> ''
    and version is not null and version <> ''
  group by 1, 2
  order by device, version_key(version) desc nulls last, version desc;
alter view device_versions set (security_invoker = true);

-- Which services a device has used, with the last time and whether any of it
-- errored. This is only ever the services that talk to a server: an app that
-- runs entirely on the device (every game, the reader itself) posts nothing
-- and cannot appear here. The page says so rather than letting the short list
-- read as "this reader does nothing".
create or replace view device_services as
  select device, service,
         count(*) as events,
         min(at) as first_at,
         max(at) as last_at,
         count(*) filter (where level = 'error') as errors
  from events
  where device is not null and device <> ''
  group by 1, 2
  order by device, max(at) desc;
alter view device_services set (security_invoker = true);

-- Per service: how many devices use it and what it does. service_users
-- (20260903000200) answers only the 7-day device count; this carries 30 days
-- beside it so a service used weekly does not read as dying, the error count
-- so a service that is up but failing is visible, and first/last so a service
-- that has stopped posting is obvious.
--
-- A service that has never posted has NO ROW here, which is the case that
-- matters most (Live posted nothing at all until 2026-09-21). The page holds
-- the list of services that are supposed to exist and prints the missing ones
-- as "nothing yet", because a table can only ever show what it collected.
create or replace view service_metrics as
  select service,
         count(distinct device) filter (
           where device is not null and device <> '' and at > now() - interval '7 days') as devices_7d,
         count(distinct device) filter (
           where device is not null and device <> '' and at > now() - interval '30 days') as devices_30d,
         count(*) filter (where at > now() - interval '7 days') as events_7d,
         count(*) filter (where at > now() - interval '30 days') as events_30d,
         count(*) filter (where level = 'error' and at > now() - interval '7 days') as errors_7d,
         min(at) as first_at,
         max(at) as last_at
  from events
  group by 1
  order by 2 desc nulls last, 1;
alter view service_metrics set (security_invoker = true);

-- Live, counted honestly.
--
-- Live's own records were 67 fridges from 19 hours of one person testing,
-- because every visit to the pairing screen minted one. Two different numbers
-- were being read as the same number, so both are here and neither is called
-- "users":
--
--   codes_shown  a reader asked for a pairing code. Curiosity, nothing more.
--   paired       a browser claimed a code. Somebody at the other end typed it.
--   checked_in   the reader came back and asked for a picture at least once.
--   returning    it checked in MORE THAN ONCE, which is the first number here
--                that means the thing is actually on somebody's fridge.
--
-- props.fridge is the pseudonymous fridge id the service posts on every Live
-- event; `device` is only set when the reader's own header was on the request,
-- so reader_devices is how many of these join to the rest of the fleet.
create or replace view live_fridges as
  select
    count(distinct props ->> 'fridge') filter (where event = 'pair-start') as codes_shown,
    count(distinct props ->> 'fridge') filter (where event = 'paired') as paired,
    count(distinct props ->> 'fridge') filter (where event = 'checkin') as checked_in,
    count(distinct props ->> 'fridge') filter (
      where event = 'checkin' and jsonb_typeof(props -> 'n') = 'number'
        and (props ->> 'n')::numeric >= 2) as returning,
    count(distinct device) filter (
      where event = 'checkin' and device is not null and device <> '') as reader_devices,
    count(*) filter (where event = 'image') as pictures_sent,
    max(at) as last_at
  from events
  where service = 'live' and at > now() - interval '30 days';
alter view live_fridges set (security_invoker = true);
