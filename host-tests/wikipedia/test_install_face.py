#!/usr/bin/env python3
"""The install screen draws in a face compiled into the firmware.

The install screen hands the SD card to USB (Storage.beginUsbDrive()) as soon
as it is up, and then redraws. Every other Wikipedia screen sets its body in
the reader's own face, SETTINGS.getReaderFontId(), which is an SD-card font
when the reader picked one. An SD font loads its glyphs from the card on
demand, so after the handoff only the glyphs already resident were drawn:
"Then plug the reader into the computer with its cable." came out as
"helheeaehecehscable." and the address line vanished (device log, 2026-10-18:
136 of 149 characters SD-font, 96 with no glyph once the card was away).

So the face render() binds for View::Install must not be the reader's.
Reads the source, as test_usb_exclusive.py does.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "apps_local" / "wikipedia" / "WikipediaActivity.cpp").read_text(encoding="utf-8")

checks = 0
bad = []


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        bad.append(message)


render = SOURCE[SOURCE.find("void WikipediaActivity::render("):]
faces = render[: render.find("toybox::makeTarget(")]
check("View::Install" in faces,
      "render() must choose the install screen's faces itself, before makeTarget()")
check(re.search(r"View::Install\s*\?\s*toybox::kReadingFontId", faces) is not None,
      "the install screen must set its body in toybox::kReadingFontId, compiled into the firmware")
check(re.search(r"Faces\{toybox::kButtonFontId,\s*bodyFont,", faces) is not None,
      "the non-prose faces must take bodyFont in the body slot")

for b in bad:
    print(f"FAIL install-face  {b}")
print(f"{'FAIL' if bad else 'ok  '}  wikipedia install-face: {checks} checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
