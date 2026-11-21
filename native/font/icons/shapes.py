import os
# splits every icon of Sony_DI_Icons.ttf into shapes (a contour plus the contours directly inside it) and
# groups shapes that look alike, size-independently, so letters can be told apart from graphics
import sys, pickle, numpy as np
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw
SRC = os.environ.get('ICONS', 'Sony_DI_Icons.ttf')
f = TTFont(SRC); glyf = f['glyf']; cmap = f.getBestCmap(); rev = {v: k for k, v in cmap.items()}

def area(p):
    a = 0
    for i in range(len(p)): x1, y1 = p[i]; x2, y2 = p[(i + 1) % len(p)]; a += x1 * y2 - x2 * y1
    return a / 2
def inside(pt, poly):
    x, y = pt; c = False; n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1: c = not c
    return c

GW, GH = 20, 28
def raster(polys, bb):
    x0, y0, x1, y1 = bb; w = max(1, x1 - x0); h = max(1, y1 - y0)
    im = Image.new('1', (GW, GH), 0)
    for p in polys:
        m = Image.new('1', (GW, GH), 0)
        ImageDraw.Draw(m).polygon([((x - x0) / w * (GW - 1), (y1 - y) / h * (GH - 1)) for x, y in p], fill=1)
        im = Image.fromarray(np.logical_xor(np.array(im), np.array(m)))
    return np.array(im, dtype=bool)

if __name__ == '__main__':
    shapes = []   # (glyph, contour index, child indices, bbox, sign, raster)
    for gname in f.getGlyphOrder():
        g = glyf[gname]
        if g.isComposite() or g.numberOfContours <= 0: continue
        coords, ends, flags = g.getCoordinates(glyf)
        cs, s = [], 0
        for e in ends: cs.append([tuple(p) for p in coords[s:e + 1]]); s = e + 1
        bbs = [(min(x for x, _ in c), min(y for _, y in c), max(x for x, _ in c), max(y for _, y in c)) for c in cs]
        ars = [area(c) for c in cs]
        parent = []
        for i, c in enumerate(cs):
            best = None
            for j, d in enumerate(cs):
                if i == j: continue
                bi, bj = bbs[i], bbs[j]
                if bi[0] >= bj[0] and bi[1] >= bj[1] and bi[2] <= bj[2] and bi[3] <= bj[3] and abs(ars[j]) > abs(ars[i]):
                    if sum(inside(p, d) for p in c[:5]) >= min(3, len(c)):
                        if best is None or abs(ars[j]) < abs(ars[best]): best = j
            parent.append(best)
        for i, c in enumerate(cs):
            kids = [k for k in range(len(cs)) if parent[k] == i]
            depth, p = 0, parent[i]
            while p is not None: depth += 1; p = parent[p]
            sign = 1 if depth % 2 == 0 else -1     # filled or a hole, by nesting (some icons wind the 'wrong' way)
            shapes.append(dict(glyph=gname, cp=rev.get(gname), ci=i, kids=kids, bb=bbs[i], sign=sign,
                               dir=1 if ars[i] > 0 else -1, pdir=None if parent[i] is None else (1 if ars[parent[i]] > 0 else -1),
                               npts=len(c) + sum(len(cs[k]) for k in kids), parent=parent[i],
                               ras=raster([c] + [cs[k] for k in kids], bbs[i])))
    print(len(shapes), 'shapes in', len({s['glyph'] for s in shapes}), 'glyphs')

    # group: same aspect (within 6%) and rasters differing in at most 5% of cells
    groups = []
    for i, s in enumerate(shapes):
        w = s['bb'][2] - s['bb'][0]; h = s['bb'][3] - s['bb'][1]; s['asp'] = w / max(h, 1); s['h'] = h
        for gi, gr in enumerate(groups):
            r = shapes[gr[0]]
            if abs(r['asp'] - s['asp']) <= 0.06 * max(r['asp'], 0.2) and r['sign'] == s['sign'] and \
               np.count_nonzero(r['ras'] ^ s['ras']) <= 0.05 * GW * GH:
                gr.append(i); s['group'] = gi; break
        else:
            s['group'] = len(groups); groups.append([i])
    print(len(groups), 'groups;', sum(1 for g in groups if len(g) >= 2), 'with 2+ members')
    pickle.dump((shapes, groups), open('shapes.pkl', 'wb'))
