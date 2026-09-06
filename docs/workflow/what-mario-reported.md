# What Mario reported

Recovered 2026-09-06 (card #370), because the board could not answer the
question. Every card now carries a `reporter`; this file is the readable
form of it and the evidence behind each `mario` row.

Live version of the same list, always current:

```
board list --from-mario          # only his
board list --reporter session    # only what a session found
board list --reporter unknown    # the ones nobody could establish
```

## The count, over all 360 cards on the board

| reporter | cards | what it means |
| --- | --- | --- |
| `mario` | 57 | he hit it, asked for it, or ruled on it, and said so |
| `user` | 2 | a person who is not Mario: the public report form, a GitHub issue |
| `session` | 295 | our own side found it: an audit, a gate, a cold review, a probe, the error trigger |
| `unknown` | 6 | the card carries no evidence either way |

## How a card earned `mario`

Two kinds of evidence, and nothing else. Topic was never evidence: a card
about wallpapers is not his because wallpapers were his idea.

- **quote** -- a run of eight or more words in the card matched, word for
  word, a real message of his in the session transcripts
  (`~/.claude/projects/-Users-mario-Projects-Personal-Code-Xteink/*.jsonl`,
  human turns only: sidechains, tool results and compaction summaries excluded).
- **card** -- the card itself names him as the source of that observation
  ("Mario reported", "Mario, 2026-09-05:", "Mario's request").

A card that merely records his ANSWER to a question a session put to him is
NOT his: #94, #95 and #96 are session work items carrying his go-ahead.

## The 57 cards Mario reported

### Bugs he hit (21)

- **#107** [get books, released] The books icon has a box around it now
  <br>evidence: quote: "the icon that looks like some books at the top left is now surrounded by a rectangle" 2026-09-03 23:14
- **#108** [get books, released] Cannot go back until the book image has loaded
  <br>evidence: quote: same 2026-09-03 23:14 message, second complaint
- **#131** [site, released] The Anki and Instapaper login pages do not look like the site
  <br>evidence: quote: "those pages don't follow at all the style of the rest of the website" 2026-09-04 03:12
- **#138** [firmware, released] Games and apps screens show a WHITE strip above the black header band, visible off-axis from below
  <br>evidence: quote: "if I look at it from the bottom, on the very, very top above the titles ... I see white space" 2026-09-04 04:15
- **#166** [instapaper, done] Sync fails: Instapaper answered the article list in a shape this bridge does not know
  <br>evidence: quote: "it said Instapaper answered the article list in a shape. This bridge does not know." 2026-09-04 02:43
- **#179** [forehead, released] Forehead: PLACES holds everyday venues, not geography
  <br>evidence: quote: "This is called Places ... And somehow it has supermarket and farm." 2026-09-04 15:27
- **#236** [connections, released] Connections: tiles and solved rows elide text; make showing the whole string a constructed guarantee
  <br>evidence: quote: "the category description or words list after a set is found to get truncated and look bad often" 2026-09-05 06:22
- **#240** [site, released] The CrossPlay tile on Seeed's playground page is a screenshot, not a lockup
  <br>evidence: quote: "the submitted image is bad, should've been something like what the others submitted" 2026-09-05 07:04
- **#243** [battleship, released] Battleship's 'Tap a target' button is light grey and badly ghosted
  <br>evidence: quote: "On battleship when the game starts the button that says Tap a target is in a light grey" 2026-09-05 07:08
- **#244** [connections, review] Connections' Get puzzles freezes the device with no feedback, and reads as a crash
  <br>evidence: quote: "The get puzzles button of the connections game makes the device stall" 2026-09-05 07:09
- **#246** [murdle, released] Murdle's refusal notice is a paragraph, and the band reserved for it pushed the grid down
  <br>evidence: quote: "That message is WAY to long, two lines, should only be like a couple words" 2026-09-05 07:12
- **#247** [connectfour, released] Connect Four's lip turns to dithered squares on the opponent's turn
  <br>evidence: quote: "I dont get the change of the top row on connect 4 from circles to some kind of squares" 2026-09-05 07:13
- **#248** [firmware, review] Yahtzee's dice sit 5px under the header rule, and the rule itself is drawn on some screens and not others
  <br>evidence: quote: "Yatzee dices touch the top header after rolled" 2026-09-05 07:16
- **#250** [firmware, released] Back swipe does not exit Trivia, and almost no game asks for it
  <br>evidence: quote: "Can't exit trivia with a back swipe, fix it and check if it happens for other games" 2026-09-05 07:18
- **#261** [wavelength, merged] Wavelength is all text and no marks; add icons, with a critic on the render
  <br>evidence: quote: "The wavelenght game has a design problem, too much text and not a single icon in sight" 2026-09-05 07:33
- **#262** [wavelength, merged] Wavelength's front door offers a score that doesn't exist, and its layout is 101 magic numbers
  <br>evidence: quote: "the UI for wavelenght is weird, it shows the see the score so far when no game is in course" 2026-09-05 07:34
- **#293** [murdle, released] Murdle's refusal notice should sit between the board and the key, not above the board
  <br>evidence: quote: "Two forty six is still not great. The text appears on top of the screen." 2026-09-05 13:19
- **#295** [firmware, released] Yahtzee's dice clearance, the half of #248 that does not touch ToyboxScreen.h
  <br>evidence: card: "Mario reported 'Yahtzee dices touch the top header after rolled'" (split of #248)
- **#311** [trivia, review] Trivia: the US-centric toggle is in global System settings, not in the Trivia app
  <br>evidence: card: "Mario reported 2026-09-05"
- **#327** [getbooks, reported] Get Books: DOWNLOAD is dead until the cover loads -- the blocking pump honours only Back and Home, and swallows every other tap
  <br>evidence: quote: "The download button on the app Get Books is completely unresponsive until the book cover loads" 2026-09-05 19:40
- **#351** [firmware, reported] CrossPoint: a non-quick-resume wake shows no sign of life until the first paint
  <br>evidence: quote: "I have to hold the thing for a lfew seconds to wake it up and takes like 5 secs" 2026-09-05 21:29

### Things he asked for (18)

- **#125** [firmware, released] Device info rides on requests to CrossPlay's own services; the device never calls home
  <br>evidence: card: "Mario, 2026-09-04: a device must never spend its radio on reporting"
- **#127** [site, done] Report from a floating button on the main site, in a dialog, on desktop too; pick one device or both
  <br>evidence: card: "Mario: the report page looks mobile-only, 'not sure' makes no sense"
- **#130** [site, done] The inbox page gets a desktop layout
  <br>evidence: quote: "The inbox is also only optimized for phone. for some reason." 2026-09-04 03:11
- **#191** [trivia, released] Trivia: a toggle for US-centric questions
  <br>evidence: quote: "US centric trivia needs to go. At least for now." 2026-09-04 19:09
- **#241** [firmware, reported] Swap the in-house game AI for a standard engine wherever one is clearly better
  <br>evidence: quote: "We dont use a standarized chess engine with standarized difficulties" 2026-09-05 07:06
- **#249** [minesweeper, working] Minesweeper: clicking a satisfied number should chord-reveal its neighbours
  <br>evidence: quote: "On minesweeper when a number is clicked ... reveal the rest of the squares" 2026-09-05 07:18
- **#253** [firmware, reported] Unify how downloadable packs work, and make xkcd's freshness knowable
  <br>evidence: quote: "Things that are downloadable also feel like should be unified" 2026-09-05 07:24
- **#257** [trivia, review] Trivia: report a question, filter what you get, and sync the pack instead of redownloading it
  <br>evidence: quote: "a way to report questions and a simple button of why is it being reported" 2026-09-05 07:30
- **#260** [instapaper, released] Instapaper has no way to disconnect the account from the device
  <br>evidence: quote: "Instapaper needs a way for the user to disconnect from the connected account" 2026-09-05 07:31
- **#264** [firmware, review] New game: Picross
  <br>evidence: quote: "I need to implement a new game Picross" 2026-09-05 07:38
- **#266** [firmware, merged] New app: Wallpapers, plus a browser uploader for them
  <br>evidence: quote: "I need a new app called wallpapers that does two things" 2026-09-05 07:42
- **#302** [firmware, reported] Wallpaper option: the cover of the last book being read
  <br>evidence: card: "Mario's request, 2026-09-05, while reviewing the wallpaper set"
- **#305** [firmware, working] Wallpapers: multi-select a set, and shuffle it as the sleep screen
  <br>evidence: quote: "how to select multiple wallpapers at one and how to make them cycle or random" 2026-09-05 17:34
- **#328** [getbooks, reported] Get Books: offer OPEN on a finished download, so the book does not require a trip to Browse Files
  <br>evidence: card: quotes him -- "there should be a button to open the book as soon as it gets downloaded"
- **#329** [getbooks, review] Get Books: the download-failure screen is three centred lines and needs a real design -- three variants for Mario to choose
  <br>evidence: quote: "we need to make the error screen when a book download fails prettier" 2026-09-05 19:40ff
- **#348** [infra, review] Open both bridges to the world: Study and Instapaper are BOTH invitation-only, not just Study (GitHub issue #115)
  <br>evidence: quote: "Make it so the Anki/Study one does too and make sure that the Get Books one too" 2026-09-05 21:31
- **#349** [firmware, review] Wallpapers: phone-to-device by QR, plus preview/delete and multi-select in the picker
  <br>evidence: quote: "The idea of the add a wallpaper thing was to have an easy way to get from picture on my phone to wallpaper" 2026-09-05 21:29
- **#365** [wallpapers, review] Wallpapers: hold a tile to preview or delete it, and sort user uploads first
  <br>evidence: quote: "user uploaded wallpapers always come in front of the deafult ones" 2026-09-05 21:29

### Decisions and standing rules he set (18)

- **#49** [mario, done] Ask: report bugs and ideas from the website
  <br>evidence: quote+card: one of the twelve "Ask:" cards; his 2026-09-03 05:24 bug-pipeline conversation
- **#50** [mario, done] Ask: one board for everything, my own work included
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#51** [mario, done] Ask: rules the sessions cannot break
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#52** [mario, done] Ask: one orchestrator; workers talk only to it; I hear only what needs me
  <br>evidence: quote: "sessions ... only be able to talk to the orchestrator session" 2026-09-03 05:45
- **#53** [mario, done] Ask: a dispatcher I tell issues to, that finds the right session
  <br>evidence: card: "Ask:" card on app mario; his dispatcher ask, 2026-09-03 06:47
- **#54** [mario, done] Ask: releases that block nobody
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#55** [mario, done] Ask: close sessions by rule, keep history, stop cluttering my mind
  <br>evidence: card: "Ask:" card on app mario; his session-closing ask, 2026-09-03 06:09
- **#56** [mario, done] Ask: keep up with CrossPoint daily, merges handled
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#57** [mario, done] Ask: analytics on everything, one place to answer 'how many'
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#58** [mario, done] Ask: errors from any service trigger a fix, one session per problem
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#59** [mario, done] Ask: GitHub issues picked up and closed automatically
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#60** [mario, done] Ask: tell Main to keep going
  <br>evidence: card: "Ask:" card on app mario, from his 2026-09-03 bug-pipeline conversation
- **#93** [trivia, done] Trivia: difficulty 1 and difficulty 5 feel identical and all questions are extremely hard -- Mario played it, the levels do not differ
  <br>evidence: card: title says "Mario played it, the levels do not differ"
- **#112** [instapaper, triaged] Instapaper pairing costs two scans: arriving at /pair by QR while signed out says "Sign in first, then scan the code on your reader again". The code is already in the URL, so it could be carried through the sign-in and paired in one pass.
  <br>evidence: quote: "I scanned the QR code and told me to log into my Instapaper account ... I had to scan the code again" 2026-09-04 02:43
- **#201** [mario, done] SCOPE RULE: a change that is not CrossPlay-specific is dismissed
  <br>evidence: quote: "stuff that crosspoint owns is not ours to fix" 2026-09-04 23:45
- **#252** [tooling, done] The two bridges are already open source; harden them for it, starting with study-bridge's gitignore
  <br>evidence: quote: "Are the study and instapaper services part of the open source project?" 2026-09-05 07:22
- **#313** [firmware, reported] VOCABULARY for #305: 'rotation' there means CYCLING through wallpapers, never screen orientation
  <br>evidence: card: "Mario flagged this on 2026-09-05 after reading the word rotation"
- **#370** [board, working] Record who reported each card, and recover it for the existing 122
  <br>evidence: card: filed for his own question, "what have I reported?"

## The 2 filed by someone who is not Mario

- **#13** [reader, released] Slow page turns, 4.2 s against 1 s on stock, measured over serial by a user
  <br>evidence: card: GitHub issue #7, reported 2026-09-01 by an outside user over serial
- **#207** [unknown, done] Test test test, I want to see if this reporting arrives.
  <br>evidence: site form, reporter_email testv@gmail.com -- a person, not Mario's address

## The 6 nobody could establish

Left `unknown` on purpose. Each could be his or could be a session's; the
card says nothing and no transcript carries the words. A wrong name here
would cost more than a blank one.

- **#25** [firmware, released] Wi-Fi picker: one press exits the picker AND closes the app underneath
- **#36** [link, released] Every link game: the loser sees zero frames of the final board -- driveLink applies the winning move and sets rematch_ in one pass, and requestUpdate is deferred to the end of the loop
- **#40** [hackernews, released] Hacker News: NOT SAVED (card full) throws you out of the article you were reading; needs a toast/overlay the app does not have
- **#43** [firmware, done] No app overrides handleHomeGesture: any app mutating saved state from a background poll can write the card after the player has left
- **#100** [trivia, done] Trivia clue text is corrupted at the source: an N./S. expansion turns "U.S. Army" into "U.South Army" and "Jose N. Duarte" into "Jose North Duarte". Present in BOTH packs, so it predates the rebuild; needs a rebuild from the Jeopardy source
- **#111** [trivia, done] Trivia Solo coverage runs backwards: 18% of level 1 has options vs 42% of level 5, because easy clues have common-noun answers whose type pool is too small. Level 1 in Solo is 141 questions.

## What this list does NOT contain

Things he reported that never became a card. Found while doing this, not
fixed here, and each is a real gap between what he said and what the board
holds:

- 2026-09-05 16:39 and 18:43, Picross: the strike line not through the
  centre of the row number, asymmetric dithering around the numbers, the
  puzzle-select screen's brackets fighting its rounded corners, and the
  size tabs being tight on vertical padding. All handled on #264 in
  conversation; none of it is on the board.
- 2026-09-05 21:15, Wallpapers: the app taking ~10s on first load, the
  "Tap one to set your sleep screen" line sitting too close to the grid,
  and the download bar filling twice.
- 2026-09-05 23:12, Wallpapers: touch refused until the previous tap's
  brackets have finished drawing, so taps feel lost.
- 2026-09-06 02:51, the wallpaper uploader: one picture at a time, and it
  does not follow the site's styling.
- 2026-09-04 23:23 and 2026-09-05 05:17: the release notes are "complete
  nonsense and bloat" and still "impossibly long".
- 2026-09-05 03:38: artifacts in a downloaded book, tildes especially.

A report that reaches a session and never reaches the board is invisible to
this list by construction, whatever the reporter column says.
