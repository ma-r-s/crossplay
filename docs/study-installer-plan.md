# The Study installer: a page that puts your Anki deck on the device

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
   fitted, cloze is refused with the reason.
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
| Review sync   | deck_to_anki.py replays in Pyodide against a staged copy; the page writes a timestamped backup then the collection; non-empty -wal/-journal stands in for the Anki-running check | done, tested (test_web_glue.py) |

## Order of work

1. Page skeleton on the site + apkg → conversion via Pyodide + flagging UI.
   (Everything after the drop is our tested code; this phase is mostly UI.)
2. The FreeType wasm module, tested byte-identical against the CLI.
3. Emulator injection preview.
4. Card writing + zip fallback.
5. Review sync against the local collection.
6. Device-native AnkiWeb client: its own project, not part of this page.

Work happens in a worktree (`./scripts/wt.sh new installer`), not in
firmware-next, per the workspace rules. The CLI (`study.py`) remains the power
tool and what host-tests exercise; the page is the front door.

## Why the preview is the emulator and not a mock

A mock is a second renderer that must be kept honest by hand. The emulator is
the firmware: same wrap, same fonts, same fallback rules, same pixels. When
the device gains a feature the preview gains it by rebuilding, not by
remembering. See `browser-emulator` in project memory and `site/emulator/`.
