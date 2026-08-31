#!/usr/bin/env python3
"""The on-card trivia pack: writer and reference reader.

Shape follows docs/apps/study-deck-format.md -- immutable content with an
offset index and a trailing sentinel, mutable state in a separate fixed-width
file. The C++ reader mirrors this; this module is the spec that proves it.
"""
import struct

MAGIC = b'XTRIVIA\0'
VERSION = 1
HEADER = struct.Struct('<8sHBBI')      # magic, version, flags, reserved, count

def _rec(item):
    """difficulty, year, alt-count, distractor-count, then uint16-length-prefixed
    UTF-8 fields: question, answer, alternates, then distractors."""
    alts = item.get('alt', [])
    wrong = item.get('w', [])
    parts = [item['q'].encode('utf-8'), item['a'].encode('utf-8')]
    parts += [a.encode('utf-8') for a in alts]
    parts += [w.encode('utf-8') for w in wrong]
    body = struct.pack('<BHBB', item['d'], item['y'], len(alts), len(wrong))
    for p in parts:
        body += struct.pack('<H', len(p)) + p
    return body

def write(items, path):
    recs = [_rec(i) for i in items]
    index, off = [], 0
    for r in recs:
        index.append(off)
        off += len(r)
    index.append(off)                                    # sentinel
    with open(path, 'wb') as f:
        f.write(HEADER.pack(MAGIC, VERSION, 0, 0, len(recs)))
        f.write(struct.pack(f'<{len(index)}I', *index))
        for r in recs:
            f.write(r)
    return HEADER.size + 4 * len(index) + off

def open_pack(path):
    """Holds NO index in RAM. The index is 195KB for a 50k pack, which the
    device does not have to spare, so index entries are seeked to as well."""
    f = open(path, 'rb')
    magic, version, _flags, _r, count = HEADER.unpack(f.read(HEADER.size))
    if magic != MAGIC:
        raise ValueError(f'not a trivia pack: {magic!r}')
    if version != VERSION:
        raise ValueError(f'unsupported pack version {version}')
    return {'f': f, 'count': count,
            'index_at': HEADER.size,
            'base': HEADER.size + 4 * (count + 1)}

def read_one(pack, i):
    """Two seeks, two reads, no scan and no resident index.
    Total RAM for a read is the record itself: 186 bytes worst case."""
    if not (0 <= i < pack['count']):
        raise IndexError(i)
    pack['f'].seek(pack['index_at'] + 4 * i)
    start, end = struct.unpack('<II', pack['f'].read(8))   # entry + its successor
    pack['f'].seek(pack['base'] + start)
    buf = pack['f'].read(end - start)
    d, y, nalt, nwrong = struct.unpack_from('<BHBB', buf, 0)
    o = 5
    fields = []
    for _ in range(2 + nalt + nwrong):
        (ln,) = struct.unpack_from('<H', buf, o); o += 2
        fields.append(buf[o:o + ln].decode('utf-8')); o += ln
    return {'d': d, 'y': y, 'q': fields[0], 'a': fields[1],
            'alt': fields[2:2 + nalt], 'w': fields[2 + nalt:]}


# --- mutable state -----------------------------------------------------------
# Separate file, fixed width, one byte per question, so marking a question is
# one seek and one byte -- never a rewrite. Same rule as study's cards.dat:
# content is immutable, state is fixed-width, power loss cannot corrupt text.
SEEN = 0x01      # served at least once
FLAGGED = 0x02   # a player marked it bad while playing

def state_path(pack_path):
    return pack_path.rsplit('.', 1)[0] + '.state'

def write_state(path, count):
    with open(path, 'wb') as f:
        f.write(b'\0' * count)

def set_flag(path, i, bit):
    with open(path, 'r+b') as f:
        f.seek(i)
        cur = f.read(1) or b'\0'
        f.seek(i)
        f.write(bytes([cur[0] | bit]))

def get_flag(path, i):
    with open(path, 'rb') as f:
        f.seek(i)
        b = f.read(1)
    return b[0] if b else 0
