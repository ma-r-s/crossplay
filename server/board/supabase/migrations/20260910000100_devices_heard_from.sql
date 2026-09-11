-- One plain number for "how many devices use CrossPlay": distinct devices
-- heard from in each window, over every event that carries a device (a
-- service used, an update checked). Mario, 2026-09-10: the inbox showed a
-- SUM over devices_by_version, which counts a device once per version it
-- ran inside the window (57 shown, 51 real), beside GitHub's download
-- counts, and he could not tell how many devices there were. The longest
-- window is 90 days because the rollup deletes raw rows past that, and
-- events_rollup keeps distinct devices per day, which cannot be summed
-- across days.
create or replace view devices_heard_from as
  select count(distinct device) filter (where at > now() - interval '1 day') as h24,
         count(distinct device) filter (where at > now() - interval '7 days') as d7,
         count(distinct device) filter (where at > now() - interval '30 days') as d30,
         count(distinct device) as d90
  from events
  where device is not null and device <> '' and at > now() - interval '90 days';
alter view devices_heard_from set (security_invoker = true);
