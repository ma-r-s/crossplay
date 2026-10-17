"""Pair for real, through the dev proxy, over plain http, from a browser.

A 200 from the claim proves nothing here: that is the entire shape of the bug.
The claim succeeded all evening and the session was never stored, so what has
to be checked is that the cookie is in the jar afterwards and that the very next
/api/state says connected.

Driven against a LAN address rather than localhost, because the address is half
the condition: a browser refuses a Secure cookie over plain http, and localhost
is the one origin some browsers treat as secure anyway.

NOT a gate suite: it wants uv, playwright, a Chrome and a LAN address.
host-tests/fridge asserts the rewriting rule itself in two seconds; this is the
pass to run after touching site/serve.py's proxy or the service's cookie.

  uv run --with playwright python tools_local/live/pair-over-http.py [address]
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
import urllib.error
import urllib.request

from playwright.async_api import async_playwright

ROOT = pathlib.Path(__file__).resolve().parents[2]
# The address to be reached on. Any non-loopback one this machine answers will
# do; localhost would not, because some browsers treat it as a secure context
# and would store the very cookie this exists to catch.
LAN = sys.argv[1] if len(sys.argv) > 1 else "192.168.68.71"
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
    # A service of our own, behind the real proxy, reached over https so it
    # marks the cookie Secure exactly as production does. That is the attribute
    # the proxy has to strip, and a service on plain http would not set it.
    api_port = free_port()
    shim_port = free_port()
    site_port = free_port()
    data = tempfile.mkdtemp()
    env = dict(os.environ, FRIDGE_DATA=data)
    api = subprocess.Popen(
        ["uv", "run", "--quiet", "--with", "fastapi", "--with", "uvicorn",
         "python", "-m", "uvicorn", "bridge.app:app",
         "--host", "127.0.0.1", "--port", str(api_port)],
        cwd=str(ROOT / "server" / "fridge-bridge"), env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    # A SHIM THAT SAYS "https", which is what Cloudflare tells the real service
    # and the only reason it marks the cookie Secure. The dev proxy must NOT
    # send that header itself -- telling the service a plain-http page is https
    # is how the cookie became unstorable in the first place -- so the header is
    # injected here instead, and the chain under test is exactly production's:
    #
    #   browser (http, IP) -> site/serve.py -> [https] -> the service
    import http.server as _h
    import threading as _t

    class Shim(_h.BaseHTTPRequestHandler):
        def _pass(self):
            length = int(self.headers.get("Content-Length") or 0)
            payload = self.rfile.read(length) if length else None
            req = urllib.request.Request(
                f"http://127.0.0.1:{api_port}{self.path}",
                data=payload, method=self.command,
            )
            req.add_header("x-forwarded-proto", "https")
            for h in ("Content-Type", "Cookie", "Authorization", "If-None-Match"):
                if self.headers.get(h):
                    req.add_header(h, self.headers[h])
            try:
                with urllib.request.urlopen(req, timeout=20) as a:
                    status, headers, body = a.status, a.headers, a.read()
            except urllib.error.HTTPError as e:
                status, headers, body = e.code, e.headers, e.read()
            self.send_response(status)
            for k, v in headers.items():
                if k.lower() not in ("transfer-encoding", "content-encoding",
                                     "connection", "content-length"):
                    self.send_header(k, v)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)

        do_GET = do_POST = do_PUT = do_DELETE = _pass

        def log_message(self, *a):
            pass

    shim = _h.ThreadingHTTPServer(("127.0.0.1", shim_port), Shim)
    _t.Thread(target=shim.serve_forever, daemon=True).start()

    site = subprocess.Popen(
        [sys.executable, "serve.py", str(site_port)],
        cwd=str(ROOT / "site"),
        env=dict(env, LIVE_API=f"http://127.0.0.1:{shim_port}"),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        if not wait(f"http://127.0.0.1:{api_port}/healthz"):
            print("service did not start")
            return 1
        if not wait(f"http://127.0.0.1:{site_port}/live/"):
            print("dev server did not start")
            return 1

        started = json.loads(
            urllib.request.urlopen(
                urllib.request.Request(
                    f"http://127.0.0.1:{api_port}/api/pair/start",
                    data=b"",
                    method="POST",
                )
            ).read()
        )

        async with async_playwright() as p:
            b = await p.chromium.launch(channel="chrome")
            c = await b.new_context(
                viewport={"width": 390, "height": 844},
                is_mobile=True,
                has_touch=True,
            )
            page = await c.new_page()
            errs = []
            page.on("pageerror", lambda e: errs.append(str(e)))

            # HIS ADDRESS SHAPE: an IP, plain http, and no ?local.
            await page.goto(f"http://{LAN}:{site_port}/live/", wait_until="networkidle")
            await page.wait_for_timeout(700)
            check(
                "the page loads over http from an IP",
                not await page.locator("#gate").is_hidden(),
            )

            await page.fill("#code", started["code"])
            await page.click("#pair")
            await page.wait_for_timeout(1500)

            # THE COOKIE IS ACTUALLY IN THE JAR. This is the assertion; a 200
            # from the claim is the thing that lied all evening.
            jar = await c.cookies()
            live = [k for k in jar if k["name"] == "live_sender"]
            check("the session cookie was stored", bool(live), [k["name"] for k in jar])
            if live:
                check(
                    "and not marked Secure, which is why it could be stored",
                    live[0]["secure"] is False,
                    live[0],
                )
                check("and kept HttpOnly", live[0]["httpOnly"] is True, live[0])

            # And the very next state call says connected.
            st = await page.evaluate(
                "fetch('/api/state', {credentials:'include'}).then(r => r.json())"
            )
            check("and /api/state says connected", st.get("connected") is True, st)

            # Which is the whole point: he lands on the board, not the gate.
            check(
                "the page shows the board", not await page.locator("#app").is_hidden()
            )
            check("and not the gate", await page.locator("#gate").is_hidden())
            check("with a way in to draw", await page.locator("#goDraw").is_visible())
            check("no page errors", not errs, errs[:2])

            await b.close()
    finally:
        for proc in (site, api):
            proc.terminate()
        shim.shutdown()
        shutil.rmtree(data, ignore_errors=True)

    print("PASS" if not bad else "FAIL")
    for x in ok:
        print("  ok   " + x)
    for x in bad:
        print("  FAIL " + x)
    print(f"{len(ok)} ok, {len(bad)} failed")
    return 1 if bad else 0


sys.exit(asyncio.run(main()))
