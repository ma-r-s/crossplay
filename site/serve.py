"""Serve this directory with the same COOP/COEP headers vercel.json sets.

The wasm build is pthreaded, so SharedArrayBuffer has to be available, which
means cross-origin isolation. A plain http.server does not send those headers
and the module silently never starts -- the canvas just stays at the shell's
300x150 default, which reads exactly like a render bug and is not one.

  serve.py [port]
"""

import functools
import http.server
import pathlib
import socketserver
import sys

ROOT = str(pathlib.Path(__file__).resolve().parent)
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8899


class Handler(http.server.SimpleHTTPRequestHandler):
    # These paths ship already-brotli (tools_local/site/precompress.py), and
    # production declares it in vercel.json. Local dev must say the same thing
    # or the browser gets compressed bytes labelled as a wasm.
    PRECOMPRESSED = ("/emulator/", "/pyodide/", "/study/NotoSansCJK.otf")

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
