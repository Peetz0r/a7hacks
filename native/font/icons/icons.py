"""Run after shapes.py, classify.py and runs.py (see README). Builds a same-layout replacement of the lettering inside the icons of Sony_DI_Icons.ttf: every letter or
digit drawn inside an icon is replaced by the same character from Comic Relief Bold, fitted into the box the
original letter took up. Each icon keeps its slot, its byte range in 'glyf', its advance and its left side
bearing (xMin == lsb); the rest of the icon (frames, symbols) keeps its original points."""
import sys as _sys
_HELP = """usage: python3 icons.py         (step 4 of 5, see README)
Replaces each letter found (shapes.pkl, corrected by fixshapes.json) inside the icons of $ICONS (default
Sony_DI_Icons.ttf) by the same character from $COMIC (default ComicRelief-Bold.ttf), each icon keeping its own
bytes. Writes $OUT.ttf (default icons.ttf, to look at), $OUT.patch.json (for ../mkheader.py) and
icons.done.json. Env WIDEN: let letters grow sideways into the gaps (default 1.0).

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
import sys, os, math, json, struct, pickle, collections
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables import ttProgram
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphCoordinates, flagOnCurve
from fontTools.pens.ttGlyphPen import TTGlyphPen
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
SRC = os.environ.get('ICONS', 'Sony_DI_Icons.ttf')
COMIC = os.environ.get('COMIC', 'ComicRelief-Bold.ttf')
OUT = os.environ.get('OUT', 'icons')
WIDEN = float(os.environ.get('WIDEN', '1.0'))      # let letters grow sideways into the gaps (1.0 = original box)

raw = bytearray(open(SRC, 'rb').read())
F = TTFont(SRC); glyf = F['glyf']; loca = F['loca']; hmtx = F['hmtx']; order = F.getGlyphOrder()
n = struct.unpack('>H', raw[4:6])[0]; tabs = {}
for i in range(n):
    tag, cs, off, ln = struct.unpack('>4sIII', raw[12 + 16 * i:28 + 16 * i]); tabs[tag.decode()] = (off, ln)
GLYF = tabs['glyf'][0]
C = TTFont(COMIC); ccmap = C.getBestCmap(); cgs = C.getGlyphSet(); cglyf = C['glyf']

shapes, groups = pickle.load(open('shapes.pkl', 'rb'))
# by-eye corrections of the classifier, per shape ("glyph:contour"; None: not a letter)
FIXS = json.load(open('fixshapes.json'))
byg = collections.defaultdict(list)
for i, s in enumerate(shapes):
    key = '%s:%d' % (s['glyph'], s['ci']); s['fixed'] = key in FIXS
    lab = FIXS[key] if s['fixed'] else (s['guess'] if s['letter'] else None)
    s['lab'] = lab
    if lab: byg[s['glyph']].append(i)
CASEPAIR = set('cosuvwxz')
DIG = set('0123456789')
def same_line(a, b):
    return a['sign'] == b['sign'] and a['parent'] == b['parent'] and \
        abs(a['bb'][1] - b['bb'][1]) <= 0.12 * max(a['h'], b['h'])
# what identical shapes elsewhere were recognised as (by group), for rescuing letters the classifier missed
glab = collections.defaultdict(collections.Counter)
for s in shapes:
    if s['lab']: glab[s['group']][s['lab']] += 1
for g, ids in list(byg.items()):
    allg = [i for i, s in enumerate(shapes) if s['glyph'] == g]
    # rescue: an unlabeled character-like shape in the line of a labeled one
    for i in allg:
        s = shapes[i]
        if s['lab'] or s['fixed'] or not glab[s['group']] or s['npts'] <= 4: continue
        known = glab[s['group']].most_common(1)[0][0]
        if s['parent'] is not None and any(shapes[j]['ci'] == s['parent'] and shapes[j]['lab'] for j in allg): continue
        for j in ids:
            t = shapes[j]
            if same_line(s, t) and 0.6 < s['h'] / t['h'] < 1.6 and \
               max(s['bb'][0], t['bb'][0]) - min(s['bb'][2], t['bb'][2]) < 0.8 * max(s['h'], t['h']):
                s['lab'] = known; ids.append(i); break
    for i in ids:
        s = shapes[i]
        line = [j for j in ids if same_line(shapes[j], s)]
        capH = max(shapes[j]['h'] for j in line)
        digits = sum(1 for j in line if j != i and shapes[j]['lab'] in DIG and shapes[j]['lab'] != '0')
        letters = sum(1 for j in line if j != i and shapes[j]['lab'] and shapes[j]['lab'] not in DIG and shapes[j]['lab'] not in 'OoIl')
        lab = s['lab']; small = s['h'] < 0.86 * capH
        if not s['fixed'] or s['lab'] in ('0', 'O', 'o'):
            if lab in ('0', 'O', 'o'): lab = '0' if digits >= letters else ('o' if small else 'O')
        if lab.lower() in CASEPAIR and not s['fixed']: lab = lab.lower() if small else lab.upper()
        # plain bars (I, l, 1 drawn as a rectangle, or the stem of an i) stay as they are: Comic's I would be
        # squeezed into a "!"
        if s['npts'] <= 4 and lab in 'Il1i': lab = None
        s['final'] = lab
    byg[g] = [i for i in ids if shapes[i].get('final')]

def comic_contours(ch):
    """Comic Relief Bold's outline of ch as TrueType contours: lists of (x, y, on)"""
    pen = TTGlyphPen(cgs); cgs[ccmap[ord(ch)]].draw(pen)
    g = pen.glyph(); cs, s = [], 0
    for e in g.endPtsOfContours:
        cs.append([(g.coordinates[k][0], g.coordinates[k][1], g.flags[k] & 1) for k in range(s, e + 1)]); s = e + 1
    return cs

def flat_ring(c):
    """a TrueType contour sampled as a closed polyline"""
    n = len(c); pts = []
    start = next((k for k in range(n) if c[k][2]), None)
    if start is None:
        c = [((c[k][0] + c[(k + 1) % n][0]) / 2, (c[k][1] + c[(k + 1) % n][1]) / 2, 1) if False else c[k] for k in range(n)]
        start = 0
    seq = c[start:] + c[:start]
    cur = seq[0]; pts.append(cur[:2]); k = 1; m = len(seq)
    while k <= m:
        p = seq[k % m]
        if p[2]: pts.append(p[:2]); cur = p; k += 1; continue
        q = seq[(k + 1) % m]
        end = q[:2] if q[2] else ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        for t in (0.25, 0.5, 0.75, 1.0):
            a, b, d = (1 - t) ** 2, 2 * t * (1 - t), t * t
            pts.append((a * cur[0] + b * p[0] + d * end[0], a * cur[1] + b * p[1] + d * end[1]))
        cur = (end[0], end[1], 1); k += 1 if not q[2] else 2
    if len(pts) > 1 and pts[0] == pts[-1]: pts.pop()
    return pts

def rdp(pts, tol):
    if len(pts) < 4: return pts
    def seg(p, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx == dy == 0: return math.hypot(p[0] - a[0], p[1] - a[1])
        t = max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)
    def rec(a, b):
        idx, dmax = None, 0
        for i in range(a + 1, b):
            dd = seg(pts[i], pts[a], pts[b % len(pts)])
            if dd > dmax: idx, dmax = i, dd
        if dmax > tol: return rec(a, idx) + rec(idx, b)
        return [a]
    far = max(range(len(pts)), key=lambda i: math.hypot(pts[i][0] - pts[0][0], pts[i][1] - pts[0][1]))
    return [pts[i] for i in rec(0, far) + rec(far, len(pts))]

def implied(c, mid):
    """drops on-curve points lying (within mid units) halfway between two off-curve neighbours"""
    n = len(c); out = []
    for i in range(n):
        p, q = c[i - 1], c[(i + 1) % n]
        if c[i][2] and not p[2] and not q[2] and n > 3 and abs(c[i][0] - (p[0] + q[0]) / 2) <= mid and abs(c[i][1] - (p[1] + q[1]) / 2) <= mid:
            continue
        out.append(c[i])
    return out

def letter(ch, bb, pdir, tol, mid):
    """ch fitted into box bb (x0, y0, x1, y1), as contours of (x, y, on); sign < 0: drawn as a hole"""
    cs = comic_contours(ch)
    xs = [p[0] for c in cs for p in c]; ys = [p[1] for c in cs for p in c]
    cx0, cy0, cx1, cy1 = min(xs), min(ys), max(xs), max(ys)
    x0, y0, x1, y1 = bb
    w = (x1 - x0) * WIDEN; xm = (x0 + x1) / 2
    sx = w / max(1, cx1 - cx0); sy = (y1 - y0) / max(1, cy1 - cy0)
    # TrueType draws filled shapes clockwise and holes the other way; Comic Relief's outlines run the other way
    def ar(c): return sum(c[i][0] * c[(i + 1) % len(c)][1] - c[(i + 1) % len(c)][0] * c[i][1] for i in range(len(c)))
    outer = max(cs, key=lambda c: abs(ar(c)))
    want = -pdir if pdir else -1            # opposite to the shape it sits in; on its own: clockwise
    flip = (1 if ar(outer) > 0 else -1) != want
    out = []
    for c in cs:
        if tol:
            pts = rdp(flat_ring(c), tol / max(sx, sy))
            # where the outline bends gently, the kept points become control points of a curve (no extra bytes,
            # and round letters stay round instead of going polygonal)
            c = []
            for k, (x, y) in enumerate(pts):
                ax, ay = pts[k - 1]; bx, by = pts[(k + 1) % len(pts)]
                v1 = (x - ax, y - ay); v2 = (bx - x, by - y)
                l1 = math.hypot(*v1); l2 = math.hypot(*v2)
                turn = math.degrees(math.acos(max(-1, min(1, (v1[0] * v2[0] + v1[1] * v2[1]) / max(l1 * l2, 1e-9)))))
                c.append((x, y, 0 if (tol >= 46 and 8 < turn < 50) else 1))
            if all(not on for _, _, on in c): c[0] = (c[0][0], c[0][1], 1)
        elif mid:
            c = implied(c, mid / max(sx, sy))
        c = [[round(xm + (x - (cx0 + cx1) / 2) * sx), round(y0 + (y - cy0) * sy), on] for x, y, on in c]
        if tol:                    # nearly straight up/down or across: exactly, as those cost no bytes
            snap = min(12, tol / 2)
            for k in range(1, len(c)):
                for a in (0, 1):
                    if abs(c[k][a] - c[k - 1][a]) <= snap: c[k][a] = c[k - 1][a]
        c = [tuple(p) for p in c]
        if flip: c = c[::-1]
        out.append(c)
    return out

def build(gname, ids, tol=0, mid=0):
    g = glyf[gname]
    coords, ends, flags = g.getCoordinates(glyf)
    cs, s = [], 0
    for e in ends: cs.append([(coords[k][0], coords[k][1], flags[k] & 1) for k in range(s, e + 1)]); s = e + 1
    drop = set()
    for i in ids: drop.add(shapes[i]['ci']); drop.update(shapes[i]['kids'])
    new = [c for k, c in enumerate(cs) if k not in drop]
    lo = min(shapes[i]['bb'][0] for i in ids)
    for i in ids:
        sh = shapes[i]; x0, y0, x1, y1 = sh['bb']
        if WIDEN > 1:                                    # never left of the original letters' left edge
            grow = (x1 - x0) * (WIDEN - 1) / 2
            if x0 - grow < lo: x0 += lo - (x0 - grow)
        new += letter(sh['final'], (x0, y0, x1, y1), sh['pdir'], tol, mid)
    ng = Glyph(); ng.numberOfContours = len(new)
    ng.coordinates = GlyphCoordinates([(x, y) for c in new for x, y, _ in c])
    ng.flags = bytearray([on for c in new for _, _, on in c])
    ng.endPtsOfContours = list(itertools_accumulate(len(c) for c in new))
    ng.program = ttProgram.Program(); ng.program.fromBytecode(b'')
    ng.recalcBounds(glyf)
    lsb = hmtx[gname][1]
    if ng.xMin > lsb:          # no ink, but keeps xMin == lsb
        ng.coordinates.append((lsb, ng.yMin)); ng.flags.append(flagOnCurve)
        ng.endPtsOfContours.append(len(ng.coordinates) - 1); ng.numberOfContours += 1; ng.recalcBounds(glyf)
    return ng

def itertools_accumulate(it):
    t = 0
    for v in it: t += v; yield t - 1

from fontTools.pens.boundsPen import ControlBoundsPen
CASP = {}
for ch in '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz':
    p = ControlBoundsPen(cgs); cgs[ccmap[ord(ch)]].draw(p); b = p.bounds; CASP[ch] = (b[2] - b[0]) / (b[3] - b[1])
def squeeze(ids):
    q = sorted(shapes[i]['asp'] / CASP[shapes[i]['final']] for i in ids)
    return q[len(q) // 2]
report = collections.Counter(); patches = []; done = {}; how = {}
for gname, ids in sorted(byg.items()):
    gi = order.index(gname); budget = loca[gi + 1] - loca[gi]
    ids = [i for i in ids if shapes[i].get('final')]
    if not ids: continue
    if squeeze(ids) < 0.58: report['too narrow'] += 1; continue      # Comic squeezed that far looks worse
    data = None
    for opts in ({}, {'mid': 12}, {'tol': 6}, {'tol': 10}, {'tol': 16}, {'tol': 24}, {'tol': 34}, {'tol': 46}, {'tol': 60}, {'tol': 90}, {'tol': 120}):
        ng = build(gname, ids, **opts)
        if ng.xMin < hmtx[gname][1]: break
        d = ng.compile(glyf, recalcBBoxes=False)
        if len(d) <= budget: data = d; how[gname] = opts; break
    if data is None: report['too big'] += 1; continue
    report['tol %s' % how[gname].get('tol', how[gname].get('mid', 0) and 'mid')] += 1
    off = GLYF + loca[gi]; newb = data + b'\0' * (budget - len(data))
    patches.append((off, bytes(raw[off:off + budget]), newb)); raw[off:off + budget] = newb
    done[gname] = ''.join(shapes[i]['final'] for i in sorted(ids, key=lambda i: shapes[i]['bb'][0]))
open(OUT + '.ttf', 'wb').write(raw)
json.dump({'patches': [(o, a.hex(), b.hex()) for o, a, b in patches]}, open(OUT + '.patch.json', 'w'))
json.dump(done, open(OUT + '.done.json', 'w'), indent=0)
print(dict(report)); print(len(done), 'icons changed,', sum(len(p[2]) for p in patches), 'bytes')

