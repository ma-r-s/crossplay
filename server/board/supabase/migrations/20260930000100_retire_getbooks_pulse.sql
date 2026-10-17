-- Get Books was retired on 2026-09-30: the app is gone from the firmware and
-- its server on the orange pi is stopped. Its pulse row would open a "books is
-- down" card every night for a host that is meant to be down, so it goes too.
delete from pulse_targets where host = 'books';
