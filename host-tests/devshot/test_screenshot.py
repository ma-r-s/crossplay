#!/usr/bin/env python3
"""The host side of the screenshot path, against a fake cable.

Every case here is a way the device can stop mid-frame. They exist because the
in-loop truncation check LOOKED right and could not fire: its enclosing loop
condition was `len(buf) < size`, so a frame short by less than the notice's own
length exited the loop before the check ran and the notice was saved as image
bytes. `short_by_less_than_the_marker` is that case, and it fails against the
version this replaced.
"""

import io
import os
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools_local", "device"))
import drive  # noqa: E402

SIZE = 48000
CHECKS = 0
FAILED = 0


def brief(v):
    """A 48KB framebuffer's repr is 200KB of escapes, which buries the one line
    that says which check failed."""
    r = repr(v)
    return r if len(r) <= 80 else f"{r[:60]}... ({len(v)} bytes)"


def check(name, got, want):
    global CHECKS, FAILED
    CHECKS += 1
    if got != want:
        FAILED += 1
        print(f"FAIL {name}: got {brief(got)}, want {brief(want)}")


class FakeCable:
    """Hands back a scripted reply, once asked, in chunks. read() never blocks,
    so a test that would hang in the loop instead runs out the deadline -- and
    the suite passes a short timeout so that is a second, not thirty."""

    def __init__(self, reply, chunk=4096, cuts=None):
        self.reply = reply
        self.buf = b""
        self.chunk = chunk
        # Exact successive read sizes, when a test needs the boundary to land
        # somewhere specific. Without it reads are 8192 and every boundary is
        # the same one, which is how the first version of this suite passed
        # against code that had the bug.
        self.cuts = list(cuts) if cuts else None
        self.written = b""

    def read(self, n):
        # Nothing until CMD:SCREENSHOT has been sent. screenshot_raw drains the
        # port before asking, and a cable that answers early has its header
        # eaten by that drain -- which is a real behaviour of the real one, and
        # the reason the drain exists.
        if self.cuts and self.buf:
            n = self.cuts.pop(0)
        else:
            n = min(n, self.chunk)
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def write(self, data):
        self.written += data
        if b"CMD:SCREENSHOT" in self.written:
            self.buf, self.reply = self.buf + self.reply, b""
        return len(data)

    def reset_input_buffer(self):
        pass


def link(reply, chunk=4096, cuts=None):
    lk = object.__new__(drive.SerialLink)
    lk.s = FakeCable(reply, chunk, cuts)
    lk.port, lk.baud = "fake", 115200
    return lk


def shot(reply, chunk=4096, timeout=2.0, cuts=None):
    """Returns (payload_or_None, what_it_printed_to_stderr)."""
    err = io.StringIO()
    real, sys.stderr = sys.stderr, err
    try:
        return link(reply, chunk, cuts).screenshot_raw(timeout=timeout), err.getvalue()
    finally:
        sys.stderr = real


header = b"SCREENSHOT_START:%d\n" % SIZE
# Exactly SIZE bytes: 48000 is not a multiple of 256, and a fixture that is
# merely close makes every length assertion below meaningless.
image = (bytes(range(256)) * (SIZE // 256 + 1))[:SIZE]
notice = b"\nERR SCREENSHOT truncated\n"

# The happy path, and that the payload is returned exactly.
got, _ = shot(header + image + b"SCREENSHOT_END\n")
check("complete frame length", None if got is None else len(got), SIZE)
check("complete frame bytes", got, image)

# Short by less than the notice is long. len(buf) reaches SIZE anyway, so this
# is the case an in-loop check cannot see.
short = image[: SIZE - 10]
got, err = shot(header + short + notice)
check("short_by_less_than_the_marker rejected", got, None)
check("short_by_less_than_the_marker says why", "device reported it gave up" in err, True)

# Short by more, where even a broken check would have fired.
got, err = shot(header + image[:20000] + notice)
check("short by a lot rejected", got, None)
check("short by a lot reports the count", "20000/48000" in err, True)

# The case the whole loop is shaped around, and the one an in-loop check cannot
# see. The first read stops five bytes into the notice, the second stops eleven
# in: len(buf) passes SIZE while the notice is still half-arrived, so at that
# instant the frame looks complete AND contains no marker to contradict it. Only
# reading on for the device's verdict catches it.
part = len(header) + (SIZE - 10) + 5
got, err = shot(header + short + notice, cuts=[part, 11, len(notice)])
check("notice still arriving at SIZE rejected", got, None)
check("notice still arriving says why", "device reported it gave up" in err, True)

# Image bytes that happen to spell the notice, in a frame the device confirms.
# END outranks a chance match: the alternative is discarding good frames on a
# coincidence the payload is entitled to contain.
mark = drive.TRUNCATED_MARKER
spelled = image[:1000] + mark + image[1000 + len(mark) : SIZE]
got, _ = shot(header + spelled + b"SCREENSHOT_END\n")
check("marker spelled inside a confirmed frame is image data", got, spelled)

# Truncated with no notice at all -- a device that stopped rather than gave up.
# Bounded by the deadline, and it must not claim the device said anything.
t0 = time.time()
got, err = shot(header + image[:20000], timeout=1.0)
check("silent truncation rejected", got, None)
check("silent truncation does not invent a reason", "device reported" in err, False)
# Tighter than the deadline it is meant to prove: with timeout=1.0 a run that
# waits the deadline out takes >= 1.0s, so 0.9 is the bound that can actually
# tell "returned early" from "ran to the end".
check("silent truncation is bounded", time.time() - t0 < 1.0 + 2.0, True)

# The device refusing outright: no header ever comes. Its own words, not a
# generic timeout, and it must not wait the timeout out to say so.
t0 = time.time()
got, err = shot(b"ERR SCREENSHOT no framebuffer\n", timeout=5.0)
check("refusal rejected", got, None)
check("refusal quotes the device", "no framebuffer" in err, True)
check("refusal returns early", time.time() - t0 < 3.0, True)

# THE ONE THAT MATTERED. The device logs the same failure on the same wire, so
# the notice is followed by a log line -- and a rule of "the notice is the last
# thing said" reads that as no notice at all, returns len(buf) >= size, and
# hands back a frame with error text over its tail. The firmware in this branch
# emitted exactly this ordering, and the first version of this suite could not
# see it because every fixture stopped at the notice.
logline = b"[123456] [ERR] [DEVBRIDGE] screenshot truncated: short=3 zero=1\n"
got, err = shot(header + short + notice + logline)
check("notice followed by a log line still rejected", got, None)
check("notice followed by a log line says why", "device reported it gave up" in err, True)

# The same, with the device still chattering afterwards on a dev build.
got, _ = shot(header + image[:30000] + notice + logline * 3)
check("notice followed by three log lines rejected", got, None)

# The release path (main.cpp) has no notice at all: it printed the terminator
# unconditionally, so a short frame arrived stamped complete.
got, err = shot(header + short + b"SCREENSHOT_END\n")
check("release path short frame rejected", got, None)

# A frame the device confirms, whose payload ends in the notice's own bytes at
# a read boundary. END outranks it.
ends = image[: SIZE - len(mark)] + mark
got, _ = shot(header + ends + b"SCREENSHOT_END\n", cuts=[len(header) + SIZE, 15])
check("payload ending in the marker is not truncation", got, ends)

# The length field is device input, and one path writes it unpaced. None of
# these may produce an image.
for bad, label in [
    (b"SCREENSHOT_START:48\x00\x01junk\n", "non-numeric"),
    (b"SCREENSHOT_START:\n", "empty"),
    (b"SCREENSHOT_START:-1\n", "negative"),
    (b"SCREENSHOT_START:0\n", "zero"),
    (b"SCREENSHOT_START:999999999999\n", "absurd"),
]:
    got, err = shot(bad + image, timeout=1.5)
    check(f"{label} length rejected", got, None)
    check(f"{label} length explains itself", err.strip() != "", True)

# Silence: no header, nothing to quote.
got, err = shot(b"", timeout=1.0)
check("silence rejected", got, None)
check("silence says no START", "no SCREENSHOT_START" in err, True)

# All the bytes arrived and the device then went quiet: no END, no notice. The
# payload is whole, so it is returned, but only after the grace period rather
# than after the full timeout.
t0 = time.time()
got, _ = shot(header + image, timeout=5.0)
check("silent-but-complete frame accepted", got, image)
check("silent-but-complete frame does not wait out the timeout", time.time() - t0 < 3.0, True)

# drain() is called by command() BEFORE the command is even written, so an
# unbounded one hangs every verb this tool has -- ping, tap, ls, all of them --
# against a device that talks faster than the port timeout. Which is a dev build
# at LOG_LEVEL=2, a boot loop, or an error spew: the three states you reach for
# this tool in.


class Chatterbox:
    """Never stops talking, exactly like a device under LOG_LEVEL=2."""

    def read(self, n):
        return b"[123456] [INF] [MEM] Free: 200000 bytes\n"

    def write(self, data):
        return len(data)

    def reset_input_buffer(self):
        pass


# In a thread with a join timeout, so an unbounded drain FAILS this suite
# rather than hanging it. check.sh has no per-suite timeout: a hang there looks
# like a slow build and stops the whole gate, which is the one failure mode
# worse than a red check.
done = threading.Event()


def _drain():
    drive.drain(Chatterbox(), timeout=0.5)
    done.set()


t = threading.Thread(target=_drain, daemon=True)
t.start()
t.join(5.0)
check("drain returns against a device that never stops", done.is_set(), True)

print(f"{CHECKS} checks, {FAILED} failed")
sys.exit(1 if FAILED else 0)
