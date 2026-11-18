# Weather

Today and the next six days for one city, from [Open-Meteo](https://open-meteo.com/)
under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The device's
screens do not name it (Mario, 2026-11-18: the screen should not explain itself);
the credit is in THIRD-PARTY-NOTICES.md, here, and on the site's Weather card and
its privacy line.
Card #763; asked for in report box #575 and GitHub #333. Shipped as a description
of what is built, not a plan.

## What it does

Opening the app shows the place with what tells it from another of the name
("Springfield, Illinois", "Zürich, Switzerland"), the temperature now with its
sky, today's high, low and chance of rain (of snow, under a snow code), and when
that was read ("UPDATED 08:12"). Under it, the days after today as rows: the
day, its sky, its chance, and a bar from its low to its high on one scale for
the whole week, so a cold snap is a shape rather than six numbers to compare.

The gear in the band opens the menu: the place (a door back to the search),
Celsius or Fahrenheit, and UPDATE NOW.

## The decisions

- **Fetched when the app opens, and on UPDATE NOW. Never in the background,
  never on a timer wake.** The forks that put weather on the sleep screen
  (t0nyz0's lockscreens, touch-licorice-apps) woke the device to refresh it and
  drained batteries. There is no sleep-screen weather in this version for the
  same reason.
- **The last forecast and when it was read stay on the card**
  (`/.crosspoint/weather/forecast.txt`), so the screen opens on them with the
  radio off. Declining Wi-Fi leaves them up marked `OFFLINE`; a request that
  fails (no answer, a 500, a proxy's HTML) leaves them up marked `COULD NOT
  UPDATE`, which is all a player can act on. The state and the time it was read
  are two lines, so neither is ever cut to fit the other. With nothing on the
  card yet the screen says so (`NO FORECAST YET`, `COULD NOT GET THE FORECAST`)
  and offers TRY AGAIN. It is never blank.
- **A reading from an earlier day is never drawn as now.** The temperature
  and sky taken with the forecast are "now" only on the day they were taken,
  in the place's own time (`weather::nowFor`). On a later day, after a failed
  update or with Wi-Fi declined, there is no number where now goes: today's
  sky stands at the left edge, then today's words, high, low and chance, all
  from today's row of the forecast, which is what the card knows about today.
- **A week that has happened is not drawn as the coming one.** Only today and
  the days after it are rows. When every day on the card is past, the screen
  shows the place, `NO FORECAST FOR TODAY`, when it was read, and UPDATE NOW,
  and no week.
- **The city is typed once, on the keyboard Wikipedia's search uses**: the
  same code, `ui/ToyboxKeyboard.h`, so its Shift key draws the keyboard's own
  arrow in every theme and its symbol pages "#+=" and "123", as Wikipedia's
  does, and
  chosen from Open-Meteo's own geocoder (ten places at most, each with its
  region so three Springfields can be told apart). The places come a page at a
  time: the side keys and a vertical swipe turn the page, and a row of page
  numbers at the foot, the shelf's, is a door to each. A search that found
  nothing or failed puts the keyboard back up with what was typed, since the
  answer to both is to type or press GO, and each says so in a second line:
  "Check the spelling, or try a larger town nearby.", "Press GO to try
  again." (or "Not connected to Wi-Fi." without the radio). The menu changes the city. The place
  lives in `/.crosspoint/weather/place.txt`.
- **Units follow the city's country until the player chooses.** Fahrenheit for
  the United States, its territories, the Bahamas, Belize, the Cayman Islands,
  Palau, Liberia, the Marshall Islands and Micronesia; Celsius everywhere else.
  A choice made in the menu outlives a change of city.
- **Requests always ask for Celsius**, and the screen converts, so switching
  units needs no request.
- **The day is the place's own day.** Open-Meteo answers with the place's UTC
  offset (`timezone=auto`), so "today" and the day names come from the
  forecast's own dates and that offset, not from the device's time zone. With a
  clock (`clockfloor::trusted`, the fork's one floor: no earlier than the day
  the firmware was built) a forecast from yesterday starts at
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
letter too. A name is folded whole or not at all: when one letter has to be
reduced to its base, every accented letter is, so "Łódź" shows as "Lodz", what
the player typed, and never as "Lódz", which is neither Polish nor plain. A
name the cuts draw whole keeps every accent ("Kraków", "São Tomé"). The
geocoder is asked for English names (`language=en`), so most names in other
scripts never reach this: "Tok" finds "Tokyo".
A letter of a script with no Latin reading (Greek, Cyrillic, CJK) has no base
and goes. The real fix for all of it is a reading cut that carries more than
Latin-1; this is the honest best until there is one.

## Faces, and the place's name

A target binds three faces and the main screen needs five: the band's display
cut, the Jersey UI and button cuts for numbers and days, the huge cut for the
temperature now, and two bold reading cuts for the place. So each screen binds
its own, pass by pass, through the `Rebind` the activity hands it
(`WeatherScreens.h`: `kPlacePass`, `kDataPass`, `kMenuPass`, `kSearchPass`).
The main screen and the menu measure the place in `kPlacePass`, lay out and draw
everything else in Jersey, then draw the place in `kPlacePass` where the layout
put it. Because the faces and their order live in the builders, the host suite
binds exactly what the device binds: `everyPlaceIsDrawnInAFaceWithItsLetters`
draws São Bernardo do Campo, Saint-Étienne-du-Rouvray, Münster and Lodz on every
screen and fails on a letter drawn in a face without it. The menu once fitted a
long name down into Jersey's button cut and printed holes; that is the case it
was written for.

A place's name is laid out here, line by line, rather than by the component:
a line may end at a space or after a hyphen, so
"Saint-Remy-en-Bouzemont-Saint-Genest-et-Isson" takes its second line before
anything is cut. The ladder is bold 16 then bold 12, two lines, on the main
screen and in the menu's row alike. The name and what tells it apart
("Illinois", `weather::placeQualifier`) are fitted together, "Springfield,
Illinois", and when they will not go together the qualifier takes the last
line to itself, whole, and the name has the lines above it: the region is
never the first thing lost, and a one-word name wider than a line
("Llanfairpwllgwyngyllgogerychwyrndrobwll...") keeps "Wales" under it. A name
is cut once, after its last whole word, and through a word only when its
first word alone will not fit. A region line in the search list keeps whole
parts ("North Rhine-Westphalia..."), so no cut ends on the comma between two.
In the search list the names take the same ladder, measured and drawn in the
place's own pass. A place with no region (a name that cleaned to nothing falls back to its
coordinates) is one line, not a line and an empty one.

## The main screen

Jersey has neither a degree sign nor a minus, so the screens draw both, sized
from the cap height: a ring at the cap line, and a bar most of a figure wide at
the middle of the capitals. Jersey's hyphen is a short dash, and "H-28" set with
it read as one hyphenated word; "H −28°" has a word space after the letter and a
real minus. A day whose weather code was null draws no sky (it used to draw a
cloud); a week with none has no sky column.

**Everything is placed by its ink**, cap top to baseline, through
`ui/ToyboxInk.h` (`docs/design-language.md`, "Stack by ink"). The first version
stacked line boxes and measured 47, 25, 19, 17, 14, 17, 20 px between its
sections, no two alike. Now the gaps come from one scale by relationship: 12
between the lines of the reading, 24 between the week's rows, 36 between the
three groups (the place, the reading, the week). The menu and the search are on
the same scale (`menuAndSearchFollowTheSameScale`): a caption 12 over what it
labels, 36 between groups, a region 12 under its name, 24 between places with
the rule in the middle. The rows are paced by their
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
and a poll every 50 ms that pumps input. Back abandons a request within a poll
and does only that: the forecast on the card stays up, or the search with what
was typed and the keyboard; the next Back leaves. The Home key abandons the
moment it goes down, and its gesture then finishes in the main loop and goes
Home as it always does: waiting inside the poll for a tap to be told from a
double tap never ended where input edges last a frame, so Home used to sit out
the whole 20 s. A finished Home gesture (the bottom-edge swipe) goes Home
directly. HttpDownloader's door could not do any of this: it waits 60 s a socket
operation and takes no poll.
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
It may start with a week and a dash: `winter-ok` (Yakutsk, every value
negative, snow codes), `gaps-ok` (nulls in every array, the code included).
The simulator's curl honours the abort poll (`scripts_local/sim_catchup.py`,
"a hung host can be left"), so Back against `slow` behaves as on the device:
measured, it abandoned the request 3 ms after the press and stayed on the
forecast, and the second Back had the Apps folder up 220 ms later; the Home key
abandoned it 18 ms after the press and Home was up 650 ms after it, the same
double-tap window an idle Home takes; with no key at all, the request gave up
at 20.3 s and kept the cached forecast. Searches starting `zur`, `lis`, `spr`,
`tok`, `mos`, `lod`, `ndj`, `sao`, `sai`, `mun`, `bad`, `lla`, `yak` and `kyo`
answer with fixtures that exercise accents, hyphens and ten results. They
answer as the service does with `language=en`, which the device always sends:
"Tok" finds Tokyo, not 東京都. `kyo` is the exception kept on purpose, a place
answered only in its own script, so the coordinates fallback can be seen. The
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
