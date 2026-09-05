-- Devices are counted from what they use, not from a heartbeat.
--
-- Mario's call (2026-09-04): a device never spends its radio on the board's
-- numbers. There is no heartbeat event any more; instead every request a
-- device makes to a CrossPlay service carries its id, board and a small
-- report, and the service posts the usage event (getbooks/search, anki/sync,
-- instapaper/sync) under that id with the version and the health numbers,
-- plus firmware/crash and firmware/update events for what the report
-- carried (docs/workflow/events.md, "What a device sends").
--
-- So the two device views read every event with a device on it, a third
-- averages the battery those events carry, and the nightly "no heartbeat
-- ever arrived" check goes: silence from a device that never touches a
-- service is now what silence means, and the pulse already watches the
-- services themselves.

-- Same columns as before, so create or replace keeps the grants and the
-- security_invoker option; restated below anyway.
create or replace view devices_by_version as
  select coalesce(board, 'unknown') as board, version,
         count(distinct device) as devices
  from events
  where device is not null and version is not null and at > now() - interval '7 days'
  group by 1, 2 order by 1, 2 desc;

create or replace view daily_active_devices as
  select date_trunc('day', at)::date as day, count(distinct device) as devices
  from events
  where device is not null and at > now() - interval '30 days'
  group by 1 order by 1 desc;

-- Battery by (board, version): each device's average over its own events
-- first, then the average of those, so a reader that syncs ten times a day
-- weighs the same as one that syncs once. devices is the denominator the
-- number needs beside it; n is how many reports it rests on.
drop view if exists battery_by_version;
create view battery_by_version as
  with per_device as (
    select coalesce(board, 'unknown') as board, coalesce(version, 'unknown') as version,
           device,
           avg((props ->> 'battery_pct')::numeric) as pct,
           count(*) as n
    from events
    where device is not null
      and jsonb_typeof(props -> 'battery_pct') = 'number'
      and at > now() - interval '7 days'
    group by 1, 2, 3
  )
  select board, version,
         round(avg(pct), 1) as battery_pct,
         count(*) as devices,
         sum(n) as n
  from per_device
  group by 1, 2 order by 1, 2 desc;

alter view devices_by_version set (security_invoker = true);
alter view daily_active_devices set (security_invoker = true);
alter view battery_by_version set (security_invoker = true);

-- The heartbeat-silence check (20260904000900) watched for a post that will
-- never come now. Unschedule it if it is scheduled (cron.unschedule raises
-- on a name it does not know), then drop the function.
do $$
begin
  if exists (select 1 from cron.job where jobname = 'heartbeat-silence') then
    perform cron.unschedule('heartbeat-silence');
  end if;
end
$$;
drop function if exists heartbeat_silence_check();
