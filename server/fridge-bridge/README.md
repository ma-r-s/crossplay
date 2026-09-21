# fridge-bridge

`fridge.ma-r-s.com` -- Live. Somebody draws a note on their phone; a reader
asleep on a fridge in another country shows it in the morning.

Runs on the Orange Pi at `/srv/fridgebridge`, behind a Cloudflare Tunnel, same
shape as `read-bridge` and `study-bridge`. Its own subnet (172.31.87.0/24) and
its own uid (10004), because a shared one would let a compromise in any of the
three read the others' bind mounts.

## The one thing to understand

**The reader is asleep and cannot be reached.** Deep sleep drops the radio and
the USB, so nothing here ever pushes, polls or opens a connection to a device.
It wakes on its own schedule, makes ONE request, and goes back down.

That is why `/api/pull` answers "is there anything new" and "when should I wake
next" in the same reply, and answers `304` when the answer is no: a wake that
finds nothing costs a few kilobytes, no SD write and no repaint.

It is also why the countdown the website shows is computed HERE, from
`last_checkin + interval`, and is an **estimate**. The reader's sleep timer runs
off an RC oscillator and drifts percent-level; a refresh taken on the way into
sleep shifts the schedule until the next check-in; a wake missed for want of
Wi-Fi is invisible until the one after. The page says "in about five hours"
and never a figure to the second.

## Endpoints

The reader, bearer token:

| | |
|---|---|
| `POST /api/pair/start` | makes a fridge and a device token, returns a six-digit code |
| `GET /api/pair/poll` | hands the reader its token once a browser has claimed the code |
| `GET /api/pull` | `304` unchanged, `204` nothing ever sent, `200` + the BMP. `X-Next-Wake` and `X-Server-Time` on all three |

The browser, cookie:

| | |
|---|---|
| `POST /api/claim` | six digits in, a sender cookie out |
| `GET /api/state` | last check-in, interval, next expected |
| `PUT /api/image` | exactly 48062 bytes |
| `PUT /api/interval` | 15 minutes to a week |

## Six digits, not eight letters

The other bridges show an 8-character code because it is scanned off a QR or
typed by whoever is holding the device. This one is read down a **telephone**,
which is the whole reason Live uses a code at all: a QR can only be scanned by
somebody already holding the reader, and the day the Wi-Fi changes that person
is on another continent.

Six digits is a million, so the guessing is held off by the attempt caps rather
than the size of the space: a code dies after five wrong answers against it,
dies at ten minutes, and is single use. A miss on no code at all is charged
against every live code, so sweeping the space burns the space.

## Deploying

    server/fridge-bridge/scripts/deploy.sh

## The hostname

`fridge.ma-r-s.com`, created with `scripts/create-hostname.sh`. Live, and the
chain the reader sees was checked rather than assumed:

    CN=ma-r-s.com -> GTS WE1 -> GTS Root R4

GTS Root R4 is in the firmware's baked bundle, the same chain Study and
Instapaper already verify against, so the reader needs no root override and
nothing anywhere calls setInsecure().

It must stay exactly one label below the apex. Universal SSL on the free plan
covers `ma-r-s.com` and `*.ma-r-s.com` and nothing deeper, so
`fridge.crossplay.ma-r-s.com` would get no certificate at all.
