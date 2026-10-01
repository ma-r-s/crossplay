# themesweep: check every Home theme after a sync

CrossPoint ships five Home themes and keeps changing them. The fork appends
GAMES and APPS to every Home, which no upstream screenshot ever shows, so a
sync can break a theme that upstream's own review passed. The 1.13.28 sync
did: with a catalog configured, Classic cut Apps in half, Lyra Extended drew it
off the panel and RoundedRaff paged it out of sight. Nobody looked until Mario
did.

This is the look, made repeatable. From inside a worktree:

```bash
python3 tools_local/themesweep/seed.py ../../library-data/epub/*/pg{72882,56019,21977,38066,68716,24439,37812,4729}.epub
python3 tools_local/themesweep/sweep.py --prime
python3 tools_local/themesweep/sweep.py --fresh-thumbs --home /tmp/sweep/home --nav /tmp/sweep/nav
python3 tools_local/themesweep/sheet.py /tmp/sweep/sheet.png 0.4 /tmp/sweep/home/*-books-opds-home.png
```

- `seed.py` copies books onto this tree's simulator card and writes the
  recents list the "books" states install, with the cover path Home needs.
- `--prime` opens the first three books once: a cover thumbnail is only made
  for a book whose cache exists, which on a device means it was opened.
- `--home` shoots 5 themes x 4 states (a recent book or none, a catalog or
  none). `--nav` walks each theme from Home into File Browser, Library,
  Settings, Games, Apps and the OPDS browser.
- `--fresh-thumbs` deletes thumbnails first, so Home takes its first-render
  path, where a stale cover snapshot once drew a ghost frame.

The sweep takes about 30 minutes. It runs the simulator, so do not run it
beside another build.

## Then have it reviewed by someone who did not do the work

Give a fresh agent `review-brief.md` with the two output directories and a
report path. The brief is the standard: nothing cut off, nothing touching an
edge, no overlap, no truncation that hides meaning, no broken alignment, and
pixels measured rather than eyeballed. 1.13.29 went through three rounds of it.
`host-tests/homefit` holds the Home menu's fitting rule to every theme's
numbers, so the arithmetic is tested; the screenshots are for everything else.
