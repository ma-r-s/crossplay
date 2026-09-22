# fridge-bridge

`fridge.ma-r-s.com` -- Live, **the API and nothing else**. Somebody draws a
note on their phone; a reader asleep on a fridge in another country shows it in
the morning.

The page they draw on is `crossplay.ma-r-s.com/live/`, in `site/live/`, set in
the CrossPlay site's own stylesheet and chrome. This host serves no page: it
served one for a while and it shared nothing with the site.

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

It is also why the countdown the website shows is **stamped once, at the
check-in**, from the interval in that reply, and never recomputed. It used to be
computed from `last_checkin + interval` live on every `/api/state`, and that is
wrong in two ways at once: changing the schedule from the website moved the
countdown while the reader was still asleep on its old alarm, and a reader that
had never checked in produced `0 + interval`, a moment in 1970, which is why the
page could only say "the reader has not checked in yet" and show no time at all
in the minute after pairing. Before the first check-in the anchor is the pairing
instant (`created + interval_s`), so there is always a figure.

**The reader does not report its own alarm, and cannot.** A version of this had
it send one in `X-Wake-In`. The headers are composed before the reply is read,
and a pull the reader gets an answer to clears its failures and makes it adopt
that reply's interval -- so the figure sent is always the alarm for the state it
was in *before* the request. One failed check was enough: the retry that
succeeded reported fifteen minutes, armed a day, and the website spent the next
day saying the check was due. The service already holds the answer, because it
is the one handing out the interval.

It is still an **estimate** and both surfaces say so in words. The reader's
sleep timer runs off an RC oscillator and drifts percent-level; it only fetches
on its way into sleep, so a device in somebody's hands is legitimately hours
past due; a wake missed for want of Wi-Fi is invisible until the one after. The
page says "in about five hours" and never a figure to the second.

**Five states, and the page expresses all five**: just synced and never heard
from (counting from the pairing instant), waiting, due now (it arrives the next
time the reader is put down), late (past due by twelve hours or a whole
interval, whichever is longer -- and the page states the fact without
diagnosing, because a flat battery, a router that moved and Live switched off
without a word out are identical from here), and off on the reader.

## Endpoints

The reader, bearer token:

|                        |                                                                                                           |
| ---------------------- | --------------------------------------------------------------------------------------------------------- |
| `POST /api/pair/start` | makes a fridge and a device token, returns a six-digit code                                               |
| `GET /api/pair/poll`   | hands the reader its token once a browser has claimed the code                                            |
| `GET /api/pull`        | `304` unchanged, `204` nothing picked, `200` + the BMP. `X-Next-Wake` (how long to sleep), `X-Cadence` (how often it repeats, which is a different number under a clock-time schedule) and `X-Server-Time` back on all three; the reader sends `X-Live-On` |
| `GET /api/senders`     | who may send, the cap, and `pending` when the schedule has moved and this reader has not picked it up yet. ABSENT when nothing is pending |
| `POST /api/off`        | Live was switched off on the reader. Fire and forget: its failure costs nothing, the fridge just goes quiet |

The browser, cookie:

|                                    |                                                                   |
| ---------------------------------- | ----------------------------------------------------------------- |
| `POST /api/claim`                  | six digits in, a sender cookie out                                |
| `GET /api/state`                   | last check-in, schedule, next expected, `liveOn`, and the same `pending` sentence the reader gets |
| `GET /api/history`                 | everything ever sent to this reader, newest first, and which is picked |
| `POST /api/history`                | exactly 48062 or 96070 bytes; `X-Kind` says drawing, message or photo. Appends and picks |
| `GET /api/history/{id}/thumb`      | a 60x100 greyscale PNG of that entry, about a kilobyte, immutable |
| `POST /api/history/{id}/select`    | point the reader at an older one, without copying it              |
| `DELETE /api/history/{id}`         | for everybody on this reader; the pick moves to the newest remaining |
| `PUT /api/schedule`                | `{mode, intervalSeconds, dailyTime, tz}`: every so often, or once a day at a clock time |

The history is SHARED. It is the record of what this reader has shown rather
than of what any one phone sent, so every connected phone sees the same list,
any of them can send an old entry out again or delete one, and each entry names
the phone that sent it.

## Two hosts, one domain

The page is on `crossplay.ma-r-s.com` and this is on `fridge.ma-r-s.com`. Both
sit under `ma-r-s.com`, which is what makes the split work rather than merely
look tidy:

- the sender cookie is set with `domain=.ma-r-s.com`, so it is **first-party**
  for both names and Safari's third-party cookie blocking never touches it;
- `SameSite=Lax` is kept. SameSite is decided by the registrable domain and not
  by the origin, so the page's XHR to this host is same-site and carries the
  cookie. Measured in a browser on the deployed pair;
- CORS allows **exactly `https://crossplay.ma-r-s.com`, with credentials**. Not
  a wildcard: the spec refuses `*` the moment credentials are included, and a
  list of origins would be a list of sites allowed to draw on someone's reader.

The reader is not a browser, sends no `Origin` and is unaffected by any of it.

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
