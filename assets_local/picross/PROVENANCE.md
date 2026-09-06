# Picross puzzle provenance

Every puzzle this app ships is third-party work, **used by permission and not
under any licence**. Read this before copying anything out of this repository.

## What is in the bank

**137 nonograms, all 10x10**, designed by named individuals and published on
<https://www.janko.at/Raetsel/Nonogramme/>. They live as ASCII grids in
`janko.txt`, the only file `gen_picross.SOURCES` emits.

`janko.txt` holds 321 -- the 184 at 15x15 are imported, judged and kept in the
file, but not shipped: `gen_picross.SHIPPED_SIZES` is `(10,)` because Mario
played a 15x15 on the panel and it is not tappable at 19px cells. The file keeps
them because re-deriving a dropped tier must not mean re-crawling janko.at. The
permission, the authors and the source URLs below cover every puzzle in the
file; the generated table at the end covers exactly the 137 that ship.

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

Every shipped puzzle names its designer in the generated table at the end of
this file. **It is not credited on the device**: the win screen drew "PUZZLE BY
<name>" until Mario saw it (*"it just looks bad"*), and the attribution left the
firmware with it. This file is the credit, and it is generated from the bank and
checked against it, so it cannot quietly stop matching what ships.

The 137 shipped puzzles, by designer:

| designer | puzzles |
|---|---|
| Yilmaz Ekici | 83 |
| Danilo Kusmin | 54 |

**Two designers, and they are the two who were asked directly.** Dropping the
15x15 tier took Kudlich, Nakata, Endel and Wolter out of the bank entirely --
every one of their kept puzzles was 15x15 -- so what ships is now exactly the
work of Yilmaz Ekici and Danilo Kusmin, both of whom granted this project
permission themselves (see above). That was a side effect of a layout decision,
not the reason for it, but it is worth recording: the rights position is now
narrower and stronger than it was, and re-adding the 15x15s would widen it back
to six designers whose permission runs through Janko rather than direct.

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

`host-tests/picrossprov` is what makes the table below a checked claim rather
than a maintained one. It re-derives the whole mapping from `janko.txt` and from
the bitmaps actually emitted into `PicrossPuzzles.h`, and fails unless the table
is exactly that -- so a hand-edit, a stale regenerate, a dropped puzzle or an
added one is caught. It also greps the firmware for every designer's name and
for `janko.at`, so the attribution cannot creep back into flash.

`host-tests/picross` asserts the rest of the bank's shape: one size, names that
the display cut can actually draw, and the uniqueness and line-solvability
proofs re-run in C++.

## The attribution is not in the firmware, and this file is why that is safe

It used to be. Every puzzle carried a `kProvenances[]` row -- author, rights
string, source URL -- and the URLs alone were ~34KB of an ~51KB bank: a URL cost
more flash than the puzzle it pointed at, on a device where flash is the scarce
thing and where no player has ever read one. Mario's call was to take it out of
the image, and the string each puzzle carries now is its NAME.

**So this file is the credit.** Not a summary of it -- the whole per-puzzle
mapping, below, and the repository is public.

`janko.txt` still declares the origin in the file itself (the `@@author` /
`@@license` / `@@source` lines; a single-`@` line above a name overrides them
for one picture), and `gen_picross.py` writes the table below **from the same
list, in the same pass, that writes the bank**. A document is the wrong place
for an answer that has to be maintained by hand; it is a fine place for one that
is generated and then checked. `host-tests/picrossprov` re-derives the mapping
from `janko.txt` and the bitmaps actually shipped in `PicrossPuzzles.h` and
fails if this table is not exactly it, so a hand-edit, a stale regenerate or a
dropped puzzle is caught rather than believed.

## Every shipped puzzle and who designed it

<!-- BEGIN GENERATED CREDITS -->

<!-- GENERATED by tools_local/picross/gen_picross.py. Do not edit by hand. -->

137 puzzles ship, and every one of them is here.

| Puzzle | Designer | Licence | Source |
| --- | --- | --- | --- |
| `JANKO222` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0222.a.htm> |
| `JANKO223` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0223.a.htm> |
| `JANKO228` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0228.a.htm> |
| `JANKO230` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0230.a.htm> |
| `JANKO272` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0272.a.htm> |
| `JANKO273` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0273.a.htm> |
| `JANKO274` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0274.a.htm> |
| `JANKO275` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0275.a.htm> |
| `JANKO276` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0276.a.htm> |
| `JANKO277` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0277.a.htm> |
| `JANKO279` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0279.a.htm> |
| `JANKO280` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0280.a.htm> |
| `JANKO321` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0321.a.htm> |
| `JANKO324` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0324.a.htm> |
| `JANKO325` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0325.a.htm> |
| `JANKO330` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0330.a.htm> |
| `JANKO371` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0371.a.htm> |
| `JANKO372` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0372.a.htm> |
| `JANKO375` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0375.a.htm> |
| `JANKO377` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0377.a.htm> |
| `JANKO379` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0379.a.htm> |
| `JANKO380` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0380.a.htm> |
| `JANKO519` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0519.a.htm> |
| `JANKO529` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0529.a.htm> |
| `JANKO549` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0549.a.htm> |
| `JANKO554` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0554.a.htm> |
| `JANKO569` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0569.a.htm> |
| `JANKO574` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0574.a.htm> |
| `JANKO599` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0599.a.htm> |
| `JANKO604` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0604.a.htm> |
| `JANKO609` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0609.a.htm> |
| `JANKO614` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0614.a.htm> |
| `JANKO624` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0624.a.htm> |
| `JANKO630` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0630.a.htm> |
| `JANKO649` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0649.a.htm> |
| `JANKO664` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0664.a.htm> |
| `JANKO679` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0679.a.htm> |
| `JANKO684` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0684.a.htm> |
| `JANKO689` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/0689.a.htm> |
| `JANKO1051` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1051.a.htm> |
| `JANKO1061` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1061.a.htm> |
| `JANKO1072` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1072.a.htm> |
| `JANKO1112` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1112.a.htm> |
| `JANKO1122` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1122.a.htm> |
| `JANKO1133` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1133.a.htm> |
| `JANKO1141` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1141.a.htm> |
| `JANKO1142` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1142.a.htm> |
| `JANKO1143` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1143.a.htm> |
| `JANKO1151` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1151.a.htm> |
| `JANKO1171` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1171.a.htm> |
| `JANKO1192` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1192.a.htm> |
| `JANKO1242` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1242.a.htm> |
| `JANKO1252` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1252.a.htm> |
| `JANKO1261` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1261.a.htm> |
| `JANKO1271` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1271.a.htm> |
| `JANKO1291` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1291.a.htm> |
| `JANKO1302` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1302.a.htm> |
| `JANKO1311` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1311.a.htm> |
| `JANKO1321` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1321.a.htm> |
| `JANKO1322` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1322.a.htm> |
| `JANKO1331` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1331.a.htm> |
| `JANKO1332` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1332.a.htm> |
| `JANKO1342` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1342.a.htm> |
| `JANKO1361` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1361.a.htm> |
| `JANKO1362` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1362.a.htm> |
| `JANKO1371` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1371.a.htm> |
| `JANKO1412` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1412.a.htm> |
| `JANKO1432` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1432.a.htm> |
| `JANKO1452` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1452.a.htm> |
| `JANKO1461` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1461.a.htm> |
| `JANKO1462` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1462.a.htm> |
| `JANKO1502` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1502.a.htm> |
| `JANKO1522` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1522.a.htm> |
| `JANKO1531` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1531.a.htm> |
| `JANKO1532` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1532.a.htm> |
| `JANKO1541` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1541.a.htm> |
| `JANKO1542` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1542.a.htm> |
| `JANKO1562` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1562.a.htm> |
| `JANKO1611` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1611.a.htm> |
| `JANKO1612` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1612.a.htm> |
| `JANKO1622` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1622.a.htm> |
| `JANKO1632` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1632.a.htm> |
| `JANKO1642` | Yilmaz Ekici | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1642.a.htm> |
| `JANKO1691` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1691.a.htm> |
| `JANKO1701` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1701.a.htm> |
| `JANKO1702` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1702.a.htm> |
| `JANKO1711` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1711.a.htm> |
| `JANKO1712` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1712.a.htm> |
| `JANKO1721` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1721.a.htm> |
| `JANKO1741` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1741.a.htm> |
| `JANKO1742` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1742.a.htm> |
| `JANKO1751` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1751.a.htm> |
| `JANKO1761` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1761.a.htm> |
| `JANKO1821` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1821.a.htm> |
| `JANKO1841` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1841.a.htm> |
| `JANKO1842` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1842.a.htm> |
| `JANKO1882` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1882.a.htm> |
| `JANKO1892` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1892.a.htm> |
| `JANKO1901` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1901.a.htm> |
| `JANKO1931` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1931.a.htm> |
| `JANKO1971` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1971.a.htm> |
| `JANKO1981` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1981.a.htm> |
| `JANKO1982` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/1982.a.htm> |
| `JANKO2011` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2011.a.htm> |
| `JANKO2012` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2012.a.htm> |
| `JANKO2021` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2021.a.htm> |
| `JANKO2032` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2032.a.htm> |
| `JANKO2041` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2041.a.htm> |
| `JANKO2051` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2051.a.htm> |
| `JANKO2061` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2061.a.htm> |
| `JANKO2062` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2062.a.htm> |
| `JANKO2072` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2072.a.htm> |
| `JANKO2082` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2082.a.htm> |
| `JANKO2091` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2091.a.htm> |
| `JANKO2101` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2101.a.htm> |
| `JANKO2111` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2111.a.htm> |
| `JANKO2112` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2112.a.htm> |
| `JANKO2122` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2122.a.htm> |
| `JANKO2152` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2152.a.htm> |
| `JANKO2161` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2161.a.htm> |
| `JANKO2181` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2181.a.htm> |
| `JANKO2202` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2202.a.htm> |
| `JANKO2222` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2222.a.htm> |
| `JANKO2231` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2231.a.htm> |
| `JANKO2241` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2241.a.htm> |
| `JANKO2261` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2261.a.htm> |
| `JANKO2262` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2262.a.htm> |
| `JANKO2271` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2271.a.htm> |
| `JANKO2272` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2272.a.htm> |
| `JANKO2281` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2281.a.htm> |
| `JANKO2292` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2292.a.htm> |
| `JANKO2301` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2301.a.htm> |
| `JANKO2302` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2302.a.htm> |
| `JANKO2312` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2312.a.htm> |
| `JANKO2322` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2322.a.htm> |
| `JANKO2331` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2331.a.htm> |
| `JANKO2332` | Danilo Kusmin | all rights reserved, used by permission | <https://www.janko.at/Raetsel/Nonogramme/2332.a.htm> |

<!-- END GENERATED CREDITS -->

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
321 kept (52% of the 10x10s, 69% of the 15x15s), of which the 137 10x10s
ship. Both the keeps and the drops
are recorded as ids, so a second opinion can disagree with a specific puzzle
rather than with a rate. It is committed because the alternative is a bank
nobody can reproduce.

Run `python3 tools_local/picross/gen_picross.py --curate` to triage candidates;
the strict default run aborts rather than shipping a picture that fails.
`host-tests/picross` re-proves uniqueness and line-solvability in C++ over the
shipped header, so a hand-edit or a bad merge cannot slip a broken puzzle onto
the device.

## Bank composition

137 puzzles, all 10x10. Stored as flash-resident `uint16_t rows[kMaxSize]`
bitmaps (clues are never stored, only derived), so `kMaxSize` must stay <= 16
for the row type to hold a full row. About 3.8KB of `Puzzle` table, down from
~51KB: half of that saving is the 15x15s going, the rest is the attribution
leaving the firmware for this file.

20x20 and larger are **not** imported, and that is a layout decision rather than
a supply one: the corpus holds 1200 more at those sizes, but at 20x20 the
row-clue gutter takes 217px of the 480px panel against a 240px grid and the
satisfied-clue strikethroughs smear through the digits. See
`docs/apps/picross.md`.
