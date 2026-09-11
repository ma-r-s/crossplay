-- The owner's facts, one view each, so the inbox page can lead with them.
-- Mario, 2026-09-10: the page showed everything about the workshop and
-- nothing he cared about. What an owner wants: how many devices, what
-- they run right now with each device counted ONCE (devices_by_version
-- counts a device once per version it ran inside the window), whether
-- they crash, and who wrote in.

-- Each device once, at the version it last reported, over the devices
-- heard from in the last 30 days. since = when it arrived on that version
-- (the first event on it after the last event on any other version).
create or replace view devices_now as
  with latest as (
    select distinct on (device) device, board, version, at as last_seen
    from events
    where device is not null and device <> '' and version is not null
      and at > now() - interval '30 days'
    order by device, at desc),
  spans as (
    select device, min(at) as first_seen, count(distinct version) as versions_run
    from events where device is not null and device <> '' group by 1)
  select l.device, coalesce(l.board, 'unknown') as board, l.version,
         (select min(e.at) from events e
           where e.device = l.device and e.version = l.version
             and e.at > coalesce((select max(x.at) from events x
                                   where x.device = l.device and x.version is not null and x.version <> l.version),
                                 '1970-01-01'::timestamptz)) as since,
         p.first_seen, l.last_seen, p.versions_run
  from latest l join spans p using (device)
  order by l.last_seen desc;
alter view devices_now set (security_invoker = true);

-- What the field runs right now: the same devices, grouped. The sum of
-- this table IS the number of devices.
create or replace view versions_now as
  select version, board, count(*) as devices
  from devices_now group by 1, 2 order by 3 desc, 1 desc, 2;
alter view versions_now set (security_invoker = true);

-- Is the firmware hurting anyone this week: devices that reported a panic,
-- devices whose install failed, devices whose install went through.
create or replace view field_7d as
  select count(distinct device) filter (where event = 'crash') as crashed,
         count(distinct device) filter (where event = 'update' and level = 'error') as update_failed,
         count(distinct device) filter (where event = 'update' and level = 'info') as updated
  from events
  where service = 'firmware' and device is not null and device <> '' and at > now() - interval '7 days';
alter view field_7d set (security_invoker = true);

-- The panics themselves, by reason, devices first: one device crashing ten
-- times is one device.
create or replace view crashes_7d as
  select coalesce(props->>'message', '(no reason recorded)') as message,
         count(distinct device) as devices, count(*) as times, max(at) as last
  from events
  where service = 'firmware' and event = 'crash' and at > now() - interval '7 days'
  group by 1 order by 2 desc, 3 desc limit 10;
alter view crashes_7d set (security_invoker = true);

-- Growth: devices first heard from this week and the week before. Read
-- with card 463 in mind: before it, every USB reinstall was a "new" device.
create or replace view devices_new as
  with f as (select device, min(at) as first_seen from events
             where device is not null and device <> '' group by 1)
  select count(*) filter (where first_seen > now() - interval '7 days') as this_week,
         count(*) filter (where first_seen <= now() - interval '7 days'
                            and first_seen > now() - interval '14 days') as last_week
  from f;
alter view devices_new set (security_invoker = true);
