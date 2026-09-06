# Picross puzzle provenance

This bank has **two origins**, and they carry different rights. Read both
before copying anything out of this repository.

| | puzzles | origin | rights |
|---|---|---|---|
| `pictures.txt` | 68 | drawn for this fork | CC0 1.0 -- public domain |
| `janko.txt` | 171 | janko.at, six named designers | **used by permission, not licensed** |

## 1. The pictures this fork drew (`pictures.txt`)

Original artwork authored for CrossPlay and released into the public domain
(CC0 1.0). No third-party puzzle set, sprite sheet or nonogram collection was
copied, traced or adapted for it. There is no external licence to honour and
nothing to attribute.

- **Author:** the CrossPlay fork (Mario's personal fork of CrossPoint).
- **Date:** 2026-09-05.
- **Licence:** CC0 1.0 / public domain dedication.
- **Subjects:** generic pictographs -- hearts, arrows, houses, animals,
  geometric shapes, letters and card suits -- none of which are trademarked
  characters or logos.
- **Composition:** 22 at 5x5, 28 at 10x10, 18 at 15x15.

That claim used to cover the whole bank. It does not any more, and this file
says so rather than quietly staying accurate about a smaller and smaller share.

## 2. The imported pictures (`janko.txt`)

171 puzzles -- 80 at 10x10 and 91 at 15x15 -- designed by named individuals and
published on <https://www.janko.at/Raetsel/Nonogramme/>. Each puzzle's page
records its author, and 515 of the 531 pages read during the crawl also carried
the note *"Lizenz: Freundliche Genehmigung des Autors"*: by kind permission of
the author. **That licence line is not in `janko-authors.json`** -- the crawl
kept only the author, because its licence and source fields were German page
furniture split on whitespace rather than data -- so the note is a fact about
janko.at that this repository records but cannot prove. Every puzzle's `@source`
URL is in `janko.txt`; the page is one click away. The author half of the claim
IS checkable here, and is.

Janko is the **publisher**; not one of these puzzles is authored by Otto or
Angela Janko.

**These are not ours and not public-domain. They are used here by permission,
and a permission is not a licence.**

- **Granted by:** the designers **Yilmaz Ekici** and **Danilo Kusmin**, and,
  separately, **Otto Janko** for the collection. Both were asked and both said
  yes; neither answer was inferred from the other. That matters because the
  designers' permission runs *to* Janko, and permission to publish is not
  normally a right to sublicense -- so a yes from Janko alone would not have
  been a yes from the designers.
- **Granted to:** Mario, the owner of this project, for use in CrossPlay.
- **Obtained:** directly by the project owner, confirmed 2026-09-05. The
  correspondence is private and is deliberately **not** reproduced here: a
  public repository is the wrong place for other people's email.
- **Scope:** the puzzles in `janko.txt`, each of which carries its own author
  and its own source URL.

### If you have forked this repository, this permission is not yours

CrossPlay's code is MIT and you inherit that. **You do not inherit this
permission.** It does not extend to forks, downstream copies or redistribution
by anyone else: it was granted to this project, by these people, for this use.
If you want to ship these puzzles you have to ask them yourself.

Dropping `janko.txt` from `SOURCES` in `tools_local/picross/gen_picross.py` and
regenerating leaves a bank that is entirely CC0 and carries no permission
question at all. That is the supported way to fork this app, and it is why the
two origins are separate files rather than one mixed one. (Delete the file
without editing that list and the generator stops with a missing-file error
rather than quietly emitting a shorter bank, which is the intended order of
events.)

### The designers

Every imported puzzle names its author in `kProvenances[]` and is credited on
the win screen, on a line under the revealed picture's name. All 171:

| designer | puzzles |
|---|---|
| Yilmaz Ekici | 129 |
| Danilo Kusmin | 21 |
| Hermann Kudlich | 11 |
| Tooru Nakata | 6 |
| Elsbeth Endel | 3 |
| Jan Wolter | 1 |

There are **no unknown authors**. Every one of the 531 candidate pages was
fetched from janko.at and read for its author line before any of this was
imported; `assets_local/picross/janko-authors.json` is that record. An earlier
count claiming four fifths of the corpus was anonymous was an artifact of a
120-entry sampled lookup -- it described the sample, not janko -- and would
have denied credit to six people who are named on every page of their own work.

### How the data reached us, which is not how the permission did

    janko.at  ->  SmilingWayne/puzzlekit  ->  puzzlekit-dataset  ->  here

`SmilingWayne/puzzlekit`'s README says its data is "mostly from Raetsel's Janko
and puzz.link", and that sentence -- in the tool repository, pointing at two
sites at once -- is the whole of the origin statement anywhere in the chain. The
grids themselves were taken from **`puzzlekit-dataset`**, the split-out data
repository, and **that one carries no LICENSE file and no provenance statement
at all**: not the Janko sentence, not a licence, not a per-puzzle author.

An intermediate holds no rights it can pass on. Nothing about the permission
above came with the data; it rests entirely on the project owner's own
correspondence with the designers and with Janko. The author names and source
URLs here were re-derived from janko.at directly rather than trusted to the
dataset, for the same reason.

## How this file is enforced rather than believed

`tools_local/picross/import_picross.py` refuses to write a corpus into this
repository under a licence it does not recognise as redistributable unless
`--permission` cites a record here that states who granted it, that it is not a
public licence, that it does not extend to forks, and a date in `YYYY-MM-DD`
form. The refusal is a mechanism rather than a review checklist because a
checklist is not a mechanism.

What it cannot do is know whether the licence it was handed is the true one: an
operator who types `--license cc0-1.0` over somebody else's puzzles is not
stopped by anything in this repository. The guard makes the honest path
recorded, not the dishonest one impossible.

`host-tests/picross` asserts every puzzle names a provenance row that exists,
that no row leaves its author or licence blank -- an empty licence and "all
rights reserved" are the same fact and must read as one -- and that any puzzle
not under our own CC0 names the source URL it came from.

## The origin is a field, not just this file

`pictures.txt` and `janko.txt` each declare their origin in the file itself
(the `@@author` / `@@license` / `@@source` lines; a single-`@` line above a name
overrides them for one picture), and the generator writes it into
`PicrossPuzzles.h` as a `kProvenances[]` row that every puzzle indexes.

That matters because a document is the wrong place for the answer once a bank
mixes origins: it would have to stay right about which puzzles it still covers.
The field is per puzzle and cannot go stale.

## How the bank is validated (not a provenance claim, a correctness one)

`tools_local/picross/gen_picross.py` DERIVES each puzzle's clues from its
picture and REFUSES to emit any puzzle that is not all three of:

- **unique** -- exactly one grid satisfies the derived clues,
- **line-solvable** -- reachable by single-line reasoning with no guessing, and
- **filling its grid** -- no empty first/last row or column, so a picture cannot
  claim a size tier it does not actually use (interior gaps are still allowed).

Imported puzzles face exactly that gate: `import_picross.py` imports
`evaluate()` from the generator rather than copying it.

**The gate cannot see the picture.** All three properties are properties of the
CLUES, and a puzzle can satisfy every one of them and still solve into a
scatter of blobs nobody can name. Legibility is a judgement somebody makes by
looking, and `janko-selection.json` is that judgement: 171 keepers out of 280
candidates judged by eye at thumbnail scale. It is committed because the
alternative is a bank nobody can reproduce.

Run `python3 tools_local/picross/gen_picross.py --curate` to triage candidates;
the strict default run aborts rather than shipping a picture that fails.
`host-tests/picross` re-proves uniqueness and line-solvability in C++ over the
shipped header, so a hand-edit or a bad merge cannot slip a broken puzzle onto
the device.

## Bank composition

239 puzzles: 22 at 5x5, 108 at 10x10 (28 ours, 80 imported), 109 at 15x15
(18 ours, 91 imported). The bank is sorted by SIZE only -- the picker's size
tabs need each size to be one contiguous run -- and the sort is stable, so
within a size the fork's own pictures come first, in the order `pictures.txt`
lists them, then the import in janko catalogue-number order. Nothing anywhere
ranks a puzzle by difficulty; size is the only difficulty signal this bank has,
and it is the one the tabs show. Stored as
flash-resident `uint16_t rows[kMaxSize]` bitmaps (clues are never stored, only
derived), so `kMaxSize` must stay <= 16 for the row type to hold a full row.

20x20 and larger are **not** imported, and that is a layout decision rather than
a supply one: the corpus holds hundreds at those sizes, but at 20x20 the
row-clue gutter takes 217px of the 480px panel against a 240px grid and the
satisfied-clue strikethroughs smear through the digits. See
`docs/apps/picross.md`.
