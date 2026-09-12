#!/usr/bin/env python3
"""An activity that hands the card to a USB host must own its loop.

ActivityManager runs its own navigation (the home gesture, the status-bar
tap, the frontlight panel) before an activity's loop, unless the activity
says `requiresExclusiveStorageLoop()`. Without that, a bottom-edge swipe on
a screen that called Storage.beginUsbDrive() goes Home through onExit with
no restart, and endUsbDrive() cannot remount the card: from then on every
SD open fails (Home says "No open book", xkcd says the card is not
writable, a Wi-Fi flash cannot stage its image) until a power cycle. The
Wikipedia install screen shipped that way on the desk unit for an hour.

Scans src/apps_local for beginUsbDrive() callers and requires the sibling
header to override requiresExclusiveStorageLoop. Upstream's UsbDriveActivity
already does; the fork's apps are what this guards.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
APPS = ROOT / "src" / "apps_local"

bad = []
checks = 0
for path in sorted(APPS.rglob("*.cpp")):
    if "Storage.beginUsbDrive(" not in path.read_text(encoding="utf-8"):
        continue
    checks += 1
    header = path.with_suffix(".h")
    declared = header.exists() and "requiresExclusiveStorageLoop() const override" in header.read_text(
        encoding="utf-8"
    )
    if not declared:
        bad.append(f"{path.relative_to(ROOT)} -> {header.name}")

for b in bad:
    print(f"FAIL usb-exclusive  {b}: calls Storage.beginUsbDrive() but the activity does not "
          "override requiresExclusiveStorageLoop(); the home gesture would leave the card detached")
# The gate counts "N checks, M failed" lines (host-tests/checksh).
print(f"{'FAIL' if bad else 'ok  '}  wikipedia usb-exclusive: {checks} checks, {len(bad)} failed")
sys.exit(1 if bad else 0)
