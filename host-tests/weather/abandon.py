"""Every request Weather makes can be abandoned with Back, within a poll.

The forecast and the geocoder are fetched while the loop is held, so whatever
the request is waiting on, nothing else samples input. Through HttpDownloader's
door that wait was sixty seconds a socket operation with no poll: a host that
accepted the connection and never answered held the app for a minute, Back
included. RssFetch's GET asks a poll every 50 ms and gives up after 20 s, and
that is the only way a request may be made here.

Read from the source, like host-tests/httpdoor, because the property lives in
how the calls are wired and a host build has no radio to hang:

  * no HttpDownloader call anywhere in the app;
  * every request is rss::fetchStreaming with a poll, and the poll is the one
    that pumps input and answers Back and Home;
  * that poll pumps input and checks both;
  * the loop acts on the interruption once the request has returned;
  * RssFetch's own timeout is still the 20 s this was written against.

    python3 host-tests/weather/abandon.py <repo-root>
"""

import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
activity = (root / "src/apps_local/weather/WeatherActivity.cpp").read_text()
rss_fetch = (root / "src/apps_local/rss/RssFetch.cpp").read_text()

checks = 0
failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f"FAIL abandon.py  {what}")


def body_of(source, signature):
    """The braced body of the first function whose definition matches."""
    m = re.search(signature, source)
    if not m:
        return ""
    i = source.index("{", m.end() - 1)
    depth = 0
    for j in range(i, len(source)):
        depth += {"{": 1, "}": -1}.get(source[j], 0)
        if depth == 0:
            return source[i : j + 1]
    return ""


app_files = sorted((root / "src/apps_local/weather").glob("*.cpp"))
for path in app_files:
    text = path.read_text()
    check(
        "HttpDownloader::" not in text,
        f"{path.name} calls HttpDownloader, whose requests cannot be abandoned",
    )

calls = re.findall(r"rss::fetchStreaming\(", activity)
check(
    len(calls) >= 1, "WeatherActivity.cpp makes no request through rss::fetchStreaming"
)
for m in re.finditer(r"rss::fetchStreaming\(", activity):
    # The call's argument list, to its closing parenthesis.
    depth, j = 0, m.end() - 1
    for j in range(m.end() - 1, len(activity)):
        depth += {"(": 1, ")": -1}.get(activity[j], 0)
        if depth == 0:
            break
    args = activity[m.end() : j]
    check(
        "pollDuringRequest()" in args,
        "an rss::fetchStreaming call passes no pollDuringRequest poll",
    )

poll = body_of(activity, r"bool WeatherActivity::pollDuringRequest\(\)\s*\{")
check(bool(poll), "WeatherActivity::pollDuringRequest is not defined")
check(
    "mappedInput.update()" in poll,
    "the poll does not pump input, so nothing sees Back while the loop is held",
)
check("Button::Back" in poll, "the poll does not answer Back")
check(
    "wasHomeGesture()" in poll,
    "the poll does not answer Home, which its own pump would otherwise swallow",
)

loop = body_of(activity, r"void WeatherActivity::loop\(\)\s*\{")
check(
    "Interrupt::Home" in loop and "onGoHome()" in loop,
    "the loop does not go Home after a request Home abandoned",
)
check(
    "Interrupt::Back" in loop and "back()" in loop,
    "the loop does not go Back after a request Back abandoned",
)

m = re.search(r"constexpr uint32_t kTimeoutMs = (\d+);", rss_fetch)
check(bool(m), "RssFetch.cpp no longer states its timeout as kTimeoutMs")
if m:
    check(
        int(m.group(1)) <= 20000,
        f"RssFetch's timeout is {m.group(1)} ms, past the 20 s this relies on",
    )

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
