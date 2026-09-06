# Picross puzzle provenance

Every puzzle this app ships is third-party work, **used by permission and not
under any licence**. Read this before copying anything out of this repository.

## What is in the bank

321 nonograms -- 137 at 10x10 and 184 at 15x15 -- designed by named individuals
and published on <https://www.janko.at/Raetsel/Nonogramme/>. They live as ASCII
grids in `janko.txt` and are the only file `gen_picross.SOURCES` emits.

Each puzzle's page records its author, and 515 of the 531 pages read during the
crawl also carried the note *"Lizenz: Freundliche Genehmigung des Autors"*: by
kind permission of the author. **That licence line is not in
`janko-authors.json`** -- the crawl kept only the author, because its licence and
source fields were German page furniture split on whitespace rather than data --
so the note is a fact about janko.at that this repository records but cannot
prove. Every puzzle's `@source` URL is in `janko.txt`; the page is one click
away. The author half of the claim IS checkable here, and is.

Janko is the **publisher**; not one of these puzzles is authored by Otto or
Angela Janko.

## The permission

- **Granted by:** the designers **Yilmaz Ekici** and **Danilo Kusmin**, and,
  separately, **Otto Janko** for the collection. Both were asked and both said
  yes; neither answer was inferred from the other. That matters because the
  designers' permission runs *to* Janko, and permission to publish is not
  normally a right to sublicense -- so a yes from Janko alone would not have been
  a yes from the designers.
- **Granted to:** Mario, the owner of this project, for use in CrossPlay.
- **Obtained:** directly by the project owner, confirmed 2026-09-05. The
  correspondence is private and is deliberately **not** reproduced here: a public
  repository is the wrong place for other people's email.
- **Scope:** the puzzles in `janko.txt`, each of which carries its own author and
  its own source URL.

### If you have forked this repository, this permission is not yours

CrossPlay's code is MIT and you inherit that. **You do not inherit this
permission.** It does not extend to forks, downstream copies or redistribution
by anyone else: it was granted to this project, by these people, for this use.
If you want to ship these puzzles you have to ask them yourself.

There is no CC0 fallback in the shipped bank. A fork that wants a Picross with
no permission question has to bring its own pictures; `pictures.txt` (below) is
a working example of the format to do that in, and is itself CC0.

### The designers

Every puzzle names its author in `kProvenances[]` and is credited on the win
screen, on a line under the revealed picture's name. All 321:

| designer | puzzles |
|---|---|
| Yilmaz Ekici | 192 |
| Danilo Kusmin | 108 |
| Hermann Kudlich | 11 |
| Tooru Nakata | 6 |
| Elsbeth Endel | 3 |
| Jan Wolter | 1 |

There are **no unknown authors**. Every one of the 531 candidate pages was
fetched from janko.at and read for its author line before any of this was
imported; `janko-authors.json` is that record, kept whole so the claim can be
checked rather than believed. An earlier count claiming four fifths of the
corpus was anonymous was an artifact of a 120-entry sampled lookup -- it
described the sample, not janko -- and would have denied credit to six people
who are named on every page of their own work.

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

## `pictures.txt` exists and is deliberately not shipped

68 pictures drawn for this fork -- 22 at 5x5, 28 at 10x10, 18 at 15x15 -- all
valid under the same gate, all CC0 1.0, and **none of them in the bank**.

Mario's call, and it is a judgement about the pictures rather than about the
count: the hand-drawn artwork "is not and won't be close to good enough" beside
puzzles somebody designed. He also dropped 5x5 as a tier when choosing sizes,
and every 5x5 in the fork's set is hand-drawn, so the two decisions are the same
set of puzzles.

The file stays in the repository because deleting a generator's input destroys
reproducible work for nothing, and because it is the worked example of the
format. **Adding it back to `gen_picross.SOURCES` is a one-line change and
regenerating emits it** -- so if you have found 68 unused CC0 puzzles and think
nobody noticed: somebody did, and this paragraph is why they are not in the bank.

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
rights reserved" are the same fact and must read as one -- and that **every** row
names the source URL it came from. `host-tests/ui` asserts every designer in the
bank is actually credited on the win screen, and that the longest name fits.

## The origin is a field, not just this file

`janko.txt` declares its origin in the file itself (the `@@author` / `@@license`
/ `@@source` lines; a single-`@` line above a name overrides them for one
picture), and the generator writes it into `PicrossPuzzles.h` as a
`kProvenances[]` row that every puzzle indexes.

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
CLUES, and a puzzle can satisfy every one of them and still solve into a scatter
of blobs nobody can name. Legibility is a judgement somebody makes by looking,
and `janko-selection.json` is that judgement: **all 531 gate-passing candidates
were judged by eye**, at the ~90px scale the picker draws a solved tile at, and
321 kept (52% of the 10x10s, 69% of the 15x15s). Both the keeps and the drops
are recorded as ids, so a second opinion can disagree with a specific puzzle
rather than with a rate. It is committed because the alternative is a bank
nobody can reproduce.

Run `python3 tools_local/picross/gen_picross.py --curate` to triage candidates;
the strict default run aborts rather than shipping a picture that fails.
`host-tests/picross` re-proves uniqueness and line-solvability in C++ over the
shipped header, so a hand-edit or a bad merge cannot slip a broken puzzle onto
the device.

## Bank composition

321 puzzles in two tiers: 137 at 10x10 and 184 at 15x15. Stored as
flash-resident `uint16_t rows[kMaxSize]` bitmaps (clues are never stored, only
derived), so `kMaxSize` must stay <= 16 for the row type to hold a full row.

20x20 and larger are **not** imported, and that is a layout decision rather than
a supply one: the corpus holds 1200 more at those sizes, but at 20x20 the
row-clue gutter takes 217px of the 480px panel against a 240px grid and the
satisfied-clue strikethroughs smear through the digits. See
`docs/apps/picross.md`.
