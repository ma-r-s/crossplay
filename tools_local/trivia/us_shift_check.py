import json, re, statistics, sys
sys.path.insert(0,'tools_local/trivia')
from compare_ratings import load, spearman
# Deliberately conservative: these keywords are US-specific with almost no
# false positives. It UNDERCOUNTS (US TV, brands and local history carry no
# keyword), so the real US share is higher than what this reports -- which is
# the safe direction for the claim being tested.
US = re.compile(r'\b(U\.?S\.?A?\b|United States|America\w*|state of|this state|'
                r'president|congress|senat\w+|governor|NFL|NBA|MLB|Super Bowl|'
                r'World Series|Broadway|Hollywood|Civil War|Confederat\w+|Union|'
                r'Washington|New York|California|Texas|Florida|Chicago|Boston|'
                r'Yankee|Dixie|Wall Street|Pentagon|Supreme Court|'
                r'Thanksgiving|Manhattan|Pulitzer|Grammy|Emmy|Oscar)\b', re.I)
corpus={json.loads(l)['id']: json.loads(l) for l in open('.rate/corpus.jsonl')}
A=load('/Users/mario/Projects/Personal/Code/Xteink/wt/triviadiff/tools_local/trivia/difficulty.tsv')          # old, American-by-default framing
B=load('.rate/intl/claude_intl.tsv')      # same model, international framing
ids=sorted(set(A)&set(B))
us=[i for i in ids if US.search(corpus[i]['q']) or US.search(corpus[i]['a'])]
non=[i for i in ids if i not in set(us)]
print(f"{len(ids)} questions: {len(us)} US-flagged ({100*len(us)/len(ids):.1f}%), {len(non)} not")
for name, grp in (("US-flagged", us), ("not flagged", non)):
    if not grp: continue
    d=[B[i]-A[i] for i in grp]
    print(f"  {name:<12} n={len(grp):3d}  old {statistics.mean(A[i] for i in grp):5.2f}"
          f"  -> intl {statistics.mean(B[i] for i in grp):5.2f}"
          f"   shift {statistics.mean(d):+5.2f}")
print("\nIf the reframing did what it was for, the US shift is clearly more")
print("negative than the non-US shift. A uniform drop would mean it just made")
print("the model harsher across the board, which is not the same thing.")
