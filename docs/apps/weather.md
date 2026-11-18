# Weather

Today and the next six days for one city, from [Open-Meteo](https://open-meteo.com/)
under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The device's
screens do not name it (Mario, 2026-11-18: the screen should not explain itself);
the credit is in THIRD-PARTY-NOTICES.md, here, and on the site's Weather card and
its privacy line.
Card #763; asked for in report box #575 and GitHub #333. Shipped as a description
of what is built, not a plan.

## What it does

Opening the app shows the city, the temperature now with its sky, today's high,
low and chance of rain, and when that was read ("UPDATED 08:12"). Under it, the
next six days as rows: the day, its sky, its chance of rain, and a bar from its
low to its high on one scale for the whole week, so a cold snap is a shape
rather than six numbers to compare.

The gear in the band opens the menu: the city (a door back to the search),
Celsius or Fahrenheit, and REFRESH NOW.

## The decisions

- **Fetched when the app opens, and on REFRESH NOW. Never in the background,
  never on a timer wake.** The forks that put weather on the sleep screen
  (t0nyz0's lockscreens, touch-licorice-apps) woke the device to refresh it and
  drained batteries. There is no sleep-screen weather in this version for the
  same reason.
- **The last forecast and when it was read stay on the card**
  (`/.crosspoint/weather/forecast.txt`), so the screen opens on them with the
  radio off. Declining Wi-Fi leaves them up marked `OFFLINE.`; a request that
  fails leaves them up marked `COULD NOT UPDATE.`. With nothing on the card yet,
  the screen says so and offers TRY AGAIN. It is never blank.
- **The city is typed once, on the keyboard Wikipedia's search uses**, and
  chosen from Open-Meteo's own geocoder (ten places at most; the screen draws
  the ones that fit and says "Not here? Type more of the name." when there were
  more). The menu changes it. The place lives in `/.crosspoint/weather/place.txt`.
- **Units follow the city's country until the player chooses.** Fahrenheit for
  the United States, its territories, the Bahamas, Belize, the Cayman Islands,
  Palau, Liberia, the Marshall Islands and Micronesia; Celsius everywhere else.
  A choice made in the menu outlives a change of city.
- **Requests always ask for Celsius**, and the screen converts, so switching
  units needs no request.
- **The day is the place's own day.** Open-Meteo answers with the place's UTC
  offset (`timezone=auto`), so "today" and the day names come from the
  forecast's own dates and that offset, not from the device's time zone. With a
  clock (`time()` above a 2023 floor) a forecast from yesterday starts at
  today's row and says `UPDATED YESTERDAY 14:07`; without one, the forecast's
  own reading time dates it.

## Place names from outside

A place name is somebody else's text in any script, and the cuts it is drawn in
(the reading serif, ASCII and Latin-1) cannot draw most of them: a glyph a cut
lacks draws as nothing. So names are cleaned where they enter
(`weather::cleanName`): composed to NFC, typography folded to ASCII, then
letters, digits, spaces and the punctuation names carry kept where the reading
cuts have a glyph (`toybox::readingCutsCarry`, asked of the font data), a letter
they lack reduced to its base letter when it has one (s-comma to s), and
everything else dropped. A name with nothing drawable left falls back to its
region, then to its coordinates (`35.69N 139.69E`), so it is never empty.

Unicode gives the stroked and dotless letters (L-stroke, D-stroke, dotless i)
no decomposition, so a short table in `WeatherCore.cpp` gives them their base
letter too: "Łódź" shows as "Lódz", which is also what the player typed.
A letter of a script with no Latin reading (Greek, Cyrillic, CJK) has no base
and goes. The real fix for all of it is a reading cut that carries more than
Latin-1; this is the honest best until there is one.

## The main screen

It needs four faces and a target binds three: the band's display cut, the Jersey
UI and button cuts for numbers and days, the huge cut for the temperature now,
and a reading cut for the place. So it is three passes on one screen:
`buildMainTop()` draws the band and MEASURES the place, the activity rebinds,
`buildMainBody()` lays the whole screen out and draws it, the activity rebinds
back, and `drawMainPlace()` draws the place where the layout put it. Jersey has
no degree sign, so the screens draw one (a ring at the cap line, sized from the
cap height).

**Everything is placed by its ink**, cap top to baseline, through
`ui/ToyboxInk.h` (`docs/design-language.md`, "Stack by ink"). The first version
stacked line boxes and measured 47, 25, 19, 17, 14, 17, 20 px between its
sections, no two alike. Now the gaps come from one scale by relationship: 12
between the lines of the reading, 24 between the week's rows, 36 between the
three groups (the place, the reading, the week). The rows are paced by their
text's capitals, so the rhythm is the same every week; each sky's ink is centred
on them. The composition is centred between the band's rule and the panel's
foot, so what it does not need is placed equally above and below. Everything is
left-aligned at the margin; only the hero's sky and the highs keep to the right
edge. The week's columns (day, sky, chance of rain, low, bar, high) are measured
from this week's strings, a gutter apart, so no dead band is reserved for a
string that is not on screen. `mainInkFollowsOneScale` and
`noWeatherScreenPrintsIntoAnything` in the ui suite measure all of this by ink
and fail on the first version.

The 88px hero sky is that size because its ink then fits inside the number's
82px capitals; at 96 it overhung them.

The layout was chosen from three rendered variants (a ruled ledger, the range
bars, a black card over a grid of tiles), composed side by side. The bars won
on one element: the week reads at a glance. The card's tiles were too short for
their content and printed the chance of rain under the next row.

## Files

|                                                  |                                                                                                                           |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| `src/apps_local/weather/WeatherCore.{h,cpp}`     | Freestanding: the JSON reader, both answers, both card files, staleness, units, days, skies, names. `host-tests/weather`. |
| `src/apps_local/weather/WeatherScreens.{h,cpp}`  | Freestanding builders: main, search, menu. `host-tests/ui`.                                                               |
| `src/apps_local/weather/WeatherActivity.{h,cpp}` | The radio, the card, the keyboard, the panel.                                                                             |
| `src/apps_local/weather/WeatherIcons.h`          | Ten skies from Lucide at 32 and 88px, generated by `tools_local/weather/gen_weather_icons.sh`.                            |
| `tools_local/weather/fake_open_meteo.py`         | A stand-in for both endpoints, for the simulator.                                                                         |

Requests go through RSS's cancellable GET (`rss::fetchStreaming`, RssFetch.h):
the radio check and the agent string HttpDownloader's door has, a 20 s timeout,
and a poll every 50 ms that pumps input, so Back or Home abandons a request that
hangs within a poll and then does what it always does. HttpDownloader's door
could not: it waits 60 s a socket operation and takes no poll.
`host-tests/weather/abandon.py` fails the build if a request goes any other way.
Answers are capped at 32KB. A forecast
answer is about 1.5KB, a geocoder answer for ten places about 6KB. Reading one
builds a small tree that lives only for the parse: `host-tests/weather`
measures its peak (27KB for ten places on a 64-bit host, less on the device's
32-bit pointers) and fails if it grows past a bound.

## Trying it in the simulator

Both endpoints can be pointed at a local server in a simulator build (never on a
device):

```bash
python3 tools_local/weather/fake_open_meteo.py 18763 &
CROSSPLAY_AUTOSTART=weather \
WEATHER_FORECAST_URL=http://127.0.0.1:18763/ok/forecast \
WEATHER_GEOCODE_URL=http://127.0.0.1:18763/ok/search \
./scripts_local/sim-shot.sh '9000:QUIT' '7000:qa-artifacts/weather.bmp'
```

The first path segment picks the answer: `ok`, `malformed` (cut off half way),
`html` (a proxy's 502 page served as 200), `error` (HTTP 500), `slow` (holds the
connection open past the 20 s timeout), `none` (a search that found nothing).
The simulator's curl honours the abort poll (`scripts_local/sim_catchup.py`,
"a hung host can be left"), so Back against `slow` behaves as on the device:
measured, it abandoned the request 2 ms after the press and the Apps folder was
up 135 ms later; with no Back, the request gave up at 20.3 s and kept the
cached forecast. Searches starting
`zur`, `lis`, `spr`, `tok`, `mos`, `lod` and `ndj` answer with fixtures that
exercise accents, ten results, and names in scripts the cuts cannot draw. The
simulator auto-connects only with a saved network in
`fs_agent/.crosspoint/wifi.json`; `CROSSPOINT_SIM_WIFI_CONNECT=fail` makes it
refuse.

## Not verified

On a device: the TLS request to Open-Meteo, how long it takes, and the heap it
needs. On the device the abort poll is asked through the TLS handshake, the wait
for the status line and every read; DNS and the TCP connect are bounded by the
20 s timeout alone, so a host that never accepts the connection can still hold
Back for up to that long. The simulator cannot show this: its curl connects
inside one request.
