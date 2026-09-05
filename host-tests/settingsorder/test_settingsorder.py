#!/usr/bin/env python3
"""Every CrossPlay settings row renders below every CrossPoint one."""
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
        print(f"FAIL settingsorder  {msg}")


def upstream(path):
    """Upstream's copy of a file, or '' when upstream has no such file."""
    r = subprocess.run(["git", "show", f"crosspoint/develop:{path}"],
                       capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else ""


LIST = "src/SettingsList.h"
ACT = "src/activities/settings/SettingsActivity.cpp"
list_src = open(LIST).read()
act_src = open(ACT).read()
up_text = upstream(LIST) + upstream(ACT)

if not up_text.strip():
    # Without the remote there is nothing to compare against, and a suite that
    # silently checks nothing is worse than one that says so.
    print("SKIP settingsorder  crosspoint/develop not fetched; nothing to compare against")
    sys.exit(0)


def body_of(func):
    """The text of one function in the activity, brace-balanced."""
    i = act_src.index(func)
    j = act_src.index("{", i)
    depth, k = 0, j
    while k < len(act_src):
        if act_src[k] == "{":
            depth += 1
        elif act_src[k] == "}":
            depth -= 1
            if depth == 0:
                return act_src[j:k]
        k += 1
    raise SystemExit(f"FAIL settingsorder  cannot find the body of {func}")


rebuild = body_of("void SettingsActivity::rebuildSettingsLists()")

# --- what the screen actually shows, in order ------------------------------
#
# Declaration order first (the loop walks getSettingsList and pushes as it
# goes), minus anything the loop holds back, then whatever the function
# appends afterwards, in the order it appends it.
held = set(re.findall(r"forkSystemSettings\.push_back", rebuild))  # presence only
holdback_ids = re.findall(r"setting\.valuePtr == &CrossPointSettings::(\w+)[^;]*?"
                          r"forkSystemSettings", rebuild, re.S)
holdback = set(re.findall(r"&CrossPointSettings::(\w+)",
                          rebuild.split("forkSystemSettings.push_back")[0].split(
                              "STR_CAT_SYSTEM")[-1] if "forkSystemSettings" in rebuild else ""))

declared = []
for m in re.finditer(r"SettingInfo::(?:Toggle|Enum|Value|String|DynamicString)\((.*?)\),\n(?=\s*(?://|Setting|\}|#|$))",
                     list_src, re.S):
    b = m.group(1)
    if "STR_CAT_SYSTEM" not in b:
        continue
    key = re.findall(r'"([^"]+)"', b)
    ids = re.findall(r"StrId::(STR_\w+)", b)
    val = re.findall(r"&CrossPointSettings::(\w+)", b)
    declared.append({"id": ids[0], "key": key[-1] if key else "", "val": val[0] if val else ""})

rendered = [d for d in declared if d["val"] not in holdback]
for m in re.finditer(r"systemSettings\.push_back\(SettingInfo::Action\(StrId::(STR_\w+), SettingAction::(\w+)",
                     rebuild):
    rendered.append({"id": m.group(1), "key": m.group(2), "val": ""})
rendered += [d for d in declared if d["val"] in holdback]

check(len(rendered) >= 8, f"only modelled {len(rendered)} System rows; the parser has drifted")


def is_ours(row):
    """A row is upstream's if upstream's own sources mention it."""
    for token in (row["key"], row["id"], row["val"]):
        if token and token in up_text:
            return False
    return True


ours = [i for i, r in enumerate(rendered) if is_ours(r)]
theirs = [i for i, r in enumerate(rendered) if not is_ours(r)]

check(bool(ours), "found no CrossPlay System rows at all; this suite just checked nothing")
check(bool(theirs), "found no CrossPoint System rows at all; the upstream comparison is broken")

if ours and theirs:
    for i in ours:
        above = [rendered[j]["id"] for j in theirs if j > i]
        check(not above,
              f"{rendered[i]['id']} is a CrossPlay setting but renders at position "
              f"{i + 1} of {len(rendered)}, above CrossPoint's {', '.join(above)}")

# The comment that made this invisible for months: the declaration is not the
# order, so no comment in SettingsList.h may claim a position.
for m in re.finditer(r"//.*\b(?:last|first) in system\b.*", list_src, re.I):
    check("SettingsActivity" in m.group(0),
          f"a comment in {LIST} claims a screen position the declaration does not "
          f"decide: {m.group(0).strip()}")

print(f"{CHECKS} checks, {FAILED} failed")
sys.exit(1 if FAILED else 0)
