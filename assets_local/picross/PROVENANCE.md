# Picross puzzle provenance

Every picture in `pictures.txt` is **original artwork authored for this fork**,
released into the **public domain (CC0)**. No third-party puzzle set, sprite
sheet, or nonogram collection was copied, traced, or adapted. There is
therefore no external licence to honour and nothing to attribute.

- **Source:** authored by hand for CrossPlay in `assets_local/picross/pictures.txt`.
- **Author:** the CrossPlay fork (Mario's personal fork of CrossPoint).
- **Date:** 2026-09-05.
- **Licence:** CC0 1.0 / public domain dedication.
- **Subjects:** generic pictographs -- hearts, arrows, houses, animals,
  geometric shapes, letters and card suits -- none of which are trademarked
  characters or logos.

## Why this satisfies the provenance gate

This fork has been burned before by shipping data whose origin could not be
cleanly stated. Authoring the pictures ourselves removes that risk entirely:
the pictures are ours, so there is nothing to launder and no licence to breach.

## The origin is a field, not just this file

Since the provenance change, `pictures.txt` declares its origin in the file
itself -- the `@@author` / `@@license` / `@@source` lines at the top -- and the
generator writes it into `PicrossPuzzles.h` as a `kProvenances[]` row that every
puzzle indexes. A single `@author` / `@license` / `@source` line above a name
overrides the file default for that one picture.

That matters because this document describes a bank that is entirely ours, and
the moment anything is added from anywhere else, a document is the wrong place
for the answer: it would have to be right about which puzzles it still covers.
The field is per puzzle and cannot go stale. `host-tests/picross` asserts every
puzzle names a row that exists and that no row leaves author or licence blank.

`tools_local/picross/import_picross.py` converts an outside corpus into this
format and REFUSES to write inside the repository unless the licence it is given
is redistributable. An unstated licence is all rights reserved; a file here is
in every clone and every release. Local evaluation is fine, shipping is a
permission somebody has to obtain first.

## How the bank is validated (not a provenance claim, a correctness one)

`tools_local/picross/gen_picross.py` DERIVES each puzzle's clues from its
picture and REFUSES to emit any puzzle that is not both:

- **unique** -- exactly one grid satisfies the derived clues,
- **line-solvable** -- reachable by single-line reasoning with no guessing, and
- **filling its grid** -- no empty first/last row or column, so a picture cannot
  claim a size tier it does not actually use (interior gaps are still allowed).

Run `python3 tools_local/picross/gen_picross.py --curate` to triage candidates
(it reports PASS/FAIL per picture without emitting); the strict default run
aborts rather than shipping a picture that fails either property.
`host-tests/picross` re-proves both properties in C++ over the shipped header,
so a hand-edit or a bad merge cannot slip a broken puzzle onto the device.

## Bank composition

68 puzzles, ordered easy-first: 22 at 5x5, 28 at 10x10, 18 at 15x15. Stored as
flash-resident `uint16_t rows[kMaxSize]` bitmaps (clues are never stored, only
derived), so `kMaxSize` must stay <= 16 for the row type to hold a full row.
