#!/usr/bin/env python3
"""Read the flags a player set on a card, and turn them into verdicts.

    python3 tools_local/trivia/collect_flags.py /Volumes/CARD/trivia/pack.dat
    python3 tools_local/trivia/collect_flags.py pack.dat --apply

This is the missing half of the curation loop. docs/apps/trivia-pack-format.md
has always said FLAGGED indices "are read back off the card and merged into
verdicts.tsv"; docs/trivia-curation.md said the device appends to a `flags.txt`.
Neither was true. Nothing in the tree read a card, no `flags.txt` was ever
written by anything, and verdicts.tsv accumulated exactly ONE verdict in its
lifetime. The loop the pack format describes had never run.

So this reads what the device actually writes -- pack.state's FLAGGED bit, and
the reasons queue from tools_local/trivia/reports.py when one is present -- and
emits the `id<TAB>verdict<TAB>reason` lines that build_pack.py and
assemble_pack.py already know how to apply.

--- THE PART THAT REFUSES, AND WHY IT MUST ---

An index means nothing without the pack it indexes, and this tool is the last
place a mistake is cheap. Downstream, a wrong id DELETES A GOOD QUESTION on the
strength of a report about a different one, silently, and the pack simply comes
out a row smaller. So every check here refuses rather than resolves:

* `pack.state` whose length is not the pack's count is STALE -- the pack was
  replaced under it and every FLAGGED byte now describes whichever question
  landed at that offset. PackState::open and TriviaActivity::ensureState both
  compare with `!=` for exactly this reason, and a LONGER file is stale in the
  same way as a shorter one (a rated pack is smaller than the 50,000 it
  replaced, so that is the case that actually happens).
* a manifest whose count disagrees with the pack is about a different pack.
* a reports queue whose header disagrees with the manifest is about a different
  pack. This is the one case the device cannot detect for itself: a replacement
  pack with the SAME count keeps pack.state, and nothing on the device sees it.

--- IDS, AND THE ONE THING THIS CANNOT FIX ---

verdicts.tsv is keyed on a 12-hex id. That id is `sha1` of the normalised clue
(build_pack.py's `norm_key`), so it is derived from text that a repair can
change -- board card #146, and the reason `--manifest` exists. A manifest
published beside a pack records `index<TAB>id` AS IT WAS ON BUILD DAY, which is
the only form of the id that a later repair cannot move.

Without a manifest this tool re-derives, which is right only while the pack's
text is the text its ids were minted from. It says so loudly rather than
quietly, because a re-derived id that misses joins NOTHING and costs a verdict
with no error at all.
"""

import argparse
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_format as PF  # noqa: E402
import reports as RP  # noqa: E402

DEFAULT_VERDICTS = "tools_local/trivia/verdicts.tsv"


class Refused(Exception):
    pass


def rederive(clue):
    """build_pack.py's key, character for character. Not a safe join key; see above."""
    return hashlib.sha1(re.sub(r"[^a-z0-9]", "", clue.lower()).encode()).hexdigest()[:12]


def read_manifest(path):
    """`index<TAB>id` per line. Returns ({index: id}, pack_id or None).

    The `# pack <id>` comment names the build this map is FOR. Without it the
    file is only a list of ids, indistinguishable from another build's -- which
    is exactly how the pack-id check this module's docstring promises came to be
    unenforceable.
    """
    out = {}
    pack_id = None
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if line.startswith("# pack "):
                pack_id = line[len("# pack "):].strip()
                continue
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                raise Refused(f"{path}:{lineno}: expected index<TAB>id")
            try:
                index = int(parts[0])
            except ValueError:
                raise Refused(f"{path}:{lineno}: {parts[0]!r} is not an index")
            if index in out:
                raise Refused(f"{path}:{lineno}: index {index} appears twice")
            out[index] = parts[1]
    return out, pack_id


def existing_ids(path):
    if not os.path.exists(path):
        return set()
    ids = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                ids.add(line.split("\t")[0])
    return ids


def flagged_indices(state_path, count):
    """Every index whose FLAGGED bit is set, with the staleness check that must not be skipped."""
    size = os.path.getsize(state_path)
    if size != count:
        raise Refused(
            f"{state_path} is {size} bytes for a {count}-question pack. "
            "The pack was replaced under this state file, so its flags now "
            "describe whichever questions landed at those offsets. Refusing: "
            "resolving them would delete questions nobody reported."
        )
    with open(state_path, "rb") as f:
        blob = f.read()
    return [i for i, b in enumerate(blob) if b & PF.FLAGGED]


def collect(pack_path, state_path=None, queue_path=None, manifest_path=None):
    """Returns (rows, notes). A row is (id, verdict, reason, index)."""
    pack = PF.open_pack(pack_path)
    count = pack["count"]
    notes = []

    state_path = state_path or PF.state_path(pack_path)
    if not os.path.exists(state_path):
        raise Refused(f"{state_path} does not exist; nothing has been flagged on this card")

    manifest = None
    manifest_pack = None
    if manifest_path:
        manifest, manifest_pack = read_manifest(manifest_path)
        # The manifest describes a pack. If it does not describe THIS one, every
        # id it hands back is about a different question.
        if len(manifest) != count:
            raise Refused(
                f"{manifest_path} has {len(manifest)} entries for a {count}-question "
                "pack. That manifest is for a different build."
            )

    reasons = {}
    if queue_path and os.path.exists(queue_path):
        try:
            queue_pack_id, queue_count, entries = RP.read(queue_path)
        except RP.Refused as err:
            raise Refused(str(err))
        # The binding the device cannot check for itself.
        if queue_count != count:
            raise Refused(
                f"{queue_path} was filed against a {queue_count}-question pack and "
                f"this pack has {count}. Refusing rather than re-labelling: the "
                "indices in it name different questions now."
            )
        # THE CHECK THIS MODULE'S DOCSTRING PROMISES, and which was missing.
        # The count comparison above catches a replacement of a DIFFERENT size;
        # this is the case it cannot see, and the one the docstring calls "the
        # one case the device cannot detect for itself": a replacement pack with
        # the same question count, whose indices name different questions.
        if manifest_pack and queue_pack_id and queue_pack_id != manifest_pack:
            raise Refused(
                f"{queue_path} was filed against pack {queue_pack_id!r} and "
                f"{manifest_path} describes {manifest_pack!r}. Same question count, "
                "different build: every index in that queue names a different "
                "question here. Use that build's own index map."
            )
        if manifest_path and not manifest_pack:
            notes.append(
                f"{os.path.basename(manifest_path)} carries no `# pack` line, so the "
                "queue's pack id could not be checked against it. A same-count "
                "rebuild would be indistinguishable; rebuild the map with a "
                "current manifest.py."
            )
        for index, reason in entries:
            # First reason wins. A player who reports the same question twice is
            # one report, and the first is the one they meant.
            reasons.setdefault(index, reason)
        notes.append(f"queue {os.path.basename(queue_path)}: pack {queue_pack_id or '(unnamed)'}, {len(entries)} report(s)")

    rows = []
    rederived = 0
    for index in flagged_indices(state_path, count):
        item = PF.read_one(pack, index)
        if manifest is not None:
            qid = manifest.get(index)
            if qid is None:
                raise Refused(f"{manifest_path} has no id for index {index}")
        else:
            qid = rederive(item["q"])
            rederived += 1
        reason = reasons.get(index, "none")
        # A verdict of `bad` DELETES the question, so only the reasons that
        # actually ask for that get one. TOO EASY, TOO HARD and THIS IS A US
        # QUESTION are repairs; emitting `bad` for them would throw away a good
        # question for a one-byte defect, silently, on the next build.
        rows.append((qid, "bad" if RP.removes(reason) else "repair", reason, index))

    if rederived:
        notes.append(
            f"{rederived} id(s) RE-DERIVED from clue text because no --manifest was "
            "given. That is correct only while this pack's text is the text its ids "
            "were minted from; a repaired clue yields an id that joins nothing, and "
            "the loss is silent (board #146)."
        )
    return rows, notes


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pack", help="pack.dat, usually on the card")
    ap.add_argument("--state", default=None, help="pack.state (default: beside the pack)")
    ap.add_argument("--queue", default=None, help="reports.dat (default: beside the pack)")
    ap.add_argument("--manifest", default=None, help="index<TAB>id for this build; strongly preferred")
    ap.add_argument("--verdicts", default=DEFAULT_VERDICTS, help="verdicts.tsv to compare against")
    ap.add_argument("--apply", action="store_true", help="append to --verdicts instead of printing")
    a = ap.parse_args(argv)

    queue = a.queue
    if queue is None:
        beside = os.path.join(os.path.dirname(os.path.abspath(a.pack)), "reports.dat")
        queue = beside if os.path.exists(beside) else None

    try:
        rows, notes = collect(a.pack, a.state, queue, a.manifest)
    except Refused as err:
        print(f"REFUSED: {err}", file=sys.stderr)
        return 2

    for note in notes:
        print(f"note: {note}", file=sys.stderr)

    known = existing_ids(a.verdicts)
    fresh = [r for r in rows if r[0] not in known]
    repairs = [r for r in fresh if r[1] != "bad"]
    if repairs:
        print(
            f"note: {len(repairs)} report(s) ask for a REPAIR, not a removal "
            f"({', '.join(sorted({r[2] for r in repairs}))}). They are written with the "
            "verdict `repair`, which no builder applies -- a person has to act on them.",
            file=sys.stderr,
        )
    print(
        f"{len(rows)} flagged, {len(rows) - len(fresh)} already in {a.verdicts}, {len(fresh)} new",
        file=sys.stderr,
    )
    if not fresh:
        return 0

    lines = [f"{qid}\t{verdict}\t{reason}\t# index {index}" for qid, verdict, reason, index in fresh]
    if a.apply:
        needs_nl = os.path.exists(a.verdicts) and os.path.getsize(a.verdicts) and \
            open(a.verdicts, "rb").read()[-1:] != b"\n"
        with open(a.verdicts, "a", encoding="utf-8") as f:
            if needs_nl:
                f.write("\n")
            for line in lines:
                f.write(line + "\n")
        print(f"appended {len(fresh)} to {a.verdicts}", file=sys.stderr)
    else:
        for line in lines:
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
