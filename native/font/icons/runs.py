import sys as _sys
_HELP = """usage: python3 runs.py          (step 3 of 5, see README)
Keeps only the character guesses in shapes.pkl that stand in a line of text (or are unmistakable on their
own), and marks those as letters in shapes.pkl.

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
# keeps the character guesses that sit in a line of text (or are unmistakable on their own)
import pickle, collections
shapes, groups = pickle.load(open('shapes.pkl', 'rb'))
byg = collections.defaultdict(list)
for i, s in enumerate(shapes): byg[s['glyph']].append(i)
AMBIG = set('Il1iO0oj')
for g, ids in byg.items():
    local = {shapes[i]['ci']: i for i in ids}
    def par(s): return shapes[local[s['parent']]] if s['parent'] is not None else None
    cand = []
    for i in ids:
        s = shapes[i]; s['letter'] = False
        if s['score'] < 0.70 or not s['guess'].isalnum(): continue
        p = par(s)
        if p is not None and p['score'] >= 0.70 and p['h'] < 1.6 * s['h'] / 0.6: continue     # a letter's counter
        if s['sign'] > 0 and p is not None and p['sign'] > 0: continue
        if s['sign'] < 0 and (p is None or p['sign'] < 0 or p['h'] < 1.08 * s['h']): continue
        cand.append(i)
    parent = {i: i for i in cand}
    def find(a):
        while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for a in cand:
        for b in cand:
            if a >= b: continue
            A, B = shapes[a], shapes[b]
            if A['sign'] != B['sign']: continue
            H = max(A['h'], B['h']); r = A['h'] / max(B['h'], 1)
            if not (0.45 < r < 2.2) or abs(A['bb'][1] - B['bb'][1]) > 0.10 * H: continue
            gap = max(A['bb'][0], B['bb'][0]) - min(A['bb'][2], B['bb'][2])
            if gap > 0.75 * H or gap < -0.15 * H: continue
            parent[find(a)] = find(b)
    runs = collections.defaultdict(list)
    for i in cand: runs[find(i)].append(i)
    for r in runs.values():
        conf = [i for i in r if shapes[i]['score'] >= 0.80 and shapes[i]['npts'] > 4]
        for i in r:
            s = shapes[i]
            others = [j for j in conf if j != i]
            if s['npts'] <= 4:
                # a plain bar: an I when it stands as tall as a confident capital next to it
                if others and s['asp'] < 0.4 and any(abs(shapes[j]['h'] - s['h']) <= 0.05 * s['h'] for j in others):
                    s['letter'] = True; s['guess'] = 'I'
            elif others and s['score'] >= 0.75:
                s['letter'] = True
            elif s['guess'] not in AMBIG and s['score'] >= 0.85:
                s['letter'] = True
n = sum(1 for s in shapes if s['letter'])
print(n, 'letter shapes in', len({s['glyph'] for s in shapes if s['letter']}), 'icons')
pickle.dump((shapes, groups), open('shapes.pkl', 'wb'))
