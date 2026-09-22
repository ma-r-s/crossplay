"""Serve this directory with the same COOP/COEP headers vercel.json sets.

The wasm build is pthreaded, so SharedArrayBuffer has to be available, which
means cross-origin isolation. A plain http.server does not send those headers
and the module silently never starts -- the canvas just stays at the shell's
300x150 default, which reads exactly like a render bug and is not one.

It also answers /api/firmware, which in production is the one Vercel function
this site has. Without it the Install button is untestable off Vercel: it fails
at the download with a 404 from the static handler, which looks exactly like a
broken endpoint and is only a missing one.

It also PROXIES every other /api/ path to the Live service
(https://fridge.ma-r-s.com, or $LIVE_API). In production /live/ calls that host
directly and the sender cookie rides along because both names sit under
ma-r-s.com; from localhost the two are cross-SITE, the Lax cookie is never
sent, and the page reports "not connected" with nothing in the console to say
why. The proxy makes the local page same-origin with the API so the journey can
be driven for real. Reached with /live/?local.

Dev only: with INBOX_FIXTURE set to a JSON file, POST /api/inbox is answered
from that file whatever the passphrase (op `list` returns its `list` object,
`numbers` its `numbers` object, `answer` says {ok: true} and changes nothing).
That is how the inbox page's layout gets looked at without a passphrase or a
board; inbox/fixture.json is the file. Without the variable the endpoint says
the inbox is not set up, which is what production says without its secrets.
Production never runs this file, so the fixture can never reach it.

  serve.py [port]
  INBOX_FIXTURE=site/inbox/fixture.json serve.py [port]
"""

import functools
import http.server
import json
import os
import pathlib
import re
import socketserver
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = str(pathlib.Path(__file__).resolve().parent)
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8899

# Keep in step with api/firmware.js. Two spellings of one fact is the usual
# way this rots, so the release host-test asserts both against the workflow.
FIRMWARE_NAMES = {
    "x4pro": "crossplay-{tag}-x4pro-full.bin",
    "sticky": "crossplay-{tag}-sticky-full.bin",
}
TAG_RE = re.compile(r"^v\d{1,3}\.\d{1,3}\.\d{1,3}$")
RELEASES = "https://github.com/ma-r-s/crossplay/releases/download"


def dev_cookie(value: str, https: bool) -> str:
    """A forwarded Set-Cookie, with the attributes a browser here would refuse.

    TWO OF THEM, AND SILENTLY IS THE WHOLE PROBLEM. The claim returns 200, the
    cookie is never stored, and the very next /api/state says "not connected":
    nothing anywhere reports a failure, and it reads as the six digits being
    wrong when they were right.

    `Domain=.ma-r-s.com`: a browser REFUSES a cookie whose Domain does not
    cover the host that set it, and this dev server's host does not.

    `Secure`: the service derives it from the scheme its request arrived on, and
    this proxy calls https://fridge.ma-r-s.com, so the cookie comes back marked
    Secure. A browser refuses a Secure cookie over plain http, which is what
    this server speaks. Dropped only when this server is not itself https, so
    it stays an accommodation rather than a policy.

    HttpOnly and SameSite are left exactly as sent: Lax is what makes the real
    pair work, and neither is refused here.

    Production never runs this file, so none of this can reach a real cookie.
    """
    kept = []
    for part in value.split("; "):
        low = part.strip().lower()
        if low.startswith("domain="):
            continue
        if not https and low == "secure":
            continue
        kept.append(part)
    return "; ".join(kept)


class Handler(http.server.SimpleHTTPRequestHandler):
    # These paths ship already-brotli (tools_local/site/precompress.py), and
    # production declares it in vercel.json. Local dev must say the same thing
    # or the browser gets compressed bytes labelled as a wasm.
    PRECOMPRESSED = ("/emulator/", "/pyodide/", "/study/NotoSansCJK.otf")

    def do_GET(self):
        if self.path.split("?")[0] == "/api/firmware":
            self.serve_firmware()
            return
        if self.path.split("?")[0] == "/api/board-config":
            self.serve_board_config()
            return
        if self.live_path():
            self.proxy_live()
            return
        super().do_GET()

    def do_POST(self):
        if self.path.split("?")[0] == "/api/inbox":
            self.serve_inbox()
            return
        if self.live_path():
            self.proxy_live()
            return
        self.fail(404, "Nothing answers POST here.")

    def do_PUT(self):
        if self.live_path():
            self.proxy_live()
            return
        self.fail(404, "Nothing answers PUT here.")

    # DELETE, because /live/ deletes a history entry with one. Without it
    # http.server answers 501 Unsupported method, the page reports "That did not
    # work" over a service that was never asked, and the one journey this proxy
    # exists to make drivable is the one that cannot be driven.
    def do_DELETE(self):
        if self.live_path():
            self.proxy_live()
            return
        self.fail(404, "Nothing answers DELETE here.")

    # /live/ talks to fridge.ma-r-s.com, which is a DIFFERENT HOST in
    # production and is same-origin with nothing here. Locally it is reached
    # through this proxy, because the alternative does not work and looks like
    # a bug when it fails: a page on localhost is CROSS-SITE with
    # fridge.ma-r-s.com, so the sender cookie -- SameSite=Lax, which is right
    # and stays right between two ma-r-s.com subdomains -- is not sent at all,
    # and every call comes back "not connected" with nothing in the console.
    # Through the proxy the page is same-origin with the API and the journey
    # can be driven for real. Reached with /live/?local; see live.js.
    LIVE_ORIGIN = os.environ.get("LIVE_API", "https://fridge.ma-r-s.com")

    def served_over_https(self):
        """Whether the page this proxy serves was fetched over https.

        `http.server` has no TLS, so today this is always false; it is written
        as a question rather than a constant because the moment somebody puts
        this behind a tunnel the answer changes, and a hardcoded False would
        then strip an attribute that was doing its job.
        """
        return self.headers.get("x-forwarded-proto", "http").lower() == "https"

    def live_path(self):
        return self.path.split("?")[0].startswith("/api/") and not self.path.split("?")[0] in (
            "/api/firmware",
            "/api/board-config",
            "/api/inbox",
        )

    def proxy_live(self):
        length = int(self.headers.get("Content-Length") or 0)
        payload = self.rfile.read(length) if length else None
        req = urllib.request.Request(self.LIVE_ORIGIN + self.path, data=payload, method=self.command)
        # Cloudflare answers urllib's default agent with its own 1010 page, which
        # arrives as a 403 that looks exactly like the service refusing the call.
        req.add_header("User-Agent", self.headers.get("User-Agent") or "Mozilla/5.0 (crossplay dev proxy)")
        for header in ("Content-Type", "Cookie", "Authorization", "If-None-Match"):
            if self.headers.get(header):
                req.add_header(header, self.headers[header])
        try:
            with urllib.request.urlopen(req, timeout=20) as answer:
                status, headers, body = answer.status, answer.headers, answer.read()
        except urllib.error.HTTPError as err:
            status, headers, body = err.code, err.headers, err.read()
        except urllib.error.URLError as err:
            self.fail(502, f"{self.LIVE_ORIGIN} could not be reached: {err}")
            return
        self.send_response(status)
        for key, value in headers.items():
            if key.lower() == "set-cookie":
                # Domain and Secure would both be refused by a browser talking
                # to this server over plain http, silently. See dev_cookie.
                self.send_header(
                    key, dev_cookie(value, self.served_over_https())
                )
            elif key.lower() not in ("transfer-encoding", "content-encoding", "connection", "content-length"):
                self.send_header(key, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def serve_inbox(self):
        # Mirrors api/inbox.js only in shape. The real function checks a
        # passphrase and reads the board; this reads INBOX_FIXTURE and checks
        # nothing, so the page can be laid out and looked at offline. See the
        # header. Without the variable it answers as production does without
        # its secrets, so the page shows its "not set up" line and not a
        # parse error from the static handler's 501 page.
        fixture = os.environ.get("INBOX_FIXTURE", "")
        if not fixture:
            self.fail(503, "The inbox is not set up on this deployment.")
            return
        try:
            with open(fixture, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError) as err:
            self.fail(500, f"INBOX_FIXTURE could not be read: {err}")
            return
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            self.fail(400, "Unreadable request.")
            return
        op = body.get("op") if isinstance(body, dict) else None
        if op == "list":
            answer = data.get("list", {"inbox": [], "cards": []})
        elif op == "numbers":
            answer = data.get("numbers", {})
        # One branch per op, spelled `op == "<name>"`: host-tests/site checks
        # that every operation api/inbox.js handles is answered here too, and
        # it reads that spelling. Folding two ops into one membership test
        # passes locally and leaves the page 400ing on the other one.
        elif op == "answer":
            answer = {"ok": True}
        elif op == "seen":
            answer = {"ok": True}
        else:
            self.fail(400, "Unknown operation.")
            return
        out = json.dumps(answer).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def serve_board_config(self):
        # Mirrors api/board-config.js: the inbox page asks for the board's
        # address and public key. Source .board/supabase.env before running
        # serve.py to work on the inbox locally; without it the page says so.
        url = os.environ.get("SUPABASE_URL", "")
        anon = os.environ.get("SUPABASE_ANON_KEY", "")
        if not url or not anon:
            self.fail(503, "The board is not set up on this deployment.")
            return
        body = json.dumps({"url": url, "anonKey": anon}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def serve_firmware(self):
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        device = (query.get("device") or [""])[0]
        tag = (query.get("tag") or [""])[0]
        if device not in FIRMWARE_NAMES:
            self.fail(400, "Unknown device. Use x4pro or sticky.")
            return
        if not TAG_RE.match(tag):
            self.fail(400, "Malformed release tag.")
            return

        name = FIRMWARE_NAMES[device].format(tag=tag)
        try:
            upstream = urllib.request.urlopen(f"{RELEASES}/{tag}/{name}", timeout=30)
        except urllib.error.HTTPError as err:
            self.fail(
                err.code if err.code == 404 else 502,
                f"GitHub returned {err.code} for {name}.",
            )
            return
        except OSError as err:
            self.fail(502, f"Could not reach GitHub: {err}")
            return

        with upstream:
            size = upstream.headers.get("content-length")
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            if size:
                self.send_header("X-Firmware-Size", size)
            self.send_header("X-Firmware-Name", name)
            self.end_headers()
            # Chunked in production (no Content-Length, so the 4.5MB cap on a
            # buffered Vercel response does not apply); chunked here too, so
            # the progress bar is exercised the same way.
            while True:
                block = upstream.read(64 * 1024)
                if not block:
                    break
                try:
                    self.wfile.write(block)
                except (BrokenPipeError, ConnectionResetError):
                    return

    def fail(self, status, message):
        body = json.dumps({"error": message}).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def end_headers(self):
        if any(self.path.split("?")[0].startswith(p) for p in self.PRECOMPRESSED):
            self.send_header("Content-Encoding", "br")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *a):
        pass


# ALL INTERFACES, DELIBERATELY, AND DEV ONLY.
#
# Bound to 127.0.0.1 this is reachable from this Mac and nothing else, and the
# one thing /live/ most needs before it ships is a phone holding it: the layout
# is for a 390px screen, the gestures are touch, and neither is really testable
# in an emulated viewport. So it listens on the LAN.
#
# It stays dev-only by construction rather than by promise: production is
# Vercel's static hosting and never runs this file at all (see the module
# docstring). What is exposed while it runs is this working tree's copy of the
# site plus the /api/ proxy, on a local network, for as long as somebody leaves
# it up. Stop it by pid when you are done.
#
# `?local` still matters from a phone, and for the reason the proxy exists: the
# page would otherwise call fridge.ma-r-s.com directly, the sender cookie is
# cross-SITE from an IP address, and the page would report "not connected" with
# nothing on screen to say why.
# Under `if __name__`, so host-tests/fridge can import dev_cookie and assert the
# rewriting above against the cookie the service really sets, without this file
# starting a server to do it.
if __name__ == "__main__":
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer(
        ("0.0.0.0", PORT), functools.partial(Handler, directory=ROOT)
    ) as httpd:
        print(f"serving {ROOT} on {PORT} (cross-origin isolated, all interfaces)")
        httpd.serve_forever()
