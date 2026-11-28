import sys as _sys
_HELP = """usage: python3 classify.py      (step 2 of 5, see README)
Compares each shape in shapes.pkl with the letters and digits of a few reference fonts (Univers from $LTT or
$UNIVERS, plus DejaVu Sans Condensed Bold, Liberation Sans Narrow Bold, Carlito Bold if installed) and stores
its best guess and score in shapes.pkl.

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
# guesses which shapes are letters/digits by comparing them (size-independently) with the same characters in
# a few narrow/bold reference fonts, then keeps only those that sit in a line of text
import os, pickle, numpy as np
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen
from shapes import raster, GW, GH          # same rasteriser
shapes, groups = pickle.load(open('shapes.pkl', 'rb'))

class Flat(BasePen):
    def __init__(s, gs): super().__init__(gs); s.polys = []; s.cur = []
    def _moveTo(s, p): s.cur = [p]
    def _lineTo(s, p): s.cur.append(p)
    def _curveToOne(s, a, b, c):
        p0 = s.cur[-1]
        for i in range(1, 7):
            t = i / 6; s.cur.append(tuple((1-t)**3*p0[k] + 3*(1-t)**2*t*a[k] + 3*(1-t)*t*t*b[k] + t**3*c[k] for k in (0, 1)))
    def _qCurveToOne(s, a, b):
        p0 = s.cur[-1]
        for i in range(1, 7):
            t = i / 6; s.cur.append(tuple((1-t)**2*p0[k] + 2*(1-t)*t*a[k] + t*t*b[k] for k in (0, 1)))
    def _closePath(s): s.polys.append(s.cur); s.cur = []
    _endPath = _closePath
def char_polys(font, ch):
    cm = font.getBestCmap()
    if ord(ch) not in cm: return None
    gs = font.getGlyphSet(); p = Flat(gs); gs[cm[ord(ch)]].draw(p)
    return [q for q in p.polys if len(q) > 2]
REFS = [os.environ.get('UNIVERS', ''),
        '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf',
        '/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf',
        '/usr/share/fonts/truetype/crosextra/Carlito-Bold.ttf']
CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/+%()&?!'
refs = []    # (char, raster, aspect)
import io
fonts = [TTFont(p) for p in REFS if p and os.path.exists(p)]
if os.environ.get('LTT'):          # Univers, as embedded in the camera's UniversOTS-SJ_wIcon.ltt
    fonts.insert(0, TTFont(io.BytesIO(open(os.environ['LTT'], 'rb').read()[53808:53808 + 377056])))
for fnt in fonts:
    for ch in CHARS:
        ps = char_polys(fnt, ch)
        if not ps: continue
        xs = [x for p in ps for x, _ in p]; ys = [y for p in ps for _, y in p]
        bb = (min(xs), min(ys), max(xs), max(ys))
        refs.append((ch, raster(ps, bb), (bb[2] - bb[0]) / max(1, bb[3] - bb[1])))
R = np.array([r[1].ravel() for r in refs], dtype=np.float32)
for s in shapes:
    v = s['ras'].ravel().astype(np.float32)
    inter = R @ v; union = R.sum(1) + v.sum() - inter
    iou = inter / np.maximum(union, 1)
    # aspect matters little (letters are condensed), but not nothing
    asp = np.array([r[2] for r in refs]); pen = np.clip(np.abs(np.log(np.maximum(s['asp'], .05) / np.maximum(asp, .05))) - 0.35, 0, None)
    sc = iou - 0.5 * pen
    best = {}
    for i in np.argsort(-sc)[:12]:
        ch = refs[i][0]
        if ch not in best: best[ch] = sc[i]
    order = sorted(best.items(), key=lambda kv: -kv[1])
    s['guess'] = order[0][0]; s['score'] = float(order[0][1]); s['alts'] = order[:3]
pickle.dump((shapes, groups), open('shapes.pkl', 'wb'))
import collections
c = collections.Counter(s['guess'] for s in shapes if s['score'] > 0.75)
print(sum(c.values()), 'shapes look like characters (score > 0.75):', ''.join(sorted(c)))
