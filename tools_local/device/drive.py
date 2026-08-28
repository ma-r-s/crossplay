#!/usr/bin/env python3
"""Drive a desk device: taps, buttons, screenshots. Cable optional.

Two transports, one vocabulary:

    --port /dev/cu.usbmodemXXXX    the serial bridge (src/DevSerialBridge.cpp)
    --ip 192.168.68.78             Developer Mode over Wi-Fi, no cable

Wi-Fi carries the input verbs and screenshots (POST /api/dev/input, GET
/api/dev/screen) and needs a pairing token, taken from ~/.crossplay-devtoken
unless --token says otherwise. The card and diagnostic verbs (ls, cat, sd,
sdformat, writetest, heap, date, reboot) exist only on the serial bridge and
say so rather than failing obscurely.

The firmware side is src/DevSerialBridge.cpp and src/network/
CrossPointWebServer.cpp, both compiled only into the dev envs
(-DCROSSPOINT_DEV_SERIAL_BRIDGE=1); the verbs themselves live once, in
lib/DevInput/DevInputCommands.cpp, so the two transports cannot drift.
Coordinates are panel-native pixels
(800x480 landscape on both the X4 Pro and the Sticky); pass --view to give
tap/swipe coordinates in the portrait view frame instead (the frame shot.png
is saved in), and the script converts.

    uv run --with pyserial --with pillow tools_local/device/drive.py \
        --port /dev/cu.usbmodem5C850495631 ping
    ... tap 400 240
    ... btn UP
    ... swipe 10 240 300 240 250        # left-edge swipe = Back
    ... shot out.png                    # portrait view PNG + raw .pbm next to it
    ... heap
    ... watch 30                        # just relay log output for N seconds
    ... ls /.crosspoint                 # list a card directory
    ... cat /.crosspoint/chess.sav      # stream a card file
    ... writetest /.crosspoint/x.sav    # the exact open the games use, verbose
    ... mkdir /d, rm /f, rmdir /d       # card housekeeping
    ... sdprobe                         # raw cardBegin/volumeBegin, error codes
    ... sdformat YES                    # DESTRUCTIVE: SdFat formatter (FAT/exFAT)
    ... sd 10000000                     # remount at a given SPI clock
    ... reboot

Multiple commands can be chained with commas: "tap 400 240, sleep 2, shot s.png".
Anything not listed passes through as CMD:<VERB>, so new firmware commands need
no script change. The device logs interleave with replies on the same UART;
replies are the lines that begin with OK/ERR/SCREENSHOT_, everything else is
relayed to stderr. Opening the port can reset the device (nondeterministic):
any flow that must keep device state alive belongs in one `serve <fifo>`
session rather than one process per step.
"""

import argparse
import http.client
import os
import sys
import time
import urllib.error
import urllib.request

TRUNCATED_MARKER = b"ERR SCREENSHOT truncated"
END_MARKER = b"SCREENSHOT_END"
# A frame is width*height/8. The largest panel here is 800x480 = 48000; the
# bound is loose on purpose, it exists to reject nonsense, not to validate.
MAX_FRAME_BYTES = 1 << 20
# Longer than the firmware's own budget for saying it gave up. writeLine() there
# allows 1000ms per stall and 4000ms overall, so a host that waits 500ms sees a
# half-written notice, calls it no notice, and returns the frame. The two numbers
# live in different languages in the same branch; this one has to be the larger.
GRACE_S = 5.0

PANEL_W = 800
PANEL_H = 480

# Serial-bridge-only verbs: they touch the SD card, the heap or the chip, none
# of which Developer Mode exposes over HTTP.
CARD_VERBS = (
    "ls", "cat", "sd", "sdprobe", "sdformat", "writetest", "mkdir", "rm", "rmdir", "heap", "date", "reboot",
)


def open_port(port, baud):
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.2
    # Deasserted so opening the port neither resets the chip nor drags GPIO0.
    s.dtr = False
    s.rts = False
    s.open()
    return s


def relay(line):
    print(f"  [dev] {line}", file=sys.stderr)


def drain(s, timeout=3.0):
    """Relay whatever the device printed since the last read. Never discard:
    the log lines that arrive between commands are exactly the diagnostics a
    failure investigation needs.

    BOUNDED, and that is not decoration: command() calls this before it writes
    the command, so an unbounded version hangs every verb this tool has against
    a device that talks faster than the port timeout -- a dev build at
    LOG_LEVEL=2, a boot loop, an error spew. Which are the three states you
    reach for this tool in."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = s.read(4096)
        if not chunk:
            return
        for ln in chunk.decode("utf-8", "replace").splitlines():
            if ln.strip():
                relay(ln.strip())


def command(s, cmd, timeout=6.0):
    """Send one CMD line, return the OK/ERR reply line (str) or None."""
    drain(s)
    s.write((cmd + "\n").encode())
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = s.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if text.startswith("OK ") or text.startswith("ERR "):
                return text
            if text:
                relay(text)
    return None



# -- transports ---------------------------------------------------------------
#
# Both expose the same three things: command() returns the device's one-line
# OK/ERR reply, screenshot_raw() returns the framebuffer, and read() drains
# whatever the device said unprompted. Only the serial bridge has that last
# one: HTTP has no log stream, so the Wi-Fi link returns nothing and `watch`
# says so rather than sitting silent for the full duration looking healthy.


class SerialLink:
    """The cable. Every byte of this path is as it was before Wi-Fi existed."""

    kind = "serial"
    supports_card_verbs = True
    panel = (PANEL_W, PANEL_H)

    def __init__(self, port, baud):
        self.s = open_port(port, baud)
        self.port = port
        self.baud = baud

    def command(self, cmd, timeout=6.0):
        return command(self.s, cmd, timeout)

    def read(self, n):
        return self.s.read(n)

    def close(self):
        self.s.close()

    def reopen(self):
        try:
            self.s.close()
        except Exception:
            pass
        time.sleep(3)
        self.s = open_port(self.port, self.baud)

    def screenshot_raw(self, timeout=30.0):
        # Drain until the device stops talking before asking. Right after a
        # reset the boot log is still streaming, and a 48KB binary reply
        # competing with it for the ring is the one case that still failed --
        # the first shot of a session, never the rest.
        #
        # WITH A DEADLINE. A device that talks more often than the port timeout
        # -- a dev build under load, a boot loop, an error spew -- refreshes
        # quiet_since forever, and the first version of this would have hung
        # here with no output at all. Every other loop in this file is bounded;
        # so is this one.
        #
        # And relay what it drains rather than discarding it: drain()'s own
        # docstring says the lines arriving between commands are exactly the
        # diagnostics a failure investigation needs.
        quiet_since = time.time()
        drain_deadline = time.time() + 3.0
        while time.time() - quiet_since < 0.3 and time.time() < drain_deadline:
            chunk = self.s.read(4096)
            if chunk:
                quiet_since = time.time()
                for ln in chunk.decode("utf-8", "replace").splitlines():
                    if ln.strip():
                        relay(ln.strip())
        self.s.reset_input_buffer()
        self.s.write(b"CMD:SCREENSHOT\n")
        deadline = time.time() + timeout
        buf = b""
        size = None
        truncated = ""
        while time.time() < deadline:
            chunk = self.s.read(4096)
            if chunk:
                buf += chunk
            marker = b"SCREENSHOT_START:"
            idx = buf.find(marker)
            if idx >= 0:
                nl = buf.find(b"\n", idx)
                if nl >= 0:
                    # Device input, not a promise. The header is one unpaced
                    # write on some paths, so it can arrive short -- and then
                    # the digits are a prefix of the real length, or the next
                    # newline found is one inside the image. A bogus size is
                    # not merely unparseable: a negative one makes every length
                    # test below pass and hands back the buffer as an image.
                    try:
                        size = int(buf[idx + len(marker) : nl])
                    except ValueError:
                        print(
                            f"bad SCREENSHOT_START length: {buf[idx : nl][:40]!r}",
                            file=sys.stderr,
                        )
                        return None
                    if not 0 < size <= MAX_FRAME_BYTES:
                        print(f"implausible frame length {size}", file=sys.stderr)
                        return None
                    buf = buf[nl + 1 :]
                    break
            elif b"ERR SCREENSHOT" in buf:
                # A refusal: it says why, and no START is coming. Only reachable
                # while no START has been seen, because past that point `buf` is
                # 48000 bytes of image and image bytes spell things -- checking
                # for this first made a frame containing those letters look like
                # a refusal and hang until the timeout.
                break
        if size is None:
            said = buf.decode("utf-8", "replace").strip().splitlines()
            why = next((ln for ln in reversed(said) if ln.startswith("ERR")), "no SCREENSHOT_START seen")
            print(why, file=sys.stderr)
            return None
        # `len(buf) >= size` is NOT proof of a complete frame, and stopping on
        # it is the bug this loop is shaped around. On truncation the device
        # appends a notice, so a frame short by less than that notice is long
        # still reaches `size` bytes -- with error text written over its tail,
        # and with the notice itself cut in half, so no check for the notice can
        # see it either. Read on until the DEVICE says which it was.
        #
        # Two signals, and QUIET is the third. END means the device finished;
        # the notice means it gave up. Neither can be trusted on arrival:
        #
        # - image bytes can spell anything, so a notice found a thousand bytes
        #   into a still-streaming frame is a coincidence, not a verdict;
        # - and the notice is NOT reliably the last thing on the wire. The
        #   device logs the same failure, on this same transport, and on a dev
        #   build any other task can log at any moment. An earlier version of
        #   this required the notice to end the buffer, which the firmware in
        #   this very branch violated -- and a corrupt frame came back clean.
        #
        # So: END ends it immediately, and anything else waits for the device to
        # stop talking. `grace` is that wait, armed once there is a reason to
        # think the device is done and extended by every byte that follows.
        # WHERE the terminator lands is the verdict, not whether it is present.
        # A WHOLE FRAME IS ONE THE DEVICE TERMINATED, and the terminator sits at
        # exactly `size` -- the firmware writes it immediately after the payload
        # with nothing in between.
        #
        # Not "at or past": anything injected mid-payload (a LOG_ from another
        # task on a LOG_LEVEL=2 build) pushes END later, and `>=` reads that
        # displacement as better-than-whole while the log text sits in the
        # image. Not "somewhere in the buffer" either: that is a byte count with
        # extra steps, and image bytes can spell anything.
        #
        # And having enough bytes is NOT a fallback. Every earlier version of
        # this ended with "well, len(buf) >= size, ship it", which is precisely
        # the assumption this whole branch exists to remove: a frame short by
        # less than the notice reaches `size` with error text over its tail. No
        # terminator means we do not know, and not knowing is a short read.
        def whole():
            return buf[size : size + len(END_MARKER)] == END_MARKER

        grace = None
        while time.time() < deadline:
            if whole():
                break
            if grace is not None and time.time() > grace:
                break
            before = len(buf)
            chunk = self.s.read(8192)
            if chunk:
                buf += chunk
            settled = len(buf) >= size or TRUNCATED_MARKER in buf or END_MARKER in buf
            if settled and (grace is None or len(buf) > before):
                grace = time.time() + GRACE_S
        if not whole():
            cut = buf.find(TRUNCATED_MARKER)
            end_at = buf.find(END_MARKER)
            if cut >= 0:
                why = "device reported it gave up"
            elif 0 <= end_at < size:
                # Every firmware before this one printed the terminator on the
                # release path unconditionally, so a short frame arrived stamped
                # complete. This tool reads devices running those builds.
                cut, why = end_at, "device ended the frame early"
            else:
                cut, why = len(buf), "no terminator: device stopped talking"
            buf = buf[:cut]
            # The device precedes the notice with one newline to break out of
            # the binary stream. Dropping it keeps the reported byte count the
            # true one rather than always one high.
            if buf.endswith(b"\n"):
                buf = buf[:-1]
            truncated = why
            if len(buf) >= size:
                # Enough bytes, no terminator. Refuse rather than guess.
                buf = buf[: size - 1]
        if len(buf) < size:
            # The device says so itself now. Report ITS reason when it gave one:
            # "short read" alone cannot tell a wedged cable from a crashed
            # device, and that ambiguity is what cost a night.
            print(
                f"short read: {len(buf)}/{size} {f'({truncated})' if truncated else ''}".rstrip(),
                file=sys.stderr,
            )
            return None
        return buf[:size]


class WifiLink:
    """Developer Mode. No cable, and no card verbs -- see the module docstring."""

    kind = "wifi"
    supports_card_verbs = False

    def __init__(self, ip, token):
        self.ip = ip
        self.token = token
        # Replaced from the X-Panel-* headers on the first screenshot. The
        # firmware sends them; assuming 800x480 forever would make them decor.
        self.panel = (PANEL_W, PANEL_H)
        # ...and whether that has happened yet, which comparing against the
        # default cannot tell you: both real boards ARE 800x480, so the warning
        # fired forever on them and would have stayed silent on the one board
        # where it mattered.
        self.panel_read = False

    def _open(self, path, data=None, method=None, timeout=10.0, auth=True):
        req = urllib.request.Request(f"http://{self.ip}{path}", data=data, method=method)
        if auth:
            req.add_header("X-Dev-Token", self.token)
        # Explicit, because urllib defaults a body to x-www-form-urlencoded and
        # the device's HTTP core then parses it into arguments instead of
        # leaving it whole. The firmware recovers from that, but sending what we
        # actually mean is better than relying on it.
        if data is not None:
            req.add_header("Content-Type", "text/plain")
        return urllib.request.urlopen(req, timeout=timeout)

    def command(self, cmd, timeout=6.0):
        verb = cmd[4:] if cmd.startswith("CMD:") else cmd
        # Two read-only verbs that are routes rather than input commands. `log`
        # especially: over the cable the device narrates continuously and
        # `watch` relays it, but HTTP has no stream, so without this there is no
        # way to see what the device thought of what you just did.
        if verb == "LOG":
            return self._get_text("/api/dev/log", timeout)
        if verb == "PING":
            return self._get_text("/api/status", timeout, auth=False)
        # The counters, over the network. Worth its own route precisely because
        # the question it answers -- what did the cable do -- is unaskable over
        # the cable once the cable is the thing that stopped answering.
        if verb == "CDCSTAT":
            return self._get_text("/api/dev/serial", timeout)
        try:
            with self._open("/api/dev/input", data=verb.encode(), method="POST", timeout=timeout) as r:
                return r.read().decode("utf-8", "replace").strip()
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "replace").strip()
            if e.code == 401:
                return "ERR not paired -- run wifi-flash.sh --pair <code> first"
            # 409 is "busy", which is the device's own ERR line, not a transport
            # failure: hand it back unchanged so callers can retry on it.
            return body or f"ERR HTTP {e.code}"
        except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
            return f"ERR unreachable: {e}"

    def _get_text(self, path, timeout, auth=True):
        try:
            with self._open(path, timeout=timeout, auth=auth) as r:
                body = r.read().decode("utf-8", "replace").strip()
            return "OK " + body if body else "OK (empty)"
        except urllib.error.HTTPError as e:
            return f"ERR HTTP {e.code}"
        except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
            return f"ERR unreachable: {e}"

    def read(self, n):
        return b""

    def close(self):
        pass

    def reopen(self):
        pass

    def screenshot_raw(self, timeout=30.0):
        try:
            with self._open("/api/dev/screen", timeout=timeout) as r:
                width = int(r.headers.get("X-Panel-Width", PANEL_W))
                height = int(r.headers.get("X-Panel-Height", PANEL_H))
                self.panel = (width, height)
                self.panel_read = True
                data = r.read()
            expected = width * height // 8
            if len(data) != expected:
                print(f"short screenshot: {len(data)}/{expected} bytes", file=sys.stderr)
                return None
            return data
        except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
            # IncompleteRead is an HTTPException, not an OSError, and it is the
            # EXPECTED shape when the device gives up mid-frame: handleDevScreen
            # promises a Content-Length and then bails on a dead peer. Catching
            # only OSError turned that into a traceback.
            print(f"screenshot failed: {e}", file=sys.stderr)
            return None


def screenshot(link, out_path, timeout=30.0):
    fb = link.screenshot_raw(timeout)
    if fb is None:
        return False

    try:
        from PIL import Image
    except ImportError:
        with open(out_path + ".raw", "wb") as f:
            f.write(fb)
        print(f"Pillow missing; raw framebuffer at {out_path}.raw", file=sys.stderr)
        return True

    # 1bpp MSB-first, row-major, PANEL_W wide. 1 = white (matches the panel).
    img = Image.frombytes("1", link.panel, fb)
    # Portrait view: rotate 90 deg clockwise so the home screen reads upright.
    view = img.rotate(-90, expand=True)
    view.save(out_path)
    print(f"saved {out_path} ({view.size[0]}x{view.size[1]})")
    return True


def view_to_panel(x, y, panel_h=PANEL_H):
    """Portrait-view pixel -> panel-native pixel.

    The view is the panel rotated 90 deg clockwise, so undo it:
    panel_x = view_y, panel_y = panel_h - 1 - view_x. This matches
    GfxRenderer::tapToLogical's Portrait case exactly; if that changes, so must
    this.

    panel_h comes from the link, not from the module constant: the device sends
    its geometry in X-Panel-Height and reading it for the PNG while assuming it
    here is the one combination that silently taps the wrong place.
    """
    return y, panel_h - 1 - x


def split_seqs(words):
    seqs = []
    cur = []
    for w in words:
        if w.endswith(","):
            w = w[:-1]
            if w:
                cur.append(w)
            seqs.append(cur)
            cur = []
        elif w == ",":
            seqs.append(cur)
            cur = []
        else:
            cur.append(w)
    if cur:
        seqs.append(cur)
    return seqs


_warned_panel = False


def warn_if_panel_unknown(link):
    """--view converts against the panel height, and the Wi-Fi link only learns
    that from a screenshot's X-Panel-* headers. Before one has run it is the
    compiled-in default, which is right for both device envs today and silently
    wrong on anything else. Say so once rather than mis-tapping quietly.

    Keyed on whether a screenshot has actually run, not on whether the value
    equals the default -- on both real boards those are the same number."""
    global _warned_panel
    if _warned_panel or link.kind != "wifi" or getattr(link, "panel_read", True):
        return
    _warned_panel = True
    print(
        f"--view is assuming a {PANEL_W}x{PANEL_H} panel; take a `shot` first to read it "
        "from the device",
        file=sys.stderr,
    )


def send_with_retry(link, cmd, tries=9):
    """Send an input command, waiting out an ERR busy rather than failing on it.

    "busy" means an event of the same kind is still playing: the command was
    well formed and will work shortly, which is a retry and not a mistake. A
    SWIPE runs up to ten seconds, so a caller that treats busy as failure
    silently drops the command that follows every long gesture. The budget is
    sized past that ceiling on purpose: 8 waits x 1.5s covers kMaxSwipeMs with
    room, where the 4 tries this started with did not.
    """
    for attempt in range(tries):
        reply = link.command(cmd)
        if not (reply and reply.startswith("ERR busy")):
            return reply
        if attempt < tries - 1:
            time.sleep(1.5)
    return reply


def run_seq(link, seq, view):
    """Execute one command; returns False on a failed reply."""
    verb = seq[0].lower()
    rest = seq[1:]
    if verb == "sleep":
        time.sleep(float(rest[0]) if rest else 1.0)
        return True
    if verb == "watch":
        if link.kind != "serial":
            print("watch needs the cable: HTTP has no log stream. Use `log` instead.", file=sys.stderr)
            return False
        deadline = time.time() + (float(rest[0]) if rest else 10.0)
        while time.time() < deadline:
            chunk = link.read(4096)
            if chunk:
                for ln in chunk.decode("utf-8", "replace").splitlines():
                    if ln.strip():
                        relay(ln.strip())
        return True
    if verb == "shot":
        return screenshot(link, rest[0] if rest else "shot.png")
    if verb in ("tap", "long", "swipe"):
        nums = [int(v) for v in rest]
        if view:
            panel_h = link.panel[1]
            warn_if_panel_unknown(link)
            nums[0], nums[1] = view_to_panel(nums[0], nums[1], panel_h)
            if verb == "swipe":
                nums[2], nums[3] = view_to_panel(nums[2], nums[3], panel_h)
        reply = send_with_retry(link, f"CMD:{verb.upper()} " + " ".join(str(n) for n in nums))
        print(reply or "no reply", flush=True)
        # Let the gesture play out before the next command.
        time.sleep(0.6)
        return bool(reply and reply.startswith("OK"))
    if verb == "btn":
        reply = send_with_retry(link, "CMD:BTN " + " ".join(rest).upper())
        print(reply or "no reply", flush=True)
        time.sleep(0.4)
        return bool(reply and reply.startswith("OK"))
    if not link.supports_card_verbs and verb in CARD_VERBS:
        print(
            f"'{verb}' is a serial-bridge verb; Wi-Fi carries input and screenshots only.",
            file=sys.stderr,
        )
        return False
    # Everything else passes through as CMD:<VERB> [args] (ping, heap, sd,
    # reboot, ...), so new firmware commands need no script change.
    reply = link.command(f"CMD:{verb.upper()}" + ("".join(" " + r for r in rest)))
    print(reply or "no reply", flush=True)
    return bool(reply and reply.startswith("OK"))


def serve(link, fifo_path, view):
    """Hold the link open and take command lines from a FIFO, forever.

    Opening the serial port can glitch the auto-reset circuit and reboot the
    device, and whether it does is not deterministic -- so any exploration
    that must keep device state alive runs through one serve process instead
    of one process per step. Over Wi-Fi there is no such hazard, but the same
    server is still the convenient way to drive a long session. Each FIFO line
    is a normal command sequence; "quit" ends the server.
    """
    print(f"serving; echo commands into {fifo_path}", flush=True)
    while True:
        with open(fifo_path) as f:
            for line in f:
                words = line.split()
                if not words:
                    continue
                if words[0] == "quit":
                    print("serve: quit", flush=True)
                    return
                for seq in split_seqs(words):
                    if not seq:
                        continue
                    try:
                        run_seq(link, seq, view)
                    except OSError as e:
                        # The CH343 can drop for a moment when the device
                        # resets; reopen rather than dying mid-session.
                        #
                        # OSError, not Exception: serial.SerialException is an
                        # OSError, so this keeps the old behaviour on the cable
                        # -- while a plain typo ("tap abc 240" -> ValueError)
                        # would otherwise take the reopen path, and reopening
                        # the port can reboot the device, which is the one thing
                        # a serve session exists to avoid.
                        print(f"link hiccup: {e}; reopening", flush=True)
                        link.reopen()
                    except Exception as e:
                        print(f"bad command: {e}", flush=True)
                print("READY", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None, help="serial device; the cable transport")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ip", default=None, help="device address; Developer Mode over Wi-Fi")
    ap.add_argument(
        "--token",
        default=None,
        help="dev-mode token (default: ~/.crossplay-devtoken)",
    )
    ap.add_argument(
        "--view", action="store_true", help="tap/swipe coords are portrait-view pixels"
    )
    ap.add_argument("words", nargs="+", help="commands, comma-separated for sequences")
    args = ap.parse_args()

    if args.ip and args.port:
        ap.error("--ip and --port are two transports; pick one")

    if args.ip:
        token = args.token
        if token is None:
            path = os.path.expanduser("~/.crossplay-devtoken")
            try:
                with open(path) as f:
                    token = f.read().strip()
            except OSError:
                # Not fatal: `ping` reads /api/status, which needs no token.
                # Everything else gets the device's own 401, which says how to
                # pair -- a better message than argparse refusing to start.
                print(
                    f"no token in {path}; only `ping` will work "
                    "(pair with scripts_local/wifi-flash.sh --pair <code>)",
                    file=sys.stderr,
                )
                token = ""
        link = WifiLink(args.ip, token)
    else:
        # Importing pyserial only on the cable path keeps the Wi-Fi transport
        # usable with nothing installed, which is most of the point of it.
        global serial
        import serial  # noqa: PLC0415
        link = SerialLink(args.port or "/dev/cu.usbmodem5C850495631", args.baud)

    ok = True
    try:
        if args.words[0] == "serve":
            serve(link, args.words[1], args.view)
        else:
            for seq in split_seqs(args.words):
                if seq:
                    ok = run_seq(link, seq, args.view) and ok
    finally:
        link.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
