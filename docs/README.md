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

`release-notes.md` is not written by hand. `scripts_local/release_notes.py`
rewrites its `### What is new in <version>` block from the merged pull
requests, the autorelease workflow commits it, and `crossplay-release.yml`
passes it as the release body. Edit the tooling, not the file.

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
