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

## Third pass: MYTHS, MUSIC and TRICKY (2026-08-30)

Mario played the shipped game and said these three were "clearly not good". He
was right, and the defect in all three was the same one: **a card that cannot be
won.** Not a hard card. An unwinnable one.

- **MYTHS** carried nine indistinguishable small magical humanoids -- DWARF,
  GNOME, ELF, IMP, PIXIE, LEPRECHAUN, GOBLIN, FAIRY, NYMPH. The room clues
  "small magic person" and there are nine correct answers, one of which counts.
  Five survive, each with a clue nobody else answers: ELF has Santa, FAIRY has
  Tinker Bell, GNOME has the garden, LEPRECHAUN has the pot of gold, GOBLIN has
  Gringotts.
- **MUSIC** carried ten mutually-correct theory nouns -- SONG, TUNE, MELODY,
  RHYTHM, BEAT, TEMPO, VERSE, CHORUS, TREBLE, SOLO. Somebody hums; four of them
  are right at once. All ten are gone, and the concept slot is carried by
  concrete nouns instead (LYRICS, SHEET MUSIC, TREBLE CLEF, PERFECT PITCH).
- **TRICKY** carried exact-synonym clusters. The room clues "green-eyed
  monster", the holder says ENVY, the card says JEALOUSY, no point. Each cluster
  collapses to one survivor. The category stays hard on purpose; being hard and
  marking a right answer wrong are different things.

### The first pass had MUSIC backwards

Worth knowing before trusting any layer of this directory: the first pass
*added* the ten theory nouns and *cut* the instruments -- ACCORDION, BANJO,
CLARINET, HARP, UKULELE, all of which are in published charades instrument
lists. It also swept seven real myth entries (FENRIR, FREYA, HARPY, KELPIE,
ODYSSEUS, PERSEUS, SATYR) into a purge of confabulations like SALAMANDER OF
FIRE and LAMP OF WISHES.

That is what writing from memory looks like from the inside: confident, tidy,
and wrong in both directions at once. The third pass overrules it, and
`curate.py` resolves the 52 resulting contradictions **explicitly** rather than
letting one layer silently shadow the other.

### Rulings worth not re-litigating

- **No deities of currently practised religions.** VOODOO DOLL is cut (Vodou is
  practised; the doll is a Hollywood invention about it), and no Hindu gods were
  added. ISIS was deliberately left out despite being top-five Egyptian: the
  acronym makes it a bad thing to shout across a room.
- **TRICKY's hint was the thing that was wrong**, not its entries. It said
  "NOTHING YOU CAN POINT AT", which describes the civic nouns nobody could clue
  (DEMOCRACY, PROGRESS, DUTY) and argues against the category's best cards
  (GALAXY, SHADOW, ECLIPSE). It now says "NOTHING YOU CAN HAND OVER": you can
  point at a galaxy, you cannot hand one over.
- **A published entry can still be broken here.** Several sources list
  DEMOCRACY and HARMONY in their hard tiers, but those are ACTING games, where
  the actor picks the clue and one person has to be satisfied. Forehead is
  describe-only and the holder must produce the exact string.
- **MIDLIFE CRISIS was kept** against the reviewer's mixed-room flag: it is a
  red sports car and a ponytail, a roast rather than a bereavement. GRIEF,
  TRAGEDY, BETRAYAL and POVERTY name suffering and are gone.
- **Cross-category repeats are fine and were not chased.** CLAPPING is legitimately
  both an action and a sound, and only one category is ever in play, so a word in
  two lists can never collide inside a round. The generator says so too.
