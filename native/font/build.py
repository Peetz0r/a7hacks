"""Builds a same-layout replacement of the Univers glyph outlines inside UniversOTS-SJ_wIcon.ltt.
Every replaced character keeps its slot, its byte range in 'glyf', its advance width and its left side
bearing (xMin == lsb); only the outline bytes inside that range change (padded with zeros)."""
import sys as _sys
_HELP = """usage: python3 build.py ComicRelief-Regular.ttf OUT_PREFIX UniversOTS-SJ_wIcon.ltt
Builds Comic Relief outlines that fit the slots of the camera's Univers (inside UniversOTS-SJ_wIcon.ltt, the
camera's /usr/share/app copy): every replaced character keeps its slot, byte range, advance and left side
bearing. Writes OUT_PREFIX.ltt and .ttf (the patched font, for checking) and OUT_PREFIX.patch.json (for
mkheader.py). Needs fontTools. Env SERIF: the I's serif length (default 2.2).

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
if __name__ == '__main__' and (len(_sys.argv) != 4):
    _sys.exit('\n'.join(l for i, l in enumerate(_HELP.split('\n')) if i == 0 or l.startswith(' ')) + '\n(see --help)')
import sys, math, json, struct
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables import ttProgram
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.recordingPen import DecomposingRecordingPen, RecordingPen
from fontTools.pens.boundsPen import BoundsPen, ControlBoundsPen
from fontTools.ttLib.tables._g_l_y_f import flagOnCurve, Glyph

# usage: python3 build.py ComicRelief-Regular.ttf <out prefix> UniversOTS-SJ_wIcon.ltt   (the camera's /usr/share/app copy)
# writes <out prefix>.ltt / .ttf (the patched files, for checking) and <out prefix>.patch.json (for mkheader.py)
LTT = sys.argv[3]
EMB = 53808
src_path = sys.argv[1]; out_prefix = sys.argv[2]
ltt = bytearray(open(LTT, 'rb').read())
uni_raw = bytes(ltt[EMB:EMB + 377056])
import io
U = TTFont(io.BytesIO(uni_raw)); C = TTFont(src_path)
ucmap = U.getBestCmap(); ccmap = C.getBestCmap(); cgs = C.getGlyphSet()
uglyf = U['glyf']; uloca = U['loca']; hu = U['hmtx']; hc = C['hmtx']
uorder = U.getGlyphOrder(); corder = set(C.getGlyphOrder())
# offset of the glyf table inside the embedded font
n = struct.unpack('>H', uni_raw[4:6])[0]
tabs = {}
for i in range(n):
    tag, cs, off, ln = struct.unpack('>4sIII', uni_raw[12 + 16 * i:28 + 16 * i]); tabs[tag.decode()] = (off, ln)
GLYF = tabs['glyf'][0]
s = U['OS/2'].sCapHeight / C['OS/2'].sCapHeight
ASC, DESC = U['hhea'].ascent, U['hhea'].descent

rev = {}
for cp, g in ucmap.items(): rev.setdefault(g, cp)
src = {}
NAMED = {'caronLowercase': 'uF004', 'dieresisGreek': 0x00A8, 'breveCyrillic': 0x02D8}
altbase = {}                                       # 'A.alt01' -> 'A': Univers's shorter capitals under accents
for gi, gn in enumerate(uorder):
    if gn in rev and 0x2500 <= rev[gn] <= 0x25ff: continue
    if gn in rev and rev[gn] in ccmap: src[gi] = ccmap[rev[gn]]
    elif gn in NAMED:
        t = NAMED[gn]
        if isinstance(t, int): t = ccmap.get(t)
        if t in corder: src[gi] = t
    elif '.alt' in gn and gn.split('.')[0] in rev and rev[gn.split('.')[0]] in ccmap:
        src[gi] = ccmap[rev[gn.split('.')[0]]]; altbase[gn] = gn.split('.')[0]
    elif gn in corder and gn not in ('.notdef', '.null', 'nonmarkingreturn', 'space'): src[gi] = gn
IFAMILY = ('I', 'I.alt01', 'I.alt02', 'Iogonek')
SERIF = float(__import__('os').environ.get('SERIF', '2.2'))   # I serif length relative to the earlier version

def contours(rec):
    """split a recording into contours (lists of (op, args))"""
    out, cur = [], []
    for op, args in rec.value:
        cur.append((op, args))
        if op in ('closePath', 'endPath'): out.append(cur); cur = []
    if cur: out.append(cur)
    return out

def cbounds(cont):
    bp = BoundsPen(None)
    for op, args in cont: getattr(bp, op)(*args)
    return bp.bounds

def flatten(cont):
    """points of a contour as a closed polyline (quadratic segments sampled)"""
    pts, cur, start = [], None, None
    for op, args in cont:
        if op == 'moveTo': cur = start = args[0]; pts.append(cur)
        elif op == 'lineTo': cur = args[0]; pts.append(cur)
        elif op == 'qCurveTo':
            offs = list(args[:-1]); end = args[-1]
            if end is None:                     # all off-curve contour
                end = ((offs[-1][0] + offs[0][0]) / 2, (offs[-1][1] + offs[0][1]) / 2)
                if cur is None: cur = start = end; pts.append(cur)
            ons = []
            for k in range(len(offs) - 1):
                ons.append(((offs[k][0] + offs[k + 1][0]) / 2, (offs[k][1] + offs[k + 1][1]) / 2))
            ons.append(end)
            p0 = cur
            for k, c in enumerate(offs):
                p2 = ons[k]
                for t in (0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0):
                    a = (1 - t) ** 2; b = 2 * t * (1 - t); d = t * t
                    pts.append((a * p0[0] + b * c[0] + d * p2[0], a * p0[1] + b * c[1] + d * p2[1]))
                p0 = p2
            cur = end
    if len(pts) > 1 and pts[0] == pts[-1]: pts.pop()
    return pts

def area(pts):
    return sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts))) / 2

def rdp(pts, tol):
    if len(pts) < 3: return pts
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
    # split the closed ring at the point farthest from the first one
    far = max(range(len(pts)), key=lambda i: math.hypot(pts[i][0] - pts[0][0], pts[i][1] - pts[0][1]))
    keep = rec(0, far) + rec(far, len(pts))
    return [pts[i] for i in keep]

def tidy(g, mid=14, snap=6):
    """byte-saving cleanup of a TrueType glyph: on-curve points within `mid` units of the midpoint of their
    two off-curve neighbours become implied (dropped); consecutive coordinates within `snap` units are made
    equal (zero deltas cost nothing)"""
    coords = list(g.coordinates); flags = list(g.flags); ends = list(g.endPtsOfContours)
    out_c, out_f, out_e = [], [], []
    start = 0
    for end in ends:
        cc = coords[start:end + 1]; ff = [f & 1 for f in flags[start:end + 1]]
        n = len(cc)
        keep = []
        for i in range(n):
            p, q = (i - 1) % n, (i + 1) % n
            if ff[i] and not ff[p] and not ff[q] and n > 3:
                mx, my = (cc[p][0] + cc[q][0]) / 2, (cc[p][1] + cc[q][1]) / 2
                if abs(cc[i][0] - mx) <= mid and abs(cc[i][1] - my) <= mid: continue
            keep.append(i)
        cc = [list(cc[i]) for i in keep]; ff = [ff[i] for i in keep]
        for i in range(1, len(cc)):
            for a in (0, 1):
                if abs(cc[i][a] - cc[i - 1][a]) <= snap: cc[i][a] = cc[i - 1][a]
        out_c += [tuple(c) for c in cc]; out_f += ff; out_e.append(len(out_c) - 1)
        start = end + 1
    from fontTools.ttLib.tables._g_l_y_f import GlyphCoordinates
    g.coordinates = GlyphCoordinates(out_c); g.flags = bytearray(out_f); g.endPtsOfContours = out_e

def find_stem(conts, yp):
    """the main vertical stem at height yp: the ink run crossing that height, if there is exactly one and it is
    narrow compared to the whole glyph (left and right x), else None"""
    xs = []
    allx = []
    for c in conts:
        pts = flatten(c)
        allx += [p[0] for p in pts]
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            if (y0 <= yp < y1) or (y1 <= yp < y0):
                xs.append(x0 + (yp - y0) * (x1 - x0) / (y1 - y0))
    xs.sort()
    if len(xs) != 2 or not allx: return None
    if xs[1] - xs[0] > 0.5 * (max(allx) - min(allx)): return None
    return xs[0], xs[1]

def build(cg, gn, dots=False, grid=1, tol=0, anchor=True):
    if gn in IFAMILY: dots, grid, tol = False, 1, 0            # keep the rounded serif ends
    rec = DecomposingRecordingPen(cgs); cgs[cg].draw(rec)
    conts = [c for c in contours(rec) if sum(1 for op, a in c if op not in ('moveTo', 'closePath', 'endPath')) >= 1
             and sum(len(a) for op, a in c if op != 'moveTo') >= 2]      # drop inkless one-point marker contours
    bp = BoundsPen(None)
    for c in conts:
        for op, args in c: getattr(bp, op)(*args)
    if bp.bounds is None: return None, 1, 0
    cxmin, _, cxmax, _ = bp.bounds
    cbp = ControlBoundsPen(None)                   # vertical limits: over all points, like the glyph header
    for c in conts:
        for op, args in c: getattr(cbp, op)(*args)
    _, cymin, _, cymax = cbp.bounds
    uadv, ulsb = hu[gn]; ug = uglyf[gn]
    wu = ug.xMax - ug.xMin; ucen = (ug.xMin + ug.xMax) / 2
    wc = (cxmax - cxmin) * s
    # the ink has to start at or right of lsb (xMin == lsb, see below) and should end before the advance
    amax = max(wu, uadv - ulsb - 30)
    fx = 1.0 if wc <= amax or wc <= 0 else max(0.35, amax / wc)
    # Letters that need a lot of squeezing (I, f, t, ...): keep their main vertical stem at full width and
    # shorten only what sticks out to the sides (serifs, crossbars, hooks).
    stem = find_stem(conts, U['OS/2'].sCapHeight / s * 0.3) if fx < 0.8 else None
    wide = gn in IFAMILY
    if wide: amax = 415 / 361 * max(wu, uadv - hu['I'][1] - 30) * 1.0   # 15% wider than the squeezed I was
    if stem:
        sl, sr = stem
        outer = (sl - cxmin) + (cxmax - sr)
        k = 1.0 if outer <= 0 else max(0.15, min(1.0, (amax / s - (sr - sl)) / outer))
        if wide: k = k * SERIF                       # the I's serifs: longer still (may exceed Comic's own)
        def H(x):
            if x < sl: return (x - cxmin) * s * k
            if x <= sr: return (sl - cxmin) * s * k + (x - sl) * s
            return (sl - cxmin) * s * k + (sr - sl) * s + (x - sr) * s * k
        w = H(cxmax)
        fx = w / wc
    else:
        H = lambda x: (x - cxmin) * s * fx
        w = wc * fx
    left = ucen - w / 2                            # centred on Univers's ink ...
    if not wide:
        left = min(left, uadv - 30 - w)            # ... but not running past the advance ...
        left = max(left, ulsb)                     # ... and never left of lsb
    # vertically: scale by s, but squeeze descenders (and very tall parts) into Univers's own limits, since the
    # UI sizes its text boxes for Univers (descent 381, ascent 1667)
    bot = -max(-DESC, -ug.yMin); top = max(ASC, ug.yMax)
    sd = s if cymin * s >= bot else bot / cymin
    capU = U['OS/2'].sCapHeight
    vs = ug.yMax / uglyf[altbase[gn]].yMax if gn in altbase and ug.yMax > 900 else 1.0
    def V(y):
        if ug.yMin >= 900: return (y - cymin) * s + ug.yMin          # accent above: sit where Univers's does
        if ug.yMax <= 250 and ug.yMin < 0: return (y - cymax) * s + ug.yMax   # accent below (cedilla, ogonek)
        if y < 0: return y * sd
        y = y * s * vs
        if cymax * s * vs > top and y > capU: y = capU + (y - capU) * (top - capU) / (cymax * s * vs - capU)
        return y
    def T(p):
        x = H(p[0]) + left; y = V(p[1])
        return (round(x / grid) * grid, round(y / grid) * grid)
    pen = TTGlyphPen(None); ndots = 0
    for cont in conts:
        b = cbounds(cont)
        if dots and b:
            w, h = b[2] - b[0], b[3] - b[1]
            if 0 < w < 420 and 0 < h < 420 and 0.6 < w / h < 1.6:
                # a dot: 8 off-curve points on an octagon (implied on-curve points in between), same direction
                cx, cy, r = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, (w + h) / 4
                rr = r / math.cos(math.pi / 8)
                sign = 1 if area(flatten(cont)) > 0 else -1
                pts = [T((cx + rr * math.cos(math.pi / 8 + sign * k * math.pi / 4), cy + rr * math.sin(math.pi / 8 + sign * k * math.pi / 4))) for k in range(8)]
                pen.qCurveTo(*pts, None); pen.closePath(); ndots += 1
                continue
        if tol:
            pts = rdp(flatten(cont), tol)
            if len(pts) >= 3:
                pen.moveTo(T(pts[0]))
                for p in pts[1:]: pen.lineTo(T(p))
                pen.closePath()
            continue
        for op, args in cont:
            getattr(pen, op)(*[None if a is None else T(a) for a in args])
    g = pen.glyph(dropImpliedOnCurves=True)
    g.program = ttProgram.Program(); g.program.fromBytecode(b'')
    if g.numberOfContours == 0: return None, fx, ndots
    g.recalcBounds(uglyf)
    if wide:
        tidy(g); g.recalcBounds(uglyf)
        return g, fx, ndots                        # its hmtx lsb gets rewritten to xMin instead
    if anchor and g.xMin > ulsb:
        # a contour of one point at x = lsb: no ink, but it makes xMin == lsb, so renderers that place the
        # glyph by its xMin and the hmtx lsb (TrueType's phantom point) keep the ink where we put it
        g.coordinates.append((ulsb, g.yMin)); g.flags.append(flagOnCurve)
        g.endPtsOfContours.append(len(g.coordinates) - 1); g.numberOfContours += 1
        g.recalcBounds(uglyf)
    elif g.xMin != ulsb:
        g.coordinates.translate((ulsb - g.xMin, 0))
        g.recalcBounds(uglyf)
    return g, fx, ndots

report = {'replaced': [], 'kept': [], 'dots': [], 'grid': []}
patches = []
for gi, cg in sorted(src.items()):
    gn = uorder[gi]; ug = uglyf[gn]
    if ug.numberOfContours <= 0: continue          # composites (accented letters) and empty glyphs stay
    budget = uloca[gi + 1] - uloca[gi]
    data = None; how = None
    steps = ({}, {'dots': True}, {'dots': True, 'grid': 4}, {'dots': True, 'grid': 8},
             {'dots': True, 'tol': 20}, {'dots': True, 'tol': 30}, {'dots': True, 'tol': 40}, {'dots': True, 'tol': 60})
    for opts in [dict(o) for o in steps] + [dict(o, anchor=False) for o in steps]:
        g, fx, nd = build(cg, gn, **opts)
        if g is None: break
        d = g.compile(uglyf, recalcBBoxes=False)
        if len(d) <= budget: data, how = d, dict(opts, ndots=nd); break
    label = chr(rev[gn]) if gn in rev else gn
    if data is None:
        report['kept'].append(label); continue
    if how.get('ndots'): report['dots'].append(label)
    if how.get('anchor') is False: report.setdefault('noanchor', []).append(label)
    if how.get('grid') or how.get('tol'): report['grid'].append((label, how.get('grid', 1), how.get('tol', 0)))
    report['replaced'].append(label)
    if fx < 0.75: report.setdefault('squeezed', []).append('%s %d%%' % (label, round(fx * 100)))
    if gn in IFAMILY:
        gg = Glyph(data); gg.expand(uglyf)
        ho = EMB + tabs['hmtx'][0] + 4 * gi + 2
        nl = struct.pack('>h', gg.xMin)
        patches.append((ho, bytes(ltt[ho:ho + 2]), nl)); ltt[ho:ho + 2] = nl
        report.setdefault('lsb', []).append('%s %d->%d' % (gn, hu[gn][1], gg.xMin))
    off = EMB + GLYF + uloca[gi]
    new = data + b'\0' * (budget - len(data))
    patches.append((off, bytes(ltt[off:off + budget]), new))
    ltt[off:off + budget] = new

open(out_prefix + '.ltt', 'wb').write(ltt)
open(out_prefix + '.ttf', 'wb').write(bytes(ltt[EMB:EMB + 377056]))
json.dump({'patches': [(o, a.hex(), b.hex()) for o, a, b in patches]}, open(out_prefix + '.patch.json', 'w'))
print('scale %.3f; replaced %d, kept Univers %d' % (s, len(report['replaced']), len(report['kept'])))
print('kept:', ' '.join(report['kept']))
print('dots redrawn:', ' '.join(report['dots']))
print('simplified (grid, tolerance):', report['grid'])
print('left-aligned (no room for the anchor point):', ' '.join(report.get('noanchor', [])))
print('squeezed below 75%:', ', '.join(report.get('squeezed', [])))
print('lsb rewritten:', report.get('lsb'))
print('patch bytes', sum(len(p[2]) for p in patches), 'in', len(patches), 'ranges')
