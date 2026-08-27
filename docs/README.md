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
reflash a desk device without a cable (`wireless-flashing.md`), and what is
knowingly unfinished (`open-items.md`).

`wireless-flashing.md` documents a route that only dev builds carry, which is
why it is a fork doc rather than a section in upstream's `webserver-*.md`:
those stay untouched so merges from CrossPoint stay cheap.

**Everything about one app lives in [apps/](apps/)**, named after its
directory in `src/apps_local/` (`dungeon.md` for the app the shelf calls
D&Diagrams, `connectfour.md`, `chess.md`). Auxiliary records keep a
qualifying suffix (`study-deck-format.md`, `xkcd-viewing-plan.md`). Not every
app has a doc; one earns a doc when something about it would be rediscovered
the hard way otherwise.
