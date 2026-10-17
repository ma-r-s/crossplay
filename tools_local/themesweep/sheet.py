#!/usr/bin/env python3
"""Contact sheet: sheet.py <out.png> <scale> <img>... -- labels each tile with its file name."""

import os, sys
from PIL import Image, ImageDraw

out, scale, files = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
ims = [Image.open(f).convert("L") for f in files]
w = int(ims[0].width * scale)
h = int(ims[0].height * scale)
gap, label = 24, 28
cols = min(len(ims), 5)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("L", (cols * (w + gap) + gap, rows * (h + gap + label) + gap), 160)
d = ImageDraw.Draw(sheet)
for i, (im, f) in enumerate(zip(ims, files)):
    x = gap + (i % cols) * (w + gap)
    y = gap + (i // cols) * (h + gap + label)
    d.text((x, y), os.path.basename(f)[:-4], fill=0)
    # A one-pixel frame shows where the panel ends, so a row drawn off the
    # bottom reads as cut rather than as white space.
    tile = im.resize((w, h))
    sheet.paste(tile, (x, y + label))
    d.rectangle([x - 1, y + label - 1, x + w, y + label + h], outline=0)
sheet.save(out)
print(out, sheet.size)
