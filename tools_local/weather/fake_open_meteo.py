#!/usr/bin/env python3
"""A stand-in for Open-Meteo's two endpoints, for the simulator.

    python3 tools_local/weather/fake_open_meteo.py 18763

Then point the simulator at it (the firmware reads these only in a
SIMULATOR build; a device always asks Open-Meteo):

    WEATHER_FORECAST_URL=http://127.0.0.1:18763/ok/forecast \
    WEATHER_GEOCODE_URL=http://127.0.0.1:18763/ok/search \
    ./scripts_local/sim-shot.sh '...' '...'

The first path segment picks how the server answers, so one server covers
every case a run needs:

    ok         the answer Open-Meteo gives, in its real shape
    malformed  an answer cut off half way, the way a dropped connection leaves it
    html       a 502 page from a proxy, served with status 200
    error      HTTP 500 with Open-Meteo's own error body
    slow       the ok answer after SLOW_SECONDS (default 70: past the
               simulator's 60 s curl limit, so it reads as a timeout)
    none       (search only) the answer to a search that found nothing

The forecast's dates are today and the next six days in the place's own time
zone, so the day names on screen are always the real ones. The numbers are a
fixed November week in Zurich, so screenshots are comparable run to run.
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
    # A place whose name, region and country are all in a script the reading
    # cuts cannot draw: the screen falls back to its coordinates.
    "tok": [place("東京都", 35.6895, 139.69171, "JP", "日本", "東京都")],
    # The name cannot be drawn, the country can.
    "mos": [
        place("Москва", 55.75222, 37.61556, "RU", "Russia", "Москва"),
        place("Moscow", 46.73239, -117.00017, "US", "United States", "Idaho"),
    ],
    # Latin Extended-A: two letters keep their base letter, one has none.
    "lod": [place("Łódź", 51.75, 19.46667, "PL", "Poland", "Łódź Voivodeship")],
    # A name with an emoji and a curly apostrophe, as a careless source sends.
    "ndj": [
        place("N’Djamena \U0001f3d9", 12.10672, 15.0444, "TD", "Chad", "Chari-Baguirmi")
    ],
}


def forecast(query):
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
            "temperature_2m": 7.3,
            "weather_code": 2,
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
            "weather_code": [w[0] for w in WEEK],
            "temperature_2m_max": [w[1] for w in WEEK],
            "temperature_2m_min": [w[2] for w in WEEK],
            "precipitation_probability_max": [w[3] for w in WEEK],
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
        query = parse_qs(url.query)
        sys.stderr.write(f"[fake-open-meteo] {mode}/{endpoint} {url.query}\n")
        if endpoint == "forecast":
            good = json.dumps(forecast(query), ensure_ascii=False)
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
