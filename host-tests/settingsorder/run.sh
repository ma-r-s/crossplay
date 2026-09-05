#!/bin/bash
# CrossPlay's settings sort below CrossPoint's.
#
# Mario's rule, 2026-09-05: a reader looking for a CrossPoint setting should
# never have to scroll past ours to reach it.
#
# It was broken and looked fine. SettingsList.h declares the fork's two System
# rows last and carried a comment saying "Last in System on purpose" -- true of
# the declaration, false of the screen, because SettingsActivity appends eight
# upstream ACTION rows (WiFi, KOReader, OPDS, Clear cache, Check for updates,
# SD update, Language, Keyboards) after that list is built. Dev Mode rendered
# 6th of 14, above WiFi and Language, for as long as those actions have existed.
#
# So this reads the append order out of the activity rather than trusting the
# declaration, and it decides which rows are ours by asking upstream instead of
# naming them: a hardcoded pair would still pass the day someone adds a third.
#
#   host-tests/settingsorder/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/../.." || exit 1
exec python3 host-tests/settingsorder/test_settingsorder.py
