"""Parse the device's own binary formats, server-side.

The device posts raw bytes in the exact shapes it already writes to the SD
card (docs/apps/study-deck-format.md): a tail of revlog.dat and its whole
cards.dat. The struct formats and flag bits are IMPORTED from deck_to_anki
rather than restated, so the wire format has exactly one definition in the
repo and the firmware needs no JSON encoder for review data.
"""

import struct

import deck_to_anki as d2a


def parse_revlog(data: bytes) -> list[dict]:
    """Bytes of revlog.dat records -> review dicts in apply()'s shape.
    Voided (undone) and malformed records are dropped, same rules as the
    CLI's read_reviews."""
    out = []
    n = len(data) // d2a.REVLOG_RECORD_SIZE
    for i in range(n):
        chunk = data[i * d2a.REVLOG_RECORD_SIZE : (i + 1) * d2a.REVLOG_RECORD_SIZE]
        card_id, at_ms, rating, state, elapsed, interval, took_ms, flags = (
            struct.unpack(d2a.REVLOG_RECORD, chunk)
        )
        if card_id == 0 or not 1 <= rating <= 4:
            continue
        if flags & d2a.REVLOG_VOIDED:
            continue
        out.append(
            {
                "cardId": card_id,
                "atMs": at_ms,
                "rating": rating,
                "state": state,
                "elapsed": elapsed,
                "interval": interval,
                "tookMs": took_ms,
            }
        )
    return out


def parse_cards(data: bytes) -> dict:
    """Bytes of cards.dat -> {card_id: device state}, apply()'s cards shape."""
    out = {}
    n = len(data) // d2a.CARD_RECORD_SIZE
    for i in range(n):
        chunk = data[i * d2a.CARD_RECORD_SIZE : (i + 1) * d2a.CARD_RECORD_SIZE]
        card_id, stability, difficulty, due, last, reps, lapses = struct.unpack(
            "<qffiiHH", chunk[:28]
        )
        state, step = chunk[28], chunk[29]
        (due_minute,) = struct.unpack("<H", chunk[30:32])
        out[card_id] = {
            "stability": stability,
            "difficulty": difficulty,
            "due": due,
            "last": last,
            "reps": reps,
            "lapses": lapses,
            "state": state,
            "step": step,
            "dueMinute": due_minute,
        }
    return out
