# Finishing the Hacker News saved-articles feature

The storage half is committed and tested. The screens and the Activity are not,
and this is what the next attempt needs to know before it starts. It replaces an
earlier `PORT-NOTES.md` on branch `app/hn-saved`, which was right about the
feature and wrong about the size of the job.

## Why this is worth finishing

The feature was written, working, and in daily use. Mario's card still holds
`.crosspoint/hn/saved.tsv` with real saved stories and three cached articles,
last written 2026-08-05. It sat uncommitted while `xteink` moved, and was finally
committed on 2026-08-08 as `85d776dd` on `app/hackernews` -- four minutes before
the first set of port notes was written. It was parked at the end of a session,
not rejected.

## What is already here

Committed on this branch, compiling into the firmware, called by nothing yet:

- `HackerNewsSaved.{cpp,h}` -- the index format. `savedIdFor`,
  `serializeSavedIndex`, `parseSavedIndex`, `sanitizeField`. Freestanding C++17,
  **60 host assertions** in `host-tests/hackernews/test_saved.cpp`.
- `HackerNewsLibrary.{cpp,h}` -- `load`, `save`, `remove`, `contains`,
  `readArticle`, `articles`. Needs `HalStorage`, so it is **device-only and has
  no host test**. That is the untested seam; treat it as such.

The API is the good news: `save(url, title, text)` and `readArticle(a, out)`
take and give a plain `std::string`, which is exactly what xteink's reader
already holds in `document_`. No reader rework is required to store or restore
an article.

## What is left, and the trap in it

`85d776dd` is not "a feature on top of current xteink". It is **an older
generation of the whole app**, carrying its own reader model (`blocks_`,
`lineText_`, `wrapBlocks`, `rowFonts_`) that xteink has since replaced with a
flat `document_`.

The first port notes said only `HackerNewsActivity.{cpp,h}` had diverged. That
is wrong, and it is the expensive kind of wrong:

| file                    | changed lines vs xteink | conflicts on cherry-pick |
| ----------------------- | ----------------------- | ------------------------ |
| `HackerNewsScreens.cpp` | 249                     | **no**                   |
| `HackerNewsScreens.h`   | 103                     | **no**                   |
| `HackerNewsCore.cpp`    | 98                      | **no**                   |
| `HackerNewsCore.h`      | 19                      | **no**                   |
| `HackerNewsActivity.*`  | --                      | yes                      |

**Screens and Core apply cleanly and are still wrong.** Applying them reverts
xteink's newer screen API: `ActionSwapView`, `ReaderModel::swapAvailable`,
`ReaderModel::text` and `hn::Story::mayBeReadable` all disappear, and the
current Activity stops compiling against them.

Worse, **the host suites stay green through all of it.** `host-tests/ui` and
`host-tests/hackernews` compile the screens and the core but never the Activity,
so they pass on a combination the firmware cannot build. Only `pio run` sees it.
Do not read a green `run.sh` as a working port.

## The approach that follows from that

Take **xteink's** `Activity`, `Screens` and `Core` as the base -- all three, not
just the Activity -- and add only the saved parts to them:

1. Screens: `ActionSave` / `ActionUnsave` / `ActionShowSaved` /
   `ActionShowFrontPage`, `ListModel::showingSaved` and its empty state,
   `ReaderModel::canSave` / `saved`. Port these onto xteink's current models by
   hand; do not take the feature's `HackerNewsScreens.*` wholesale.
2. Activity: `library_`, `showingSaved_`, `readingSaved_`, `readerUrl_`, and
   `saveCurrentArticle` / `unsaveCurrentArticle` / `openSavedArticle` /
   `buildSavedRows`. Against xteink's `document_` these are short -- save is
   `library_.save(readerUrl_, readerTitle_, document_)` and open is
   `library_.readArticle(a, document_)` then `showDocument(...)`.
3. `tools_local/toybox/icons.txt` gains `saved = bookmark`. Both sides only append, so
   keep both.

   **Do NOT run `gen_toybox_icons.sh` for this.** icons.txt's own header
   warns that `icon_yahtzee_32` and `icon_connectfour_32` match no SVG in
   the vendored Lucide pin, so their source was never committed and a
   straight regeneration drops Yahtzee and Connect Four from the shelf.
   Splice `icon_saved_32` out of `85d776dd`'s `ToyboxIcons.h` instead,
   append it at the end, and bump the `Icons: N` count in the header
   comment. Then verify all four symbols are present -- the two you added
   and the two you must not lose -- because nothing else will tell you.

   The same applies to merges. Resolving a keep-both conflict in that file
   hunk by hunk interleaves the two icons and leaves a bitmap array
   dangling, which surfaces as a syntax error in whichever game includes
   the header first. Take one side wholesale and re-splice.

Two behaviours worth keeping from the original, both easy to miss:

- Back from an article opened out of the library returns to the **library**, not
  the front page.
- Removing the article you are currently reading drops you back to the list,
  because the reader is otherwise left on something no longer in it.

## What the storage half gets wrong, found by using Mario's own file

`parseSavedIndex` decides how many columns a version-1 row has from the
version header: version 1 "carried an Instapaper bookmark id and a
have-we-got-the-text flag". **Mario's version-1 library has four columns,
not six.** So it read his title out of a field past the end of the row, got
an empty one, and discarded the entry as damage -- his shelf came up empty.

Both shapes exist in the wild, on his card. The parser counts the row's
columns now rather than trusting the header, and there is an assertion built
from the shape of his real file.

The reason this survived sixty passing assertions is worth more than the fix:
they were written from the same assumption as the parser, so they could not
falsify it. They were evidence of internal consistency dressed as evidence of
correctness, and the "60 host assertions" reassurance above was the thing that
hid it. It was found by copying `.crosspoint/hn/saved.tsv` off his card onto
the test SD card instead of writing a fixture. Do that before trusting any
format code here.

## `ensureConnected` landed (2026-09-03, `app/hnoffline`)

The app no longer touches the radio in `onEnter`. It opens on the front-page
list with the radio down, and `ensureConnected` raises the Wi-Fi picker only
when a tap needs the network -- opening a story, a thread, the swap control, the
notice's way onward, and the empty front page's own LOAD button. Backing out of
the picker returns to whatever was on screen, never out of the app.

Three things about it that are NOT what `85d776dd` did, each deliberate:

- **The radio is asked, not remembered.** The branch latched a `Link::Connected`
  enum once. `WiFi.status() == WL_CONNECTED` on every call is what makes an AP
  that drops mid-article recoverable: a latch says yes, the fetch fails, and the
  picker is never offered again. (Ownership is a different question with a
  different answer -- see `devmode::holdsRadio` in `onExit`.)
- **The empty state carries a labelled button**, not a hit rect over its
  headline. An empty shelf and an unloaded front page are the same expanse of
  paper otherwise, and a live control drawn like a dead one is one nobody tries.
- **A failed front page lands on the LIST**, with TRY AGAIN and both segments,
  instead of the full-screen notice that had one way off it and the offline
  shelf on the other side of it.

`hn::emptyState` (in `HackerNewsRows`) owns the three empty screens so
`host-tests/hackernews` can drive them; the drawing and its hit target are
asserted in `host-tests/ui`.

**Wording on that screen is measured, and the estimate that looks right is
wrong.** Reserving lines by dividing a single-line width by the column is one
line short whenever the greedy wrap cannot fill a line: "The front page needs a
connection. Saved articles do not." is 2.6 columns wide, needs THREE lines, and
shipped to a render as "Saved articles do ...". The cut HAS the ellipsis, so the
glyph gate stayed quiet and the screenshot looked finished. Use
`fui::measureWrappedText`, whose own header says so.

## Verifying

`check.sh` is necessary and not sufficient. The failure mode the old
`ensureConnected` rewiring produces is a saved article that will not open, at
runtime, with no compile error -- so drive it:

    ./scripts_local/check.sh
    ./scripts_local/sim-shot.sh '<taps>' '<shots>'   # save mark, then the library screen

(`ui:paperOnTheBand` was a known baseline failure for a while; it is fixed
and the suite runs clean now.)

**The simulator has real outbound network**, so a scripted run reaches the live
front page and proves nothing about being offline. Deny it and verify the denial
BEFORE trusting the run:

    printf '(version 1)\n(allow default)\n(deny network*)\n' > /tmp/nonet.sb
    sandbox-exec -f /tmp/nonet.sb curl -s -m 8 -o /dev/null -w '%{http_code}\n' \
      'https://hn.algolia.com/api/v1/search?tags=front_page&hitsPerPage=1'   # must be 000
    CROSSPOINT_SIM_SD="$PWD/fs_agent" CROSSPLAY_AUTOSTART="HACKER NEWS" \
    CROSSPOINT_SIM_INPUT_SCRIPT='...' CROSSPOINT_SIM_SCREENSHOTS='...' \
      sandbox-exec -f /tmp/nonet.sb .pio/build/simulator_x4_pro/program

`[ERR] [HTTP] open failed: ESP_FAIL` in the log is the app actually offline.
The activity trace is the other half of the evidence: `Entering activity:
HackerNews` with no `WifiSelection` after it is the entry fix, and no `Exiting
activity: HackerNews` after the picker's exit is the cancel fix.

Last, the site: `site/emulator/` is a committed artifact, so the browser demo
keeps running the old firmware until someone rebuilds it. Saving works there --
the packed card is writable in memory and `/canned/hn-article.txt` answers any
article -- but it does not survive a page reload, which is already true of every
other save on the site.
