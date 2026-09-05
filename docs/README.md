# What is in docs/

Two owners share this directory, and the split is deliberate.

**Upstream's reference docs stay at the root, untouched and unrenamed**, so
merges from CrossPoint stay cheap: `activity-manager.md`, `comparison.md`,
`dictionary.md`, `file-formats.md`, `fix-bricked-xteink.md`,
`focus-reading.md`, `hyphenation-trie-format.md`, `i18n.md`,
`sd-card-fonts.md`, `translators.md`, `troubleshooting.md`,
`webserver*.md`, `contributing/`, `images/`, and `crosspoint-readme.md`
(upstream's README, preserved when the fork took the filename).

**The fork's cross-cutting docs also live at the root**: how to build an app
(`building-apps.md`), how it should look (`design-language.md`), what the
project is (`identity.md`), the shelf contract (`shelf.md`), the two real
buttons (`buttons.md`), what scale does to games (`games-at-scale.md`), how to
reflash and inspect a device over Wi-Fi with no cable
(`developer-mode.md`), and what is knowingly unfinished (`open-items.md`).

**Two files, and only one of them is published.** `release-body.md` is what a
tag publishes, and it is deliberately tiny: one line of links, then this
release's `### What is new in <version>` block. Nothing else. It describes
neither the project nor how to install it, because `README.md`, `install.md`
and the site all already do and a release page that repeats them is a release
page nobody finishes. `release-notes.md` is the history, newest first, and
nothing publishes it. They were one file until 2026-09-04, which is why
v1.12.21's release page ran to 20,402 characters and carried six earlier
releases under "What WAS new in ...".

Neither is written by hand. `scripts_local/release_notes.py` rewrites the block
in the body and prepends the same block to the history; the autorelease
workflow commits both. Edit the tooling, not the files -- except the body's
one standing line of links, which is prose and is left alone by the generator.
`host-tests/release` holds the body to a size ceiling and refuses install steps
on it; growing it back is a test failure, not a judgement call.

Only landings a person could receive something different from become notes, and
the question is put to `scripts_local/device-build-needed.sh --ships` -- the
same column of the same table `release-needed.sh` uses to decide whether to
release at all, asked rather than copied. That table carries two independent
attributes per path prefix, `builds` and `ships`, because the two questions have
opposite risk profiles: a wrong "build" costs runner minutes, a wrong "release"
puts an update prompt on every device in the field. While one predicate answered
both, `.gitignore` cut v1.12.21 (live for a build, invisible to a release) and a
fix to `crossplay-release.yml` was invisible to both (card #190). A path in no
row of the table is not guessed at: the build runs and says so, and the release
question REFUSES, naming the path.

`ships` has three values, not two, because "cut a release" and "put a line on
the page" are two more questions that were sharing one answer. `yes` is a change
in the thing a person uses. `quiet` is a change only in how the release was
packaged -- `.github/workflows/crossplay-release.yml`, the one workflow that
uploads what anybody downloads. A `quiet` landing cuts a release exactly like a
`yes` one, and it earns a bullet only if its pull request wrote a `What is new:`
line, because a build workflow's title is developer prose and the page is read
by players. `crossplay-ci.yml` asks for that line at pull-request time, so a
packaging fix is never silently missing from the page it belongs on.

The excluded landings are named in the autorelease job's log and nowhere else.
They used to be a trailing bullet -- "Plus 4 changes nothing on the device can
see." -- which is itself a line a player cannot act on, on a page written for
players. A sync's notes come from its body, which lists the upstream commit
subjects, rather than from a title that only counts them.

This text is read on the GitHub release page. The device never shows it: it
parses `tag_name` and the asset's name, url and size, and the update screen
draws two version numbers. What a device raises is "there is a release".

`install.md` is the reader-facing one: everything about getting the firmware
onto a device except the one-click browser install, which stays on the front
page because it is what almost everybody wants. It exists so the README does
not have to carry esptool invocations, the update-an-existing-install rules and
Developer Mode's pairing flow above the fold.

`developer-mode.md` covers a runtime setting and the routes it exposes. It is a
fork doc rather than a section in upstream's `webserver-*.md` because those stay
untouched so merges from CrossPoint stay cheap.

**Everything about one app lives in [apps/](apps/)**, named after its
directory in `src/apps_local/` (`dungeon.md` for the app the shelf calls
D&Diagrams, `connectfour.md`, `chess.md`). Auxiliary records keep a
qualifying suffix (`study-deck-format.md`, `xkcd-viewing-plan.md`). Not every
app has a doc; one earns a doc when something about it would be rediscovered
the hard way otherwise.
