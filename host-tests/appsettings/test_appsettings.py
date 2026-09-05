#!/usr/bin/env python3
"""An app's own option never renders in the device's global Settings.

Mario's rule, 2026-09-05: CrossPoint owns the reader, the keyboard and the
system; a CrossPlay app's options belong inside that app.

It was broken and looked deliberate. `triviaShowUsCentric` shipped in v1.12.29
as a row under Settings > System, one line below Developer Mode, and read by
exactly one file in the repo, TriviaActivity.cpp. Nothing could see that,
because nothing here had ever asked WHO READS a setting.

So this carries no list of app names, which would pass the day somebody adds an
app it has not heard of. It asks the code: for every row that renders in the
global Settings screen, find every file that reads its value. If they all live
under one src/apps_local/<app>/ directory, the setting belongs to that app and
the row must not be in the global menu at all.

A category-less SettingInfo is the intended shape for these. It still persists
under its key and is still exposed by the web settings API -- which is what lets
a setting move screens without resetting on devices that already have it -- it
simply does not render in the on-device Settings. SettingsList.h already used
that shape for the OPDS and frontlight values before this suite existed.

TWO THINGS THIS SUITE MUST NOT DO, both learned the hard way:

  It must not silently skip a row. The first version matched constructor calls
  with a regex ending `),\\n`, which missed `tiltPageTurn` -- inserted with
  `v.insert(..., SettingInfo::Enum(...));`, so its call ends `));`. That row was
  categorised, was in neither the checked set nor the disclosure list, and the
  suite reported clean over it. Calls are now brace-matched, and the parser
  ASSERTS it accounted for every `SettingInfo::` call in the file, so an
  unparsed one is a failure rather than an absence.

  It must not disclose into a void. The rows whose ownership cannot be traced
  are printed as SKIP, not as a note: check.sh surfaces `SKIP` from a passing
  suite's log and deletes that log on green, so a "note" line was written,
  never shown, then removed. host-tests/checksh polices the SKIP pattern.

  host-tests/appsettings/test_appsettings.py
"""
import os
import re
import subprocess
import sys

CHECKS = 0
FAILED = 0


def check(ok, msg):
    global CHECKS, FAILED
    CHECKS += 1
    if not ok:
        FAILED += 1
        print(f"FAIL appsettings  {msg}")


LIST = "src/SettingsList.h"
# Files that MUST mention every setting: the declaration, the struct that holds
# it, and the activity that sorts the fork's rows. A reader here says nothing
# about who owns the value.
PLUMBING = {
    LIST,
    "src/CrossPointSettings.h",
    "src/CrossPointSettings.cpp",
    "src/activities/settings/SettingsActivity.cpp",
}

list_src = open(LIST).read()


def calls(src):
    """Every SettingInfo::X(...) call, brace-matched to its closing paren.

    Paren-matched rather than pattern-matched on purpose: the call sites end in
    `),`, `));`, `)` and `).withTextSettings()`, and a regex tuned to the shapes
    present on the day it was written is how tiltPageTurn went missing.
    """
    out = []
    for m in re.finditer(r"SettingInfo::(\w+)\(", src):
        i = m.end() - 1
        depth = 0
        j = i
        while j < len(src):
            if src[j] == "(":
                depth += 1
            elif src[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out.append({"kind": m.group(1), "body": src[i + 1 : j]})
    return out


parsed = calls(list_src)
# Every call in the file is accounted for. `SettingInfo::` appears only as a
# constructor call here, so the two counts must agree exactly; if they ever do
# not, the parser lost one and every verdict below is over an unknown subset.
check(
    len(parsed) == list_src.count("SettingInfo::"),
    f"parsed {len(parsed)} SettingInfo:: calls but the file has "
    f"{list_src.count('SettingInfo::')}; the parser lost one and this suite is "
    f"reporting over an unknown subset of the settings",
)
# Three rows are not constructor calls at all: fontFamily, fontSize and
# dictionary are built field by field, because their enum values come off the SD
# card at runtime. They carry a category and they render, so they are rows, and a
# suite that only knew about constructors would report clean over them exactly as
# the first version did over tiltPageTurn.
builders = []
for m in re.finditer(r"\bs\.category\s*=\s*StrId::STR_CAT_(\w+)\s*;", list_src):
    # The surrounding builder block: back to the `SettingInfo s;` that starts it,
    # forward to the return. Both fields we need are set within it.
    start = list_src.rfind("SettingInfo s;", 0, m.start())
    end = list_src.find("return s;", m.end())
    block = list_src[start if start >= 0 else m.start() : end if end >= 0 else m.end()]
    ids = re.findall(r"s\.nameId = StrId::(STR_\w+)", block)
    keys = re.findall(r's\.key = "([^"]+)"', block)
    builders.append({"id": ids[0] if ids else "?", "key": keys[0] if keys else ""})

# Every STR_CAT_ in the file is now accounted for by a call or a builder. The
# only other mention is a comparison (`s.category == StrId::STR_CAT_CONTROLS`),
# which is a lookup, not a declaration.
declared = sum(1 for c in parsed if "STR_CAT_" in c["body"]) + len(builders)
mentions = len(re.findall(r"STR_CAT_\w+", list_src)) - len(re.findall(r"==\s*StrId::STR_CAT_\w+", list_src))
check(
    declared == mentions,
    f"{mentions} STR_CAT_ declarations in {LIST} but this suite modelled "
    f"{declared}; a row is being declared in a shape it cannot see, and every "
    f"verdict below is over an unknown subset",
)

rows = []
uncategorised = 0
for c in parsed:
    if "STR_CAT_" not in c["body"]:
        uncategorised += 1
        continue  # category-less: persisted and web-exposed, but not on the screen
    field = re.findall(r"&CrossPointSettings::(\w+)", c["body"])
    ids = re.findall(r"StrId::(STR_\w+)", c["body"])
    rows.append(
        {
            "field": field[0] if field else "",
            "id": ids[0] if ids else "?",
        }
    )

# The runtime-built rows read the SD card through getters rather than a member
# pointer, so there is no field to trace; they are disclosed, not skipped in
# silence.
for b in builders:
    rows.append({"field": "", "id": b["id"]})

check(len(rows) >= 30, f"only found {len(rows)} categorised rows; the parser has drifted")

APPS_DIR = "src/apps_local"
apps = sorted(d for d in os.listdir(APPS_DIR) if os.path.isdir(os.path.join(APPS_DIR, d)))
check(len(apps) > 10, f"only found {len(apps)} apps under {APPS_DIR}; the sweep would be vacuous")


def readers(field):
    """Every file under src/ that reads this settings field.

    Deliberately loose. A false POSITIVE here only makes a row look shared and
    pass; a false negative would drop it into the untraceable list, which is
    printed. Both `.field` and `->field` are matched because the settings object
    is reached both ways.
    """
    pattern = rf"(\.|->){field}\b|CrossPointSettings::{field}\b"
    r = subprocess.run(["grep", "-rlE", pattern, "src"], capture_output=True, text=True)
    return sorted(f for f in r.stdout.split() if f not in PLUMBING)


def owning_app(paths):
    """The single app that owns these readers, or None."""
    owners = set()
    for p in paths:
        parts = p.split("/")
        # src/apps_local/<app>/File.cpp -- anything shallower is shared code.
        if len(parts) >= 4 and parts[0] == "src" and parts[1] == "apps_local" and parts[2] in apps:
            owners.add(parts[2])
        else:
            return None  # a reader outside the apps: not one app's setting
    return owners.pop() if len(owners) == 1 else None


untraceable = []
examined = 0
for row in rows:
    if not row["field"]:
        # An ENUM/String row driven by getters rather than a member pointer
        # (STR_FONT_SIZE and the three SD-card-backed builders are these).
        # There is no field to trace readers for.
        untraceable.append(row["id"])
        continue
    who = readers(row["field"])
    if not who:
        # Nothing reads it but the plumbing. That is its own smell, but this
        # suite cannot tell an unused setting from one read through a macro or
        # an alias, so it says so rather than guessing.
        untraceable.append(f"{row['id']} ({row['field']})")
        continue
    examined += 1
    app = owning_app(who)
    check(
        app is None,
        f"{row['id']} ({row['field']}) is read only by src/apps_local/{app}/ "
        f"and renders in the device's global Settings. An app's own option "
        f"belongs on that app's own settings screen; drop the StrId::STR_CAT_* "
        f"from its SettingInfo (it keeps its key and the web settings API) and "
        f"put the control in the app.",
    )

# A clean verdict has to say what it did NOT look at, somewhere a reader sees.
if untraceable:
    print(
        f"SKIP appsettings  {len(untraceable)} of {len(rows)} categorised rows "
        f"have no traceable reader outside the settings plumbing, so ownership "
        f"was NOT checked for them: " + ", ".join(untraceable)
    )
print(
    f"appsettings: {len(parsed)} SettingInfo calls, {uncategorised} category-less, "
    f"{len(rows)} categorised, {examined} ownership-checked"
)
print(f"{CHECKS} checks, {FAILED} failed")
sys.exit(1 if FAILED else 0)
