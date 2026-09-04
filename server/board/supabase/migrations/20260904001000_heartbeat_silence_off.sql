-- The firmware no longer posts a heartbeat: a device never makes a request of
-- its own to report. It rides a device id, the board and a small report as
-- headers on the requests it makes to CrossPlay's own services anyway, and
-- the services post the events (docs/workflow/events.md). A check that waits
-- for a firmware/heartbeat row would therefore open one card every night for
-- a silence that is by design. Any card it already opened closes by hand.
select cron.unschedule('heartbeat-silence');
drop function if exists heartbeat_silence_check();
