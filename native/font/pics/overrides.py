"""Lines whose reading was settled by a person (the review page) or by the confirmed vocabulary: per picture,
the line with its text, lined up with the shapes found (shapes left over are not lettering and stay)."""
import pickle, json, glob, re, sys, collections, difflib; sys.path.insert(0, '.')
from ocr import finalize2, nrm
S = pickle.load(open('scan2.pkl', 'rb')); sl = {s['h_']: s for s in S}
cat = pickle.load(open('cat.pkl', 'rb')); byh = {}
for r in cat: byh.setdefault(r['h_'], r)
keep, auto = pickle.load(open('human.pkl', 'rb'))
LOOK = str.maketrans({'0': 'O', '1': 'I', 'l': 'I', 'i': 'I', 'J': 'I', 'j': 'I', '5': 'S', '8': 'B', '2': 'Z', '|': 'I'})
def norm(s): return s.upper().translate(LOOK)
def align(ans, ours):
    """indices of ours (box per char) for each char of ans, or None"""
    a = ans.replace(' ', '')
    if len(a) == len(ours): return list(range(len(a)))
    if len(a) > len(ours): return None
    sm = difflib.SequenceMatcher(None, norm(ours), norm(a), autojunk=False)
    idx = [None] * len(a)
    for blk in sm.get_matching_blocks():
        for k in range(blk.size): idx[blk.b + k] = blk.a + k
    return idx if all(i is not None for i in idx) else None
# the vocabulary's spelling (case) for each word
forms = collections.defaultdict(collections.Counter)
for s in S:
    p = byh[s['h_']]['pic']
    for l in s['lines']:
        f = finalize2(l, p)
        if f: forms[f['text'].upper()][f['text']] += 1
# what each group reads: the person's answer, or the vocabulary's word
G = pickle.load(open('human_groups.pkl', 'rb'))
answers = {}   # answers.json: what a person read on the review page, per group (null: not lettering)
for d in json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__), 'answers.json'))):
    if d['group'] in G: answers[G[d['group']]] = d['reads']
settled = []
for it, l, t in keep:
    if (t, it['text']) in answers and answers[(t, it['text'])]: settled.append((it, answers[(t, it['text'])], 'person'))
for it, t in auto:
    # words only (a number "confirmed elsewhere" says nothing about this one, and loses its decimal point), in
    # the casing seen here if the vocabulary has it
    if sum(c.isalpha() for c in t) < 2: continue
    # only a word that really is in the vocabulary (confirmed elsewhere); where the shapes' word isn't but
    # Tesseract's is (the shapes took R for B, H or P, K for R), Tesseract's
    w = t if t.upper() in forms else it['tess'] if (it['tess'] or '').upper() in forms else None
    if w is None: continue
    f = forms[w.upper()]
    settled.append((it, w if w in f else f.most_common(1)[0][0], 'vocabulary'))
out = collections.defaultdict(list); stats = collections.Counter()
for it, text, how in settled:
    for fn, res, h in it['uses']:
        for l in sl[h]['lines']:
            if l['text'] != it['text'] or l.get('tess') != it['tess']: continue
            ours = ''.join(b[4] for b in l['boxes'])
            chars = text.replace(' ', '')
            if how == 'vocabulary':
                # the vocabulary's word through the usual rules (case from positions, decimal points), and only
                # if it differs from what the shapes looked like in one character at most
                diff = [(x, y) for x, y in zip(chars, ours) if norm(x) != norm(y)]
                if len(chars) != len(ours) or len(diff) > 1 or any(x.isdigit() or y.isdigit() for x, y in diff):
                    stats[how + ' no fit'] += 1; continue
                f = finalize2(dict(l, tess=chars), byh[h]['pic'], force=True)
                if not f: stats[how + ' no fit'] += 1; continue
                out[h].append(dict(f, settled=how, orig=l['text'])); stats[how] += 1; continue
            idx = align(text, ours)
            # a person's answer: only where it fits what the shapes looked like (otherwise the shapes found
            # aren't its letters, e.g. lettering cut out of a box)
            if idx is None or difflib.SequenceMatcher(None, norm(ours), norm(chars), autojunk=False).ratio() < 0.5:
                stats[how + ' no fit'] += 1; continue
            boxes = [tuple(l['boxes'][i][:4]) + (c, 1.0) for c, i in zip(chars, idx)]
            out[h].append(dict(l, boxes=boxes, text=chars, settled=how, orig=l['text']))
            stats[how] += 1
# pictures whose lettering is known for sure but the scan missed or misread: the menu's page numbers (the
# current page's, with its underline, and the dimmer others), whose shapes are plain to find
from ocr import components
C = 'image_cmn_43.uxc'
KNOWN = {(C, 0x205cb24): '4'}
KNOWN.update({(C, 0x205cb2f + i): str(i + 1) for i in range(10)})
for r in cat:
    text = KNOWN.get((r['fn'], r['res']))
    if text is None: continue
    cs = sorted((c for c in components(r['pic'], True) if c['y1'] - c['y0'] >= 12), key=lambda c: c['x0'])
    if len(cs) != len(text): stats['known no fit'] += 1; continue
    h = r['h_']
    sc = [l for l in sl[h]['lines'] if h in sl] if h in sl else []
    l0 = sc[0] if len(sc) == 1 else {}
    boxes = [(c['x0'], c['y0'], c['x1'], c['y1'], ch, 1.0) for c, ch in zip(cs, text)]
    out[h] = [l for l in out.get(h, []) if l.get('orig') != l0.get('text')] + [dict(text=text, score=1.0, light=True,
        H=max(c['y1'] - c['y0'] for c in cs), boxes=boxes, tess=l0.get('tess', text), sim=1.0, settled='known',
        orig=l0.get('text', ''))]
    stats['known'] += 1
# the small creative style values (contrast, saturation, sharpness on the Quick Navi screen): -3 .. +3; the
# sign is one or two shapes (the plus and the bar of a ±) left of the digit
SIGNED = {(C, 0x205c08d + i): t for i, t in enumerate(['-3', '-2', '-1', '\u00b10', '+1', '+2', '+3'])}
for r in cat:
    text = SIGNED.get((r['fn'], r['res']))
    if text is None: continue
    cs = [c for c in components(r['pic'], True) if c['area'] >= 2]
    dg = max(cs, key=lambda c: c['y1'] - c['y0']); sg = [c for c in cs if c is not dg and c['x1'] <= dg['x0']]
    if not sg or len(sg) + 1 != len(cs): stats['signed no fit'] += 1; continue
    sb = (min(c['x0'] for c in sg), min(c['y0'] for c in sg), max(c['x1'] for c in sg), max(c['y1'] for c in sg))
    boxes = [sb + (text[0], 1.0), (dg['x0'], dg['y0'], dg['x1'], dg['y1'], text[1], 1.0)]
    mbox = [(c['x0'], c['y0'], c['x1'], c['y1']) for c in cs]
    out[r['h_']] = [dict(text=text, score=1.0, light=True, H=dg['y1'] - dg['y0'], boxes=boxes, mbox=mbox, tess=text,
                         sim=1.0, settled='known', orig='', exclusive=True)]
    stats['known'] += 1

# OFF and AUTO beside an icon or a badge (the microphone, D-R, ...): whole words on their own, so what's next to
# them (or read into the line) doesn't hold them back. The line's last shapes, as tall as each other, read the word.
WORDS = ('OFF', 'AUTO', 'USB')
for h, s_ in sl.items():
    if h not in byh: continue
    have = {(l.get('tess'), l.get('orig')) for l in out.get(h, [])}
    for l in s_['lines']:
        bx = l['boxes']; t = (l.get('tess') or '').upper()
        if not l['light'] or (l.get('tess'), l['text']) in have: continue
        done = False
        for w in WORDS:
            n = len(w)
            # the word at the end of the line (an icon read into its start) or at its start (a bar read as '-')
            for ws, txt in ((bx[-n:], l['text'][-n:]), (bx[:n], l['text'][:n])):
                if len(bx) < n or txt.upper().replace('0', 'O').replace('D', 'O') != w.replace('0', 'O'): continue
                if not (w[:2] in t or w[-2:] in t): continue
                hs = [b[3] - b[1] for b in ws]
                if max(hs) > 1.2 * min(hs) or any(ws[i + 1][0] - ws[i][2] > 0.5 * hs[0] for i in range(n - 1)): continue
                boxes = [tuple(b[:4]) + (ch, 1.0) for b, ch in zip(ws, w)]
                out[h].append(dict(l, boxes=boxes, text=w, settled='word', orig=l['text'], free=True)); stats['word ' + w] += 1
                done = True; break
            if done: break

import badges
ISOVALUES = set('50 64 80 100 125 160 200 250 320 400 500 640 800 1000 1250 1600 2000 2500 3200 4000 5000 6400 8000 10000 12800 16000 20000 25600 32000 40000 51200 64000 80000 102400 128000 160000 204800 256000 320000 409600'.split())
# whole numbers Tesseract reads where the shapes lost a digit (ISO values: '100' read as '00')
for h, s_ in sl.items():
    if h not in byh: continue
    have = {(l.get('tess'), l.get('orig')) for l in out.get(h, [])}
    if any(l.get('exclusive') for l in out.get(h, [])): continue
    for l in s_['lines']:
        if (l.get('tess'), l['text']) in have: continue
        t = (l.get('tess') or '').replace('O', '0').replace('o', '0')
        if t not in ISOVALUES: continue          # ISO values only (not 5.6 read as 56, nor the 250 of 1/250)
        n = badges.number_line(byh[h]['pic'], l)
        if n: out[h].append(n); stats['number'] += 1
    # an ISO beside a number, that the scan missed altogether
    isov = lambda t: (t or '').replace('O', '0').replace('o', '0') in ISOVALUES
    if any(isov(l.get('tess')) or isov(l['text']) for l in s_['lines'] + out.get(h, [])) and not any('ISO' in (l.get('tess') or '') + l['text'] for l in s_['lines']):
        il = badges.iso_line(byh[h]['pic'], s_['lines'] + out[h])
        if il: out[h].append(il); stats['word ISO'] += 1

# the D-R / DRO / HDR / ISO boxes (and the HI beside an ISO box)
for h, s_ in sl.items():
    if h not in byh: continue
    hl = badges.hi_line(byh[h]['pic'], s_['lines'])
    if hl and not any(l.get('text') == 'HI' for l in out.get(h, [])): out[h].append(hl); stats['word HI'] += 1
    for L in badges.box_lines(byh[h]['pic'], s_['lines']):
        out[h].append(L); stats['box ' + L['text']] += 1

# the creative style badges: made from their shapes (badges.py), in place of anything else for that picture
import badges
# the smallest dimmed set (textured letters packed on a light box) doesn't redraw cleanly: left as it is
DIMMED = {('image_cmn_43.uxc', 0x205c080 + i) for i in range(13)}
for h, s_ in sl.items():
    if h not in byh: continue
    if (byh[h]['fn'], byh[h]['res']) in DIMMED: out[h] = [dict(exclusive=True, settled='leave')]; continue
    L = badges.lines_for(byh[h]['pic'], s_['lines'])
    if L: out[h] = L; stats['badge'] += 1
print(stats, 'pictures', len(out))
pickle.dump(dict(out), open('overrides.pkl', 'wb'))
