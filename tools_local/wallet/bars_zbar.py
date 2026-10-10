"""Draws each line of bars_dump's output and reads it back with zbarimg.

Exits non-zero when a barcode does not read back as what it was made from.
Known gaps, not failures: an EAN-13 starting 0 reads as the UPC-A it is, and
zbar does not decode UPC-E with number system 1.
"""
import os, sys, subprocess, tempfile
OUT = os.path.join(tempfile.mkdtemp(), 't.png')
from PIL import Image
bad = 0
lines = open(sys.argv[1]).read().strip().split('\n')
for line in lines:
    kind, payload, text, bits = line.split('\t')
    px, quiet, h = 3, 15, 80
    w = (len(bits) + 2*quiet)*px
    im = Image.new('L', (w, h+40), 255)
    for i, b in enumerate(bits):
        if b == '1': im.paste(0, ((quiet+i)*px, 20, (quiet+i+1)*px, 20+h))
    im.save(OUT)
    out = subprocess.run(['zbarimg', '-q', '--raw', '-Senable', OUT], capture_output=True, text=True).stdout.strip()
    sym = subprocess.run(['zbarimg', '-q', '-Senable', OUT], capture_output=True, text=True).stdout.strip().split(':')[0]
    known = (kind == 'ean13' and text.startswith('0')) or (kind == 'upce' and text.startswith('1'))
    ok = known or out == text or (kind == 'codabar' and out[1:-1] == text) or (kind in ('upca', 'upce') and out.lstrip('0') == text.lstrip('0'))
    bad += not ok
    print('ok  ' if ok else 'BAD ', kind, repr(payload), '->', repr(out), sym)
print(len(lines), 'cases,', bad, 'bad')
sys.exit(1 if bad else 0)
