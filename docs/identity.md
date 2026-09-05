## The sentence

> **Everything else a still screen is good at.**

Everything else on this page is downstream of that. If a paragraph does not
eventually serve it, cut the paragraph.

## The idea underneath

What this device already had is aimed at reading. CrossPoint is an e-reader and
says so ("Read without limits"); FreeInk is the stack it stands on ("An open
ecosystem for e-readers"). Neither is wrong, and CrossPlay is not a criticism of
either: it is built on CrossPoint and tracks it daily.

The difference is one observation. **The thing that makes e-ink good for reading
is not that it shows text. It is that it holds still while you think.** A page
sits there at zero power for as long as you need it. That property was spent
entirely on books, and it fits a much wider class of activity: anything where the
screen presents a situation, you think, and then you decide.

A flashcard is that. A chess position is that. A logic grid, a nonogram, a hand
of cards, a comic, a saved article. In every one the screen is still for almost
the whole activity, and the interesting part happens in your head. A device that
is bad at animation is not a compromised game console; it is exactly the right
console for that kind of game, and nothing else is trying to be it.

So the pitch is not "games on an e-reader", which sounds like a novelty. It is:
**this hardware was always suited to more than one still activity, and someone
finally built the rest of them.**

### What it is like

A DS and a Playdate, with the lid off. The DS for two devices on one table with
no setup between them, the Playdate for one small screen with a handful of
made-for-it things on it and no store in the way. The lid off because it is
your device: open firmware, no app store, no account, no approval.

Use that comparison sparingly and never as the opening line. It lands only once
someone already knows what the thing is.

## The two things that are actually novel

Most of what CrossPlay adds is "apps exist". These two are the load-bearing ones,
and any description that omits them has described a folder of minigames.

**A shape for still activities.** The apps are not ports that happen to run.
They share a shelf, a header band, a font, a control vocabulary and an
interaction model, so a person who learns one has learned all of them. That
shared surface is what turns a shelf of separate programs into one device. See
[`shelf.md`](shelf.md) and [`LOCAL_SCOPE.md`](../LOCAL_SCOPE.md).

**Multiplayer with nothing to configure.** Two devices on a table find each
other and play. No pairing screen, no room code, no account, no router, no
internet. On every game that supports it, it is one `PLAY NEARBY` row and that is the
entire user-facing surface of it. The north star is the DS: you sat down next to
someone and it worked, and no part of the experience ever mentioned a network.
The layer itself is `src/apps_local/link/`, described in
[`building-apps.md`](building-apps.md) and covered by the `link` host-test suite
(real UDP on loopback).

## What the identity is made of

These are the marks. They are shared with the design language rather than
invented here; what this section adds is which of them carry off the device.

| Mark                     | On the device                                        | Off the device                                                                                                                                                           |
| ------------------------ | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Pure 1-bit**           | no grey pixel exists; every tone is dither           | the _look_ carries over, the constraint does not. Black and white is the identity, not a limitation to apologise for, but the page is free to use real greys (see below) |
| **The black band**       | solid header, knocked-out white type, never repaints | keep as the section marker                                                                                                                                               |
| **Jersey 25**            | the display cut, condensed, one weight               | keep for headings and labels                                                                                                                                             |
| **A serif counterpoint** | Instrument Serif for a game's own surface            | keep, for prose                                                                                                                                                          |
| **Dither**               | the only grey there is                               | use sparingly; a web page with no real greys is a costume                                                                                                                |
| **Corner brackets**      | the frame around a board or an ornament              | keep. It is the most recognisable shape we own                                                                                                                           |
| **Uppercase labels**     | every control and header                             | keep for labels, never for prose                                                                                                                                         |

Two rules about carrying it off the device:

**Do not cosplay the panel.** A website that fakes e-ink refresh, or renders its
body text as a bitmap font, is imitating a constraint rather than expressing a
choice. The device is 1-bit because the hardware is; the page is 1-bit because it
looks like us. Where the web can do something better (real greys for hierarchy,
selectable text, a font that stays legible at any size), use it.

**The screenshots are the artwork.** Every app on the shelf spent real effort
on one still screen. Nothing drawn for a web page will beat a photograph
of the thing itself, and anything decorative that is not a screenshot is
competing with them. This is the same rule as ornament on the device: it has to
be made of the app's own material.

## The voice

Read the strings the apps already ship, because the voice is already decided and
it is consistent:

> `PICK IT UP` · `DEAL FRESH` · `PLAY NEARBY` · `297 TO GO` · `0 MOVES DEEP`
> `TAP A SHIP TO MOVE IT` · `WHAT EACH GOOD PAYS, BEST FIRST` · `YOUR LAST SIXTEEN`
> `PASS IT ON` · `NOTHING RECORDED YET` · `3 DEALT, 0 CLEARED, 0 IN A ROW`
> `DROP THIS CASE? The marks you have made will go with it.`
> `THAT IS EVERY RULE. SIXTY-FOUR DUNGEONS ARE WAITING FOR YOU.`

What those have in common:

- **Say the thing, not the category.** `PLAY NEARBY`, never "Multiplayer".
  `PICK IT UP`, never "Resume". The abstract noun is always the weaker word.
- **Second person, present tense, plain verbs.** The device is talking to one
  person who is holding it.
- **Concrete numbers over adjectives.** `297 TO GO` tells you more than "lots of
  cards due", and it is different every day, which is the point.
- **State the situation, then the action.** `DROP THIS CASE?` then what it costs.
  Never the action alone.
- **No exclamation marks, no emoji, no jargon, and no enthusiasm on our own
  behalf.** Nothing is "powerful", "seamless", "revolutionary" or "blazingly
  fast". The dryness is the charm; it is what makes the occasional warm line
  ("SIXTY-FOUR DUNGEONS ARE WAITING FOR YOU") actually land.
- **Never mention the machinery.** No user-facing string says UDP, socket, peer,
  sync or protocol. If a sentence explains how it works rather than what it does,
  it belongs in a doc, not on a screen.

The web voice is the same voice at slightly greater length. A page can afford a
sentence where a button gets three words, but it may not become a brochure.

### Wording that is settled

- **CrossPlay**, one word, no space, capital C and capital P. Not "Crossplay".
  The lowercase form survives only where it is an identifier rather than the
  name: the `ma-r-s/crossplay` repo, the `crossplay.ma-r-s.com` host, the
  release binary, and the `crossplay_*` symbols the browser build exports.
- **D&Diagrams** for the nonogram game. Lowercase "dungeons" when you mean the
  puzzles themselves.
- **PLAY NEARBY** for the link layer, in any user-facing context. "Link" and
  "linkplay" are internal words.
- It is a **fork of CrossPoint**, said plainly and early. We track it, we did not
  replace it, and the relationship is a credential rather than an embarrassment.
- **Upstream's name stays on upstream's things**, and only on those: the
  `/.crosspoint` card directory that holds every user's saves and progress, the
  `CROSSPOINT-BOARD-V1` image tag upstream's web flasher scans for, the
  `crosspoint` token in Calibre's discovery reply, the Calibre plugin, the
  `sync.crosspointreader.com` sync server and the `crosspoint-fonts` repo.
  Renaming any of those does not rebrand anything -- it loses somebody's data or
  breaks a handshake. Everything a person actually reads is ours and says
  CrossPlay. `host-tests/brand/` holds the line in both directions.
- The device is the **Xteink X4 Pro**. CrossPlay does not claim the others.

## The mark

Corner brackets around a screen. See `site/assets/logo.svg`, and
`scripts/generate_logo.py` for the version the device draws.

The brackets are the one shape this fork invented rather than inherited: they
frame the chess board, the mini board on its menu, and every Toybox panel. That
makes the mark the same material as everything else here, which is the only kind
of ornament this document allows.

It also has to survive being 16px in a browser tab. Four right angles and a
solid block have no detail to lose, and unlike a pair of overlapping rectangles
it does not collide with the copy/duplicate glyph every desktop already ships.
Four alternatives were drawn and rejected on exactly those two grounds: a pair
of panels side by side (reads as an open book, which is upstream's idea, not
ours), overlapping panels (the duplicate icon), the band over shelf rows (a
hamburger, small), and a pixel C monogram -- the last one only because it says
nothing about a device, and it is what to reach for if the mark ever has to live
off a screen.

### Where the mark lives

Off the device, two files carry it by hand. `site/assets/logo.svg` keeps
`currentColor` and is inlined in the page so the theme toggle moves it;
`site/assets/favicon.svg` resolves the colour with a `prefers-color-scheme`
rule, because a favicon renders alone and `currentColor` would come out black
against a dark tab strip. One shape, two files: change one and change the other.

On the device it is the boot screen and the sleep screen, which is where most
people meet it. Those are generated rather than drawn, by
`scripts/generate_logo.py`, which writes all three of `src/images/Logo120.h`
(the 1-bit array the panel draws), `src/images/Logo120.png` (the same pixels as
RGBA, which the user guide cover is built from) and `src/images/logo.svg`. The
geometry lives in the script; the files are output. Edit the script and re-run
it, never the files -- a hand-edited header still compiles, and a green build
says nothing about whether your change is in the tree.

Two details that cost time if rediscovered. The array is stored a quarter turn
from how it is seen (`stored(a, b)` is `visible(H-1-b, a)`), and `1` is WHITE,
so the mark itself is the zeroes. And it is rasterised from exact rectangles
rather than rendered and thresholded: every edge is axis-aligned, the panel is
1-bit, and an anti-aliased grey edge has nowhere to go. That also means the
script needs neither PIL nor cairosvg, neither of which is installed here.

`host-tests/brand/` fails if the generated files stop matching the script.

# CrossPlay: the identity, on the device and off it

[`design-language.md`](design-language.md) is the authority for what an app looks
like on the panel. This is the layer above it: what CrossPlay _is_, how it says
so, and what survives when the identity leaves the device for a screenshot, a
README or a web page.

Read this before writing any copy or building anything the public sees. Read the
design language before drawing anything the device shows.

## What this rules out

Written down because each was considered and rejected, and rediscovering them
costs a session:

- **A mascot.** Static art becomes wallpaper by the third day on the device, and
  a mascot on the web would be the only thing on the page not made of the
  product.
- **Feature-count marketing.** "11 apps!" invites comparison with an app store
  we would lose. The apps are evidence for the sentence at the top, not the
  pitch.
- **Colour as a brand accent.** Everything upstream uses one. Ours is the
  absence: a black-and-white page next to accented ones is the most
  differentiated we can be, and it is honest about what the screen does.
- **Calling it a games console.** It is a device for still activities. Half of
  them are not games, and the reading the fork inherited is still there and still
  good.
