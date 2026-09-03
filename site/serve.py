"""Serve this directory with the same COOP/COEP headers vercel.json sets.

The wasm build is pthreaded, so SharedArrayBuffer has to be available, which
means cross-origin isolation. A plain http.server does not send those headers
and the module silently never starts -- the canvas just stays at the shell's
300x150 default, which reads exactly like a render bug and is not one.

It also answers /api/firmware, which in production is the one Vercel function
this site has. Without it the Install button is untestable off Vercel: it fails
at the download with a 404 from the static handler, which looks exactly like a
broken endpoint and is only a missing one.

  serve.py [port]
"""

import functools
import http.server
import json
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
        super().do_GET()

    def serve_board_config(self):
        # Mirrors api/board-config.js: the inbox page asks for the board's
        # address and public key. Source .board/supabase.env before running
        # serve.py to work on the inbox locally; without it the page says so.
        import os

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


socketserver.TCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(
    ("127.0.0.1", PORT), functools.partial(Handler, directory=ROOT)
) as httpd:
    print(f"serving {ROOT} on {PORT} (cross-origin isolated)")
    httpd.serve_forever()
