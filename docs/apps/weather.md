# Weather

Today and the next six days for one city, from [Open-Meteo](https://open-meteo.com/).
Card #763; asked for in report box #575 and GitHub #333. Shipped as a description
of what is built, not a plan.

## What it does

Opening the app shows the city, the temperature now with its sky, today's high,
low and chance of rain, and when that was read ("UPDATED 08:12"). Under it, the
next six days as rows: the day, its sky, its chance of rain, and a bar from its
low to its high on one scale for the whole week, so a cold snap is a shape
rather than six numbers to compare.

The gear in the band opens the menu: the city (a door back to the search),
Celsius or Fahrenheit, REFRESH NOW, and where the numbers come from.

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

## The main screen in two passes

It needs four faces and a target binds three: the band's display cut, the Jersey
UI and button cuts for numbers and days, the huge cut for the temperature now,
and a reading cut for the place. `buildMainTop()` draws the band and the place;
the activity rebinds the slots; `buildMainBody()` draws the rest below them.
Jersey has no degree sign, so the screens draw one (a ring at the cap line,
sized from the cap height).

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
| `src/apps_local/weather/WeatherIcons.h`          | Ten skies from Lucide at 32 and 96px, generated by `tools_local/weather/gen_weather_icons.sh`.                            |
| `tools_local/weather/fake_open_meteo.py`         | A stand-in for both endpoints, for the simulator.                                                                         |

Requests go through `HttpDownloader::fetchUrl`, the firmware's one door (the
radio check, the agent string, the status), capped at 32KB an answer. A forecast
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
`html` (a proxy's 502 page served as 200), `error` (HTTP 500), `slow` (past the
simulator's 60 s limit), `none` (a search that found nothing). Searches starting
`zur`, `lis`, `spr`, `tok`, `mos`, `lod` and `ndj` answer with fixtures that
exercise accents, ten results, and names in scripts the cuts cannot draw. The
simulator auto-connects only with a saved network in
`fs_agent/.crosspoint/wifi.json`; `CROSSPOINT_SIM_WIFI_CONNECT=fail` makes it
refuse.

## Not verified

On a device: the TLS request to Open-Meteo, how long it takes, and the heap it
needs. The simulator's HTTP is curl. A request that hangs blocks the loop for up
to HttpDownloader's 60 s socket timeout, with the cached forecast on the panel
marked UPDATING; Back is not live during that wait.
