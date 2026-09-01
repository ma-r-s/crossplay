# Instapaper on the reader

Your Instapaper unread queue, on the device. Articles arrive as text, the
reader remembers where you got to, and ARCHIVE puts one away everywhere.

It works offline once articles are on the card. That is the point of it: the
network is for syncing, not for reading.

## Setting it up, once

1. Open **Apps > INSTAPAPER** and press **SYNC**. The device shows a code and
   a QR.
2. On a phone or a laptop, sign in at **read.crossplay.ma-r-s.com** with your Instapaper
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
shows how far in you are.

- Tap a row to open it. The side keys turn pages, so do the arrows.
- **ARCHIVE** puts the article away -- it leaves your unread list everywhere,
  the same as archiving it in any other Instapaper client. It is reversible
  from the Instapaper website or app.
- **BACK** leaves the article where you are. Your position syncs up on the next
  SYNC, so the phone picks up where the reader left off, and the other way
  round.

The reader holds the newest 120 articles. Older ones stay in your account; they
are simply not on the card.

## What syncing does

Both directions, in one press:

- **Up**: how far you have read, and anything you archived.
- **Down**: new articles, changed titles, anything archived or deleted
  elsewhere, and how far you read on another device.

A sync takes seconds. The first one on a large queue prepares 25 articles and
says how many more are coming; press SYNC again for the rest.

## When something is not right

**"NOT SHOWABLE HERE"** -- the article is written in a script this reader has
no letters for. Chinese, Japanese, Arabic, Greek and Cyrillic articles all land
here. The text is on the card; the panel's reading face simply cannot draw it.

**"n Instapaper could not prepare"** -- Instapaper itself could not produce a
text version of that page. It is usually a PDF, a video, or a page that needs a
login. Nothing is wrong with the reader.

**"NOT ON THE CARD"** -- the article's text did not finish downloading. Sync
again; it is picked up automatically.

**"This reader was unpaired"** -- somebody removed it on read.crossplay.ma-r-s.com, or
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
  holding the pairing token. Deleting that folder resets the app; deleting
  `.bridge` alone unpairs it.
- The service: `server/read-bridge/` in this repo, and its design in
  [instapaper-plan.md](instapaper-plan.md).
