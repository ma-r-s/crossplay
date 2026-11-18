[Install it](https://crossplay.ma-r-s.com/#get) · [which file, and by hand](https://github.com/ma-r-s/crossplay/blob/xteink/docs/install.md) · [earlier releases](https://github.com/ma-r-s/crossplay/blob/xteink/docs/release-notes.md)

### What is new in 1.15.1

- RSS: the last page of a list shows its posts again; it came up empty in All, a single feed and Saved
- Study: in the Classic theme the card count no longer covers the back arrow
- Study: a short question with a long answer starts the answer on the same page instead of leaving that page almost empty
- Wikipedia, RSS and Study: the article's back button, the reading progress bar, the recent list, RSS's scroll bar and Study's chart now keep the page margins
- Study no longer says there is nothing to review when the reader's clock is wrong. It says WRONG DATE, and SYNC sets the clock (on the reTerminal Sticky, its clock chip too) so your due cards show up.
- Notes: the menu row that changes a note's words now says EDIT ON YOUR PHONE.
- Wordle and Connections open today's puzzle even when the reader's clock is months behind, and the next Wi-Fi connection sets the clock right.
- BOOX Picco (experimental, untested by us): each release carries `firmware-picco.bin`, written into the Picco's one app slot at 0xE0000 with CrossPoint's web flasher (BOOX Picco, Custom .bin) or esptool. Its maker's partitions and recovery partition are never written.
- The Install button reads a device's partition table first and refuses a BOOX Picco instead of writing over its recovery partition.
- Report a problem can name the BOOX Picco.

