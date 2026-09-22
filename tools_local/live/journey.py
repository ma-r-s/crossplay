"""The real page against the real service, in a browser.

serve.py proxies /api/ to whatever $LIVE_API names, so with a local uvicorn
behind it the page is same-origin with a real service and the whole journey can
be driven: pair, draw, send, watch it land in the rail, pick an older one,
delete the picked one, set a clock time, read the pending line back.

Everything a unit test can prove is proved in host-tests/fridge; this is for the
half that only exists when the two halves are connected.

It starts its own service on a scratch directory and its own copy of the site,
so it touches no deployed thing and no real reader.

NOT a gate suite on purpose: it wants uv, playwright and a Chrome, and it takes
half a minute. host-tests/fridge proves the service on its own in two seconds;
this is the pass to run after changing either half of the pair.

  uv run --with playwright python tools_local/live/journey.py
"""

import asyncio
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

from playwright.async_api import async_playwright

ROOT = pathlib.Path(__file__).resolve().parents[2]
ok, bad = [], []


def check(name, cond, detail=""):
    (ok if cond else bad).append(f"{name}{(' -- ' + str(detail)) if detail else ''}")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait(url, seconds=25):
    for _ in range(seconds * 5):
        try:
            urllib.request.urlopen(url, timeout=1)
            return True
        except Exception:  # noqa: BLE001
            time.sleep(0.2)
    return False


async def main():
    api_port = free_port()
    site_port = free_port()
    data = tempfile.mkdtemp()
    env = dict(os.environ, FRIDGE_DATA=data)
    api = subprocess.Popen(
        [
            "uv",
            "run",
            "--quiet",
            "--with",
            "fastapi",
            "--with",
            "uvicorn",
            "python",
            "-m",
            "uvicorn",
            "bridge.app:app",
            "--host",
            "127.0.0.1",
            "--port",
            str(api_port),
        ],
        cwd=str(ROOT / "server" / "fridge-bridge"),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    site = subprocess.Popen(
        [sys.executable, "serve.py", str(site_port)],
        cwd=str(ROOT / "site"),
        env=dict(env, LIVE_API=f"http://127.0.0.1:{api_port}"),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        if not wait(f"http://127.0.0.1:{api_port}/healthz"):
            print("the service did not come up")
            return 1
        if not wait(f"http://127.0.0.1:{site_port}/live/"):
            print("the site did not come up")
            return 1

        base = f"http://localhost:{site_port}/live/?local"

        def post(path, body=None):
            req = urllib.request.Request(
                f"http://127.0.0.1:{api_port}{path}",
                data=json.dumps(body).encode() if body is not None else b"",
                headers={"content-type": "application/json"},
                method="POST",
            )
            return json.loads(urllib.request.urlopen(req).read())

        started = post("/api/pair/start")

        async with async_playwright() as p:
            b = await p.chromium.launch(channel="chrome")
            c = await b.new_context(
                viewport={"width": 390, "height": 844},
                is_mobile=True,
                has_touch=True,
                device_scale_factor=2,
            )
            page = await c.new_page()
            errs = []
            page.on("pageerror", lambda e: errs.append(str(e)))
            # A resource that 401s is the page asking a question and being told
            # no, which it handles and says out loud. /_vercel/insights only
            # exists in production. Anything else that fails to load, and every
            # uncaught throw, is a real finding.
            def noted(m):
                if m.type != "error":
                    return
                where = (m.location or {}).get("url", "") or ""
                if "_vercel" in where or "status of 401" in m.text:
                    return
                errs.append(f"{m.text} <- {where}")

            page.on("console", noted)

            # --- pairing, through the six-digit box ------------------------
            await page.goto(base, wait_until="networkidle")
            await page.wait_for_timeout(400)
            check(
                "the page asks for a code", not await page.locator("#gate").is_hidden()
            )
            await page.fill("#code", started["code"])
            await page.click("#pair")
            await page.wait_for_timeout(700)
            check("and connects", not await page.locator("#app").is_hidden())
            check("the rail starts empty", await page.locator(".lv-empty").count() == 1)

            # --- drawing and sending ---------------------------------------
            # The canvas has a screen of its own; Draw is the door.
            await page.click("#goDraw")
            await page.wait_for_timeout(350)
            box = await page.locator("#stage").bounding_box()
            cx, cy = box["x"] + box["width"] / 2, box["y"] + box["height"] / 2
            for dy in (-40, 0, 40):
                await page.mouse.move(cx - 50, cy + dy)
                await page.mouse.down()
                await page.mouse.move(cx + 50, cy + dy, steps=6)
                await page.mouse.up()
            await page.click("#send")
            await page.wait_for_timeout(1200)
            note = await page.locator("#sendNote").text_content()
            check("sending says it went", note.startswith("Sent."), note)
            check(
                "and it puts you back where the history is",
                await page.locator("#ways").is_visible(),
            )
            check("and it says when", "reader takes it" in note, note)
            n = await page.locator(".lv-card").count()
            check("and the rail has one in it", n == 1, n)
            badge = await page.locator(
                '.lv-card[data-selected="true"] .lv-card-badge'
            ).count()
            check("and it is picked", badge == 1, badge)

            # The tile is a real picture from the service, not a placeholder.
            loaded = await page.evaluate(
                "(() => { const i = document.querySelector('.lv-card img');"
                " return i ? [i.naturalWidth, i.naturalHeight, i.src.includes('/api/history/')] : null; })()"
            )
            check("the tile is a real thumbnail", loaded == [60, 100, True], loaded)

            # --- a second send, and picking the older one ------------------
            # Sending returned us to the home page, which is the point of it.
            await page.click("#goDraw")
            await page.wait_for_timeout(350)
            await page.click("#clear")
            await page.mouse.move(cx - 30, cy - 60)
            await page.mouse.down()
            await page.mouse.move(cx + 30, cy + 60, steps=8)
            await page.mouse.up()
            await page.click("#send")
            await page.wait_for_timeout(1200)
            check("two in the rail", await page.locator(".lv-card").count() == 2)
            await page.locator(".lv-card").nth(1).locator(".lv-card-pick").click()
            await page.wait_for_timeout(700)
            picked = await page.evaluate(
                "[...document.querySelectorAll('.lv-card')].findIndex("
                "e => e.dataset.selected === 'true')"
            )
            check("picking an older one sticks", picked == 1, picked)
            # Ask the service directly: the page must not be the only witness.
            hist = await page.evaluate(
                "fetch('/api/history', {credentials:'include'}).then(r => r.json())"
            )
            check(
                "and the SERVICE agrees about what is next",
                hist["selected"] == hist["entries"][1]["id"],
                hist["selected"],
            )

            # --- deleting the picked one -----------------------------------
            await page.click("#histAct .lv-btn")
            await page.wait_for_timeout(200)
            await page.click("#histAct .is-yes")
            await page.wait_for_timeout(800)
            note = await page.locator("#sendNote").text_content()
            check("deleting says the pick moved", note.startswith("Deleted."), note)
            hist = await page.evaluate(
                "fetch('/api/history', {credentials:'include'}).then(r => r.json())"
            )
            check("one left", len(hist["entries"]) == 1, hist)
            check(
                "and the service picked the newest remaining",
                hist["selected"] == hist["entries"][0]["id"],
                hist,
            )

            # --- a clock-time schedule -------------------------------------
            await page.click("#schedChip")
            await page.click("#modeDaily")
            await page.fill("#dailyTime", "07:00")
            await page.click("#schedDone")
            await page.wait_for_timeout(900)
            chip = await page.locator("#schedChipText").text_content()
            check("the chip names the clock time", chip == "07:00 daily", chip)
            st = await page.evaluate(
                "fetch('/api/state', {credentials:'include'}).then(r => r.json())"
            )
            check(
                "the service kept it", st["schedule"]["mode"] == "daily", st["schedule"]
            )
            check(
                "and says it is pending until the reader wakes",
                st.get("pending") == "Changing to 07:00 daily after the next check.",
                st.get("pending"),
            )
            # And the READER is told the same sentence, verbatim.
            polled = json.loads(
                urllib.request.urlopen(
                    f"http://127.0.0.1:{api_port}/api/pair/poll?pollToken={started['pollToken']}"
                ).read()
            )
            req = urllib.request.Request(
                f"http://127.0.0.1:{api_port}/api/senders",
                headers={"Authorization": f"Bearer {polled['deviceToken']}"},
            )
            senders = json.loads(urllib.request.urlopen(req).read())
            check(
                "one sentence, both surfaces",
                senders.get("pending") == st.get("pending"),
                (senders.get("pending"), st.get("pending")),
            )
            # AND THE PAGE SAYS IT. The service sent this and the reader drew it
            # on its foot line while the page mentioned nothing, so the panel
            # was saying something about the reader the page never named -- to
            # the one person who had just caused it.
            shown = await page.locator("#pendingLine").text_content()
            check(
                "and the page says it, in the service's own words",
                shown.strip() == st.get("pending"),
                (shown.strip(), st.get("pending")),
            )
            check(
                "and the editor says a change waits for the next check",
                "next" in (await page.locator(".lv-sched-lag").text_content()).lower(),
            )
            # AN ABSENT KEY IS NOTHING PENDING. Once the reader has picked the
            # schedule up, the line goes away rather than standing as a claim.
            urllib.request.urlopen(
                urllib.request.Request(
                    f"http://127.0.0.1:{api_port}/api/pull",
                    headers={"Authorization": f"Bearer {polled['deviceToken']}"},
                )
            ).read()
            await page.reload(wait_until="networkidle")
            await page.wait_for_timeout(800)
            st2 = await page.evaluate(
                "fetch('/api/state', {credentials:'include'}).then(r => r.json())"
            )
            check("nothing is pending after the reader wakes", "pending" not in st2, st2.get("pending"))
            check(
                "and the page says nothing rather than something",
                await page.locator("#pendingLine").is_hidden(),
            )

            # --- a second phone sees the same rail -------------------------
            other = await b.new_context(viewport={"width": 1440, "height": 900})
            page2 = await other.new_page()
            join = urllib.request.Request(
                f"http://127.0.0.1:{api_port}/api/pair/join",
                data=b"",
                headers={"Authorization": f"Bearer {polled['deviceToken']}"},
                method="POST",
            )
            joined = json.loads(urllib.request.urlopen(join).read())
            await page2.goto(base, wait_until="networkidle")
            await page2.fill("#code", joined["code"])
            await page2.click("#pair")
            await page2.wait_for_timeout(900)
            check(
                "a second phone sees the same history",
                await page2.locator(".lv-card").count() == 1,
                await page2.locator(".lv-card").count(),
            )
            check(
                "and the same schedule",
                (await page2.locator("#schedChipText").text_content()) == "07:00 daily",
                await page2.locator("#schedChipText").text_content(),
            )
            by = await page2.locator(".lv-card-by").first.text_content()
            check("and who sent it", by in ("Mac", "iPhone", "A phone"), by)

            check("no page errors anywhere", not errs, errs[:3])
            await b.close()
    finally:
        for proc in (site, api):
            proc.terminate()
        shutil.rmtree(data, ignore_errors=True)

    print("PASS" if not bad else "FAIL")
    for x in ok:
        print("  ok   " + x)
    for x in bad:
        print("  FAIL " + x)
    print(f"{len(ok)} ok, {len(bad)} failed")
    return 1 if bad else 0


sys.exit(asyncio.run(main()))
