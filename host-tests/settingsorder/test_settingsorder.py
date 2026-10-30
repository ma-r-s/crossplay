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


LIST = "src/SettingsList.cpp"
ACT = "src/activities/settings/SettingsActivity.cpp"
list_src = open(LIST).read()
act_src = open(ACT).read()
# The commit this fork and upstream last had in common. Everything ownership
# is decided against is read out of THIS, never out of a branch tip.
_mb = subprocess.run(["git", "merge-base", "HEAD", "crosspoint/develop"],
                     capture_output=True, text=True)
MERGE_BASE = _mb.stdout.strip() if _mb.returncode == 0 else ""

if not MERGE_BASE:
    # TWO DIFFERENT SITUATIONS, and they must not print the same sentence.
    #
    # No remote at all is a developer who has not fetched upstream: nothing to
    # compare against, so skip and say so.
    #
    # A remote that EXISTS but yields no merge base is a shallow fetch, and it
    # is not benign. It happens only where this suite is configured to run --
    # CI -- and it is the one environment where the check does real work. A
    # `--depth=1` fetch of upstream did exactly this: the suite printed
    # "not fetched" about a ref that was right there, exited 0, and tested
    # nothing on the only machine that runs it. Silence there buys nothing and
    # hides a suite, so it is a failure with the command that fixes it.
    _have = subprocess.run(["git", "rev-parse", "--verify", "-q",
                            "crosspoint/develop"], capture_output=True, text=True)
    if _have.returncode != 0:
        print("SKIP settingsorder  crosspoint/develop not fetched; nothing to "
              "compare against (git fetch crosspoint develop)")
        sys.exit(0)
    print("FAIL settingsorder  crosspoint/develop is fetched but too shallow to "
          "share history with HEAD, so ownership cannot be decided and every "
          "check below would be vacuous. Deepen it: "
          "git fetch --no-tags --shallow-since='6 months ago' "
          "https://github.com/crosspoint-reader/crosspoint-reader.git "
          "develop:refs/remotes/crosspoint/develop")
    print("1 checks, 1 failed")
    sys.exit(1)


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


rebuild = body_of("void SettingsActivity::rebuildSettings()")
section_for = body_of("SettingsSection sectionFor(const SettingInfo& setting) {")
fork_rows = body_of("bool isForkGeneralSetting(const SettingInfo& setting)")

# --- what the General screen actually shows, in order -----------------------
#
# rebuildSettings() builds one section at a time. General is the device-wide
# one, where CrossPlay's own rows live. Its order is: the General actions added
# before the loop, then the shared list's System rows in declaration order
# (minus the ones the loop holds back), then the General actions added after
# the loop, then whatever the second walk over the list appends.
holdback = set(re.findall(r"&CrossPointSettings::(\w+)", fork_rows))
# sectionFor() routes these System rows to another section (Library, Display).
elsewhere = set(re.findall(r"&CrossPointSettings::(\w+)", section_for))

declared = []
for m in re.finditer(r"emit\(SettingInfo::(?:Toggle|Enum|Value|String|DynamicString)\((.*?)\)\);", list_src, re.S):
    b = m.group(1)
    if "STR_CAT_SYSTEM" not in b:
        continue
    key = re.findall(r'"([^"]+)"', b)
    ids = re.findall(r"StrId::(STR_\w+)", b)
    val = re.findall(r"&CrossPointSettings::(\w+)", b)
    row = {"id": ids[0], "key": key[-1] if key else "", "val": val[0] if val else ""}
    if row["val"] not in elsewhere:
        declared.append(row)

def loop_body(at):
    """The brace-balanced body of the for-loop starting at offset `at`."""
    j = rebuild.index("{", at)
    depth, k = 0, j
    while k < len(rebuild):
        depth += {"{": 1, "}": -1}.get(rebuild[k], 0)
        if depth == 0:
            return rebuild[j:k]
        k += 1
    return ""


rendered = []
walks = 0
for m in re.finditer(r"addAction\(SettingsSection::General, StrId::(STR_\w+), SettingAction::(\w+)\)"
                     r"|for \(auto& setting : all\)", rebuild):
    if m.group(1):
        rendered.append({"id": m.group(1), "key": m.group(2), "val": ""})
        continue
    walks += 1
    # Read what each walk DOES, not what the helper says: a first walk that
    # stopped skipping the fork rows would render them in declaration order.
    body = loop_body(m.end())
    if walks == 1:
        held = holdback if re.search(r"isForkGeneralSetting\(setting\)\)\s*continue", body) else set()
        rendered += [d for d in declared if d["val"] not in held]
    elif walks == 2 and "isForkGeneralSetting(setting)" in body:
        rendered += [d for d in declared if d["val"] in holdback]

check(walks == 2, f"expected two walks over the shared list in rebuildSettings(), found {walks}; "
      "the parser has drifted")
check(len(rendered) >= 8, f"only modelled {len(rendered)} General rows; the parser has drifted")


_UP_SEEN = {}


def upstream_mentions(token):
    """Did the code this fork INHERITED mention this token?

    Asked of the MERGE BASE, and of its whole src/ and lib/ rather than of
    two files. Both halves of that were wrong before and each produced the
    same false verdict: a row the fork merely inherited reported as one the
    fork added, so the suite demanded upstream's own row sort below upstream's
    own rows, which cannot be done.

    THE BASE, NOT THE TIP. Ownership is a fact about our history: a row absent
    from the common ancestor and present here is ours, and nothing upstream
    does later can change that. Read off the tip instead, the answer moves
    when upstream moves -- STR_LIBRARY_REBUILD arrived in their Library view
    (their #3366, 2026-09-14), they later moved the rebuild action out of
    Settings and onto the Library screen, and the row flipped to "ours"
    without a line changing here. The base only moves when WE sync, which is
    when a re-classification is legitimate and reviewable.

    That is the mechanism behind notice n14 as well, and it is why this went
    unseen: the verdict depended on how recently the tree had fetched
    upstream. A local ref from two days earlier still had the row and the
    suite passed; CI fetches fresh every run and failed. Nothing was skipped
    and no local gate could have caught it.

    THE WHOLE TREE, NOT THE TWO SETTINGS FILES. Where upstream keeps a string
    is not a statement about who owns it; theirs lives in HomeActivity.cpp,
    LibraryListActivity.cpp and every translations yaml.
    """
    if token not in _UP_SEEN:
        r = subprocess.run(
            ["git", "grep", "-q", "-F", token, MERGE_BASE, "--", "src", "lib"],
            capture_output=True, text=True)
        _UP_SEEN[token] = (r.returncode == 0)
    return _UP_SEEN[token]


def is_ours(row):
    """A row is upstream's if upstream's own sources mention it."""
    for token in (row["key"], row["id"], row["val"]):
        if token and upstream_mentions(token):
            return False
    return True


ours = [i for i, r in enumerate(rendered) if is_ours(r)]
theirs = [i for i, r in enumerate(rendered) if not is_ours(r)]

check(bool(ours), "found no CrossPlay General rows at all; this suite just checked nothing")
check(bool(theirs), "found no CrossPoint General rows at all; the upstream comparison is broken")

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
