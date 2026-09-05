# Instapaper on the reader

Your Instapaper unread queue, on the device. Articles arrive as text, the
reader remembers where you got to, and ARCHIVE puts one away everywhere.

It works offline once articles are on the card. That is the point of it: the
network is for syncing, not for reading.

## Setting it up, once

1. Open **Apps > INSTAPAPER** and press **SYNC**. The device shows a code and
   a QR.
2. On a phone or a laptop, sign in at **read.ma-r-s.com** with your Instapaper
   account, then scan the QR (or type the code).
3. The device asks **IS THIS YOU?** and shows the account name. Press the pill
   only if the name is yours.

Nothing is stored on the device until you press that pill. If a code you did
not ask for shows somebody else's name, press BACK: that is a stranger trying
to pair their account to your reader, and it fails.

The code lasts five minutes. Press SYNC again for a fresh one.

## Reading

The queue lists your unread articles, newest first. Each row shows the title,
how long it takes to read, and where it came from. A row that has been started
shows how far in you are: that is the point you would resume at, which is one
page behind the last page you looked at.

The two side keys page the LIST when the queue is longer than the screen. They
do nothing on a queue that already fits, because there is nowhere to page to.
Articles are opened by tapping; there is no cursor, and no key to move one --
the X4 Pro has no Confirm button.

- Tap a row to open it. The side keys turn pages, so do the two arrows at the
  bottom right.
- **ARCHIVE** puts the article away -- it leaves your unread list everywhere,
  the same as archiving it in any other Instapaper client. It sits at the
  bottom LEFT, away from the page arrows, so a miss while paging turns a page
  instead.
- **PUT BACK** appears beside SYNC once you archive something, and takes it
  back. It is there until the next sync carries the archive up, or until you
  open another article. After that, undo it from the Instapaper website or app.
- **BACK** leaves the article where you are. Your position syncs up on the next
  SYNC, so the phone picks up where the reader left off, and the other way
  round. An article you read to the end reopens on its last page, not at the
  top.

The reader holds the newest 120 articles. Older ones stay in your account; they
are simply not on the card.

## What syncing does

Both directions, in one press:

- **Up**: how far you have read, and anything you archived.
- **Down**: new articles, changed titles, anything archived or deleted
  elsewhere, and how far you read on another device.

A sync takes seconds. It ends on a screen that says what went each way: what
it sent up (archives, reading positions) and what came down. A sync with
nothing to send says only what came down.

The first one on a large queue prepares 25 articles and says how many more are
coming; press SYNC again for the rest.

## Disconnecting

The account icon sits on the queue's footer, beside SYNC. It opens a screen that
names who is signed in and asks **DISCONNECT?**. Disconnecting signs the reader
out of Instapaper AND deletes every article saved on the card -- it leaves the
app exactly as it was before you paired, so the next SYNC starts pairing again.
It is never a single tap: the safe answer, **KEEP IT**, is the wide button a
thumb lands on, and **DISCONNECT** is a smaller one set apart above it, so a
stray tap costs a glance rather than your reading list. Your articles stay in
your Instapaper account; only the copies on this reader are removed. The screen
says the exact number it will delete before you tap.

## When something is not right

**"NOT SHOWABLE HERE"** -- the article is written in a script this reader has
no letters for. Chinese, Japanese, Arabic, Greek and Cyrillic articles all land
here. The text is on the card; the panel's reading face simply cannot draw it.

**"n Instapaper could not prepare"** -- Instapaper itself could not produce a
text version of that page. It is usually a PDF, a video, or a page that needs a
login. Nothing is wrong with the reader.

**"NOT ON THE CARD"** -- the article's text did not finish downloading. Sync
again; it is picked up automatically.

**"This reader was unpaired"** -- somebody removed it on read.ma-r-s.com, or
the account was reconnected. Press SYNC and pair again.

## What it will not do

- **Delete.** Nothing here can delete a bookmark, on purpose. ARCHIVE is the
  only write this app makes, and it is reversible.
- **Star, folders, highlights, or saving new URLs.** The reader reads and
  archives. Everything else is what the phone and the browser extension are
  for.

## Where things are

- On the card: `/.crosspoint/instapaper/` -- a tab-separated `index.tsv` you
  can read in any text editor, one `a<id>.txt` per article, and `.bridge`
  holding the pairing token and the account name. Deleting that folder resets
  the app; deleting `.bridge` alone unpairs it. The account icon on the queue
  does the whole reset from the reader itself -- it removes the folder and, when
  online, tells the service to forget this reader too.
- The service: `server/read-bridge/` in this repo, and its design in
  [instapaper-plan.md](instapaper-plan.md).
