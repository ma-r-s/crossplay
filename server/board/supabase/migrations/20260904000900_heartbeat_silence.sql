-- Silence looks like success. The firmware heartbeat (PR #14) verifies TLS
-- against a root bundle; if the bundle lacks the right root, no device ever
-- posts and nothing says so. So the board asks the question itself: once a
-- day, if a release newer than the heartbeat merge is more than three days
-- old and no firmware heartbeat has ever arrived, post an error event. The
-- error trigger turns it into one card (fixed fingerprint); the first real
-- heartbeat closes it by carrying the same fingerprint as an info event
-- (see heartbeat_silence_check below, which posts that too).
create or replace function heartbeat_silence_check() returns void
language plpgsql security definer set search_path = public as $$
declare
  first_release timestamptz;
  seen bigint;
begin
  select min(at) into first_release from events
    where service = 'release' and at > '2026-09-04 01:00+00';
  select count(*) into seen from events where service = 'firmware' and event = 'heartbeat';
  if seen > 0 then
    insert into events (service, event, level, fingerprint, props)
      values ('firmware', 'heartbeat-silence', 'info', 'firmware|no-heartbeat',
              jsonb_build_object('heartbeats', seen));
  elsif first_release is not null and first_release < now() - interval '3 days' then
    insert into events (service, event, level, fingerprint, props)
      values ('firmware', 'heartbeat-silence', 'error', 'firmware|no-heartbeat',
              jsonb_build_object('message', 'no device has posted a heartbeat since the release that carries it (' ||
                to_char(first_release, 'YYYY-MM-DD') || '); the TLS root bundle or the config fetch is the first suspect',
                'app', 'firmware', 'first_release', first_release));
  end if;
end
$$;
select cron.schedule('heartbeat-silence', '25 4 * * *', $$select public.heartbeat_silence_check()$$);
