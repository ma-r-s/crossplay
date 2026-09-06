# What Mario reported

Recovered 2026-09-06 (card #370), because the board could not answer the
question. Every card carries a `reporter`; this file is the readable form of
it and the evidence behind every `mario` row.

```
board list --from-mario          # only his
board list --reporter session    # only what a session found
board list --reporter unknown    # the ones nobody could establish
```

Those flags reach `board` when this change is in `firmware-next`, which is
what `/opt/homebrew/bin/board` resolves to and which runs behind `xteink`.
The counts and the list below are the recovery itself and need no CLI.

## The count, over all 378 cards on the board

| reporter | cards | what it means |
| --- | --- | --- |
| `mario` | 75 | he hit it, asked for it, or ruled on it, and said so |
| `user` | 1 | a person who is not Mario: a GitHub issue, the public report form |
| `session` | 300 | our own side found it: an audit, a gate, a cold review, a probe, the error trigger |
| `unknown` | 2 | the card carries no evidence either way |

## How a card earned `mario`

Two kinds of evidence, and nothing else. Topic was never evidence: a card is
not his because it is about wallpapers and wallpapers were his idea.

- **quote**: a run of words from the card matched, word for word, something he
  actually typed.
- **card**: the card itself names him as the source of that observation.

A card that records his ANSWER to a question a session put to him is NOT his.
#94, #95 and #96 are session work items carrying his go-ahead.

### Three channels, and the first pass read only one

This matters more than any single row, because it is why the first attempt
was narrower than it looked.

1. **Ordinary turns**, stored as transcript entries of type `user`. The first
   pass read these and nothing else.
2. **Anything he typed while a session was mid-turn.** These are never stored
   as a `user` entry at all. They arrive as an attachment of type
   `queued_command` whose origin kind is `human`, and there are **221** of
   them, **74** inside the window this board covers. They carry the source
   text behind #125, #127, #302, #311, #328, #329 and most of the twelve
   `Ask:` cards, and every one of the reports in the last section.
3. **His typed answers in the `blockers` table**, 34 of them. #93's evidence
   lives there and nowhere else.

Reading all three did not add or remove anybody from the `mario` set by
verbatim match. What it changed is the evidence: several rows previously
cited a quotation that was not in the corpus at all, which means they were
resting on a session's paraphrase without saying so.

## The 75 cards Mario reported

### Bugs he hit (34)

- **#107** [get books, released] The books icon has a box around it now
  <br>evidence: quote: "the icon that looks like some books at the top left is now surrounded by a rectangle" 2026-09-03 23:14
- **#108** [get books, released] Cannot go back until the book image has loaded
  <br>evidence: quote: same 2026-09-03 23:14 message, second complaint
- **#131** [site, released] The Anki and Instapaper login pages do not look like the site
  <br>evidence: quote: "those pages don't follow at all the style of the rest of the website" 2026-09-04 03:12
- **#138** [firmware, released] Games and apps screens show a WHITE strip above the black header band, visible off-axis from below
  <br>evidence: quote: "if I look at it from the bottom, on the very, very top above the titles of everything in apps and games, I see white space. We should make it black" 2026-09-04 04:15
- **#166** [instapaper, done] Sync fails: Instapaper answered the article list in a shape this bridge does not know
  <br>evidence: quote: "it said Instapaper answered the article list in a shape. This bridge does not know." 2026-09-04 02:43
- **#179** [forehead, released] Forehead: PLACES holds everyday venues, not geography
  <br>evidence: quote: "This is called Places ... And somehow it has supermarket and farm." 2026-09-04 15:27
- **#236** [connections, released] Connections: tiles and solved rows elide text; make showing the whole string a constructed guarantee
  <br>evidence: quote: "What I see really often is the category description or words list after a set is found to get truncated and look bad often" 2026-09-05 06:22. The card's own opening quote was a paraphrase and has been corrected
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
  <br>evidence: quote (queued): "the setting for the US centric trivia somehow ended in system settings. That's absolutely not correct. It should be in a settings menu inside the trivia app" 2026-09-05 17:37
- **#327** [getbooks, reported] Get Books: DOWNLOAD is dead until the cover loads -- the blocking pump honours only Back and Home, and swallows every other tap
  <br>evidence: quote: "The download button on the app Get Books is completely unresponsive until the book cover loads" 2026-09-05 19:40
- **#351** [firmware, reported] CrossPoint: a non-quick-resume wake shows no sign of life until the first paint
  <br>evidence: quote: "I have to hold the thing for a lfew seconds to wake it up and takes like 5 secs" 2026-09-05 21:29
- **#372** [firmware, reported] Settings: CrossPlay's rows sit above CrossPoint's, and Developer Mode is near the top
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#373** [wallpapers, reported] Wallpapers: the starter set is basic, two of them look bad, and he wants sixteen-plus researched rather than generated
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#374** [wallpapers, reported] Wallpapers picker: two columns not three, brackets instead of a thick border, and three wallpapers to drop
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#375** [firmware, reported] Header height: Yahtzee's is taller than the shelf's, and he wants ALL of them at the shorter one with one styling
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#376** [getbooks, reported] Get Books: every page spends room saying a book is an EPUB, and EPUB is the only format supported
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#378** [instapaper, reported] Instapaper: a long article takes a long time to open, and he asked what the mechanism is
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#381** [wavelength, reported] Wavelength: the eye icon and others are too small after the icon pass
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#382** [firmware, reported] Picross: the strike line misses the centre of the number, the dithering is asymmetric, and the select screen needs redesigning
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#384** [wallpapers, reported] Wallpapers: the app takes about ten seconds to open, the hint sits too close to the grid, and the download bar fills twice
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#385** [wallpapers, reported] Wallpapers: taps are refused until the previous tap's brackets finish drawing, so touches feel lost
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#386** [site, reported] The wallpaper uploader takes one picture at a time and does not follow the site's styling
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#387** [tooling, reported] Release notes are nonsense and bloat, and still impossibly long after the first pass
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#388** [getbooks, reported] A downloaded book shows artifacts, tildes especially
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board

### Things he asked for (19)

- **#125** [firmware, released] Device info rides on requests to CrossPlay's own services; the device never calls home
  <br>evidence: quote: "I don't want the devices to spend battery on connecting to Wi Fi every now and then and telling me that they are being used" 2026-09-04 03:01
- **#127** [site, done] Report from a floating button on the main site, in a dialog, on desktop too; pick one device or both
  <br>evidence: quote (queued): "it looks like it's made for, like, mobile, but only mobile. Second, it has a not sure option, which makes no sense ... a button on the main website, like, on the lower right" 2026-09-04 03:02
- **#130** [site, done] The inbox page gets a desktop layout
  <br>evidence: quote: "The inbox is also only optimized for phone. for some reason." 2026-09-04 03:11
- **#191** [trivia, released] Trivia: a toggle for US-centric questions
  <br>evidence: quote: "US centric trivia needs to go. At least for now." 2026-09-04 19:09
- **#207** [unknown, done] Test test test, I want to see if this reporting arrives.
  <br>evidence: quote: he sent it himself. Card created 05:43:33; at 05:45:50 he typed "No, I sent it, check". The card also carries HIS two devices and version. The earlier "not his address" reading was the topical inference the method forbids
- **#241** [firmware, reported] Swap the in-house game AI for a standard engine wherever one is clearly better
  <br>evidence: quote: "We dont use a standarized chess engine with standarized difficulties" 2026-09-05 07:06
- **#249** [minesweeper, review] Minesweeper: clicking a satisfied number should chord-reveal its neighbours
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
  <br>evidence: quote (queued): "Blake door looks like the cover of a book. And cover of a book should be one of the options that are allowed as a wallpaper. Simply the last book that was being read" 2026-09-05 16:59
- **#305** [firmware, working] Wallpapers: multi-select a set, and shuffle it as the sleep screen
  <br>evidence: quote: "how to select multiple wallpapers at one and how to make them cycle or random" 2026-09-05 17:34
- **#328** [getbooks, reported] Get Books: offer OPEN on a finished download, so the book does not require a trip to Browse Files
  <br>evidence: quote (queued): "there should be a button to open the book as soon as it gets downloaded. No need to go to browse files" 2026-09-05 19:41
- **#329** [getbooks, review] Get Books: the download-failure screen is three centred lines and needs a real design -- three variants for Mario to choose
  <br>evidence: quote (queued): "we need to make the error screen when a book download fails prettier, design them and show me 3 alternatives" 2026-09-05 19:42
- **#348** [infra, review] Open both bridges to the world: Study and Instapaper are BOTH invitation-only, not just Study (GitHub issue #115)
  <br>evidence: quote: "Make it so the Anki/Study one does too and make sure that the Get Books one too. The objective is for anyone in the world with the OS installed to be able to use all these services" 2026-09-05 21:31. The DEFECT came from GitHub issue #115, filed by an outside user; his instruction is what made it a card and what set its scope
- **#349** [firmware, review] Wallpapers: phone-to-device by QR, plus preview/delete and multi-select in the picker
  <br>evidence: quote: "The idea of the add a wallpaper thing was to have an easy way to get from picture on my phone to wallpaper" 2026-09-05 21:29
- **#365** [wallpapers, review] Wallpapers: hold a tile to preview or delete it, and sort user uploads first
  <br>evidence: quote: "user uploaded wallpapers always come in front of the deafult ones" 2026-09-05 21:29

### Decisions, standing rules and asks (22)

- **#49** [mario, done] Ask: report bugs and ideas from the website
  <br>evidence: quote: "I wish on the website there was a really quick way a user could just describe what he's experiencing, and maybe if he wants attach a picture" 2026-09-03 05:24
- **#50** [mario, done] Ask: one board for everything, my own work included
  <br>evidence: quote: "what I want is for those to land in what I think would be a web app ... it's not only for future requests, it's for my own development too" 2026-09-03 05:45
- **#51** [mario, done] Ask: rules the sessions cannot break
  <br>evidence: quote: "sessions are supposed to not talk between them ... and they are just ignoring me at this point" 2026-09-03 06:27, the ask the hooks were built to enforce
- **#52** [mario, done] Ask: one orchestrator; workers talk only to it; I hear only what needs me
  <br>evidence: quote: "I want those sessions to only be able to talk to the orchestrator session, which will either resolve itself or actually raise it to me" 2026-09-03 05:45
- **#53** [mario, done] Ask: a dispatcher I tell issues to, that finds the right session
  <br>evidence: quote: "I wish I had just one session that would do that for me so I can just tell it anything ... and now it's working on it" 2026-09-03 06:47
- **#54** [mario, done] Ask: releases that block nobody
  <br>evidence: quote: "we are getting stuck by releases a lot ... I just wish we could, like, accumulate fixes and then release" 2026-09-03 05:24
- **#55** [mario, done] Ask: close sessions by rule, keep history, stop cluttering my mind
  <br>evidence: quote: "I have a big issue where I'm basically unable to close sessions because there's always something that stays hanging ... for it not to clutter my mind" 2026-09-03 05:45
- **#56** [mario, done] Ask: keep up with CrossPoint daily, merges handled
  <br>evidence: quote: "I need to update my dependencies, which are the repository I worked from and free ink, and for it to happen autonomously" 2026-09-04 03:51
- **#57** [mario, done] Ask: analytics on everything, one place to answer 'how many'
  <br>evidence: quote: "Remember what I told you yesterday that I wanted numbers on everything? ... everything that involves this project needs good observability" 2026-09-03 22:29
- **#58** [mario, done] Ask: errors from any service trigger a fix, one session per problem
  <br>evidence: quote: "What happens when one of the services study or Instapaper or getbooks has a error? Does it investigate by itself?" 2026-09-04 10:17
- **#59** [mario, done] Ask: GitHub issues picked up and closed automatically
  <br>evidence: quote (queued): "an automatic way to keep up with GitHub issues ... if easily solvable or replicable. than just solving them and closing them" 2026-09-03 07:21
- **#60** [mario, done] Ask: tell Main to keep going
  <br>evidence: quote (queued): "I know you stopped the session called main ... when you're done, make sure to tell it to keep going. So we are not stuck not moving stuff forward" 2026-09-03 07:24
- **#93** [trivia, done] Trivia: difficulty 1 and difficulty 5 feel identical and all questions are extremely hard -- Mario played it, the levels do not differ
  <br>evidence: quote, from his typed inbox answer on card #1: "The difficulty seems to be completely random. I can't feel any difference between difficulty one and difficulty five questions. They're all extremely hard."
- **#112** [instapaper, triaged] Instapaper pairing costs two scans: arriving at /pair by QR while signed out says "Sign in first, then scan the code on your reader again". The code is already in the URL, so it could be carried through the sign-in and paired in one pass.
  <br>evidence: quote: "I scanned the QR code and told me to log into my Instapaper account. I logged in, and then I had to scan the code again" 2026-09-04 02:43
- **#201** [mario, done] SCOPE RULE: a change that is not CrossPlay-specific is dismissed
  <br>evidence: quote: "stuff that crosspoint owns is not ours to fix" 2026-09-04 23:45
- **#252** [tooling, done] The two bridges are already open source; harden them for it, starting with study-bridge's gitignore
  <br>evidence: quote: "Are the study and instapaper services part of the open source project?" 2026-09-05 07:22
- **#313** [firmware, reported] VOCABULARY for #305: 'rotation' there means CYCLING through wallpapers, never screen orientation
  <br>evidence: card: "Mario flagged this on 2026-09-05 after reading the word rotation"
- **#370** [board, working] Record who reported each card, and recover it for the existing 122
  <br>evidence: quote: "Yes, recover what I reported and what came from somewhere else." 2026-09-06 04:03
- **#377** [getbooks, reported] Get Books stays usable during a transfer while Connections did not, which changes what #244's fix was for
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#379** [tooling, reported] Repo organisation: an unbiased agent to make the repo pristine, fixing the obvious and raising the rest
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#380** [tooling, reported] CI: he asked for one build step and wants CI optimisation treated as a priority
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board
- **#383** [firmware, reported] Picross: he wants premade puzzle sets found online, not original artwork
  <br>evidence: filed 2026-09-06 by card #370 from his verbatim words; it had never reached the board

## The 1 filed by someone who is not Mario

- **#13** [reader, released] Slow page turns, 4.2 s against 1 s on stock, measured over serial by a user
  <br>evidence: card: GitHub issue #7, reported 2026-09-01 by an outside user over serial

## The 2 nobody could establish

Left `unknown` on purpose. Each could be his or a session's; the card says
nothing and no transcript carries the words. A wrong name costs more than a
blank one.

- **#25** [firmware, released] Wi-Fi picker: one press exits the picker AND closes the app underneath
- **#40** [hackernews, released] Hacker News: NOT SAVED (card full) throws you out of the article you were reading; needs a toast/overlay the app does not have

## What he reported that had never reached the board

This is the part worth reading. A list of what he reported is worth much less
than one that shows what was dropped, and reading the queued channel turned up
seventeen reports that existed only in a conversation. All are now cards,
stamped `mario`, each carrying his verbatim words and the timestamp.

- **#372** [firmware] Settings: CrossPlay's rows sit above CrossPoint's, and Developer Mode is near the top
- **#373** [wallpapers] Wallpapers: the starter set is basic, two of them look bad, and he wants sixteen-plus researched rather than generated
- **#374** [wallpapers] Wallpapers picker: two columns not three, brackets instead of a thick border, and three wallpapers to drop
- **#375** [firmware] Header height: Yahtzee's is taller than the shelf's, and he wants ALL of them at the shorter one with one styling
- **#376** [getbooks] Get Books: every page spends room saying a book is an EPUB, and EPUB is the only format supported
- **#377** [getbooks] Get Books stays usable during a transfer while Connections did not, which changes what #244's fix was for
- **#378** [instapaper] Instapaper: a long article takes a long time to open, and he asked what the mechanism is
- **#379** [tooling] Repo organisation: an unbiased agent to make the repo pristine, fixing the obvious and raising the rest
- **#380** [tooling] CI: he asked for one build step and wants CI optimisation treated as a priority
- **#381** [wavelength] Wavelength: the eye icon and others are too small after the icon pass
- **#382** [firmware] Picross: the strike line misses the centre of the number, the dithering is asymmetric, and the select screen needs redesigning
- **#383** [firmware] Picross: he wants premade puzzle sets found online, not original artwork
- **#384** [wallpapers] Wallpapers: the app takes about ten seconds to open, the hint sits too close to the grid, and the download bar fills twice
- **#385** [wallpapers] Wallpapers: taps are refused until the previous tap's brackets finish drawing, so touches feel lost
- **#386** [site] The wallpaper uploader takes one picture at a time and does not follow the site's styling
- **#387** [tooling] Release notes are nonsense and bloat, and still impossibly long after the first pass
- **#388** [getbooks] A downloaded book shows artifacts, tildes especially

Every one of them was handled in conversation and none of it was on the
board, so none of it survived the session that heard it. **A report that
reaches a session and never reaches the board is invisible to this list by
construction, whatever the reporter column says.** That is the standing risk
this file cannot close on its own; only filing at the moment he speaks does.

## Two corrections to cards, made while doing this

Both were words attributed to him that he never said. A fabricated quote in
his own voice is worse than a missing attribution, because this list exists
so he can trust it.

- **#236** opened with `Mario: 'a tile needs to show the whole text, no
  exceptions.'` That string exists nowhere on this machine except the
  transcript of the session that wrote the card. What he actually said was
  *"What I see really often is the category description or words list after a
  set is found to get truncated and look bad often"*. The requirement was
  right; the attribution was invented. Card body corrected.
- **#174** said *"Mario reported ~10 page turns feeling slow while reading"*.
  No such report exists. A session asked **him** to turn ten pages so a PERF
  line could be read off the panel. The slow page turn on record is GitHub
  issue #7, from an outside user (#13). Card body corrected.

## Where two halves of one message got different answers

#295 is `mario` and #359 is `session`, and both trace to his 2026-09-05 07:16
message about Yahtzee. The rule that separates them: #295's subject is his
sentence word for word (*"Yatzee dices touch the top header after rolled"*).
#359's subject is Study, which he never mentioned; a session found that
instance of his general complaint. The same rule puts #375 in his column,
because *"They should ALL be the shorter height"* is his sentence too.

#348 is his, but the defect came from GitHub issue #115 filed by an outside
user. His instruction is what made it a card and what set its scope.
