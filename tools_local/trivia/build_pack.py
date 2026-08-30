#!/usr/bin/env python3
"""Build the trivia question pack from the Jeopardy clue dataset.

Source: https://github.com/jwolle1/jeopardy_clue_dataset (season TSVs).
Output: a ranked JSONL pack plus a report. See docs/trivia-curation.md.

The device never sees this script; it reads the finished pack from the SD card.
"""
import argparse, collections, csv, hashlib, json, math, os, re, sys

csv.field_size_limit(10**7)

# --- clue text hygiene -------------------------------------------------------
def clean(s):
    s = (s or '').strip()
    s = s.replace('\\"', '"').replace("\\'", "'").replace('\\\\', '\\')
    s = re.sub(r'<[^>]+>', '', s)
    s = re.sub(r'\s+', ' ', s).strip()
    if len(s) > 1 and s[0] == '"' and s[-1] == '"' and s.count('"') == 2:
        s = s[1:-1]
    return s.strip()

def norm(s):
    """Space-preserving: for tokenising and for frequency keys."""
    return re.sub(r'[^a-z0-9 ]', '', (s or '').lower()).strip()

def norm_key(s):
    """Aggressive: spaces gone too, so spacing variants collide for dedupe."""
    return re.sub(r'[^a-z0-9]', '', (s or '').lower())

def unshout(s):
    """Jeopardy stores some answers in caps. str.title() breaks apostrophes
    ("PAUL'S" -> "Paul'S"), so capitalise word by word instead."""
    letters = [c for c in s if c.isalpha()]
    if len(letters) <= 3 or not all(c.isupper() for c in letters):
        return s
    return ' '.join(w[:1] + w[1:].lower() for w in s.split())

# --- answers carry their own alternates, in parentheses and after "or" -------
# "(Luther) Burbank"  -> Burbank            + Luther Burbank
# "spores (or seeds)" -> spores             + seeds
# "the haiku"         -> haiku              + the haiku
def parse_answer(raw):
    a = unshout(clean(raw))
    accepts = set()
    m = re.match(r'^\((.+?)\)\s*(.+)$', a)          # leading optional forename
    if m:
        primary = m.group(2).strip()
        accepts.add(f"{m.group(1).strip()} {primary}")
    else:
        m = re.match(r'^(.+?)\s*\(\s*or\s+(.+?)\s*\)$', a, re.I)   # explicit "or"
        if m:
            primary = m.group(1).strip()
            accepts.add(m.group(2).strip())
        else:
            m = re.match(r'^(.+?)\s*\((.+?)\)$', a)                # trailing gloss
            if m:
                primary = m.group(1).strip()
                accepts.add(f"{primary} {m.group(2).strip()}")
            else:
                primary = a
    for alt in re.split(r'\s+or\s+', primary):                     # bare "X or Y"
        if alt.strip() and alt.strip() != primary:
            accepts.add(alt.strip())
    stripped = re.sub(r'^(a|an|the)\s+', '', primary, flags=re.I).strip()
    if stripped and stripped != primary:
        accepts.add(primary)
        primary = stripped
    accepts.discard(primary)
    return primary, sorted(x for x in accepts if x and len(x) <= 40)

# --- Jeopardy's on-screen shorthand reads badly aloud -----------------------
# The clues were written to be READ on a screen while Alex spoke them. At a bar
# somebody reads them out, and "ran off to Switz." is not a sentence.
ABBREV = [
    (re.compile(r"\bSwitz\."), "Switzerland"), (re.compile(r"\bint'l\b", re.I), "international"),
    (re.compile(r"\bAmer\."), "American"),     (re.compile(r"\bBrit\."), "British"),
    (re.compile(r"\bGov\."), "Governor"),      (re.compile(r"\bPres\."), "President"),
    (re.compile(r"\bSen\."), "Senator"),       (re.compile(r"\bGen\."), "General"),
    (re.compile(r"\bCapt\."), "Captain"),      (re.compile(r"\bCol\."), "Colonel"),
    (re.compile(r"\bLt\."), "Lieutenant"),     (re.compile(r"\bSgt\."), "Sergeant"),
    (re.compile(r"\bMt\."), "Mount"),          (re.compile(r"\bMts\."), "Mountains"),
    (re.compile(r"\bFt\."), "Fort"),           (re.compile(r"\bSt\.(?=\s[A-Z])"), "Saint"),
    (re.compile(r"\bN\.(?=\s[A-Z])"), "North"), (re.compile(r"\bS\.(?=\s[A-Z])"), "South"),
    (re.compile(r"\bE\.(?=\s[A-Z])"), "East"),  (re.compile(r"\bW\.(?=\s[A-Z])"), "West"),
    (re.compile(r"\bcent\.(?=\s|$)"), "century"), (re.compile(r"\bpop\.(?=\s\d)"), "population"),
    (re.compile(r"\bmil\.(?=[\s,;.]|$)"), "million"), (re.compile(r"\bbil\.(?=[\s,;.]|$)"), "billion"),
    (re.compile(r"\byrs\."), "years"),         (re.compile(r"\bc\.\s*(?=\d)"), "circa "),
    (re.compile(r"\bno\.\s*(?=\d)", re.I), "number "),
]

def expand_abbrev(s):
    for rx, full in ABBREV:
        s = rx.sub(full, s)
    return re.sub(r'\s+', ' ', s).strip()

# --- rejection rules ---------------------------------------------------------
EVENT = re.compile(r'(Teen|College|Kids|Celebrity|Tournament of Champions|Battle of the Decades'
                   r'|All-Star|Masters|Professors|Teachers|Back to School|Seniors|Armed Forces'
                   r'|Million Dollar|Ultimate|IBM|Watson|Second Chance|Champions Wildcard)', re.I)
CONSTRAINT = re.compile(r'(will begin with|begin with that letter|identify the|all of the'
                        r'|each correct response|notice the spelling|rhym|anagram|hidden|alphabet)', re.I)
DEMO = re.compile(r'\b(this|these|his|her|its|hers|he|she|they|it)\b', re.I)
ASIDE = re.compile(r"^\s*\(|\bI'm [A-Z]\w+.{0,40}\bhere\b")
MEDIA = re.compile(r'(seen here|heard here|shown here|pictured|this video|audio clue'
                   r'|on your monitor|\[|depicted|this logo|this map)', re.I)
WORDPLAY = re.compile(r'("|\bLETTER|\bRHYM|\bANAGRAM|\bSPELL|\bBEGIN|\bSTART|\bEND(S|ING)?\b'
                      r'|\bWORD|\b[A-Z]\.\s?[A-Z]\.|^[A-Z]{1,3}$|_|\bHIDDEN\b|\bMISSING\b)')
ADULT = re.compile(r'\b(sex|sexual|porn|nude|naked|erotic|prostitut|rape|masturbat|slur)\b', re.I)
# clues whose truth moved on, or whose subject no longer exists
TIMEROT = re.compile(r'\b(current(ly)?|today|nowadays|this year|last year|recent(ly)?|so far'
                     r'|to date|still|now the|newest|latest|upcoming|next year'
                     r'|Yugoslav\w*|Soviet Union|U\.S\.S\.R|Czechoslovakia|Zaire|Burma'
                     r'|East Germany|West Germany|Rhodesia)\b', re.I)
SELFREF = re.compile(r'\b(on our show|here on Jeopardy|our staff|this program|Alex\b|Trebek'
                     r'|Ken Jennings|our home viewer|contestant)\b', re.I)

def difficulty(row):
    v = row['clue_value']
    if not v or not v.isdigit() or int(v) == 0:
        return None
    v = int(v)
    if row['air_date'] < '2001-11-26':      # values doubled 2001-11-26
        v *= 2
    if row['round'] == '2':                 # Double Jeopardy -> round 1 scale
        v //= 2
    if row['round'] == '3':
        return 5                            # Final Jeopardy
    return {200: 1, 400: 2, 600: 3, 800: 4, 1000: 5}.get(v)

def leaks_answer(clue, primary):
    """The clue contains the answer, so it answers itself."""
    toks = [t for t in norm(primary).split() if len(t) > 3]
    if not toks:
        return False
    c = norm(clue)
    return all(re.search(rf'\b{re.escape(t)}', c) for t in toks)

def build(src, keep_events=False):
    rows = list(csv.DictReader(open(src, encoding='utf-8'), delimiter='\t'))
    ansfreq = collections.Counter(norm(r['question']) for r in rows)
    catfreq = collections.Counter(clean(r['category']) for r in rows)

    drop = collections.Counter()
    pack, seen = [], set()
    for r in rows:
        clue = expand_abbrev(clean(r['answer']))   # NB: dataset columns are inverted
        raw_a = r['question']
        cat = clean(r['category'])
        note, com = r['notes'] or '', r['comments'] or ''

        if not clue or not clean(raw_a):    drop['empty'] += 1; continue
        if ASIDE.search(clue):              drop['presenter aside'] += 1; continue
        if MEDIA.search(clue):              drop['media-dependent'] += 1; continue
        if SELFREF.search(clue):            drop['references the show'] += 1; continue
        if CONSTRAINT.search(com):          drop['show-declared constraint'] += 1; continue
        if WORDPLAY.search(cat):            drop['wordplay category'] += 1; continue
        if not DEMO.search(clue):           drop['category-dependent'] += 1; continue
        if TIMEROT.search(clue):            drop['time-rotted'] += 1; continue
        if ADULT.search(clue):              drop['adult'] += 1; continue
        if not (30 <= len(clue) <= 140):    drop['clue length'] += 1; continue
        if clue.count('"') % 2 or clue.count('(') != clue.count(')'):
            drop['unbalanced punctuation'] += 1; continue

        ev = EVENT.search(note)
        if ev and not keep_events:          drop['special event'] += 1; continue

        primary, accepts = parse_answer(raw_a)
        if not (0 < len(primary) <= 25):    drop['answer length'] += 1; continue
        if leaks_answer(clue, primary):     drop['clue leaks the answer'] += 1; continue

        d = difficulty(r)
        if d is None:                       drop['no difficulty'] += 1; continue

        k = norm_key(clue)
        if k in seen:                       drop['duplicate'] += 1; continue
        seen.add(k)

        af, cf = ansfreq[norm(raw_a)], catfreq[cat]
        score = round(2.0 * math.log1p(af) + math.log1p(cf) - (0.6 if ev else 0), 3)
        item = {'id': hashlib.sha1(k.encode()).hexdigest()[:12],
                'q': clue, 'a': primary, 'd': d, 'y': int(r['air_date'][:4]), 'g': score}
        if accepts:
            item['alt'] = accepts
        if ev:
            item['ev'] = ev.group(1).title()
        pack.append(item)

    pack.sort(key=lambda x: (-x['g'], x['id']))
    return pack, drop, len(rows)

def apply_verdicts(pack, path):
    if not path or not os.path.exists(path):
        return pack, 0, 0
    bad, good = set(), set()
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) < 2:
            continue
        (bad if parts[1].strip() == 'bad' else good).add(parts[0].strip())
    out = [x for x in pack if x['id'] not in bad]
    for x in out:
        if x['id'] in good:
            x['g'] += 100          # pin
    out.sort(key=lambda x: (-x['g'], x['id']))
    return out, len(bad), len(good)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='combined_season*.tsv')
    ap.add_argument('--out', required=True)
    ap.add_argument('--limit', type=int, default=0, help='keep only the top N by score')
    ap.add_argument('--verdicts', default='')
    ap.add_argument('--keep-events', action='store_true')
    a = ap.parse_args()

    pack, drop, total = build(a.src, a.keep_events)
    pack, n_bad, n_good = apply_verdicts(pack, a.verdicts)
    if a.limit:
        pack = pack[:a.limit]

    with open(a.out, 'w', encoding='utf-8') as f:
        for x in pack:
            f.write(json.dumps(x, ensure_ascii=False, separators=(',', ':')) + '\n')

    print(f"source clues      : {total:,}")
    for k, v in drop.most_common():
        print(f"  dropped {v:>7,}  {k}")
    if n_bad or n_good:
        print(f"  verdicts          : {n_bad} bad, {n_good} pinned")
    print(f"\npack              : {len(pack):,}")
    print(f"  with alternates : {sum(1 for x in pack if x.get('alt')):,}")
    print(f"  difficulty      : {dict(sorted(collections.Counter(x['d'] for x in pack).items()))}")
    print(f"  size            : {os.path.getsize(a.out)/1e6:.1f} MB")

if __name__ == '__main__':
    main()
