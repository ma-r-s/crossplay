# Notes on a small, slow screen: what people have actually built

Research into interaction design for text on constrained displays, and the
design that follows. 2026-09-15. Nothing here is about any existing
implementation; it is about what the best version would be.

## 1. The honest headline

**Nobody has solved typing on a small e-ink screen.** Not Amazon, not Onyx, not
Light Phone, not Mudita. Every shipping product does one of five things, and
four of them are "avoid it".

| Strategy | Who | How well it works |
|---|---|---|
| Offload to a companion device | Apple TV, Roku, Boox (BT keyboard), the published e-ink design guidance | **Best in class.** The only one that is genuinely pleasant |
| Voice | Light Phone II, Apple Watch, Pebble | Best when available. Needs a mic |
| Pick, don't type | Apple Watch quick replies, Pebble canned responses, grocery staples lists | Excellent for the 80% case |
| Physical keyboard | Pomera, Freewrite, AlphaSmart, Boox + BT | Great, but it is a different object |
| A better on-screen method | MessagEase, Quikwriting, Graffiti, T9 | **Trap.** See below |

The last row is the one worth killing early. The HCI literature is brutal about
novel text entry: MessagEase on first exposure measured **1482 ms per character
and 35.75% errors per line**; Quikwriting reached **16 wpm only after twelve
participants trained for ten hours across twenty sessions**. Mudita shipped an
e-ink phone with a physical keypad and **deliberately no T9 at all**. The
lesson: an input method your user has to learn is not an ergonomic win, it is a
tax you collect on their first day and repay never.

The published design guidance for e-ink says it outright: *"Make sure that using
the device's on-screen keyboard is as limited as possible"*, and offload typing
to companion devices.

## 2. The most polished solution that exists: the second screen

Apple TV and Roku have the best keyboardless text entry anyone ships, and the
detail that makes it good is not the phone, it is the **live echo**:

- The TV shows a text field. Your phone gets a notification. You tap it.
- You type on the phone's real keyboard. **Each character appears on the TV as
  you type, backspace included.** Search-as-you-type narrows results on the TV
  while you type on the phone.
- Dictation, autocorrect, emoji and every language come free, because it is the
  system keyboard.

The whole trick is that the big screen stays the thing you are looking at. It is
not "type on the phone, then submit and hope". The phone is a keyboard, not a
form. Copy that exactly.

The DIY e-ink world reached the same conclusion by a different road: the Kindle
dashboard pattern (a server renders a PNG, the Kindle wakes, fetches, displays,
sleeps) treats the device as a **surface**, with all authoring elsewhere. Those
projects are beloved and get weeks of battery.

## 3. If you must have an on-screen keyboard, these are the tricks

All of these are how shipping e-ink devices make it bearable:

1. **Repaint only the text line. Never repaint the keys.** The keyboard is
   static furniture. This is the single biggest trick and it is why Kindle
   typing feels tolerable.
2. **Pure black-on-white in the changing region.** Practitioner consensus:
   *"black <-> white is dramatically faster and more reliable (less ghosting)
   than anything involving grey."*
3. **No key-press animation and no blinking cursor.** Every blink is a refresh.
4. **Huge targets.** A mis-tap costs two refreshes: the wrong character and the
   delete. Target size is a latency feature, not an accessibility nicety.
5. **Word prediction.** KSPC research puts prediction at roughly **45% of the
   keystrokes**. On a panel where each keystroke costs ~300 ms of screen, that
   is a straight halving of the pain.
6. **A fast waveform while typing, one clean full refresh when you leave.**
   This is exactly what A2/DU mode is for on Android e-ink devices: accept
   ghosting during the burst, clean it once at the end.

## 4. The ergonomic rules for everything around the keyboard

From practitioners who have shipped e-ink UIs:

- *"Do not count on anything refreshing ever. Do not have animations, or
  transitions."*
- **Pages, not scrolling.** Design it like a comic book: static separate pages.
  Every pixel scrolled off screen is wasted time.
- **Reserve fixed white areas for output, then fill in with black.** Much better
  than the reverse.
- **Selection is modal, not a drag:** touch and hold to enter selection mode,
  then refine with **taps, not drags**. No drag and drop anywhere.
- **Line art over gradients.** Thick sans over thin serif. No shadows, no
  layering, no 3D.
- **Look at PalmOS 3.5 and the early Macintosh HIG**, not at modern mobile.
  *"5Hz e-ink is closer to a moving newspaper than a slow computer."*
- **Instant, cheap first feedback.** Because the real work takes 300 ms to 2 s,
  the tap itself must produce something immediately. Inverting a small rect is
  the fastest update an e-ink panel can do. Without it, every tap reads as a
  dead screen.

## 5. What the polished use case actually is

Three independent bodies of practice point at the same answer.

- **Friction research**: habit trackers live or die on whether logging takes
  more than a few seconds; the survivors are one-tap. Most tested trackers are
  abandoned by week two.
- **Grocery/list app UX**: the recurring problem is re-entering the same items
  every week. The fix everyone converges on is a **staples list you tap**, not
  faster typing.
- **Constrained-device messaging**: Apple Watch quick replies, Pebble canned
  responses. Picking from a short list beats composing, every time.

So the polished use case is not "notes". It is:

> **The list you carry, that you tick with one hand, that stays on the screen
> when you put it down.**

Shopping, packing, a recipe, the steps of a job you are doing with your hands.
The unifying condition is that **your hands are busy and a phone is a bad fit**:
wet, floury, gloved, cold, in a shop, in a workshop, on a bike. That is a real
product. "Notes in general" is not.

And the property no phone can match: the panel holds an image at zero power, so
**a note left open is a physical object**. Design for putting it down and
walking away, not for opening and closing.

## 6. The design

### 6.1 A deck of cards, not a document tree

A note is one card: a title line and a body that ideally fits one screen. No
folders, no tags, no rich text. A line beginning `- [ ]` is tickable; everything
else is prose. A shopping list is a card that is all checkboxes; a recipe is a
card with none. One data type, one parser, and the tickable line is the only
thing the device itself needs to be able to edit.

Long notes still page, but the unit to design for is one screen. A note longer
than a few screens is a document, and documents want a computer.

### 6.2 The input ladder

Ordered by cost to the user. Almost everything should happen in the first two
rungs:

1. **Tick an existing line.** One tap, one tiny repaint.
2. **Tap a frequent item.** A strip of the things this list has held before,
   learned from the user's own history. This is the grocery-staples insight and
   the quick-replies insight and the one-tap-logging insight, all the same
   finding.
3. **Type on your phone, with live echo on the panel.** The Apple TV pattern.
   This is the "add three things I have never bought before" path.
4. **On-screen keyboard.** Last resort, one line, static keys, prediction if
   there is a dictionary to hand.

### 6.3 The e-ink craft that makes it feel good

- **A tick is a black square appearing inside a white box.** Smallest, fastest,
  most reliable update an e-ink panel can perform. Strike the text with a line
  rather than greying it: grey ghosts, line art does not.
- **A ticked item does not move.** Every list app on a phone sinks completed
  items to the bottom. On e-ink that is a full-screen reflow for every tap, and
  it moves the thing your finger is next to. Leave it in place.
- **Invert the row on touch-down, before doing the work.** That is the button
  press-state, and it is the difference between "responsive" and "broken".
- **Nothing on the screen changes unless the user changed it.** No clocks, no
  live sync indicator, no polling spinner.
- **One full refresh when leaving a card**, to clean up the burst of partials.

### 6.4 What makes it comfortable rather than merely possible

The comfort argument is entirely about the moment **after** you look at it. On a
phone the list disappears in 30 seconds and you unlock again with a wet hand. On
this device you set it down, it stays lit at zero power, and you glance at it
ten times over an hour without touching it. That is the whole pitch, and it
means the design target is not "how fast can I enter a note" but "how good is
this to live beside for an hour".

## 7. What I would not build

- **A novel text-entry method.** The numbers in section 1 are decisive.
- **Handwriting.** Needs a digitizer; finger-drawing at this refresh rate is
  worse than the keyboard it replaces.
- **Sinking completed items, live search, autosave indicators, spinners.** All
  of them are per-keystroke repaints.
- **Rich text, folders, tags, a sync engine.** None of them are the reason
  anyone picks up a device like this.
- **Anything requiring an account.** The second-screen pattern needs a link, not
  an identity.
