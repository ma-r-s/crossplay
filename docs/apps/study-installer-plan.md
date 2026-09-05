# The Study installer: a page that puts your Anki deck on the device

**Shipped 2026-08-11** (merged as `2dadd20a`, live at crossplay.ma-r-s.com/study).
Phases 1 to 5 below are built and verified on production: a real conversion, a
real firmware boot, and a graded card writing a review record. Phase 6 remains
its own future project. This file is kept as the record of what was decided and
why, not as a to-do list; the state of the thing is in the page itself and in
the `study-installer-state` project memory.

**Scope, settled by Mario over three conversations: the page is for decks of
English characters and numbers.** Chinese works on the device and through the
command line (his own deck arrived that way and stays), but "someone else
importing a Chinese deck" is explicitly not a goal, and the page's HSK-specific
paths are known-imperfect on purpose. Cloze, pictures and audio are refused,
loudly, by design.

Decided with Mario, 2026-08-10. The ideal workflow is a section of the site;
no installs, no terminal, no addons:

1. **Connect your device**: pick the mounted SD card (File System Access API).
2. **Drop your Anki deck**: the `.apkg` Anki exports (with scheduling). All
   conversion happens in the browser; nothing is uploaded anywhere.
3. **See it before you commit**: the converted deck is injected into the site's
   CrossPlay emulator, which is the real firmware. The user taps through their
   own cards on the rendered device before anything is written.
4. **Honest flagging**: every character the fonts cannot draw is listed with
   the cards it lives in (check_deck's logic, given a face). Long words are
   fitted, an empty cloze card is dropped with the reason.
5. **Write to card.** Browsers without File System Access get a zip and a
   sentence about where to unpack it.
6. **Sync back (interim loop)**: the page reads revlog.dat from the card and
   applies the reviews to the user's local `collection.anki2` (user picks the
   file once; Anki must be closed; backup first; the replay is keyed by
   timestamp so re-running is safe). Then Anki's own Sync button carries them
   to AnkiWeb. No credentials touch the page.
7. **Later, separately**: the device becomes a true incremental AnkiWeb sync
   client (credentials entered once, stored in device NVS, never on a server
   of ours; full protocol: protobuf + zstd + USN bookkeeping over a real
   collection replica on the SD). Decision on record: the full-upload
   shortcut is banned; it can erase reviews made on other devices.

## Architecture

One static page under `site/`, plus a wasm module. **The Python tools stay the
single source of truth**: the page runs them via Pyodide rather than porting
them, so the CLI, CI and the installer cannot drift apart.

| Piece         | How                                                                                                                                                                                                                                                                           | Status                          |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------- |
| .apkg opening | `apkg.py`: zipfile + zstandard + sqlite3, all inside Pyodide; the CLI uses the same module, so there is no JS parser to keep honest (this replaced the JSZip/fzstd/sql.js idea). Modern (v3) packages only; legacy ones get re-export instructions | done, tested (test_apkg.py) |
| Conversion    | `anki_to_deck.py` in Pyodide (sqlite3 + fontTools are pure-Python or built in)                                                                                                                                                                                                | exists, runs unchanged          |
| Images        | `make_images.py` in Pyodide (Pillow is a Pyodide built-in)                                                                                                                                                                                                                    | exists, runs unchanged          |
| Flagging      | `check_deck.py` in Pyodide                                                                                                                                                                                                                                                    | exists, runs unchanged          |
| Fonts         | FreeType 2.13.2 compiled to wasm (tools_local/wasm-ft/, 457 KB) behind a freetype-py stand-in (web_shims/freetype.py), so mono_cpfont.py runs unchanged. fontTools pinned to 4.56.0 on both sides. **Acceptance held: byte-identical .cpfont vs the CLI (test_font_parity.py), verified 2026-08-10.** | done, parity verified |
| Preview       | preview.html iframe, one boot per document; boot options env/files/wipe in emulator.js write the deck under /fs_/study and CROSSPLAY_AUTOSTART=study (a fork seam in Shelf.cpp, getenv precedent) boots straight into the app | done, verified: injected deck graded, revlog grew |
| Write to card | File System Access under study/<slug>/, unsynced-review overwrite guard; zip fallback via zipfile in Pyodide | done (pickers hand-tested) |
| Review sync   | REMOVED from the page (2026-08-26, Mario's call): the device's SYNC button is the only sync surface, via the bridge (study-sync-bridge-plan.md). The Pyodide replay plumbing stays in web_glue/worker for test coverage of the shared code; no UI reaches it | removed |

## Order of work, as built

1. ~~Page skeleton + apkg → conversion via Pyodide + flagging UI.~~ Done, and
   later rebuilt as a single-viewport wizard: the first version was a scrolling
   page of prose bands, which Mario called "a weird way to build a wizard".
2. ~~The FreeType wasm module, byte-identical to the CLI.~~ Done; the
   acceptance held on the first run and `test_font_parity.py` keeps it.
3. ~~Emulator injection preview.~~ Done, via a `CROSSPLAY_AUTOSTART` firmware
   seam and an iframe per boot.
4. ~~Card writing + zip fallback.~~ Done.
5. ~~Review sync against the local collection.~~ Done.
6. Device-native AnkiWeb client: still its own project, not part of this page.

**Not automated, and known:** the three File System Access pickers (SD card,
Anki profile, write-back) open native dialogs, so nothing in CI or in three
rounds of user-test agents has ever exercised them. They need one pass by hand.

Work happens in a worktree (`./scripts/wt.sh new installer`), not in
firmware-next, per the workspace rules. The CLI (`study.py`) remains the power
tool and what host-tests exercise; the page is the front door.

## Why the preview is the emulator and not a mock

A mock is a second renderer that must be kept honest by hand. The emulator is
the firmware: same wrap, same fonts, same fallback rules, same pixels. When
the device gains a feature the preview gains it by rebuilding, not by
remembering. See `browser-emulator` in project memory and `site/emulator/`.

## Testing the folder pickers without a folder picker

`showDirectoryPicker()` opens a native OS panel, so no browser automation can
click it and the write and sync flows were written off as hand-test-only. They
are not. OPFS hands back the *same interface*:

```js
const opfs = await navigator.storage.getDirectory();  // FileSystemDirectoryHandle
window.showDirectoryPicker = async () => opfs;
window.confirm = () => true;                          // or false, to test Cancel
```

With that stub in the page, every line downstream of the dialog runs for real
against real files, and you can read back exactly what landed. Verified this
way on 2026-08-12: the 8 files under `study/<slug>/`, the "already carries N
review record(s)" guard counting correctly, Cancel leaving the card untouched
(a sentinel file survived), all three sync-picker branches, the timestamped
backup, and the replayed row arriving in the collection's `revlog` table.

Two things it cannot cover, and they are the honest remainder: the OS dialog
itself, and Chrome's permission prompt.

For the replay you need a collection the deck's card ids match. The sample
deck under `site/study/demo/` is the same fixture the tests use, so
`apkg.extract()` on it yields a usable `collection.anki2`; serve it from the
site's own origin (COEP `require-corp` blocks a cross-origin fetch) and pull
it into OPFS. Forge a device review with the same 32-byte layout
`test_web_glue.py` packs: `<qqBBhiIB3x`, card id first.

One behaviour to know before reading a result: **Replay disables its own
button on success**, so pressing it twice is a no-op rather than a second
replay. The replay's idempotence is asserted in `test_web_glue.py`, not by
clicking twice.
