-- What the release watcher decides, asserted against the real GitHub payloads
-- of 2026-09-04 -- the morning two releases failed four times in five hours and
-- nothing said anything -- and against the two pairs that have made probes
-- report confidently and wrongly before:
--
--   * a build still running is not a build that failed;
--   * an empty conclusion is not a success;
--   * a failure already adjudicated is not a new failure, and a new failure is
--     not silenced by an old one still being open.
--
-- Every fixture under fixtures/ came out of api.github.com (fixtures/capture.sh
-- recaptures them). A payload written from memory agrees with a function that
-- reads the wrong field name; one captured does not.

create table results (n serial primary key, label text, got text, want text, ok boolean);
create or replace function t(p_label text, p_got text, p_want text) returns void
language sql as $$
  insert into results (label, got, want, ok)
  values (p_label, p_got, p_want, p_got is not distinct from p_want)
$$;

-- A boolean got, so a "does it say the right thing" check reads as one line.
create or replace function t(p_label text, p_got boolean, p_want text) returns void
language sql as $$ select t(p_label, p_got::text, p_want) $$;

create or replace function fx(n text) returns jsonb
language sql as $$ select pg_read_file('/tmp/fx/' || n || '.json')::jsonb $$;

-- Both release workflows' runs, as relwatch_collect concatenates them.
create or replace function runs() returns jsonb language sql as $$
  select (fx('release-runs') -> 'workflow_runs') || (fx('autorelease-runs') -> 'workflow_runs')
$$;

create or replace function ekeys(res jsonb, lvl text default 'error') returns text
language sql as $$
  select coalesce(string_agg(v ->> 'key', ' ' order by v ->> 'key'), '')
  from jsonb_array_elements(res -> 'verdicts') v where v ->> 'level' = lvl
$$;

create or replace function saying(res jsonb, k text) returns text
language sql as $$
  select v ->> 'message' from jsonb_array_elements(res -> 'verdicts') v where v ->> 'key' = k
$$;

-- Replace fields on one run, by id, leaving the rest of the real payload alone.
create or replace function with_run(rs jsonb, p_id text, patch jsonb) returns jsonb
language sql as $$
  select jsonb_agg(case when r ->> 'id' = p_id then r || patch else r end)
  from jsonb_array_elements(rs) r
$$;

create or replace function a_run(p_id text, p_ref text, p_status text, p_concl jsonb, p_created text)
returns jsonb language sql as $$
  select jsonb_build_object('id', p_id, 'name', 'CrossPlay release', 'head_branch', p_ref,
                            'status', p_status, 'conclusion', p_concl, 'created_at', p_created,
                            'html_url', 'https://github.com/ma-r-s/crossplay/actions/runs/' || p_id)
$$;

create or replace function a_bump(p_version text, p_at text) returns jsonb language sql as $$
  select jsonb_build_array(jsonb_build_object(
    'sha', 'deadbeef', 'commit', jsonb_build_object(
      'message', 'chore: crossplay ' || p_version,
      'committer', jsonb_build_object('date', p_at))))
$$;

create or replace function v(
  p_runs jsonb default null, p_latest jsonb default null, p_commits jsonb default null,
  p_pending jsonb default '[]'::jsonb, p_seen jsonb default '[]'::jsonb,
  p_now timestamptz default '2026-09-04 15:00:00+00',
  p_last_ok timestamptz default '2026-09-04 14:50:00+00'
) returns jsonb language sql as $$
  select release_verdicts(
    coalesce(p_runs, runs()), coalesce(p_latest, fx('latest-release')),
    coalesce(p_commits, fx('xteink-commits')), p_pending, p_seen,
    p_now, p_last_ok, interval '15 minutes', interval '60 minutes', interval '3 hours')
$$;

-- The six keys the real morning produces. Written out rather than computed,
-- because a test that derives its expectation from the code under test agrees
-- with the code however wrong it is.
create or replace function morning() returns text language sql as $$
  select 'release|owed|1.12.14 release|owed|1.12.15 '
      || 'release|run|33868277061 release|run|33868278238 '
      || 'release|run|33876387142 release|run|33876388600'
$$;

-- 1. The real condition of 2026-09-04, adjudicated by nobody yet.
select t('the four failed runs and the two owed versions all fire', ekeys(v()), morning());
select t('the two successful releases in the same payload fire nothing',
         (select count(*)::text from jsonb_array_elements(v() -> 'verdicts') x
          where x ->> 'key' in ('release|run|33825044029', 'release|run|33814725941')), '0');
select t('the failed-build message names the run and the conclusion',
         saying(v(), 'release|run|33876387142') like 'CrossPlay release for v1.12.15 ended failure%', 'true');
select t('the owed message names the version and what is published',
         saying(v(), 'release|owed|1.12.15') like '%crossplay 1.12.15 was tagged 112 minutes ago and is still not published%'
           and saying(v(), 'release|owed|1.12.15') like '%Newest published is v1.12.13%', 'true');

-- 2. Old failure against new failure.
select t('failures already adjudicated fire nothing a second time',
         ekeys(v(p_seen := to_jsonb(string_to_array(morning(), ' ')))), '');
select t('a new failure fires while every old one is still open',
         ekeys(v(p_runs := runs() || jsonb_build_array(
                   a_run('99999001', 'v1.12.16', 'completed', '"failure"'::jsonb, '2026-09-04T14:50:00Z')),
                 p_seen := to_jsonb(string_to_array(morning(), ' ')))),
         'release|run|99999001');
select t('four failures are four keys, not one summary',
         (select count(*)::text from jsonb_array_elements(v() -> 'verdicts') x
          where x ->> 'key' like 'release|run|%'), '4');

-- 3. A build that is merely running against a build that failed.
select t('a run in progress for 30 minutes fires nothing',
         ekeys(v(p_runs := with_run(runs(), '33876388600',
                 '{"status":"in_progress","conclusion":null,"created_at":"2026-09-04T14:30:00Z"}'::jsonb))),
         'release|owed|1.12.14 release|owed|1.12.15 '
      || 'release|run|33868277061 release|run|33868278238 release|run|33876387142');
select t('the same run in progress for 120 minutes does fire',
         ekeys(v(p_runs := with_run(runs(), '33876388600',
                 '{"status":"in_progress","conclusion":null,"created_at":"2026-09-04T13:00:00Z"}'::jsonb)))
         like '%release|run|33876388600%', 'true');
select t('a queued run past the boundary says how long it has been queued',
         saying(v(p_runs := with_run(runs(), '33876388600',
                 '{"status":"queued","conclusion":null,"created_at":"2026-09-04T13:00:00Z"}'::jsonb)),
                'release|run|33876388600') like '%has been queued for 120 minutes%', 'true');

-- 4. An empty conclusion is not a success. This is the one that has been got
-- wrong twice: `completed` with a null conclusion reads as finished-and-fine to
-- anything that only looks at the conclusion field, and as finished-and-broken
-- to anything that only looks at the status.
select t('completed with no conclusion is not called a failure while it is young',
         ekeys(v(p_runs := with_run(runs(), '33876388600',
                 '{"status":"completed","conclusion":null,"created_at":"2026-09-04T14:55:00Z"}'::jsonb)))
         like '%33876388600%', 'false');
select t('completed with no conclusion is not called a success either',
         saying(v(p_runs := with_run(runs(), '33876388600',
                 '{"status":"completed","conclusion":null,"created_at":"2026-09-04T13:00:00Z"}'::jsonb)),
                'release|run|33876388600') like '%completed with no conclusion for 120 minutes%', 'true');
select t('a conclusion of success fires nothing however old the run',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('1', 'v1.12.13', 'completed', '"success"'::jsonb, '2026-01-01T00:00:00Z')),
                 p_commits := '[]'::jsonb)), '');
select t('a cancelled run is a fault, not a shrug',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('2', 'v1.12.14', 'completed', '"cancelled"'::jsonb, '2026-09-04T14:00:00Z')),
                 p_commits := '[]'::jsonb)), 'release|run|2');
select t('a skipped run is not a fault',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('3', 'xteink', 'completed', '"skipped"'::jsonb, '2026-09-04T14:00:00Z')),
                 p_commits := '[]'::jsonb)), '');

-- 5. The two clocks, and the boundary between "slow" and "stuck".
select t('a version tagged 5 minutes ago with nothing building it is not a fault',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := a_bump('1.12.20', '2026-09-04T14:55:00Z'))), '');
select t('a version tagged 20 minutes ago with nothing building it is',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := a_bump('1.12.20', '2026-09-04T14:40:00Z'))),
         'release|owed|1.12.20');
select t('and it says no build ever started',
         saying(v(p_runs := '[]'::jsonb, p_commits := a_bump('1.12.20', '2026-09-04T14:40:00Z')),
                'release|owed|1.12.20') like '%no release build has started%', 'true');
select t('a version tagged 30 minutes ago whose build is still running is not a fault',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('7', 'v1.12.20', 'in_progress', 'null'::jsonb, '2026-09-04T14:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T14:30:00Z'))), '');
select t('the same one at 90 minutes is a fault twice over: the run and the release',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('7', 'v1.12.20', 'in_progress', 'null'::jsonb, '2026-09-04T13:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z'))),
         'release|owed|1.12.20 release|run|7');

-- 6. Versions at or below what is published are not owed, and one that
-- publishes closes its own card.
select t('the published version is never owed', ekeys(v()) like '%1.12.13%', 'false');
select t('an owed version that publishes returns an info verdict that closes the card',
         ekeys(v(p_runs := '[]'::jsonb, p_latest := '{"tag_name":"v1.12.15"}'::jsonb,
                 p_seen := to_jsonb(string_to_array(morning(), ' '))), 'info'),
         'release|owed|1.12.14 release|owed|1.12.15');
select t('and stops being pending',
         (v(p_runs := '[]'::jsonb, p_latest := '{"tag_name":"v1.12.15"}'::jsonb) -> 'pending')::text, '[]');
select t('a recovery is not announced for a fault that was never reported',
         ekeys(v(p_runs := '[]'::jsonb, p_latest := '{"tag_name":"v1.12.15"}'::jsonb), 'info'), '');

-- 7. A pending release does not go quiet when its bump scrolls out of the
-- commit window: that is how a stuck release becomes silent again.
select t('a remembered pending version still fires with an empty commit window',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := '[]'::jsonb,
                 p_pending := '[{"version":"1.12.20","at":"2026-09-04T13:00:00+00:00","sha":"abc"}]'::jsonb)),
         'release|owed|1.12.20');

-- 8. The watcher's own eyes.
select t('no answer from GitHub for five hours is a fault',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := '[]'::jsonb, p_last_ok := '2026-09-04 10:00:00+00'))
         like '%release|blind%', 'true');
select t('seeing GitHub again closes it',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := '[]'::jsonb,
                 p_seen := '["release|blind"]'::jsonb), 'info'), 'release|blind');

-- 9. Version ordering, because 1.12.9 sorts after 1.12.10 as text.
select t('1.12.9 is older than 1.12.10', (semver_key('v1.12.9') < semver_key('1.12.10'))::text, 'true');
select t('a tag that is not a version compares with nothing', coalesce(semver_key('xkcd-pack')::text, 'null'), 'null');

-- ---------------------------------------------------------------------------
-- The caller, against the board's real events table, trigger and cards.
-- ---------------------------------------------------------------------------

-- 10. Arriving. The history on record belongs to cards #155 and #158; a watcher
-- that opens its whole backlog on arrival is the trap that caught two sessions.
--
-- The first two checks are here because the live board caught this out: the
-- very first relwatch_fire() calls relwatch_collect() before any request has
-- been made, so the opening pass sees nothing at all. Arming on that pass makes
-- every fault on record look new to the pass after it, and six cards opened for
-- a morning two cards already covered. Arming now requires having seen GitHub.
select t('a pass that saw nothing does not arm the watcher',
         (relwatch_apply('[]'::jsonb, null, null, '2026-09-04 14:50:00+00', false) ->> 'armed'), 'false');
select t('and it is still unarmed afterwards',
         (select seeded::text from release_state), 'false');
select t('the first pass posts nothing',
         (relwatch_apply(runs(), fx('latest-release'), fx('xteink-commits'),
                         '2026-09-04 15:00:00+00', true) -> 'posted')::text, '0');
select t('but it remembers all six faults', (select count(*)::text from release_seen), '6');
select t('and opens no cards', (select count(*)::text from cards where source = 'error'), '0');
select t('and it remembers the two owed versions', (select count(*)::text from release_pending), '2');
select t('a second pass on the same evidence still posts nothing',
         (relwatch_apply(runs(), fx('latest-release'), fx('xteink-commits'),
                         '2026-09-04 15:30:00+00', true) -> 'posted')::text, '0');

-- 11. A failure that happens AFTER arming.
select t('a new failed run posts exactly one event',
         (relwatch_apply(runs() || jsonb_build_array(
              a_run('99999002', 'v1.12.16', 'completed', '"failure"'::jsonb, '2026-09-04T15:40:00Z')),
            fx('latest-release'), fx('xteink-commits'), '2026-09-04 15:50:00+00', true) -> 'posted')::text, '1');
select t('and it opens exactly one card, on tooling',
         (select count(*) || ' ' || min(app) from cards where source = 'error'), '1 tooling');
select t('whose title names the release', (select left(min(title), 18) from cards where source = 'error'),
         'release: CrossPlay');
select t('the same failure on the next pass posts nothing more',
         (relwatch_apply(runs() || jsonb_build_array(
              a_run('99999002', 'v1.12.16', 'completed', '"failure"'::jsonb, '2026-09-04T15:40:00Z')),
            fx('latest-release'), fx('xteink-commits'), '2026-09-04 16:20:00+00', true) -> 'posted')::text, '0');

-- 12. Four failures are four cards. The fault was that it failed four times and
-- said nothing four times; one tidy summary would have reproduced it.
select t('four fresh failures open four cards',
         (relwatch_apply(runs() || jsonb_build_array(
              a_run('99999011', 'v1.12.17', 'completed', '"failure"'::jsonb, '2026-09-04T16:30:00Z'),
              a_run('99999012', 'v1.12.17', 'completed', '"failure"'::jsonb, '2026-09-04T16:30:00Z'),
              a_run('99999013', 'v1.12.18', 'completed', '"failure"'::jsonb, '2026-09-04T16:40:00Z'),
              a_run('99999014', 'v1.12.18', 'completed', '"failure"'::jsonb, '2026-09-04T16:40:00Z')),
            fx('latest-release'), fx('xteink-commits'), '2026-09-04 16:50:00+00', true) -> 'posted')::text, '4');
select t('which are four distinct cards', (select count(*)::text from cards where source = 'error'), '5');

-- 13. Recovery: publishing 1.12.15 closes the card the watcher would have
-- opened for it. Reported first (so there is a card), then published.
select t('an owed version reported after arming opens its card',
         (select count(*)::text from cards c
          join error_fingerprints f on f.card_id = c.id
          where f.fingerprint = 'release|owed|1.12.14'), '0');
delete from release_seen where key = 'release|owed|1.12.14';
select t('the owed release opens a card once it is no longer seeded away',
         (relwatch_apply(runs(), fx('latest-release'), fx('xteink-commits'),
                         '2026-09-04 17:00:00+00', true) -> 'posted')::text, '1');
select t('the card is open',
         (select c.state from cards c join error_fingerprints f on f.card_id = c.id
          where f.fingerprint = 'release|owed|1.12.14'), 'triaged');
select t('publishing it closes the card by itself',
         (relwatch_apply('[]'::jsonb, '{"tag_name":"v1.12.14"}'::jsonb, '[]'::jsonb,
                         '2026-09-04 17:30:00+00', true) -> 'posted')::text, '1');
select t('the card is done',
         (select c.state from cards c join error_fingerprints f on f.card_id = c.id
          where f.fingerprint = 'release|owed|1.12.14'), 'done');

-- 14. The plumbing: nothing has answered yet is not the same as everything is
-- fine, and half the evidence is not a verdict.
delete from events; delete from relwatch_http; delete from net._http_response;
update release_state set last_ok_at = '2026-09-04 17:00:00+00';
insert into relwatch_http (request_id, kind, fired_at) values (1, 'release_runs', now());
select t('a pass where nothing has come back yet decides nothing',
         (select count(*)::text from events), '0');
select relwatch_collect();
select t('collect on an unanswered young request posts nothing',
         (select count(*)::text from events), '0');
select t('and does not move the last-seen clock',
         (select last_ok_at::text from release_state), '2026-09-04 17:00:00+00');
