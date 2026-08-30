# Where the words came from

The first version of these lists was written from memory in one pass and never
checked against anything. It shipped in v1.8.0 and Mario found the result in a
few minutes of play: entries nobody could guess, a category of abstractions that
barely worked at all, and spelling nobody had proofread.

The rebuild is anchored on published, play-tested lists rather than on taste.
Two things those sources supply that memory did not:

1. **A difficulty ladder.** Every serious charades list grades its categories
   easy / medium / hard in roughly equal thirds, and their EASY tier is far
   easier than anything the first version contained -- CAT, DOG, PIZZA, DOCTOR,
   FOOTBALL. Ours had no easy tier at all. That single fact is most of what
   "some of them are too hard" meant.

2. **Proof of guessability.** A word that appears in a published party list has
   been played by strangers. That is evidence; "I think this is fair" is not.

## Sources read

- how-to-play-charades.com -- 300+ ideas across eleven categories, graded
- charades.io -- categories including kids, animals, sports, abstract
- gamingrooms.net -- animals, food, jobs, sports, movies, songs, famous people
  and actions, each split easy / medium / hard
- Heads Up's own deck names, for what a category of this game is FOR:
  Act It Out, Sound It Out, Superstars, Blockbuster Movies, Food Fight,
  Animals Gone Wild, Sing It

## The objective filter

`curate.py` scores every entry against Google's 20k-word frequency list
(first20hours/google-10000-english, n-gram frequency over the Trillion Word
Corpus), taking the RAREST token in each entry. Frequency alone is a weak
signal -- FLAMINGO scores rare in a web corpus and every five-year-old knows it
-- so it is used to FLAG rather than to decide, and a flagged entry is kept only
if a published list vouches for it or it is a concrete thing a child could draw.
