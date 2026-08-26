#!/usr/bin/env python3
"""Drive a desk device over its serial bridge: taps, buttons, screenshots.

The firmware side is src/DevSerialBridge.cpp, compiled only into the dev envs
(-DCROSSPOINT_DEV_SERIAL_BRIDGE=1). Coordinates are panel-native pixels
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
import sys
import time

import serial

PANEL_W = 800
PANEL_H = 480


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


def drain(s):
    """Relay whatever the device printed since the last read. Never discard:
    the log lines that arrive between commands are exactly the diagnostics a
    failure investigation needs."""
    while True:
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


def screenshot(s, out_path, timeout=30.0):
    s.reset_input_buffer()
    s.write(b"CMD:SCREENSHOT\n")
    deadline = time.time() + timeout
    buf = b""
    size = None
    while time.time() < deadline:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
        marker = b"SCREENSHOT_START:"
        idx = buf.find(marker)
        if idx >= 0:
            nl = buf.find(b"\n", idx)
            if nl >= 0:
                size = int(buf[idx + len(marker) : nl])
                buf = buf[nl + 1 :]
                break
    if size is None:
        print("no SCREENSHOT_START seen", file=sys.stderr)
        return False
    while len(buf) < size and time.time() < deadline:
        chunk = s.read(8192)
        if chunk:
            buf += chunk
    if len(buf) < size:
        print(f"short read: {len(buf)}/{size}", file=sys.stderr)
        return False
    fb = buf[:size]

    try:
        from PIL import Image
    except ImportError:
        with open(out_path + ".raw", "wb") as f:
            f.write(fb)
        print(f"Pillow missing; raw framebuffer at {out_path}.raw", file=sys.stderr)
        return True

    # 1bpp MSB-first, row-major, PANEL_W wide. 1 = white (matches the panel).
    img = Image.frombytes("1", (PANEL_W, PANEL_H), fb)
    # Portrait view: rotate 90 deg clockwise so the home screen reads upright.
    view = img.rotate(-90, expand=True)
    view.save(out_path)
    print(f"saved {out_path} ({view.size[0]}x{view.size[1]})")
    return True


def view_to_panel(x, y):
    """Portrait-view pixel (480x800) -> panel-native pixel (800x480).

    The view is the panel rotated 90 deg clockwise, so undo it:
    panel_x = view_y, panel_y = PANEL_H - 1 - view_x.
    """
    return y, PANEL_H - 1 - x


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


def run_seq(s, seq, view):
    """Execute one command; returns False on a failed reply."""
    verb = seq[0].lower()
    rest = seq[1:]
    if verb == "sleep":
        time.sleep(float(rest[0]) if rest else 1.0)
        return True
    if verb == "watch":
        deadline = time.time() + (float(rest[0]) if rest else 10.0)
        while time.time() < deadline:
            chunk = s.read(4096)
            if chunk:
                for ln in chunk.decode("utf-8", "replace").splitlines():
                    if ln.strip():
                        relay(ln.strip())
        return True
    if verb == "shot":
        return screenshot(s, rest[0] if rest else "shot.png")
    if verb in ("tap", "long", "swipe"):
        nums = [int(v) for v in rest]
        if view:
            nums[0], nums[1] = view_to_panel(nums[0], nums[1])
            if verb == "swipe":
                nums[2], nums[3] = view_to_panel(nums[2], nums[3])
        reply = command(s, f"CMD:{verb.upper()} " + " ".join(str(n) for n in nums))
        print(reply or "no reply", flush=True)
        # Let the gesture play out before the next command.
        time.sleep(0.6)
        return bool(reply and reply.startswith("OK"))
    if verb == "btn":
        reply = command(s, "CMD:BTN " + " ".join(rest).upper())
        print(reply or "no reply", flush=True)
        time.sleep(0.4)
        return bool(reply and reply.startswith("OK"))
    # Everything else passes through as CMD:<VERB> [args] (ping, heap, sd,
    # reboot, ...), so new firmware commands need no script change.
    reply = command(s, f"CMD:{verb.upper()}" + ("".join(" " + r for r in rest)))
    print(reply or "no reply", flush=True)
    return bool(reply and reply.startswith("OK"))


def serve(s, fifo_path, view):
    """Hold the port open and take command lines from a FIFO, forever.

    Opening the serial port can glitch the auto-reset circuit and reboot the
    device, and whether it does is not deterministic -- so any exploration
    that must keep device state alive runs through one serve process instead
    of one process per step. Each FIFO line is a normal command sequence;
    "quit" ends the server.
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
                        run_seq(s, seq, view)
                    except serial.SerialException as e:
                        # The CH343 can drop for a moment when the device
                        # resets; reopen rather than dying mid-session.
                        print(f"serial hiccup: {e}; reopening", flush=True)
                        try:
                            s.close()
                        except Exception:
                            pass
                        time.sleep(3)
                        s = open_port(s.port, s.baudrate)
                print("READY", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem5C850495631")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument(
        "--view", action="store_true", help="tap/swipe coords are portrait-view pixels"
    )
    ap.add_argument("words", nargs="+", help="commands, comma-separated for sequences")
    args = ap.parse_args()

    s = open_port(args.port, args.baud)
    ok = True
    try:
        if args.words[0] == "serve":
            serve(s, args.words[1], args.view)
        else:
            for seq in split_seqs(args.words):
                if seq:
                    ok = run_seq(s, seq, args.view) and ok
    finally:
        s.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
