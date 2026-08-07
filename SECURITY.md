# Security

Crossplay is firmware. It runs on a device you own, talks to the network, and
listens on a radio, so it is worth saying plainly what to do if you find a
problem and what this project can honestly promise.

## Reporting something

Open a [private security advisory](https://github.com/ma-r-s/crossplay/security/advisories/new).
That reaches me and nobody else, and it lets us talk before anything is public.

If you would rather not use GitHub, a normal issue is fine for anything that is
not exploitable. Use your judgement: a crash on a malformed EPUB is an issue, a
way to run code on someone else's device from across a room is an advisory.

This is one person's side project, not a company. I will acknowledge within a
week. I will not send you a bounty, and I will credit you in the release notes
unless you ask me not to.

## What is worth reporting

The parts of this fork that take input from outside the device:

- **PLAY NEARBY** (`src/apps_local/link/`). ESP-NOW frames from any device in
  range, parsed on the device. The protocol has no authentication and no
  encryption: anything in radio range can join a match or send a packet. That is
  a deliberate trade for "two devices on a table with nothing to type", and it
  is documented rather than defended. A packet that crashes the device or writes
  outside its buffer is a bug and I want to hear about it.
- **The apps that fetch** (`src/apps_local/hackernews/`, `xkcd/`,
  `connections/`). HTTP responses parsed on a device with 8MB of PSRAM and no
  memory protection. Malformed JSON, oversized images, redirect loops.
- **The SD card**. Every app reads its own state from files that a user can
  edit. Save files, packs, decks and fonts are all parsed.

Upstream's reader, EPUB engine, sync and network stack are
[CrossPoint's](https://github.com/crosspoint-reader/crosspoint-reader). If you
can reproduce a problem on stock CrossPoint, report it there: it affects far
more people, and they are better placed to fix it.

## What this project does not promise

- **No signed firmware and no verified boot.** Anyone with USB access to the
  device can flash anything onto it, including this. That is how you installed
  it and how you put CrossPoint back.
- **No security guarantees on the link layer.** See above. Do not use PLAY
  NEARBY as though it were a private channel.
- **Reading an article sends its URL to a third party.** The Hacker News app
  proxies through `r.jina.ai` to extract readable text, so that service learns
  what you open. This is stated on the site and in the README because it is a
  choice, not an accident.
- **Only the `xteink` branch is supported.** There is no long-term support
  branch and no backported fixes; the fix goes in the next release.

## Supported versions

The latest release. This fork tracks CrossPoint daily and has one branch.
