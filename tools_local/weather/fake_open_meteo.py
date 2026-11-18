#!/usr/bin/env python3
"""A stand-in for Open-Meteo's two endpoints, for the simulator.

    python3 tools_local/weather/fake_open_meteo.py 18763

Then point the simulator at it (the firmware reads these only in a
SIMULATOR build; a device always asks Open-Meteo):

    WEATHER_FORECAST_URL=http://127.0.0.1:18763/ok/forecast \
    WEATHER_GEOCODE_URL=http://127.0.0.1:18763/ok/search \
    ./scripts_local/sim-shot.sh '...' '...'

The first path segment picks how the server answers, so one server covers
every case a run needs. It may start with a week and a dash ("winter-ok"):
the week is one of WEEKS below, the base November week when none is named.

    ok         the answer Open-Meteo gives, in its real shape
    malformed  an answer cut off half way, the way a dropped connection leaves it
    html       a 502 page from a proxy, served with status 200
    error      HTTP 500 with Open-Meteo's own error body
    slow       the ok answer after SLOW_SECONDS (default 70: past the
               simulator's 60 s curl limit, so it reads as a timeout)
    none       (search only) the answer to a search that found nothing

The forecast's dates are today and the next six days in the place's own time
zone, so the day names on screen are always the real ones. The numbers are
fixed weeks, so screenshots are comparable run to run.
"""

import datetime as dt
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

SLOW_SECONDS = float(os.environ.get("SLOW_SECONDS", "70"))

# One week, today first: weather code, high, low, chance of rain (None = null).
WEEK = [
    (2, 9.1, 3.2, 20),
    (61, 8.4, 4.1, 65),
    (63, 6.0, 2.5, 90),
    (3, 5.2, 0.4, 35),
    (71, 1.9, -2.6, 70),
    (0, 4.4, -3.8, 5),
    (45, 3.0, -1.0, None),
]


def place(name, lat, lon, cc, country, admin1="", **extra):
    out = {
        "id": abs(hash(name + cc)) % 10_000_000,
        "name": name,
        "latitude": lat,
        "longitude": lon,
        "elevation": 400.0,
        "feature_code": "PPL",
        "country_code": cc,
        "timezone": "Europe/Zurich",
        "country": country,
    }
    if admin1:
        out["admin1"] = admin1
    out.update(extra)
    return out


SPRINGFIELDS = [
    ("Illinois", 39.80, -89.64),
    ("Missouri", 37.22, -93.30),
    ("Massachusetts", 42.10, -72.59),
    ("Ohio", 39.92, -83.81),
    ("Oregon", 44.05, -123.02),
    ("Tennessee", 36.51, -86.88),
    ("Vermont", 43.30, -72.48),
    ("Virginia", 38.79, -77.19),
    ("Kentucky", 37.69, -85.22),
    ("Colorado", 37.41, -102.62),
]

SEARCHES = {
    "zur": [
        place(
            "Zürich", 47.36667, 8.55, "CH", "Switzerland", "Zurich", population=341730
        ),
        place("Zurich", 43.41678, -81.62498, "CA", "Canada", "Ontario"),
        place("Zurich", 38.84, -99.43, "US", "United States", "Kansas"),
    ],
    "lis": [
        place("Lisbon", 38.71667, -9.13333, "PT", "Portugal", "Lisbon"),
        place("Lisbon", 44.25, -76.15, "US", "United States", "Maine"),
    ],
    "spr": [
        place("Springfield", lat, lon, "US", "United States", state)
        for state, lat, lon in SPRINGFIELDS
    ],
    # What the service answers with language=en, which the device always
    # sends (checked against geocoding-api.open-meteo.com on 2026-11-18):
    # English names where it has them, so Tokyo is "Tokyo".
    "tok": [place("Tokyo", 35.6895, 139.69171, "JP", "Japan", "Tokyo")],
    "mos": [
        place("Moscow", 55.75222, 37.61556, "RU", "Russia", "Moscow"),
        place("Moscow", 46.73239, -117.00017, "US", "United States", "Idaho"),
    ],
    # The English name is plain, the region keeps its Polish letters, two of
    # which the reading cuts lack: the region is folded whole, "Lodz Voivodeship".
    "lod": [place("Lodz", 51.75, 19.46667, "PL", "Poland", "Łódź Voivodeship")],
    # A place the service has no English name for, answered in its own
    # script with nothing the cuts can draw: the screen falls back to its
    # coordinates. Rare with language=en; kept so the fallback can be seen.
    "kyo": [place("京都市", 35.01, 135.77, "JP", "日本", "京都府")],
    # A name with an emoji and a curly apostrophe, as a careless source sends.
    "ndj": [
        place("N’Djamena \U0001f3d9", 12.10672, 15.0444, "TD", "Chad", "Chari-Baguirmi")
    ],
    # Names a face without accents prints holes in.
    "sao": [
        place("São Bernardo do Campo", -23.69, -46.56, "BR", "Brazil", "São Paulo"),
        place("São Paulo", -23.55, -46.63, "BR", "Brazil", "São Paulo"),
    ],
    "sai": [
        place("Saint-Étienne-du-Rouvray", 49.38, 1.1, "FR", "France", "Normandy"),
        place(
            "Saint-Remy-en-Bouzemont-Saint-Genest-et-Isson",
            48.63,
            4.65,
            "FR",
            "France",
            "Grand Est",
        ),
        place("Saint Petersburg", 27.77, -82.64, "US", "United States", "Florida"),
    ],
    "mun": [
        place("Münster", 51.96, 7.63, "DE", "Germany", "North Rhine-Westphalia"),
        place("München", 48.14, 11.58, "DE", "Germany", "Bavaria"),
    ],
    "bad": [
        place("Bad Münstereifel", 50.55, 6.76, "DE", "Germany", "North Rhine-Westphalia")
    ],
    # One name too long for a line at any size, and nothing to break it at.
    "lla": [
        place(
            "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch",
            53.22,
            -4.2,
            "GB",
            "United Kingdom",
            "Wales",
        ),
        place("Llandudno", 53.32, -3.83, "GB", "United Kingdom", "Wales"),
    ],
    "yak": [place("Yakutsk", 62.03, 129.73, "RU", "Russia", "Sakha Republic")],
    # Ten places with long names and long regions: page boundaries with
    # two-line rows, and regions cut at a whole part.
    "san": [
        place(n, 10.0 + i, -60.0 - i, cc, country, admin)
        for i, (n, cc, country, admin) in enumerate(
            [
                ("San José", "CR", "Costa Rica", "San José"),
                ("San José del Cabo", "MX", "Mexico", "Baja California Sur"),
                ("San Juan", "PR", "Puerto Rico", ""),
                ("San Sebastián de los Reyes", "ES", "Spain", "Comunidad de Madrid"),
                ("Santa Cruz de la Sierra", "BO", "Bolivia", "Santa Cruz"),
                ("San Cristóbal de las Casas", "MX", "Mexico", "Chiapas"),
                ("San Fernando del Valle de Catamarca", "AR", "Argentina", "Catamarca"),
                ("Sankt Peter-Ording", "DE", "Germany", "Schleswig-Holstein"),
                ("Santo Domingo", "DO", "Dominican Republic", "Distrito Nacional"),
                (
                    "San Andrés",
                    "CO",
                    "Colombia",
                    "Archipiélago de San Andrés, Providencia y Santa Catalina",
                ),
            ]
        )
    ],
}

# Weeks, today first: weather code, high, low, chance (None = null), and the
# reading now (temperature, code).
WEEKS = {
    "base": (7.3, 2, WEEK),
    # Siberia in winter: negative and double-digit, snow under snow codes.
    "winter": (
        -31.6,
        71,
        [
            (71, -28.0, -40.0, 100),
            (73, -25.4, -38.2, 90),
            (0, -30.1, -41.7, 0),
            (3, -18.6, -33.3, 15),
            (75, -12.0, -27.5, 100),
            (45, -9.4, -22.0, 40),
            (0, -15.0, -35.0, 0),
        ],
    ),
    # Nulls everywhere they can be: highs, lows, chances and codes.
    "gaps": (
        12.0,
        None,
        [
            (3, 14.0, None, None),
            (61, None, 6.0, 55),
            (None, 15.0, 8.0, None),
            (63, None, None, 80),
            (2, 16.0, 9.0, 0),
            (0, 17.0, 10.0, 100),
            (45, 13.0, 7.0, None),
        ],
    ),
}


def forecast(query, week="base"):
    now_c, now_code, days_of = WEEKS.get(week, WEEKS["base"])
    lat = float(query.get("latitude", ["47.37"])[0])
    # Roughly the place's own offset, so "today" is the place's today.
    offset_hours = round(float(query.get("longitude", ["8.55"])[0]) / 15)
    offset = offset_hours * 3600
    now = dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=offset)
    today = now.date()
    quarter = now.replace(minute=now.minute - now.minute % 15, second=0, microsecond=0)
    days = [today + dt.timedelta(days=i) for i in range(7)]
    return {
        "latitude": lat,
        "longitude": float(query.get("longitude", ["8.55"])[0]),
        "generationtime_ms": 0.09,
        "utc_offset_seconds": offset,
        "timezone": "GMT",
        "timezone_abbreviation": "GMT",
        "elevation": 409.0,
        "current_units": {
            "time": "iso8601",
            "interval": "seconds",
            "temperature_2m": "°C",
            "weather_code": "wmo code",
            "is_day": "",
        },
        "current": {
            "time": quarter.strftime("%Y-%m-%dT%H:%M"),
            "interval": 900,
            "temperature_2m": now_c,
            "weather_code": now_code,
            "is_day": 1 if 7 <= now.hour < 17 else 0,
        },
        "daily_units": {
            "time": "iso8601",
            "weather_code": "wmo code",
            "temperature_2m_max": "°C",
            "temperature_2m_min": "°C",
            "precipitation_probability_max": "%",
        },
        "daily": {
            "time": [d.isoformat() for d in days],
            "weather_code": [w[0] for w in days_of],
            "temperature_2m_max": [w[1] for w in days_of],
            "temperature_2m_min": [w[2] for w in days_of],
            "precipitation_probability_max": [w[3] for w in days_of],
        },
    }


def search(query):
    name = query.get("name", [""])[0].strip().lower()
    for prefix, results in SEARCHES.items():
        if name.startswith(prefix):
            return {"results": results, "generationtime_ms": 0.6}
    return {"generationtime_ms": 0.53}


class Handler(BaseHTTPRequestHandler):
    def send(self, status, body, content_type="application/json; charset=utf-8"):
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        parts = [p for p in url.path.split("/") if p]
        mode, endpoint = (parts + ["", ""])[:2]
        week = "base"
        if "-" in mode:
            week, mode = mode.split("-", 1)
        query = parse_qs(url.query)
        sys.stderr.write(f"[fake-open-meteo] {week}-{mode}/{endpoint} {url.query}\n")
        if endpoint == "forecast":
            good = json.dumps(forecast(query, week), ensure_ascii=False)
        elif endpoint == "search":
            good = json.dumps(
                {"generationtime_ms": 0.5} if mode == "none" else search(query),
                ensure_ascii=False,
            )
        else:
            self.send(404, '{"error":true,"reason":"Not Found"}')
            return
        if mode in ("ok", "none"):
            self.send(200, good)
        elif mode == "malformed":
            self.send(200, good[: len(good) // 2])
        elif mode == "html":
            self.send(
                200, "<html><body><h1>502 Bad Gateway</h1></body></html>", "text/html"
            )
        elif mode == "error":
            self.send(500, '{"error":true,"reason":"Internal error"}')
        elif mode == "slow":
            time.sleep(SLOW_SECONDS)
            self.send(200, good)
        else:
            self.send(404, '{"error":true,"reason":"unknown mode"}')

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18763
    sys.stderr.write(f"[fake-open-meteo] listening on 127.0.0.1:{port}\n")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
