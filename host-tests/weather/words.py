"""No Weather screen names its provider or explains what can be read off it.

The forecast's credit (CC BY 4.0) lives in THIRD-PARTY-NOTICES.md,
docs/apps/weather.md and the site's Weather card. On the panel the name is
noise, and "Open-Meteo did not answer" was wrong as well: a 500 or an HTML 502
is an answer, and the only thing a player can do about any of them is try
again. host-tests/ui checks every screen and state the builders draw; the
activity hands them words too, so this reads every string literal a screen
can be handed, in every .cpp of the app:

  * no provider name, in any case, outside the endpoint URLs;
  * none of the sentences that only explained the screen ("Which city is the
    forecast for?", "Try again in a minute.", "It stays on the card for when
    you are offline.", "Tap the field and press GO to try again.").

Log lines are the exception: they are read by whoever debugs, who does need
the endpoint.

    python3 host-tests/weather/words.py <repo-root>
"""

import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
NEVER = [
    "meteo",
    "which city is the forecast for",
    "try again in a minute",
    "it stays on the card",
    "tap the field and press go",
    "did not answer",
]

checks = 0
failed = 0
literal = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

for path in sorted((root / "src/apps_local/weather").glob("*.cpp")):
    for number, line in enumerate(path.read_text().splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//") or "LOG_" in line:
            continue
        for text in literal.findall(line):
            if text.startswith("http"):
                continue
            checks += 1
            lowered = text.lower()
            for never in NEVER:
                if never in lowered:
                    failed += 1
                    print(
                        f'FAIL words.py  {path.name}:{number} a screen can be handed "{text}"'
                    )
                    break

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
