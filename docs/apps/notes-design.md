# Notes: the design

Four screens, and the rules that produce them. Supersedes the earlier drafts.
Research behind it: `notes-interaction-design.md`.

## The constraint that shapes everything

**A person needs the device and nothing else.** No purchase, no account, no
subscription, no app install, no cable. A phone is optional and, when used, is
just a browser pointed at a page the device itself serves. This kills the one
tempting wrong turn (a Bluetooth keyboard), which would have made the thing a
worse Pomera and cost the user money.

## The input ladder

Everything is designed so the user lives on the top two rungs.

| Rung | Cost | Covers |
|---|---|---|
| Tick an existing line | one tap, one tiny repaint | ~70% |
| Tap an OFTEN pill | one tap, no typing | ~20% |
| Type on your phone, echoed live on the panel | ~20s, occasional | ~10% |
| On-screen keyboard | slow, last resort | rare |

## The four screens

1. **The deck.** Every note is one card, title plus body. No folders, no tags.
   A card whose lines are `- [ ]` shows a count; a prose card shows none. One
   data type, one parser, and the only edit the device must support is a tick.
2. **The open list.** 44px checkboxes, 32px type, ticked items struck in place.
   A fixed OFTEN row at the bottom holds the items this list has held before.
3. **Add items.** A QR and a plain address for the page the device serves, and
   under it the live echo: characters appear on the panel as they are typed on
   the phone, the way Apple TV and Roku do it. That echo is what makes it feel
   like a keyboard rather than a form.
4. **Asleep.** The panel holds its image at zero power, so the list is still
   there when you put the device down. This is the one thing no phone can do
   and it costs no code: it is what the screen does when nothing is drawn.

## The e-ink rules these obey

- **A ticked item never moves.** Every phone list app sinks completed items.
  Here that is a full-screen reflow per tap, and it shifts the row your finger
  is next to.
- **Strike, do not grey.** Grey ghosts; line art does not.
- **Invert the row on touch-down, before the work.** That is the press-state,
  and without it a 300ms gap reads as a dead screen.
- **Pages, not scrolling. No animation. Nothing moves unless the user moved it.**
- **Only the changed row repaints**, never the whole list.
- One full refresh when leaving a card, to clear the burst of partials.

## Deliberately absent

Folders, tags, search, rich text, sync engine, accounts, cloud, a novel text
entry method, handwriting, sinking completed items, spinners, live search.
