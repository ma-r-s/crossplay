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

create or replace function a_run(p_id text, p_ref text, p_status text, p_concl jsonb, p_created text,
                                 p_event text default 'push')
returns jsonb language sql as $$
  select jsonb_build_object('id', p_id, 'name', 'CrossPlay release', 'head_branch', p_ref,
                            'status', p_status, 'conclusion', p_concl, 'created_at', p_created,
                            'event', p_event,
                            'html_url', 'https://github.com/ma-r-s/crossplay/actions/runs/' || p_id)
$$;

-- The real payload of a HEALTHY release that nonetheless carries the duplicate:
-- v1.12.16, published 14:48:38, built twice from the same second by a push and
-- a dispatch, both green. Nothing about it is failing, which is the point.
create or replace function hruns() returns jsonb language sql as $$
  select fx('healthy-release-runs') -> 'workflow_runs'
$$;
create or replace function drop_run(rs jsonb, p_id text) returns jsonb language sql as $$
  select coalesce(jsonb_agg(r), '[]'::jsonb) from jsonb_array_elements(rs) r where r ->> 'id' <> p_id
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
  select 'release|dup|1.12.14 release|dup|1.12.15 '
      || 'release|owed|1.12.14 release|owed|1.12.15'
$$;

-- 1. The real condition of 2026-09-04, adjudicated by nobody yet. Two releases
-- were lost that morning, and each is its own card: two runs of one tag are one
-- release attempt, but 1.12.14 and 1.12.15 are two, and merging them would be
-- the collapse this watcher exists to avoid. Each tag also carries the
-- duplicate, which is a separate finding about the pipeline rather than about
-- either release.
select t('the two lost releases and the two duplicated tags all fire', ekeys(v()), morning());
select t('the two published releases in the same payload fire nothing',
         (select count(*)::text from jsonb_array_elements(v() -> 'verdicts') x
          where x ->> 'key' like '%1.12.13%' or x ->> 'key' like '%1.12.12%'), '0');
select t('the failure message names every run of the set',
         saying(v(), 'release|owed|1.12.15') like 'every release build for crossplay 1.12.15 failed (2 run(s): 33876387142 failure, 33876388600 failure)%', 'true');
select t('and says what is actually published',
         saying(v(), 'release|owed|1.12.15') like '%Newest published is v1.12.13%', 'true');
select t('two lost releases are two cards, not one',
         (select count(*)::text from jsonb_array_elements(v() -> 'verdicts') x
          where x ->> 'key' like 'release|owed|%'), '2');

-- 2. Old failure against new failure.
select t('failures already adjudicated fire nothing a second time',
         ekeys(v(p_seen := to_jsonb(string_to_array(morning(), ' ')))), '');
select t('a new lost release fires while every old one is still open',
         ekeys(v(p_runs := runs() || jsonb_build_array(
                   a_run('99999001', 'v1.12.17', 'completed', '"failure"'::jsonb, '2026-09-04T14:50:00Z')),
                 p_commits := fx('xteink-commits') || a_bump('1.12.17', '2026-09-04T14:50:00Z'),
                 p_seen := to_jsonb(string_to_array(morning(), ' ')))),
         'release|owed|1.12.17');

-- 3. A tag's runs are resolved as a SET, and no single run is ever the verdict.
-- Get this wrong in either direction and the watcher either cries wolf on every
-- release or goes silent on a real one.
select t('one run failed while a sibling is still going is not a failed release',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T14:30:00Z'),
                   a_run('11', 'v1.12.20', 'in_progress', 'null'::jsonb, '2026-09-04T14:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T14:30:00Z'))),
         'release|dup|1.12.20');
select t('past the boundary it is still building, not failed',
         saying(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T13:30:00Z'),
                   a_run('11', 'v1.12.20', 'in_progress', 'null'::jsonb, '2026-09-04T13:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z')),
                'release|owed|1.12.20') like '%is still building (2 run(s): 10 failure, 11 in_progress)%', 'true');
select t('one run failed while a sibling went green is not a failed release',
         saying(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T13:30:00Z'),
                   a_run('11', 'v1.12.20', 'completed', '"success"'::jsonb, '2026-09-04T13:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z')),
                'release|owed|1.12.20') like 'crossplay 1.12.20 built green%no release is published%', 'true');
select t('and once the sibling has published, the failed run says nothing at all',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T13:30:00Z'),
                   a_run('11', 'v1.12.20', 'completed', '"success"'::jsonb, '2026-09-04T13:30:00Z')),
                 p_latest := '{"tag_name":"v1.12.20"}'::jsonb,
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z'))),
         'release|dup|1.12.20');

-- A set of one is a set: nothing below requires or assumes two.
select t('a single failed run is a failed release',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T14:50:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T14:50:00Z'))),
         'release|owed|1.12.20');
select t('a single run still going is silent inside the boundary',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'in_progress', 'null'::jsonb, '2026-09-04T14:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T14:30:00Z'))), '');
select t('and fires past it, as still building',
         saying(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'in_progress', 'null'::jsonb, '2026-09-04T13:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z')),
                'release|owed|1.12.20') like '%is still building (1 run(s): 10 in_progress)%', 'true');

-- 4. Exactly one run per tag. The duplicate is invisible to every other signal
-- -- both runs exit 0, both publish the same bytes -- so multiplicity is the
-- only observable there is, and tolerating it would absorb the fix regressing.
select t('the real healthy release that was nonetheless built twice is reported',
         ekeys(v(p_runs := hruns(), p_latest := fx('healthy-latest-release'))),
         'release|dup|1.12.16');
select t('and the message names both runs and how each started',
         saying(v(p_runs := hruns(), p_latest := fx('healthy-latest-release')), 'release|dup|1.12.16')
         like '2 release builds started for v1.12.16 where exactly one is expected: 33884760111 (workflow_dispatch), 33884760714 (push)%', 'true');
-- The probe that must stay silent could have fired: it is the SAME payload with
-- one run taken out, run through the same rule that just fired on it.
select t('the same release with one run says nothing at all',
         ekeys(v(p_runs := drop_run(hruns(), '33884760111'), p_latest := fx('healthy-latest-release'))), '');
select t('a third run on one tag is still one finding, and it counts them',
         saying(v(p_runs := hruns() || jsonb_build_array(
                    a_run('33884760999', 'v1.12.16', 'completed', '"success"'::jsonb,
                          '2026-09-04T14:36:58Z', 'workflow_dispatch')),
                  p_latest := fx('healthy-latest-release')), 'release|dup|1.12.16')
         like '3 release builds started for v1.12.16%', 'true');

-- 4b. The two layers are separate, and the run count is no part of the health
-- verdict. Asserted as an invariance rather than as two examples: the SAME
-- payload with one run and with two must give the SAME answer about whether the
-- release shipped, and differ only in the hygiene finding. A tag with two green
-- runs and assets is a healthy release with a note; a tag with one run and no
-- assets past the tolerance is a fault.
select t('two green runs with the release published: healthy, with a hygiene note',
         ekeys(v(p_runs := hruns(), p_latest := fx('healthy-latest-release'))),
         'release|dup|1.12.16');
select t('dropping one of them changes the note and nothing else',
         ekeys(v(p_runs := drop_run(hruns(), '33884760111'), p_latest := fx('healthy-latest-release'))), '');
select t('unpublished past the tolerance is a fault with two runs',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"success"'::jsonb, '2026-09-04T13:30:00Z', 'push'),
                   a_run('11', 'v1.12.20', 'completed', '"success"'::jsonb, '2026-09-04T13:30:00Z', 'workflow_dispatch')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z'))),
         'release|dup|1.12.20 release|owed|1.12.20');
select t('and the identical fault with one run: the count is not part of the verdict',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"success"'::jsonb, '2026-09-04T13:30:00Z', 'push')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z'))),
         'release|owed|1.12.20');
select t('the hygiene note says the release itself is unaffected',
         saying(v(p_runs := hruns(), p_latest := fx('healthy-latest-release')), 'release|dup|1.12.16')
         like '%The release itself is unaffected%', 'true');

-- 5. An empty conclusion is not a success. `completed` carrying no conclusion
-- reads as finished-and-fine to anything that looks only at the conclusion, and
-- as finished-and-broken to anything that looks only at the status.
select t('a tag run completed with no conclusion is not counted as a success',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', 'null'::jsonb, '2026-09-04T14:50:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T14:50:00Z'))), '');
select t('and past the boundary it is reported as unfinished, not as shipped',
         saying(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', 'null'::jsonb, '2026-09-04T13:30:00Z')),
                 p_commits := a_bump('1.12.20', '2026-09-04T13:30:00Z')),
                'release|owed|1.12.20') like '%is still building (1 run(s): 10 completed)%', 'true');
select t('an autorelease run completed with no conclusion is judged on its own',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('20', 'xteink', 'completed', 'null'::jsonb, '2026-09-04T13:00:00Z')),
                 p_commits := '[]'::jsonb)), 'release|run|20');
select t('an autorelease run that failed is its own fault: no release was even started',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('21', 'xteink', 'completed', '"failure"'::jsonb, '2026-09-04T14:55:00Z')),
                 p_commits := '[]'::jsonb)), 'release|run|21');
select t('a skipped autorelease run is not a fault',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('22', 'xteink', 'completed', '"skipped"'::jsonb, '2026-09-04T13:00:00Z')),
                 p_commits := '[]'::jsonb)), '');

-- 6. The two clocks.
select t('a version tagged 5 minutes ago with nothing building it is not a fault',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := a_bump('1.12.20', '2026-09-04T14:55:00Z'))), '');
select t('a version tagged 20 minutes ago with nothing building it is',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := a_bump('1.12.20', '2026-09-04T14:40:00Z'))),
         'release|owed|1.12.20');
select t('and it says no build ever started',
         saying(v(p_runs := '[]'::jsonb, p_commits := a_bump('1.12.20', '2026-09-04T14:40:00Z')),
                'release|owed|1.12.20') like '%no release build has started%', 'true');
select t('a tag with a run but no bump commit is still tracked',
         ekeys(v(p_runs := jsonb_build_array(
                   a_run('10', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T14:50:00Z')),
                 p_commits := '[]'::jsonb)), 'release|owed|1.12.20');

-- 7. Versions at or below what is published are not owed, and one that
-- publishes closes its own card.
select t('the published version is never owed', ekeys(v()) like '%owed|1.12.13%', 'false');
select t('an owed version that publishes returns an info verdict that closes the card',
         ekeys(v(p_runs := '[]'::jsonb, p_latest := '{"tag_name":"v1.12.15"}'::jsonb,
                 p_seen := to_jsonb(string_to_array(morning(), ' '))), 'info'),
         'release|owed|1.12.14 release|owed|1.12.15');
select t('and stops being pending',
         (v(p_runs := '[]'::jsonb, p_latest := '{"tag_name":"v1.12.15"}'::jsonb) -> 'pending')::text, '[]');
select t('a recovery is not announced for a fault that was never reported',
         ekeys(v(p_runs := '[]'::jsonb, p_latest := '{"tag_name":"v1.12.15"}'::jsonb), 'info'), '');

-- 8. A pending release does not go quiet when its bump scrolls out of the
-- commit window: that is how a stuck release becomes silent again.
select t('a remembered pending version still fires with an empty commit window',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := '[]'::jsonb,
                 p_pending := '[{"version":"1.12.20","at":"2026-09-04T13:00:00+00:00","sha":"abc"}]'::jsonb)),
         'release|owed|1.12.20');

-- 9. The watcher's own eyes.
select t('no answer from GitHub for five hours is a fault',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := '[]'::jsonb, p_last_ok := '2026-09-04 10:00:00+00'))
         like '%release|blind%', 'true');
select t('seeing GitHub again closes it',
         ekeys(v(p_runs := '[]'::jsonb, p_commits := '[]'::jsonb,
                 p_seen := '["release|blind"]'::jsonb), 'info'), 'release|blind');

-- 10. Version ordering, because 1.12.9 sorts after 1.12.10 as text.
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
select t('but it remembers all four faults', (select count(*)::text from release_seen), '4');
select t('and opens no cards', (select count(*)::text from cards where source = 'error'), '0');
select t('and it remembers the two owed versions', (select count(*)::text from release_pending), '2');
select t('a second pass on the same evidence still posts nothing',
         (relwatch_apply(runs(), fx('latest-release'), fx('xteink-commits'),
                         '2026-09-04 15:30:00+00', true) -> 'posted')::text, '0');

-- 11. A release lost AFTER arming.
select t('a release lost after arming posts exactly one event',
         (relwatch_apply(runs() || jsonb_build_array(
              a_run('99999002', 'v1.12.17', 'completed', '"failure"'::jsonb, '2026-09-04T15:40:00Z')),
            fx('latest-release'), fx('xteink-commits') || a_bump('1.12.17', '2026-09-04T15:40:00Z'),
            '2026-09-04 15:50:00+00', true) -> 'posted')::text, '1');
select t('and it opens exactly one card, on tooling',
         (select count(*) || ' ' || min(app) from cards where source = 'error'), '1 tooling');
select t('whose title names the lost release',
         (select left(min(title), 45) from cards where source = 'error'),
         'release: every release build for crossplay 1.');
select t('the same loss on the next pass posts nothing more',
         (relwatch_apply(runs() || jsonb_build_array(
              a_run('99999002', 'v1.12.17', 'completed', '"failure"'::jsonb, '2026-09-04T15:40:00Z')),
            fx('latest-release'), fx('xteink-commits') || a_bump('1.12.17', '2026-09-04T15:40:00Z'),
            '2026-09-04 16:20:00+00', true) -> 'posted')::text, '0');

-- 12. Four lost releases are four cards. The fault was that it failed and said
-- nothing, four times over; one tidy summary would have reproduced it. Two runs
-- of ONE tag are one release attempt and collapse correctly; four different
-- tags are four attempts and must not.
select t('four freshly lost releases open four cards',
         (relwatch_apply(runs() || jsonb_build_array(
              a_run('99999011', 'v1.12.18', 'completed', '"failure"'::jsonb, '2026-09-04T16:30:00Z'),
              a_run('99999013', 'v1.12.19', 'completed', '"failure"'::jsonb, '2026-09-04T16:40:00Z'),
              a_run('99999015', 'v1.12.20', 'completed', '"failure"'::jsonb, '2026-09-04T16:40:00Z'),
              a_run('99999017', 'v1.12.21', 'completed', '"failure"'::jsonb, '2026-09-04T16:40:00Z')),
            fx('latest-release'), fx('xteink-commits'), '2026-09-04 16:50:00+00', true) -> 'posted')::text, '4');
select t('which are four distinct cards', (select count(*)::text from cards where source = 'error'), '5');
select t('while two runs of one tag are one card and one duplicate finding',
         (relwatch_apply(jsonb_build_array(
              a_run('99999031', 'v1.12.22', 'completed', '"failure"'::jsonb, '2026-09-04T17:00:00Z', 'push'),
              a_run('99999032', 'v1.12.22', 'completed', '"failure"'::jsonb, '2026-09-04T17:00:00Z', 'workflow_dispatch')),
            fx('latest-release'), a_bump('1.12.22', '2026-09-04T17:00:00Z'),
            '2026-09-04 17:10:00+00', true) -> 'posted')::text, '2');
select t('the duplicate finding is its own card',
         (select left(c.title, 40) from cards c join error_fingerprints f on f.card_id = c.id
          where f.fingerprint = 'release|dup|1.12.22'), 'release: 2 release builds started for v1');

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

-- 15. A card addressed to Mario is an item in his inbox, by construction
-- (card #209). The inbox view is the open `mario` blockers and nothing else,
-- and a card is not a blocker, so a card filed on app `mario` -- the app that
-- already means "only Mario can decide this" -- used to reach him only if
-- somebody also remembered to block on it. Cards 75 and 84 aged a day in
-- `reported` because nobody did. The rule is here as well as in the board CLI
-- because the CLI is not the only writer: the site's report function, the
-- inbox page and a hand-typed UPDATE all reach `cards` directly.
insert into cards (title, app, kind, state) values
  ('Retire Main and open a fresh orchestrator', 'mario', 'task', 'reported'),
  ('Sudoku keeps its own puzzle', 'sudoku', 'bug', 'reported');
select t('a card filed on app mario opens a mario blocker asking its title',
         (select b.ask from blockers b join cards c on c.id = b.card_id
          where c.title = 'Retire Main and open a fresh orchestrator' and b.open and b.need = 'mario'),
         'Retire Main and open a fresh orchestrator');
select t('and it says what happens if he never answers',
         (select b."default" from blockers b join cards c on c.id = b.card_id
          where c.title = 'Retire Main and open a fresh orchestrator' and b.open and b.need = 'mario'),
         'nothing happens until he answers');
select t('so it is in the inbox view, which is the only thing he reads',
         (select count(*)::text from inbox where title = 'Retire Main and open a fresh orchestrator'), '1');
select t('and the card says it was blocked',
         (select count(*)::text from history h join cards c on c.id = h.card_id
          where c.title = 'Retire Main and open a fresh orchestrator'
            and h.what = 'blocked (mario): Retire Main and open a fresh orchestrator'), '1');
select t('a card on any other app opens nothing',
         (select count(*)::text from blockers b join cards c on c.id = b.card_id
          where c.title = 'Sudoku keeps its own puzzle'), '0');

-- Moved there, not only filed there: a decision that becomes his on Tuesday is
-- as invisible as one that was his on Monday.
update cards set app = 'mario' where title = 'Sudoku keeps its own puzzle';
select t('moving a card to app mario opens one too',
         (select count(*)::text from inbox where title = 'Sudoku keeps its own puzzle'), '1');
update cards set app = 'mario' where title = 'Sudoku keeps its own puzzle';
update cards set state = 'triaged' where title = 'Sudoku keeps its own puzzle';
update cards set title = 'Sudoku keeps its own puzzle' where title = 'Sudoku keeps its own puzzle';
select t('moving it there again, or editing it in place, opens no second one',
         (select count(*)::text from blockers b join cards c on c.id = b.card_id
          where c.title = 'Sudoku keeps its own puzzle' and b.need = 'mario'), '1');
update cards set app = 'sudoku' where title = 'Sudoku keeps its own puzzle';
update cards set app = 'mario' where title = 'Sudoku keeps its own puzzle';
select t('a round trip through another app while the blocker is open opens no second one',
         (select count(*)::text from blockers b join cards c on c.id = b.card_id
          where c.title = 'Sudoku keeps its own puzzle' and b.need = 'mario'), '1');

-- Answered and moved back is a new question, not the old one: the numbering
-- has to survive it, because (card_id, n) is unique.
update blockers set open = false, answer_choice = 'keep it', answered_at = now()
  where need = 'mario' and card_id = (select id from cards where title = 'Sudoku keeps its own puzzle');
update cards set app = 'sudoku' where title = 'Sudoku keeps its own puzzle';
update cards set app = 'mario' where title = 'Sudoku keeps its own puzzle';
select t('once answered, moving it back asks again',
         (select count(*)::text from blockers b join cards c on c.id = b.card_id
          where c.title = 'Sudoku keeps its own puzzle' and b.need = 'mario'), '2');
select t('and the second blocker took the next number',
         (select string_agg(b.n::text, ',' order by b.n) from blockers b join cards c on c.id = b.card_id
          where c.title = 'Sudoku keeps its own puzzle'), '1,2');

-- The other half of that migration: the cards already dropped before the rule
-- existed. Seeded behind the trigger's back, because the point is a card that
-- never met it; run.sh re-applies the migration file after this, and the two
-- checks it runs are the ones that see the repair.
alter table cards disable trigger cards_mario_inbox_ins;
insert into cards (title, app, kind, state) values
  ('A decision that aged in reported', 'mario', 'task', 'reported'),
  ('A decision he already took', 'mario', 'task', 'done');
alter table cards enable trigger cards_mario_inbox_ins;
