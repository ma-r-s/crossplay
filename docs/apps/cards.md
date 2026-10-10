# Cards

Codes you show at a counter -- a loyalty card, a boarding pass, a link, your
contact details -- kept on the reader, one per screen, at a size a scanner reads.

## Using it

CARDS is on the shelf. It opens on the list of cards by title; tap one to show
it. The card's title is on the band, the code fills the width under it, and the
caption (if it has one) sits beneath. A barcode sits centred in the page with its
number under it in large type, for the cashier to key in when the scanner will
not read it; a barcode too long to keep two pixels a module across the page
turns and runs down it instead. PREV and NEXT at the foot, or the Up and
Down keys, move between cards; Back returns to the list. The bin on the band
deletes the card on screen after a confirm, and KEEP IT covers where the moon
and NEXT were, so a double tap keeps the card.

## On the sleep screen

The moon between PREV and NEXT puts the open card on the sleep screen, so a
boarding pass at the gate is one press of the power button away. It is outlined
while the card is not there and filled while it is; tap it again to take the
card off. Putting a card up sets Settings > Sleep screen to **Card** and turns
off Quick Resume on Timeout (which would skip the sleep screen); taking it off
puts back both settings as they were. It also turns Live off, as a note does.

The card is drawn from its file each time the reader sleeps, with no counter or
buttons and the code centred on the page. The choice is kept in
`/.crosspoint/cards-asleep.txt`: the card's file name, then the sleep screen
mode and Quick Resume setting it replaced. Deleting the card, on the reader or
from the phone page, takes it off the sleep screen; if the file goes missing
some other way, the reader shows the default sleep screen.

## Adding a card

The pencil on the list's band starts a small web server and shows a QR code for
it. On the phone page, give the card a title, choose a screenshot or photo of the
code, and optionally a caption. The page reads the code **on the phone**
(`BarcodeDetector` where the browser has it, otherwise the bundled ZXing, tried
both ways up), shows what it found, and sends only its text and kind to the
reader. A code that will not read can be typed in instead, with a picker for its
kind. The phone page also lists the cards and can delete them.

The reader draws QR codes and these barcodes: Code 128, Code 39, EAN-13, EAN-8,
UPC-A, UPC-E, ITF and Codabar. An EAN or UPC typed without its check digit gets
one. The phone can also read Aztec, PDF417, Data Matrix and Code 93, which some
boarding passes use, and says it found one, but the reader cannot draw those
yet.

## On the SD card

One file per card in `/cards`, named `0001.txt`, `0002.txt`, ... in the order
they were added:

```
Lidl Plus            <- title
Member since 2021    <- caption, may be an empty line
https://example.com  <- everything after line two is the code's payload
```

A barcode card names its kind after the title and a tab, `Lidl Plus<TAB>ean13`;
the names are `code128`, `code39`, `ean13`, `ean8`, `upca`, `upce`, `itf` and
`codabar`. A title with no kind is a QR code, so files from before barcodes read
as they always did.

The payload is kept verbatim (a contact card's own line ends survive); only
trailing line ends are dropped. The reader redraws the code from the payload at
error correction M (L when only L holds it), so it is sharp at any size.
Payloads are capped at 1200 bytes, which keeps every module at three pixels or
more on the panel. Barcodes are redrawn the same way, wide elements three times
the narrow ones, with ten modules of white either side; one wider than 260
modules with those (about 20 characters of Code 128 text) is refused, because it
would not get two pixels a module even running down the page.

## Where the code is

| File | What it holds |
| ---- | ------------- |
| `src/apps_local/wallet/WalletCore.*` | File format, upload parsing, QR version choice. Host suite: `host-tests/wallet`. |
| `src/apps_local/wallet/WalletBars.*` | The barcode kinds and their encoders. Host suite: `host-tests/wallet`; `tools_local/wallet/bars_roundtrip.sh` reads every encoder's bars back with zbar. |
| `src/apps_local/wallet/WalletSleep.*` | Drawing a card's code, and the card on the sleep screen. |
| `src/apps_local/wallet/WalletScreens.*` | The list, card, confirm and phone screens. Tested in `host-tests/ui`. |
| `src/apps_local/wallet/WalletStore.*` | SD I/O for `/cards`. |
| `src/apps_local/wallet/WalletServer.*` | The phone page's routes under `/cards`. |
| `src/apps_local/wallet/WalletPage.html`, `WalletZxing.js` | The phone page, and ZXing for JavaScript 0.21.3 (Apache-2.0, `zxing-LICENSE`). |

The directory is `wallet/` because `cards/` already holds the playing-card art
the card games share.
