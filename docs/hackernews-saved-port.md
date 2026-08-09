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
3. `tools_local/icons.txt` gains `saved = bookmark`. Both sides only append, so
   keep both and run `./tools_local/gen_toybox_icons.sh`. Never hand-merge
   `ToyboxIcons.h`; it is generated. Correct output is 24 icons x 2 sizes.

Two behaviours worth keeping from the original, both easy to miss:

- Back from an article opened out of the library returns to the **library**, not
  the front page.
- Removing the article you are currently reading drops you back to the list,
  because the reader is otherwise left on something no longer in it.

## Verifying

`check.sh` is necessary and not sufficient. The failure mode the old
`ensureConnected` rewiring produces is a saved article that will not open, at
runtime, with no compile error -- so drive it:

    ./scripts_local/check.sh
    ./scripts_local/sim-shot.sh '<taps>' '<shots>'   # save mark, then the library screen

`ui:paperOnTheBand` is a known baseline failure; ignore that one.

Last, the site: `site/emulator/` is a committed artifact, so the browser demo
keeps running the old firmware until someone rebuilds it. Saving works there --
the packed card is writable in memory and `/canned/hn-article.txt` answers any
article -- but it does not survive a page reload, which is already true of every
other save on the site.
