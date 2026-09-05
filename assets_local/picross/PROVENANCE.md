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

## How the bank is validated (not a provenance claim, a correctness one)

`tools_local/picross/gen_picross.py` DERIVES each puzzle's clues from its
picture and REFUSES to emit any puzzle that is not both:

- **unique** -- exactly one grid satisfies the derived clues, and
- **line-solvable** -- reachable by single-line reasoning with no guessing.

Run `python3 tools_local/picross/gen_picross.py --curate` to triage candidates
(it reports PASS/FAIL per picture without emitting); the strict default run
aborts rather than shipping a picture that fails either property.
`host-tests/picross` re-proves both properties in C++ over the shipped header,
so a hand-edit or a bad merge cannot slip a broken puzzle onto the device.

## Bank composition

68 puzzles, ordered easy-first: 22 at 5x5, 28 at 10x10, 18 at 15x15. Stored as
flash-resident `uint16_t rows[kMaxSize]` bitmaps (clues are never stored, only
derived), so `kMaxSize` must stay <= 16 for the row type to hold a full row.
