#!/usr/bin/env python3
"""Live's own numbers: what a fridge record means, and what reaches the board.

Mario, 2026-09-21: Live "reports nothing at all", and its own records were 67
fridges from nineteen hours of one person testing. Two separate faults, and a
test for each, because fixing either alone still leaves a number nobody can
use:

  * A FRIDGE PER PAIRING ATTEMPT. A fridge used to be written when the reader
    SHOWED a code. The reader mints one every time the Live screen opens on an
    unpaired device, again when a code expires on screen, and again on a 401,
    and nothing ever deleted the ones nobody claimed. So "fridges" counted
    visits to a setup screen. A fridge is now written when somebody CLAIMS the
    code, and the old records are swept.

  * A COUNT THAT CANNOT TELL A PERSON FROM AN ABANDONED SETUP. last_checkin is
    a pure overwrite, so it answered only "ever" or "never" -- and "ever"
    counts a reader that pulled once during setup the same as one that has
    been on a fridge for a month. The second check-in is the first evidence
    anybody kept it, so it has to be countable.

Run: .venv/bin/python tests/test_live_events.py
"""

import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))

checks = 0
failures = 0


def ok(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"  FAIL: {label}")


def main() -> int:
    work = tempfile.mkdtemp()
    import os

    os.environ["FRIDGE_DATA"] = work
    # Off, so nothing here can reach a network. The posts are captured by
    # replacing events.post itself, below.
    os.environ.pop("SUPABASE_URL", None)
    os.environ.pop("SUPABASE_ANON_KEY", None)

    from bridge import app as appmod
    from bridge import events, store

    posted = []
    events_post = events.post

    def capture(service, event, **kw):
        posted.append({"service": service, "event": event, **kw})
        return None

    # Patched on the app module too: it imported the name, so replacing it on
    # the events module alone would leave the handlers calling the original.
    events.post = capture
    appmod.events.post = capture

    from fastapi.testclient import TestClient
    from bridge.ratelimit import Window

    # This suite is about what gets COUNTED and what gets WRITTEN, and it makes
    # a dozen pairings from one address to prove abandoned ones leave nothing.
    # The real PAIR_IP is 10 per 300s, so without this the twelfth request is
    # refused and the test fails on the limiter rather than on the thing it is
    # checking. The limits themselves are the ratelimit module's own business.
    appmod.PAIR_IP = Window(10000, 300)
    appmod.CLAIM_IP = Window(10000, 300)
    appmod.CLAIM_GLOBAL = Window(10000, 60)
    appmod.PULL_DEVICE = Window(10000, 300)

    client = TestClient(appmod.app)

    # --- a code that nobody claims leaves NOTHING behind -------------------
    fridges = pathlib.Path(work) / "fridges"
    before = len(list(fridges.iterdir())) if fridges.is_dir() else 0
    r = client.post("/api/pair/start")
    ok(r.status_code == 200, f"a reader can ask for a code, got {r.status_code}")
    code = r.json().get("code", "")
    ok(len(code) == 6 and code.isdigit(), f"six digits, got {code!r}")
    after = len(list(fridges.iterdir())) if fridges.is_dir() else 0
    ok(
        after == before,
        f"showing a code writes NO fridge (was {before}, now {after}) -- this is the 67-record bug",
    )
    ok(
        any(e["event"] == "pair-start" for e in posted),
        "but it does post pair-start, so curiosity is still counted",
    )
    started = [e for e in posted if e["event"] == "pair-start"][-1]
    ok(started["service"] == "live", "as the live service")
    ok(
        "fridge" in started.get("props", {}),
        "carrying a fridge id so codes can be counted without a record on disk",
    )
    ok(
        not started.get("device"),
        "and no device, because no reader header rode this request",
    )

    # Ten more abandoned screens, still nothing on disk.
    for _ in range(10):
        client.post("/api/pair/start")
    after = len(list(fridges.iterdir())) if fridges.is_dir() else 0
    ok(after == before, f"eleven abandoned pairings, still {after} fridges")

    # --- claiming a code is what creates one ------------------------------
    r = client.post("/api/pair/start")
    code = r.json()["code"]
    del posted[:]
    r = client.post("/api/claim", json={"code": code, "name": "A phone"})
    ok(r.status_code == 200, f"a good code claims, got {r.status_code} {r.text[:120]}")
    after = len(list(fridges.iterdir())) if fridges.is_dir() else 0
    ok(after == before + 1, f"NOW there is a fridge (was {before}, now {after})")
    ok(
        any(e["event"] == "paired" for e in posted),
        "and a paired event says somebody typed the code",
    )

    # The reader collects its token, and it opens the fridge that was made.
    poll_token = r_poll = None
    r2 = client.post("/api/pair/start")
    code2, poll_token = r2.json()["code"], r2.json()["pollToken"]
    client.post("/api/claim", json={"code": code2, "name": "Another phone"})
    r_poll = client.get("/api/pair/poll", params={"pollToken": poll_token})
    ok(
        r_poll.status_code == 200,
        f"the reader polls and is told, got {r_poll.status_code}",
    )
    device_token = r_poll.json().get("deviceToken", "")
    ok(bool(device_token), "and is handed a device token")
    ok(
        store.fridge_for_device(device_token) is not None,
        "which opens a fridge that EXISTS -- the token is never ahead of the record",
    )

    # --- the check-in counter ---------------------------------------------
    fridge = store.fridge_for_device(device_token)
    state = fridge.load()
    ok(
        state.get("checkins") == 0,
        f"a new fridge has checked in 0 times, got {state.get('checkins')}",
    )
    ok(
        not state.get("last_checkin"),
        "and has never been heard from",
    )

    del posted[:]
    hdr = {"Authorization": f"Bearer {device_token}", "X-Live-On": "1"}
    client.get("/api/pull", headers=hdr)
    state = fridge.load()
    ok(
        state.get("checkins") == 1,
        f"one pull is one check-in, got {state.get('checkins')}",
    )
    ok(state.get("first_checkin"), "and the first one is stamped")
    first = state["first_checkin"]

    checkins = [e for e in posted if e["event"] == "checkin"]
    ok(len(checkins) == 1, f"one checkin event, got {len(checkins)}")
    ok(checkins[0]["props"]["n"] == 1, "carrying n=1")

    del posted[:]
    client.get("/api/pull", headers=hdr)
    state = fridge.load()
    ok(state.get("checkins") == 2, f"two pulls is two, got {state.get('checkins')}")
    ok(
        state.get("first_checkin") == first,
        "the first check-in does not move when a second arrives",
    )
    checkins = [e for e in posted if e["event"] == "checkin"]
    ok(
        checkins and checkins[0]["props"]["n"] == 2,
        "and n=2 is what separates a fridge in use from an abandoned setup",
    )

    # A fridge written before the counter existed must not read as zero.
    old = store.Fridge(store.new_fridge_id())
    old.create("deadbeef")
    s = old.load()
    del s["checkins"]
    s["last_checkin"] = 1700000000
    old.save(s)
    n = old.touch_checkin(900, True)
    ok(n == 2, f"an old record that had checked in counts as at least 2, got {n}")

    # --- the sweep repairs what the old flow left --------------------------
    # Three orphans of the kind the service is full of: no sender, no
    # check-in, and old enough not to be a claim in flight.
    import time as _t

    made = []
    for _ in range(3):
        orphan = store.Fridge(store.new_fridge_id())
        orphan.create("cafebabe")
        s = orphan.load()
        s["created"] = int(_t.time()) - 7200
        orphan.save(s)
        store.index_token(f"tok-{orphan.id}", orphan.id)
        made.append(orphan)
    ok(all(o.exists() for o in made), "three orphans planted")

    live_before = fridge.load()
    gone = store.sweep_orphans()
    ok(gone == 3, f"the sweep removes exactly the three, got {gone}")
    ok(not any(o.exists() for o in made), "and they are off the disk")
    ok(fridge.exists(), "the fridge that checked in is untouched")
    ok(
        fridge.load() == live_before,
        "and its record is byte-for-byte what it was",
    )
    index = json.loads((pathlib.Path(work) / "tokens.json").read_text())
    ok(
        all(v != o.id for v in index.values() for o in made),
        "their tokens are out of the index too, not left pointing at nothing",
    )
    ok(
        store.fridge_for_device(device_token) is not None,
        "and a live device token still opens its fridge",
    )

    # A fridge that was claimed but has not checked in yet is NOT an orphan:
    # somebody is mid-setup and its sender is real.
    r3 = client.post("/api/pair/start")
    client.post("/api/claim", json={"code": r3.json()["code"], "name": "Mid setup"})
    fresh = [
        store.Fridge(d.name)
        for d in fridges.iterdir()
        if d.is_dir()
        and not json.loads((d / "state.json").read_text()).get("last_checkin")
    ]
    ok(bool(fresh), "a just-claimed fridge exists")
    ok(
        store.sweep_orphans(older_than_s=0) == 0,
        "and is never swept, however old: it has a sender",
    )
    ok(all(f.exists() for f in fresh), "so it is still there")

    events.post = events_post
    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
