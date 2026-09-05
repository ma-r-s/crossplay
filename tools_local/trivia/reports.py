#!/usr/bin/env python3
"""The report queue the device writes, and the reason codes both sides share.

    /trivia/reports.dat

This module is the reference reader AND the definition of the codes. The
firmware's TriviaReport.h carries the same table and host-tests/trivia asserts
the two agree, because a code that means "wrong answer" on one side and
"too easy" on the other is a silent corpus edit -- see the
`derived-facts-written-as-literals` memory for how that shape has bitten here
five times.

WHY A SECOND FILE, beside pack.state's FLAGGED bit. They answer different
questions and have different lifetimes: FLAGGED is local and permanent ("never
serve me this again") and the queue is drained ("tell someone about this").
Deriving one from the other loses information in both directions -- a player who
hides a question without giving a reason still hid it, and a queue truncated
after a successful sync must not un-hide anything.

THE HEADER IS THE BINDING, and it is the whole reason this is not just a list of
integers. An index means nothing without the pack it indexes. `pack_id` and
`count` are recorded when the queue is CREATED, so a queue that survives a pack
update is still readable against the pack it was filed against -- and a queue
whose header disagrees with the pack beside it is REFUSED rather than resolved.
docs/apps/trivia-pack-format.md's own residual is why: a replacement pack with
the same count keeps pack.state, so nothing on the device can see that its
indices changed meaning. Refusing is the only honest answer.
"""

import struct

MAGIC = b"XTRIVRPT"
VERSION = 1

# magic, version, reserved, count, sent, pack id. 52 bytes.
#
# `sent` is how many LEADING entries a sync has already delivered, and it exists
# instead of truncating the file. Truncating on a 2xx destroys any report the
# player filed between the request being built and the response arriving, and
# WritableByteSource has no truncate anyway -- so the device advances a cursor
# instead. Entries before it are delivered; entries after it are not; nothing is
# ever lost in the gap.
HEADER = struct.Struct("<8sHHII32s")
ENTRY = struct.Struct("<IBBH")  # index, reason, reserved, reserved
PACK_ID_BYTES = 32

# 0 is NOT "unset": it is a deliberate, complete report with no reason attached.
# Mario's rule on card #257 -- "a report with no reason is still a report, and
# demanding a category is how you get no reports" -- is this constant.
NONE = 0

REASONS = {
    NONE: "none",
    1: "wrong",  # the answer is factually wrong. Unplayable, not merely annoying.
    2: "nonsense",  # the clue does not parse as a question
    3: "giveaway",  # solo only: the options tell you the answer
    4: "ambiguous",  # more than one option is defensibly right
    5: "outdated",  # true when written, false now
    6: "broken",  # mangled text, bad encoding, truncation
    7: "regional",  # only someone local could know it
    8: "us",  # a US question the pack FAILED TO MARK -- see below
    9: "hard",  # levelled too easy for what it is
    10: "easy",  # levelled too hard for what it is
}
CODES = {name: code for code, name in REASONS.items()}

# `us` repairs a BIT, not a row, and that makes it the odd one out. With the US
# toggle off, Chooser::next already skips marked records, so a player who sees a
# US question at all is looking at one the pack failed to mark. The fix is to
# set bit 7 of that record's difficulty byte, not to remove the question.
BIT_REPAIRS = {"us"}


class Refused(Exception):
    """The queue cannot be trusted against this pack. Never resolved, never guessed."""


def encode_pack_id(pack_id):
    raw = pack_id.encode("utf-8")
    if len(raw) > PACK_ID_BYTES:
        raise ValueError(f"pack id {pack_id!r} is longer than {PACK_ID_BYTES} bytes")
    return raw.ljust(PACK_ID_BYTES, b"\0")


def write(path, pack_id, count, entries=(), sent=0):
    """Create a queue. `entries` is (index, reason_code) pairs."""
    with open(path, "wb") as f:
        f.write(HEADER.pack(MAGIC, VERSION, 0, count, sent, encode_pack_id(pack_id)))
        for index, reason in entries:
            f.write(ENTRY.pack(index, reason, 0, 0))


def append(path, index, reason=NONE):
    with open(path, "r+b") as f:
        f.seek(0, 2)
        f.write(ENTRY.pack(index, reason, 0, 0))


def read(path):
    """Return (pack_id, count, [(index, reason_name)]). Raises Refused on anything torn.

    Every entry comes back, delivered or not: a card being read by hand wants
    the whole history, and `sent` only tells a SYNC where to resume.
    """
    with open(path, "rb") as f:
        blob = f.read()
    if len(blob) < HEADER.size:
        raise Refused(f"{path}: shorter than a header ({len(blob)} bytes)")
    magic, version, _resv, count, sent, pack_raw = HEADER.unpack_from(blob, 0)
    if magic != MAGIC:
        raise Refused(f"{path}: not a report queue (magic {magic!r})")
    if version != VERSION:
        raise Refused(f"{path}: version {version}, this reader knows {VERSION}")
    body = len(blob) - HEADER.size
    # A torn tail is refused rather than truncated to the last whole entry. The
    # device appends one fixed-width record at a time, so a partial entry means
    # something other than the device wrote this file.
    if body % ENTRY.size:
        raise Refused(f"{path}: {body} bytes of entries is not a multiple of {ENTRY.size}")
    if sent > body // ENTRY.size:
        raise Refused(f"{path}: says {sent} entries were sent but holds {body // ENTRY.size}")
    pack_id = pack_raw.rstrip(b"\0").decode("utf-8", "replace")
    out = []
    for off in range(HEADER.size, len(blob), ENTRY.size):
        index, reason, _a, _b = ENTRY.unpack_from(blob, off)
        if reason not in REASONS:
            raise Refused(f"{path}: unknown reason code {reason} at index {index}")
        if index >= count:
            raise Refused(f"{path}: index {index} is past the pack's {count} questions")
        out.append((index, REASONS[reason]))
    return pack_id, count, out
