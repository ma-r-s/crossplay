# Library data: every book ranked, and what a card holds of it

Status: measured 2026-09-15, card #517, the data pass for
[`library-plan.md`](library-plan.md). Two universes, kept apart on purpose
because the first report confused them:

1. **Every book.** Open Library's catalog of all works and its reading log,
   the largest open record of people reaching for a book. This is the
   universe the brief is about, and the list generator is run over it.
2. **The pool a site can copy today.** The public-domain books, which is
   what the page can actually write to a card. It carries 1.9% of the
   intent in the first universe. Nothing below calls it "everything".

Reproducible from `tools_local/library/`, about forty minutes, mostly
downloads; the data lives in the workspace's `library-data/`:

```
universe.py   --ol DIR --out universe.md --pd ol.jsonl                # every book: Open Library dumps, 4.7 GB
catalog.py    --rdf rdf-files.tar.bz2 --out catalog.jsonl              # the pool: Gutenberg's daily RDF, 127 MB
rank.py       --catalog catalog.jsonl --report r.md --list-json ranked.jsonl --ol ol.jsonl --wiki wiki.jsonl
olsignal.py   --catalog catalog.jsonl --pool ranked.jsonl --ol DIR --out ol.jsonl
wikisignal.py --out wiki.jsonl --pool ranked.jsonl
```

## Every book

Open Library catalogs 41.6 million works. 3.3 million of them have at least
one reader event (want to read, currently reading, already read, or a
rating), 13.9 million events in all, 10.6 million of them want-to-read.
Those events are the intent signal: someone heard of the book and reached
for it, which is the objective Mario set ("if I somehow hear of a book,
it is there").

**How concentrated intent is across all books.** Share of all events held
by the top N works when every book is ranked by intent:

| top 100 | top 1,000 | top 10,000 | top 50,000 | top 200,000 | top 1,000,000 |
| --- | --- | --- | --- | --- | --- |
| 6.5% | 14.2% | 26.8% | 40.4% | 56.0% | 79.2% |

Books are far longer-tailed than films: the same measure on Netflix puts
87% of viewing in the top 3,000 titles. Seattle's library checkouts and
Open Library's log agree with each other (41% and 40% in the top 50,000).

**What a card holds of it**, filling in rank order at 300 KB a book (a
text-only EPUB is 200 to 500 KB; the pool below measures 205 KB):

| card | fill to 30% | fill to 95% |
| --- | --- | --- |
| 16 GB | 16,000 books, 30% of all intent | 50,666 books, 41% |
| 32 GB | 32,000 books, 36% | 101,333 books, 48% |
| 64 GB | 64,000 books, 43% | 202,666 books, 56% |
| 128 GB | 128,000 books, 51% | 405,333 books, 65% |
| 256 GB | 256,000 books, 59% | 810,666 books, 76% |

So the dream's closing line, if every book existed, is about 40% on the
card the X4 Pro ships with and about 75% on the largest card it takes.
Those are aggregate shares; a head inventory covers most of aggregate
demand and almost nobody completely (the plan's Goel argument), and this
log is one population (Internet Archive patrons, nine years, English-heavy
with a strong Portuguese and Spanish presence).

**What the list looks like.** The generator over all books puts this
decade's bestsellers and self-help at the head: Atomic Habits, The 48 Laws
of Power, It Ends With Us, Rich Dad Poor Dad, then Harry Potter at 8,
A Game of Thrones at 14, Nineteen Eighty-Four at 25, and the first
public-domain book, Pride and Prejudice, at 28. Four of the top 25 are
Portuguese or Spanish titles, which is the language question showing
itself unprompted. The full top 100 is in the first generated report
below.

**Where the pool sits in it.** The public-domain pool a site can copy
today carries 1.9% of all intent. Filling a card with it is filling the
card with 2% of what people reach for, and the other 98% is a question of
having the books, not of ranking them.

## The pool a site can copy today

After the filters (text only, public domain, has a text-only EPUB, reads
on the panel, one edition per work) the pool is **63,443 works, 15.4 GB**,
median 205 KB per book. Card sizes as printed, nothing else on the card;
the shares are of the pool's own demand, not of all intent:

| card | fill to 30% | fill to 50% | fill to 75% | fill to 95% |
| --- | --- | --- | --- | --- |
| 16 GB (what the X4 Pro ships with) | 23,856 books, 89% of the pool's demand | 39,255 books, 94% | 54,594 books, 98% | the whole pool |
| 32 GB | 45,786 books, 96% | the whole pool | the whole pool | the whole pool |
| 64 GB and up | the whole pool | the whole pool | the whole pool | the whole pool |

Per language, the pool is 49,300 English works (12.2 GB, 88% of its
demand), then Finnish 3,600, French 3,100, German 2,200, Dutch 950,
Italian 940, Spanish 810, Hungarian 630, Portuguese 600. Every non-English
language fits in under a gigabyte.

### How the pool's list is made

Each work carries a value, its share of demand, and the card is filled in
order of value per byte (the plan's knapsack argument: within one book of
optimal). The value is a mixture of three demand distributions, each taken
as a share of its own total over the pool:

    value = 0.4 x share of Gutenberg downloads (30 days)
          + 0.4 x share of Open Library shelvings (want-to-read, reading, read, ratings)
          + 0.2 x share of Wikipedia page views by humans (a year, eight languages)

Over every book the same generator would use the same shape with the
signals that exist there: shelvings and ratings (Open Library; Goodreads
if its licence allowed), sales rank, library checkouts, page views. The
pool needed downloads because the other two signals only reach its head.

Why each, and why those weights:

- **Downloads** are the only signal that covers every title in every
  language, so they order the tail. Their head is polluted: crawlers put
  "A Pickle for the Knowing Ones" and one volume of Oliver Twist in the
  top 15 on six-figure monthly counts. A work with no other signal keeps
  only its downloads term, which sinks those to rank 150 to 500, where
  they cost 100 KB each and nobody sees them.
- **Shelvings** are the objective itself: someone heard of the book and
  reached for it. 15,036 works in the pool carry them, matched by folded
  title and author surname against the works dump (an Open Library work
  spans every translation, so its count is split across the pool's
  languages in proportion to downloads; before that the Finnish Hamlet sat
  in the top 30). They get the largest weight with downloads.
- **Page views** are "heard of" in the widest sense, and they are what
  lifts the books that happen to be on nobody's Open Library shelf. 2,081
  works carry them: 4,135 Gutenberg ids are linked from Wikidata, and the
  top 8,000 works without a link were looked up by title and author (a hit
  counts only when the article's title is the book's, and a bracketed
  disambiguator must name a written form, or "Wuthering Heights (2026
  film)" stands in for the novel). Views count only for a work with some
  shelvings or 5,000 downloads: Magna Carta, the Rosary and Luther's
  theses have traffic about the subject, not readers of the text, and sat
  above Alice on it. The weight is the smallest because an article is
  about a subject and a shelf is about reading.

A mixture rather than a product: multiplying the three heavy-tailed
signals put 74% of all value in the top 1,000 works, where every demand
curve measured for this project puts 6 to 44%.

### How concentrated the pool's demand is

Share of the pool's value in the top N works, in fill order:

| top 100 | top 1,000 | top 5,000 | top 10,000 | top 20,000 | top 50,000 |
| --- | --- | --- | --- | --- | --- |
| 29% | 62% | 78% | 83% | 88% | 97% |

Steeper than the universe's curve (29% against 6.5% in the top 100)
because the pool's head is the canon everyone has heard of and its tail is
what nobody has.

### What the pool's list looks like

The top 100 is in the second generated report below. The head is Gatsby,
Alice, Romeo and Juliet, Jekyll and Hyde, Wuthering Heights, Pride and
Prejudice, the Odyssey, Hamlet, Frankenstein, Peter Rabbit, the Time
Machine, the Wizard of Oz, Dracula: a bookshop's classics table, with the
two odd seven-figure view counts (Wuthering Heights, the Odyssey)
explained by this year's films. At rank 500, period fiction and translated
drama; at 2,000, Plato's Meno and Martin Chuzzlewit; at 5,000, a Frank
Herbert story and a mushroom-growing manual; at 20,000, sermons and a USDA
canning pamphlet; at 40,000, series fiction for children from the 1910s.
The tail is worth having at 200 KB a book and worth nothing as a list,
which is the case for the device-side search.

### What is still rough

Named so nobody rediscovers it:

- **Duplicates the merge misses.** "A Christmas Carol" and "A Christmas
  Carol in Prose; Being a Ghost Story of Christmas" are both in the top 40,
  one carrying the shelvings and the other the views; "The History of Don
  Quixote, Volume 1" and "Don Quixote" are separate works. The merge keys
  on a folded title cut at the first colon or semicolon, and edition
  dressing before that survives. A pass keyed on author plus a fuzzier
  title, or on Open Library's own work ids, would fold these.
- **Crawler traffic still shapes the middle.** Two Finnish plays by Lauri
  Haarla sit at ranks 502 and 505 on 23,000 monthly downloads each, and
  "The Book of the Thousand Nights and a Night, Volume 1 (of 10)" at 503
  on 130,000. Averaging several monthly catalog snapshots would damp the
  ones that come and go; a work with no shelvings and no article past a
  download threshold could be capped outright.
- **Non-English works rank mostly on downloads.** Open Library and
  Wikipedia are English-heavy, so the per-language ordering inside French,
  German or Spanish is closer to v0 than the English one. Per-language
  Wikipedia views exist and are fetched; per-language shelvings do not.
- **Only 2,081 works carry page views.** The title search covered the top
  8,000 by value; extending it to the whole pool costs an hour of API time
  and would mostly find nothing, which is itself the signal.
- **"Share of demand" is share of this model's value over the pool.** It
  says nothing about books outside the pool; the universe section does.

## The generated reports

What follows is `universe.py`'s output, then `rank.py`'s, verbatim.

# The universe: every book, ranked by reading intent

Open Library catalogs 41,591,088 works. 3,319,286 of them have at least one reader event (want to read, reading, read, or a rating); 13,884,657 events in all, 10,558,404 of them want-to-read. Produced by `tools_local/library/universe.py`.

**The public-domain pool a site can copy today carries 1.9% of that intent.**

## How concentrated intent is across all books

| top N works | share of all intent | events at rank N |
| --- | --- | --- |
| 100 | 6.5% | 3,500 |
| 1,000 | 14.2% | 584 |
| 10,000 | 26.8% | 97 |
| 50,000 | 40.4% | 28 |
| 100,000 | 47.7% | 16 |
| 200,000 | 56.0% | 9 |
| 500,000 | 68.4% | 4 |
| 1,000,000 | 79.2% | 2 |
| 2,000,000 | 90.5% | 1 |
| all 3,319,286 | 100% | 1 |

## What fits, at 300 KB per book

A text-only EPUB with its images stripped is 200 to 500 KB; the public-domain pool measures a median of 205 KB. 300 KB is the working figure. The share is of all intent, filling in rank order.

| card | fill to 30% | fill to 50% | fill to 75% | fill to 95% |
| --- | --- | --- | --- | --- |
| 16 GB | 16,000 books, 30% | 26,666 books, 35% | 40,000 books, 38% | 50,666 books, 41% |
| 32 GB | 32,000 books, 36% | 53,333 books, 41% | 80,000 books, 45% | 101,333 books, 48% |
| 64 GB | 64,000 books, 43% | 106,666 books, 48% | 160,000 books, 53% | 202,666 books, 56% |
| 128 GB | 128,000 books, 51% | 213,333 books, 57% | 320,000 books, 62% | 405,333 books, 65% |
| 256 GB | 256,000 books, 59% | 426,666 books, 66% | 640,000 books, 72% | 810,666 books, 76% |

## The top 100 of all books, by reading intent

| # | title | author | want to read | all events |
| --- | --- | --- | --- | --- |
| 1 | Atomic Habits | James Clear | 56,029 | 63,992 |
| 2 | The 48 Laws of Power | Robert Greene | 46,119 | 52,360 |
| 3 | It Ends With Us | Colleen Hoover | 40,409 | 45,430 |
| 4 | Rich Dad, Poor Dad | Robert T. Kiyosaki, Sharon L. Lechter | 31,727 | 37,916 |
| 5 | The Subtle Art of Not Giving a F*ck | Mark Manson | 31,709 | 35,313 |
| 6 | Control Your Mind and Master Your Feelings | Eric Robertson | 22,413 | 25,787 |
| 7 | Um casamento arranjado | Zana Kheiron | 20,775 | 24,801 |
| 8 | Harry Potter and the Philosopher's Stone | J. K. Rowling | 20,213 | 24,717 |
| 9 | It Starts with Us | Colleen Hoover | 18,141 | 19,924 |
| 10 | Think and Grow Rich | Napoleon Hill | 15,685 | 18,621 |
| 11 | The Psychology of Money | Morgan Housel | 13,291 | 15,291 |
| 12 | Twisted Love | Ana Huang | 13,328 | 15,018 |
| 13 | How to Win Friends and Influence People | Dale Carnegie | 12,324 | 14,413 |
| 14 | A Game of Thrones | George R. R. Martin | 11,627 | 14,177 |
| 15 | Haunting Adeline | H. D. Carlton | 10,750 | 12,382 |
| 16 | It | Stephen King | 10,602 | 12,372 |
| 17 | I Don't Love You Anymore | Rithvik Singh | 9,565 | 11,250 |
| 18 | The Power of Your Subconscious Mind | Joseph Murphy | 8,888 | 10,552 |
| 19 | Latidos Que No Dije | Roos | 8,882 | 10,324 |
| 20 | Icebreaker | Hannah Grace | 9,194 | 10,109 |
| 21 | O Alquimista | Paulo Coelho | 8,124 | 10,021 |
| 22 | Una corte de niebla y furia | Sarah J. Maas | 8,111 | 9,765 |
| 23 | The Love Hypothesis | Ali Hazelwood | 8,285 | 9,284 |
| 24 | Shatter Me | Tahereh Mafi | 8,114 | 9,241 |
| 25 | Nineteen Eighty-Four | George Orwell | 7,088 | 8,910 |
| 26 | Can't Hurt Me | David Goggins | 7,257 | 8,224 |
| 27 | Diary of a Wimpy Kid | Jeff Kinney | 6,428 | 8,090 |
| 28 | Pride and Prejudice | Jane Austen | 6,388 | 8,016 |
| 29 | The 7 Habits of Highly Effective People | Stephen R. Covey, Sean Covey | 6,678 | 7,914 |
| 30 | The Hunger Games | Suzanne Collins | 5,914 | 7,898 |
| 31 | The Lightning Thief | Rick Riordan | 5,617 | 7,433 |
| 32 | Fifty Shades of Grey | E. L. James | 6,011 | 7,303 |
| 33 | Girl in Pieces | Kathleen Glasgow | 6,510 | 7,287 |
| 34 | Ugly Love | Colleen Hoover | 6,212 | 7,258 |
| 35 | Harry Potter and the Chamber of Secrets | J. K. Rowling | 5,285 | 7,135 |
| 36 | Twilight | Stephenie Meyer | 5,286 | 6,819 |
| 37 | To Kill a Mockingbird | Harper Lee | 5,447 | 6,604 |
| 38 | A Good Girl's Guide to Murder | Holly Jackson | 5,670 | 6,478 |
| 39 | Harry Potter and the Deathly Hallows | J. K. Rowling | 4,844 | 6,376 |
| 40 | Harry Potter and the Prisoner of Azkaban | J. K. Rowling | 4,230 | 6,323 |
| 41 | Thinking, fast and slow | Daniel Kahneman, Daniel Kahneman | 5,332 | 6,250 |
| 42 | The Art of Seduction | Robert Greene, Joost Elffers | 5,568 | 6,233 |
| 43 | Sapiens | Yuval Noah Harari | 5,205 | 6,160 |
| 44 | Read People Like a Book | Patrick King | 5,443 | 6,141 |
| 45 | The Art of War | 孙武 (Sun Tzu), Stephen F. Kaufman, Lionel | 5,228 | 6,028 |
| 46 | Twisted Lies | Ana Huang | 5,293 | 5,973 |
| 47 | The Silent Patient | Alex Michaelides | 5,119 | 5,896 |
| 48 | Twisted Games | Ana Huang | 5,168 | 5,843 |
| 49 | The Cruel Prince | Holly Black | 5,147 | 5,839 |
| 50 | Animal Farm | George Orwell | 3,794 | 5,823 |
| 51 | The Laws of Human Nature | Robert Greene | 5,099 | 5,660 |
| 52 | The Hobbit | J.R.R. Tolkien | 3,906 | 5,606 |
| 53 | The Seven Husbands of Evelyn Hugo | Taylor Jenkins Reid | 4,903 | 5,527 |
| 54 | Harry Potter and the Goblet of Fire | J. K. Rowling | 3,874 | 5,267 |
| 55 | Ikigai | Héctor García, Francesc Miralles | 4,619 | 5,267 |
| 56 | Verity | Colleen Hoover | 4,696 | 5,230 |
| 57 | Red, White & Royal Blue | Casey McQuiston | 4,663 | 5,224 |
| 58 | Harry Potter and the Order of the Phoenix | J. K. Rowling | 3,618 | 4,917 |
| 59 | Fahrenheit 451 | Ray Bradbury | 3,502 | 4,849 |
| 60 | Le petit prince | Antoine de Saint-Exupéry | 3,794 | 4,821 |
| 61 | Dune | Frank Herbert | 3,392 | 4,812 |
| 62 | Charlotte's Web | E. B. White | 3,439 | 4,776 |
| 63 | The Summer I Turned Pretty | Jenny Han | 4,193 | 4,774 |
| 64 | The Intelligent Investor | Benjamin Graham, Jason Zweig, Atsuhiro D | 4,143 | 4,771 |
| 65 | The Fault in Our Stars | John Green | 3,779 | 4,728 |
| 66 | Deep Work | Cal Newport | 3,964 | 4,716 |
| 67 | The Power of Positive Thinking | Norman Vincent Peale | 4,059 | 4,687 |
| 68 | A Little Life | Hanya Yanagihara | 4,096 | 4,657 |
| 69 | 人間失格 | 太宰 治 | 4,052 | 4,596 |
| 70 | Twisted Hate | Ana Huang | 4,063 | 4,545 |
| 71 | We Were Never Meant To Be | Palle Vasu | 3,872 | 4,542 |
| 72 | A Court of Thorns and Roses | Sarah J. Maas | 3,879 | 4,533 |
| 73 | A Gentle Reminder | Bianca Sparacino | 3,832 | 4,398 |
| 74 | The Power of Now | Eckhart Tolle | 3,702 | 4,336 |
| 75 | Hunting Adeline | H. D. Carlton | 3,694 | 4,296 |
| 76 | ... Trotzdem Ja zum Leben sagen | Viktor E. Frankl | 3,499 | 4,294 |
| 77 | Metamorphosis | Franz Kafka | 3,415 | 4,268 |
| 78 | /works/OL24150460W | ? | 3,893 | 4,231 |
| 79 | I'm Glad My Mom Died | Jennette McCurdy, Jannettte Mcury | 3,812 | 4,074 |
| 80 | 101 Essays That Will Change The Way You Think | Brianna Wiest, Andrea Hernández González | 3,661 | 4,015 |
| 81 | The Shining | Stephen King | 3,034 | 3,992 |
| 82 | Harry Potter and the Half-Blood Prince | J. K. Rowling | 2,995 | 3,961 |
| 83 | Brave New World | Aldous Huxley | 2,609 | 3,945 |
| 84 | The Song of Achilles | Madeline Miller | 3,327 | 3,910 |
| 85 | Wuthering Heights | Emily Brontë | 2,974 | 3,882 |
| 86 | The Richest Man in Babylon | George S. Clason | 3,255 | 3,879 |
| 87 | Wonder | R. J. Palacio | 2,974 | 3,797 |
| 88 | The Eye of the World | Robert Jordan | 2,879 | 3,789 |
| 89 | Can We Be Strangers Again? | Shrijeet Shandilya | 3,180 | 3,773 |
| 90 | The Summer I Turned Pretty Trilogy | Jenny Han | 3,258 | 3,757 |
| 91 | King of Wrath | Ana Huang | 3,350 | 3,722 |
| 92 | The Da Vinci Code | Dan Brown | 2,910 | 3,693 |
| 93 | Lord of the Flies | William Golding | 2,613 | 3,684 |
| 94 | Heartstopper, Volume 1 | Alice Oseman | 3,103 | 3,670 |
| 95 | /works/OL32521579W | ? | 3,243 | 3,647 |
| 96 | How to Talk to Anyone | Leil Lowndes | 3,175 | 3,643 |
| 97 | The Great Gatsby | F. Scott Fitzgerald | 2,591 | 3,550 |
| 98 | Lolita | Vladimir Nabokov | 3,031 | 3,535 |
| 99 | L’étranger | Albert Camus | 2,835 | 3,503 |
| 100 | Преступление и наказание | Fiódor Dostoievski | 2,842 | 3,500 |


# Library data: what is in the catalog, what fits, what the list looks like

Produced by `tools_local/library/rank.py` from Project Gutenberg's catalog (`catalog.py`), v0: value = Gutenberg's 30-day download count, weight = the text-only EPUB's size. Every count below carries its share of demand (downloads), because a filter that removes 10% of titles and 0.1% of demand costs nothing.

## Census

Records: 79,395. Demand mass (30-day downloads): 76,612,080.

### By type

| | titles | share of demand |
| --- | --- | --- |
| Text | 78,144 | 98.2% |
| Sound | 1,114 | 1.7% |
| Dataset | 89 | 0.0% |
| Image | 33 | 0.0% |
| MovingImage | 8 | 0.0% |
| Collection | 4 | 0.0% |
| StillImage | 3 | 0.0% |

### By rights

| | titles | share of demand |
| --- | --- | --- |
| Public domain in the USA. | 78,532 | 99.0% |
| Copyrighted. Read the copyright notice i | 863 | 1.0% |

### By language (first)

| | titles | share of demand |
| --- | --- | --- |
| en | 63,018 | 88.2% |
| fr | 4,205 | 3.5% |
| fi | 3,686 | 1.9% |
| de | 2,463 | 2.2% |
| it | 1,110 | 0.7% |
| nl | 1,109 | 0.7% |
| es | 898 | 0.9% |
| hu | 654 | 0.2% |
| pt | 647 | 0.3% |
| zh | 436 | 0.5% |
| sv | 253 | 0.1% |
| el | 216 | 0.1% |
| eo | 124 | 0.1% |
| la | 108 | 0.1% |
| ca | 95 | 0.1% |

### By LC class (first letter of the first code)

A: general works, B: philosophy/religion, D-F: history, G: geography/recreation, H: social sciences, J: politics, K: law, L: education, M: music, N: arts, P: literature, Q: science, R: medicine, S: agriculture, T: technology, U/V: military, Z: bibliography.

| | titles | share of demand |
| --- | --- | --- |
| P | 45,012 | 64.8% |
| D | 8,136 | 8.2% |
| B | 4,855 | 5.9% |
| A | 3,239 | 2.3% |
| Q | 2,842 | 2.6% |
| E | 2,396 | 2.5% |
| H | 1,984 | 2.3% |
| F | 1,900 | 2.0% |
| T | 1,639 | 1.4% |
| G | 1,476 | 1.7% |
| N | 1,206 | 1.1% |
| S | 798 | 0.7% |
| R | 686 | 0.5% |
| M | 665 | 0.6% |
| Z | 590 | 0.6% |
| C | 456 | 0.8% |
| J | 411 | 0.7% |
| L | 375 | 0.3% |
| U | 281 | 0.4% |
| K | 252 | 0.2% |
| V | 113 | 0.1% |
| none | 83 | 0.0% |

## Filters, in order, each counted

| step | removed | demand removed | remaining | demand remaining |
| --- | --- | --- | --- | --- |
| Text only | 1,251 | 1.77% | 78,144 | 98.2% |
| Public domain only | 427 | 0.62% | 77,717 | 97.6% |
| Has a text-only EPUB | 196 | 0.07% | 77,521 | 97.5% |
| Form (reads on the panel) | 7,601 | 7.06% | 69,920 | 90.5% |
| One edition per work | 6,477 | 6.38% | 63,443 | 90.5% |

The work merge folded 6,477 editions into their most downloaded one; the survivor carries the sum of their demand, so the step removes titles, not demand.

### What each form rule removed

| rule | titles | share of demand |
| --- | --- | --- |
| locc:AP | 2,759 | 1.68% |
| subject | 1,944 | 2.28% |
| locc:Z | 597 | 0.66% |
| locc:ML | 395 | 0.48% |
| title | 389 | 0.49% |
| locc:ND | 327 | 0.33% |
| locc:NA | 266 | 0.23% |
| locc:N | 241 | 0.24% |
| locc:NC | 204 | 0.15% |
| locc:NK | 168 | 0.16% |
| locc:QA | 91 | 0.13% |
| locc:MT | 79 | 0.09% |
| locc:NE | 52 | 0.07% |
| locc:M | 45 | 0.04% |
| locc:NB | 38 | 0.03% |
| locc:NX | 6 | 0.01% |

## The value each work carries

v1: value = 0.4 x share of downloads + 0.4 x share of Open Library shelvings + 0.2 x share of Wikipedia page views, each share taken over the pool (69,311,106 downloads in 30 days, 266,230 shelvings, 154,096,544 views in a year). Matched: 15,036 works carry shelvings and 1,898 carry views; of the 1,000 most downloaded, 533 have shelvings and 259 have an article. A work with neither keeps only its downloads term, which is what demotes crawler-inflated titles; views count only for a work with some shelvings or at least 5,000 downloads, so a subject looked up but not read (Magna Carta) does not ride its article.

## The pool after the filters

63,443 works, 15.4 GB as text-only EPUBs (median 206 KB), Coverage below is the share of the pool's value (defined above).

### Per language

| language | works | GB | share of pool demand |
| --- | --- | --- | --- |
| en | 49,398 | 12.25 | 90.2% |
| fi | 3,639 | 0.68 | 1.0% |
| fr | 3,166 | 0.79 | 3.9% |
| de | 2,174 | 0.54 | 2.4% |
| nl | 953 | 0.23 | 0.3% |
| it | 939 | 0.24 | 0.5% |
| es | 814 | 0.22 | 0.5% |
| hu | 631 | 0.16 | 0.1% |
| pt | 599 | 0.10 | 0.2% |
| sv | 243 | 0.05 | 0.1% |
| zh | 169 | 0.05 | 0.3% |
| eo | 102 | 0.01 | 0.0% |

## How concentrated demand is

Share of the pool's value held by the top N works, in the order the card is filled (value per byte) and in raw value order.

| top N | by value per byte | by popularity |
| --- | --- | --- |
| 100 | 29.1% | 31.7% |
| 500 | 51.5% | 54.0% |
| 1,000 | 61.5% | 63.4% |
| 2,000 | 70.8% | 72.2% |
| 5,000 | 77.9% | 79.3% |
| 10,000 | 82.5% | 84.3% |
| 20,000 | 87.6% | 89.9% |
| 30,000 | 91.2% | 93.4% |
| 40,000 | 94.3% | 95.9% |
| 50,000 | 97.0% | 98.0% |

## What fits

Card sizes as printed (decimal gigabytes); the budget is the slider's share of the card, with nothing else on it. A real card loses a few percent to the file system and to whatever is already there. 'The whole pool' means every book in this pool fits with room over; the pool is not every book, see universe.py for that.

| card | fill to 30% | fill to 50% | fill to 75% | fill to 95% |
| --- | --- | --- | --- | --- |
| 16 GB | 23,856 books, 4.8 GB, 89.0% of demand | 39,255 books, 8.0 GB, 94.1% of demand | 54,594 books, 12.0 GB, 98.1% of demand | 63,232 books, 15.2 GB, 100.0% of demand |
| 32 GB | 45,786 books, 9.6 GB, 95.9% of demand | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) |
| 64 GB | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) |
| 128 GB | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) |
| 256 GB | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) | the whole pool (63,443 books, 15.4 GB) |

## The top 100, in fill order

| # | title | author | lang | downloads/30d | OL shelvings | Wikipedia views/yr | KB |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | The Great Gatsby | Fitzgerald, F. Scott (Francis Scott) | en | 53,454 | 3,759 | 1,821,662 | 181 |
| 2 | Alice's Adventures in Wonderland | Carroll, Lewis | en | 115,042 | 2,570 | 900,439 | 136 |
| 3 | Romeo and Juliet | Shakespeare, William | en | 192,777 | 3,489 | 1,087,926 | 199 |
| 4 | The strange case of Dr. Jekyll and Mr. Hyde | Stevenson, Robert Louis | en | 179,223 | 1,856 | 1,212,612 | 142 |
| 5 | Wuthering Heights | Brontë, Emily | en | 47,901 | 3,907 | 7,266,598 | 435 |
| 6 | Pride and Prejudice | Austen, Jane | en | 287,776 | 8,409 | 2,271,081 | 558 |
| 7 | The Odyssey | Homer | en | 130,423 | 1 | 8,364,901 | 381 |
| 8 | As a man thinketh | Allen, James | en | 12,094 | 1,607 | 59,122 | 91 |
| 9 | Hamlet | Shakespeare, William | en | 31,312 | 2,101 | 1,755,557 | 209 |
| 10 | Frankenstein; or, the modern prometheus | Shelley, Mary Wollstonecraft | en | 97,676 | 2,974 | 3,234,257 | 356 |
| 11 | The Tale of Peter Rabbit | Potter, Beatrix | en | 28,142 | 915 | 157,403 | 71 |
| 12 | The Time Machine | Wells, H. G. (Herbert George) | en | 17,297 | 2,066 | 371,175 | 162 |
| 13 | The Wonderful Wizard of Oz | Baum, L. Frank (Lyman Frank) | en | 79,778 | 1,690 | 843,771 | 181 |
| 14 | The Yellow Wallpaper | Gilman, Charlotte Perkins | en | 24,156 | 850 | 342,979 | 87 |
| 15 | The Raven | Poe, Edgar Allan | en | 5,706 | 619 | 445,275 | 72 |
| 16 | The Prince | Machiavelli, Niccolò | en | 34,211 | 2,255 | 709,267 | 217 |
| 17 | Heart of Darkness | Conrad, Joseph | en | 16,455 | 1,435 | 684,610 | 163 |
| 18 | A Christmas Carol | Dickens, Charles | en | 6,992 | 1,156 | 904,494 | 153 |
| 19 | Macbeth | Shakespeare, William | en | 12,059 | 1,471 | 897,467 | 183 |
| 20 | Dracula | Stoker, Bram | en | 201,834 | 3,028 | 1,699,110 | 433 |
| 21 | The Invisible Man: A Grotesque Romance | Wells, H. G. (Herbert George) | en | 6,943 | 2,221 | 203,298 | 215 |
| 22 | Siddhartha | Hesse, Hermann | en | 8,859 | 1,198 | 720,767 | 165 |
| 23 | A Midsummer Night's Dream | Shakespeare, William | en | 47,383 | 1,131 | 731,434 | 188 |
| 24 | The Importance of Being Earnest: A Trivial Comedy for Serious People | Wilde, Oscar | en | 22,795 | 537 | 477,934 | 104 |
| 25 | The Legend of Sleepy Hollow | Irving, Washington | en | 37,791 | 404 | 375,965 | 88 |
| 26 | The call of the wild | London, Jack | en | 29,853 | 1,105 | 365,318 | 156 |
| 27 | The war of the worlds | Wells, H. G. (Herbert George) | en | 13,735 | 1,634 | 526,331 | 242 |
| 28 | The book of Enoch | ? | en | 8,173 | 19 | 1,815,141 | 187 |
| 29 | Carmilla | Le Fanu, Joseph Sheridan | en | 13,007 | 822 | 438,002 | 150 |
| 30 | The call of Cthulhu | Lovecraft, H. P. (Howard Phillips) | en | 70,233 | 259 | 360,868 | 101 |
| 31 | Treasure Island | Stevenson, Robert Louis | en | 32,940 | 1,409 | 805,184 | 273 |
| 32 | The Cask of Amontillado | Poe, Edgar Allan | en | 62,184 | 100 | 261,805 | 71 |
| 33 | The Secret Garden | Burnett, Frances Hodgson | en | 49,058 | 1,674 | 316,809 | 271 |
| 34 | Through the Looking-Glass | Carroll, Lewis | en | 13,106 | 702 | 429,602 | 143 |
| 35 | The Prophet | Gibran, Kahlil | en | 13,967 | 667 | 206,183 | 116 |
| 36 | Winnie-the-Pooh | Milne, A. A. (Alan Alexander) | en | 13,378 | 833 | 47,287 | 123 |
| 37 | A Study in Scarlet | Doyle, Arthur Conan | en | 65,902 | 939 | 305,380 | 194 |
| 38 | Little Women; Or, Meg, Jo, Beth, and Amy | Alcott, Louisa May | en | 102,542 | 3,128 | 756,762 | 558 |
| 39 | A Christmas Carol in Prose; Being a Ghost Story of Christmas | Dickens, Charles | en | 18,624 | 0 | 1,196,287 | 148 |
| 40 | The Picture of Dorian Gray | Wilde, Oscar | en | 45,508 | 2,414 | 913,173 | 460 |
| 41 | The Hound of the Baskervilles | Doyle, Arthur Conan | en | 137,175 | 840 | 305,580 | 228 |
| 42 | Adventures of Huckleberry Finn | Twain, Mark | en | 40,966 | 1,837 | 546,723 | 347 |
| 43 | The Fall of the House of Usher | Poe, Edgar Allan | en | 4,979 | 273 | 386,884 | 88 |
| 44 | Candide | Voltaire | en | 10,887 | 691 | 473,749 | 161 |
| 45 | A Visit From Saint Nicholas | Moore, Clement Clarke | en | 670 | 419 | 0 | 59 |
| 46 | The Adventures of Tom Sawyer, Complete | Twain, Mark | en | 49,064 | 1,351 | 547,014 | 288 |
| 47 | Anne of Green Gables | Montgomery, L. M. (Lucy Maud) | en | 27,505 | 1,622 | 667,555 | 330 |
| 48 | The King in Yellow | Chambers, Robert W. (Robert William) | en | 93,459 | 442 | 1,280,524 | 286 |
| 49 | The Mysterious Affair at Styles | Christie, Agatha | en | 66,730 | 1,013 | 225,604 | 223 |
| 50 | The History of a Lie: "The Protocols of the Wise Men of Zion" | Bernstein, Herman | en | 822 | 3 | 1,130,731 | 154 |
| 51 | The Jungle Book | Kipling, Rudyard | en | 29,141 | 582 | 524,390 | 182 |
| 52 | The Tempest | Shakespeare, William | en | 9,441 | 772 | 489,956 | 206 |
| 53 | Jane Eyre: An Autobiography | Brontë, Charlotte | en | 95,505 | 2,064 | 1,376,491 | 607 |
| 54 | The Turn of the Screw | James, Henry | en | 68,373 | 462 | 398,500 | 183 |
| 55 | The Waste Land | Eliot, T. S. (Thomas Stearns) | en | 2,710 | 156 | 353,549 | 82 |
| 56 | Othello | Shakespeare, William | en | 6,254 | 556 | 655,756 | 202 |
| 57 | The Communist Manifesto | Marx, Karl, Engels, Friedrich | en | 10,659 | 11 | 632,912 | 106 |
| 58 | A farewell to arms | Hemingway, Ernest | en | 75,011 | 678 | 265,691 | 214 |
| 59 | Moby Dick; Or, The Whale | Melville, Herman | en | 193,299 | 2,451 | 978,037 | 726 |
| 60 | The Rime of the Ancient Mariner | Coleridge, Samuel Taylor | en | 2,731 | 98 | 391,199 | 81 |
| 61 | A Tale of Two Cities | Dickens, Charles | en | 38,643 | 1,819 | 760,302 | 487 |
| 62 | The Great God Pan | Machen, Arthur | en | 60,590 | 329 | 111,517 | 125 |
| 63 | Sense and Sensibility | Austen, Jane | en | 109,932 | 1,247 | 606,030 | 421 |
| 64 | The Seven Dials mystery | Christie, Agatha | en | 63,965 | 163 | 1,139,613 | 271 |
| 65 | Paradise Lost | Milton, John | en | 43,949 | 928 | 1,029,505 | 397 |
| 66 | The Merchant of Venice | Shakespeare, William | en | 6,381 | 610 | 367,963 | 193 |
| 67 | The Dunwich horror | Lovecraft, H. P. (Howard Phillips) | en | 69,569 | 158 | 147,468 | 113 |
| 68 | Jenseits von Gut und Böse | Nietzsche, Friedrich Wilhelm | de | 1,977 | 942 | 223,043 | 235 |
| 69 | The Wind in the Willows | Grahame, Kenneth | en | 13,528 | 827 | 341,752 | 242 |
| 70 | Pygmalion | Shaw, Bernard | en | 8,147 | 371 | 373,129 | 155 |
| 71 | The murder of Roger Ackroyd | Christie, Agatha | en | 70,611 | 1,138 | 226,301 | 345 |
| 72 | Julius Caesar | Shakespeare, William | en | 7,134 | 603 | 257,134 | 185 |
| 73 | Anthem | Rand, Ayn | en | 7,614 | 412 | 76,274 | 111 |
| 74 | Autobiography of Benjamin Franklin | Franklin, Benjamin | en | 39,519 | 801 | 33,516 | 216 |
| 75 | Utopia | More, Thomas, Saint | en | 28,606 | 397 | 301,933 | 170 |
| 76 | King Lear | Shakespeare, William | en | 6,658 | 448 | 506,961 | 204 |
| 77 | A Room with a View | Forster, E. M. (Edward Morgan) | en | 115,764 | 353 | 381,330 | 254 |
| 78 | The Sun Also Rises | Hemingway, Ernest | en | 5,152 | 619 | 366,682 | 217 |
| 79 | Carmen | Mérimée, Prosper | en | 111,826 | 46 | 82,444 | 126 |
| 80 | Struwwelpeter: Merry Stories and Funny Pictures | Hoffmann, Heinrich | en | 5,543 | 58 | 418,724 | 102 |
| 81 | Heidi | Spyri, Johanna | en | 7,451 | 741 | 224,830 | 224 |
| 82 | Persuasion | Austen, Jane | en | 11,337 | 967 | 449,105 | 330 |
| 83 | White Fang | London, Jack | en | 24,837 | 720 | 236,525 | 241 |
| 84 | Mrs. Dalloway | Woolf, Virginia | en | 31,070 | 741 | 270,048 | 260 |
| 85 | Aesop's Fables; a new translation | Aesop | en | 31,769 | 307 | 394,137 | 184 |
| 86 | Les Fleurs du Mal | Baudelaire, Charles | fr | 2,879 | 296 | 336,767 | 144 |
| 87 | Sonnets from the Portuguese | Browning, Elizabeth Barrett | en | 56,670 | 24 | 31,384 | 65 |
| 88 | Gulliver's Travels | Swift, Jonathan | en | 1,664 | 1,055 | 437,920 | 352 |
| 89 | Emma | Austen, Jane | en | 17,131 | 1,396 | 552,752 | 475 |
| 90 | As You Like It | Shakespeare, William | en | 58,305 | 338 | 276,271 | 198 |
| 91 | Oliver Twist | Dickens, Charles | en | 111,970 | 1,162 | 477,842 | 505 |
| 92 | Peter Pan: [Peter and Wendy] | Barrie, J. M. (James Matthew) | en | 35,840 | 1,111 | 340,206 | 390 |
| 93 | Poirot Investigates | Christie, Agatha | en | 66,238 | 447 | 75,279 | 196 |
| 94 | The game of life and how to play it | Shinn, Florence Scovel | en | 5,430 | 619 | 0 | 164 |
| 95 | A Doll's House : a play | Ibsen, Henrik | en | 26,581 | 272 | 300,724 | 163 |
| 96 | Bartleby, the Scrivener: A Story of Wall-Street | Melville, Herman | en | 5,828 | 151 | 282,414 | 107 |
| 97 | The Scarlet Letter | Hawthorne, Nathaniel | en | 28,345 | 1,411 | 353,808 | 473 |
| 98 | Le Fantôme de l'Opéra | Leroux, Gaston | fr | 101,041 | 794 | 80,837 | 325 |
| 99 | The island of Doctor Moreau | Wells, H. G. (Herbert George) | en | 5,152 | 561 | 214,690 | 200 |
| 100 | Dubliners | Joyce, James | en | 11,556 | 633 | 252,931 | 234 |

## Samples down the list

Five works at and after each rank, to see what the tail looks like.

| # | title | author | lang | downloads/30d | OL shelvings | Wikipedia views/yr | KB |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 501 | The crowd: A study of the popular mind | Le Bon, Gustave | en | 3,683 | 39 | 113,459 | 205 |
| 502 | Ihmisten kapina: Kolminäytöksinen draama | Haarla, Lauri | fi | 23,129 | 0 | 0 | 120 |
| 503 | The Book of the Thousand Nights and a Night — Volume 01 (of 10) | ? | en | 130,733 | 0 | 0 | 683 |
| 504 | Fuente Ovejuna | Vega, Lope de | es | 1,294 | 71 | 0 | 103 |
| 505 | Velisurmaaja: Kolmenäytöksinen murhenäytelmä | Haarla, Lauri | fi | 24,195 | 0 | 0 | 126 |
| 1001 | The History of Don Quixote, Volume 1, Complete | Cervantes Saavedra, Miguel de | en | 50,913 | 0 | 0 | 577 |
| 1002 | Lucifer: roman moderne | Magre, Maurice | fr | 24,659 | 0 | 0 | 279 |
| 1003 | Sibylla: or, The revival of prophecy | Mace, C. A. (Cecil Alec) | en | 11,362 | 1 | 0 | 131 |
| 1004 | Public opinion | Lippmann, Walter | en | 2,859 | 71 | 30,077 | 319 |
| 1005 | The Lives of the Twelve Caesars, Complete | Suetonius | en | 49,908 | 38 | 0 | 679 |
| 2001 | Sketches New and Old | Twain, Mark | en | 5,835 | 19 | 1,510 | 331 |
| 2002 | Meno | Plato | en | 3,444 | 0 | 0 | 103 |
| 2003 | Beantwortung der Frage: Was ist Aufklärung? | Kant, Immanuel | de | 2,842 | 0 | 0 | 85 |
| 2004 | Martin Chuzzlewit | Dickens, Charles | en | 2,395 | 55 | 61,236 | 912 |
| 2005 | The Burgess Bird Book for Children | Burgess, Thornton W. (Thornton Waldo) | en | 1,111 | 26 | 0 | 235 |
| 5001 | Nagualism: A Study in Native American Folk-lore and History | Brinton, Daniel G. (Daniel Garrison) | en | 1,122 | 2 | 0 | 159 |
| 5002 | The Young Man's Guide | Alcott, William A. (William Andrus) | en | 1,122 | 6 | 0 | 260 |
| 5003 | The Flaming Forest | Curwood, James Oliver | en | 397 | 8 | 0 | 240 |
| 5004 | Operation Haystack | Herbert, Frank | en | 470 | 2 | 0 | 96 |
| 5005 | Mushroom Culture: Its Extension and Improvement | Robinson, W. (William) | en | 485 | 5 | 0 | 173 |
| 10001 | Life of Johann Wolfgang Goethe | Sime, James | en | 1,280 | 0 | 0 | 218 |
| 10002 | Parables from flowers | Dyer, Gertrude P. | en | 816 | 0 | 0 | 139 |
| 10003 | The silent places | White, Stewart Edward | en | 425 | 3 | 0 | 205 |
| 10004 | Poema del Otoño y otros poemas | Darío, Rubén | es | 603 | 0 | 0 | 102 |
| 10005 | That scholarship boy | Leslie, Emma | en | 764 | 0 | 0 | 130 |
| 20001 | Sermons on Evil-Speaking | Barrow, Isaac | en | 603 | 0 | 0 | 161 |
| 20002 | Widger's Quotes and Images from Zilah by Jules Claretie | Claretie, Jules | en | 214 | 0 | 0 | 57 |
| 20003 | Canning, Freezing, Storing Garden Produce | United States. Department of Agriculture | en | 725 | 0 | 0 | 194 |
| 20004 | Double-Cross | Pohl, Frederik | en | 293 | 0 | 0 | 78 |
| 20005 | Obil, Keeper of Camels: being the parable of the man whom the disciple | Bell, Lucia Chase | en | 308 | 0 | 0 | 82 |
| 30001 | Charles Darwin | Allen, Grant | en | 591 | 0 | 0 | 211 |
| 30002 | A Young Folks' History of the Church of Jesus Christ of Latter-day Sai | Anderson, Nephi | en | 519 | 0 | 0 | 186 |
| 30003 | Agnes Mary Clerke and Ellen Mary Clerke: An Appreciation | Huggins, Lady | en | 249 | 0 | 0 | 89 |
| 30004 | De Klucht der Vergissingen | Shakespeare, William | nl | 371 | 0 | 0 | 132 |
| 30005 | Practicable Socialism: Essays on Social Reform | Barnett, S. A. (Samuel Augustus), Barnet | en | 455 | 1 | 0 | 256 |
| 40001 | The Vulture Maiden [Die Geier-Wally.] | Hillern, Wilhelmine von | en | 453 | 0 | 0 | 214 |
| 40002 | The Corner House Girls on a Houseboat | Hill, Grace Brooks | en | 403 | 0 | 0 | 190 |
| 40003 | The Bellman Book of Fiction, 1906-1919 | ? | en | 452 | 0 | 0 | 214 |
| 40004 | Der Roman eines geborenen Verbrechers | M., Antonino | de | 621 | 0 | 0 | 294 |
| 40005 | Billy Whiskers at the Fair | Montgomery, Frances Trego | en | 383 | 0 | 0 | 181 |

