import numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw, ImageFont
from ocr import split, components
import os
# the app's own copy of the font
COMIC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../app/src/main/assets/fonts/ComicRelief-Bold.ttf')
SS = 4
SHADOW = True; LEVELS = 16       # style knobs: soft outer shadow; grey/alpha steps used in redrawn areas
XH = set('acemnorsuvwxz'); DESC = set('gjpqy'); SMALL = set('+-/%.\u00b1')
_f = {}
def font(px):
    if px not in _f: _f[px] = ImageFont.truetype(COMIC, px)
    return _f[px]
_ref = {}
def refh(kind):
    """ink height of a reference char at size 1000: 'cap' (H), 'x' (x)"""
    if kind not in _ref:
        c = 'H' if kind == 'cap' else 'x'
        a, _ = ink(c, 1000); _ref[kind] = a.shape[0]
    return _ref[kind]
def ink(text, size):
    f = font(size); asc, desc = f.getmetrics()
    W = int(f.getlength(text)) + 2 * size; H = asc + desc + 2 * size
    im = Image.new('L', (W, H), 0); ImageDraw.Draw(im).text((size, size), text, font=f, fill=255)
    a = np.array(im, float) / 255; ys, xs = np.nonzero(a > 0.01)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1], size + asc - ys.min()   # (ink, baseline row in ink)
def runs(chars):
    """split a line's chars (x0,y0,x1,y1,ch) into runs of one size: (kind, [chars])"""
    out = []
    capmax = max([c[3] - c[1] for c in chars if c[4] not in XH and c[4] not in DESC and c[4] not in SMALL] or [0])
    # a word space: a gap clearly wider than the line's letter spacing ("NO CARD", where the letters sit close)
    gs = sorted(chars[i + 1][0] - chars[i][2] for i in range(len(chars) - 1))
    usual = gs[len(gs) // 2] if gs else 0
    for i, c in enumerate(chars):
        if i and len(gs) >= 3 and c[0] - chars[i - 1][2] >= max(4, 2 * usual + 2):
            out.append([None, []])                         # a new run; its kind is set by its first char
        if out and out[-1][0] is None:
            h = c[3] - c[1]
            out[-1][0] = 'x' if c[4] in XH else ('small' if c[4] not in DESC and c[4] not in SMALL and h < 0.92 * capmax else 'cap')
            out[-1][1].append(c); continue
        h = c[3] - c[1]
        k = 'x' if c[4] in XH else ('small' if c[4] not in DESC and c[4] not in SMALL and h < 0.92 * capmax else 'cap')
        if out and (out[-1][0] == k or c[4] in DESC or c[4] in SMALL) and c[0] - out[-1][1][-1][2] <= max(2, int(0.3 * capmax)):
            out[-1][1].append(c)
        else: out.append([k, [c]])
    return out
BOX_RADIUS = 0.2; BOX_BANG = 0.75   # the warning box: corner radius and the cut-out '!', as parts of its height
BOX_CUT = 2                         # the cut-out is the '!' this many pixels wider all round (the line's edge and
                                    # shadow fill that ring; Comic Relief's '!' itself stays clear, like Sony's)
BOX_BAR = 3                         # pixels of box left between the stem's and the dot's cut-outs
BOX_FOR = 42                        # ... both for a box this high, and in proportion for others (but at least
                                    # 3 pixels between the cut-outs: the edge goes into both)
def isbox(c):
    """Sony's warning sign, a light box with a '!' cut out of it: read as '!', but as wide as it is high"""
    return c[4] == '!' and c[2] - c[0] >= 0.6 * (c[3] - c[1])
def warnbox(w, h):
    """the warning box at SS (w x h pixels): a rounded box with Comic Relief's '!' cut out of it (the cut-out
    gets the line's edge and shadow inside, like Sony's)"""
    im = Image.new('L', (w, h), 0)
    ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), radius=BOX_RADIUS * h, fill=255)
    a = np.array(im, float) / 255
    # Comic's '!' as stem and dot, set further apart (its own gap is far too narrow to keep a ring round each)
    b, _ = ink('!', 1000); rows = (b > 0.5).any(axis=1)
    t = int(np.argmax(rows)); s = t + int(np.argmin(rows[t:])); d = s + int(np.argmax(rows[s:]))   # stem ends, dot starts
    f = h / SS / BOX_FOR; r = round(BOX_CUT * f * SS)
    gap = max(2 * r + round(BOX_BAR * f * SS), 3 * SS)       # the edge and shadow go into both cut-outs
    k = (BOX_BANG * h - 2 * r - gap) / (s + b.shape[0] - d)
    def scaled(x): return np.array(Image.fromarray((x * 255).astype(np.uint8)).resize((max(1, round(x.shape[1] * k)), max(1, round(x.shape[0] * k))), Image.LANCZOS), float) / 255
    stem, dot = scaled(b[:s]), scaled(b[d:])
    b = np.zeros((stem.shape[0] + gap + dot.shape[0], stem.shape[1]))
    b[:stem.shape[0]] = stem; b[-dot.shape[0]:, :dot.shape[1]] = dot
    b = np.pad(b, r); yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
    b = ndimage.grey_dilation(b, footprint=yy * yy + xx * xx <= r * r)
    y, x = (h - b.shape[0]) // 2, (w - b.shape[1]) // 2
    a[y:y + b.shape[0], x:x + b.shape[1]] -= b
    return np.clip(a, 0, 1)
def coverage(shape, chars, free, squeeze_min=0.62):
    """Comic coverage (1x, 0..1) for a line of chars; free: (xa, xb) columns it may use"""
    H, W = shape
    parts = []                                             # (ink array at SS, baseline in ink, orig x0, x1, base)
    groups = []                                            # each warning box on its own, the rest in runs
    for kind, rc in runs(chars):
        cur = []
        for c in rc:
            if isbox(c):
                if cur: groups.append((kind, cur)); cur = []
                groups.append(('box', [c]))
            else: cur.append(c)
        if cur: groups.append((kind, cur))
    for kind, rc in groups:
        if kind == 'box':                                  # its own size, never squeezed or made smaller
            c = rc[0]; a = warnbox((c[2] - c[0]) * SS, (c[3] - c[1]) * SS)
            parts.append([a, a.shape[0], c[0], c[2], c[3], None, None]); continue
        text = ''.join(c[4] for c in rc)
        nd = [c for c in rc if c[4] not in DESC and c[4] not in SMALL] or rc
        hs = sorted(c[3] - c[1] for c in nd)
        target = hs[len(hs) // 2] if kind == 'x' else hs[-1]
        base = max(c[3] for c in nd)
        size = max(6, int(round(1000 * target * SS / refh('x' if kind == 'x' else 'cap'))))
        a, bl = ink(text, size)
        parts.append([a, bl, min(c[0] for c in rc), max(c[2] for c in rc), base, size, text])
    gaps = [max(0, parts[i + 1][2] - parts[i][3]) * SS for i in range(len(parts) - 1)]
    def natural(): return sum(p[0].shape[1] for p in parts) + sum(gaps)
    x0 = parts[0][2]; x1 = parts[-1][3]
    avail = (free[1] - free[0]) * SS
    fixed = sum(p[0].shape[1] for p in parts if p[6] is None)          # warning boxes
    def squeeze(): return min(1.0, max(1e-3, avail - fixed) / max(1e-3, natural() - fixed))
    sq = squeeze()
    if sq < squeeze_min:                                   # even squeezed it won't fit: smaller too
        f = sq / squeeze_min
        for p in parts:
            if p[6] is None: continue
            p[5] = max(6, int(p[5] * f)); p[0], p[1] = ink(p[6], p[5])
        gaps = [g * f for g in gaps]; sq = squeeze()
    total = (natural() - fixed) * sq + fixed
    cx = (x0 + x1) / 2 * SS
    X = cx - total / 2
    X = min(max(X, free[0] * SS), free[1] * SS - total)
    cov = np.zeros((H * SS, W * SS))
    for i, (a, bl, _, _, base, _, text) in enumerate(parts):
        w = a.shape[1] if text is None else max(1, int(round(a.shape[1] * sq)))
        a = np.array(Image.fromarray((a * 255).astype(np.uint8)).resize((w, a.shape[0]), Image.LANCZOS), float) / 255
        Xi = int(round(X)); Y = int(round(base * SS - bl))
        ys, xs = slice(max(0, Y), min(H * SS, Y + a.shape[0])), slice(max(0, Xi), min(W * SS, Xi + w))
        if ys.stop > ys.start and xs.stop > xs.start:
            cov[ys, xs] = np.maximum(cov[ys, xs], a[ys.start - Y:ys.stop - Y, xs.start - Xi:xs.stop - Xi])
        X += w + (gaps[i] * sq if i < len(gaps) else 0)
    return cov.reshape(H, SS, W, SS).mean(axis=(1, 3))
def freespan(occ, y0, y1, x0, x1, W, margin=True):
    """columns around [x0,x1) free of other content in rows y0..y1 (occ: bool array of other content)"""
    band = occ[max(0, y0 - 1):y1 + 1].any(axis=0)
    xa = x0
    while xa > 1 and not band[xa - 1]: xa -= 1
    xb = x1
    while xb < W - 1 and not band[xb]: xb += 1
    # keep some room from what's next to it (a quarter of the height), never less than it had
    m = max(2, int(round(0.25 * (y1 - y0)))) if margin else 0
    xa += min(m, max(0, x0 - xa)); xb -= min(m, max(0, xb - x1))
    return (xa + (1 if xa > 1 else 0), xb - (1 if xb < W - 1 else 0))
def mode(vals):
    v, n = np.unique(vals, return_counts=True); return v[np.argmax(n)]
def fill_from_nearest(K, a, E):
    """cleared pixels take the value (all colour channels) of the nearest pixel outside the cleared area
    (transparent stays transparent, a box keeps its own fill)"""
    if not E.any() or E.all(): return
    _, (iy, ix) = ndimage.distance_transform_edt(E, return_indices=True)
    for c in range(K.shape[0]): K[c][E] = K[c][iy[E], ix[E]]
    a[E] = a[iy[E], ix[E]]
def rgb(v): return np.array([(v >> 12) & 15, (v >> 8) & 15, (v >> 4) & 15], float)
def med(K, m, default):
    """per channel median of K over mask m (a colour)"""
    return np.array([float(np.median(K[c][m])) for c in range(3)]) if m.any() else np.array([default] * 3, float)
def clean(p):
    """the one change made everywhere (by the camera too): fully transparent pixels get colour 0"""
    return np.where((p & 15) == 0, 0, p).astype(np.uint16)
def redraw(p, line):
    """p: picture (uint16 RGBA4444; gray, or coloured like the orange 'selected' ones); line: dict(light,
    boxes=[(x0,y0,x1,y1,ch,score)...]) -> new picture, changed only around the line (its old lettering and the
    new). Decisions (light, dark, edges) are made on the brightest channel; every channel is painted with the
    original's own colours (fill, edge, shadow)."""
    p0 = p
    _, a = split(p); a = a.astype(float); a0 = a.copy()
    K = np.stack([(p >> 12) & 15, (p >> 8) & 15, (p >> 4) & 15]).astype(float); K0 = K.copy()
    g = K.max(axis=0)
    comps = components(p, line['light'])
    boxes = set(line.get('mbox') or [b[:4] for b in line['boxes']])   # the shapes to clear (an i's dot is its own)
    M = np.zeros(p.shape, bool)
    punct = [b for b in line['boxes'] if b[4] in ':.']    # a colon's dots (and a point) are shapes of their own
    for c in comps:
        if (c['x0'], c['y0'], c['x1'], c['y1']) in boxes or any(b[0] <= c['x0'] and c['x1'] <= b[2] and b[1] <= c['y0'] and c['y1'] <= b[3] for b in punct):
            M[c['y0']:c['y1'], c['x0']:c['x1']] |= c['mask']
    D1 = ndimage.binary_dilation(M, iterations=1); D2 = ndimage.binary_dilation(M, iterations=2)
    D3 = ndimage.binary_dilation(M, iterations=3); ring = ndimage.binary_dilation(M, iterations=5) & ~D3
    full = a == 15
    if line['light']:
        fg = med(K, M & full, 13)
        ol = D1 & ~M
        o_a = float(np.median(a[ol])) if ol.any() else 10
        # the edge's own colour (black on clear, box on a box): from the pixels clearly darker than the letters
        # (not the letters' own soft light rim, which would draw a light ghost line beside each new letter)
        olk = ol & (g <= max(fg) - 4) & (a >= 4)
        o_g = med(K, olk if olk.sum() >= 4 else ol, 0)
        sh = D2 & ~D1; s_a = float(np.median(a[sh])) if sh.any() else 2
        s_g = med(K, sh, 0)
        transparent = (a[ring] <= 3).mean() > 0.5 if ring.any() else True
        # erase: the text, its edge and shadow, but not other light shapes
        D6 = ndimage.binary_dilation(M, iterations=6)
        # other light shapes: keep them and their edges (in dim pictures 'light' is what's nearly as bright as the
        # letters: the thin bars around a value)
        others = ~M & (g >= min(9, max(fg) - 3)) & (a >= 8) & ~ndimage.binary_dilation(M, iterations=1)
        keepo = ndimage.binary_dilation(others, iterations=2) & ~ndimage.binary_dilation(M, iterations=1)
        E = (D3 | (D6 & (a < 8) & (g <= 6))) & ~others & ~keepo
        filled = ring & ~ndimage.binary_dilation(others, iterations=2) & (g < 9)
        if not transparent and filled.sum() >= 6:
            # text on a filled area (a badge): the fill's own most common value, not the nearest pixel (the frame)
            bg = int(mode(p[filled].astype(np.int64)))
            E = (D3 | (D6 & (g <= 6))) & ~others & ndimage.binary_dilation(M, iterations=4)   # the frame's dark edge is fill-coloured anyway
            for c in range(3): K[c][E] = rgb(bg)[c]
            a[E] = bg & 15
            if not ol.any(): o_g = rgb(bg)
        elif transparent:
            K[:, E] = 0; a[E] = 0          # on a clear background, what's erased becomes clear (not a neighbour's copy)
        else:
            fill_from_nearest(K, a, E)
            # the old faint shadow becomes clear (not a copy of a neighbour's edge); clear stays clear
            clear0 = E & (a0 < 8); K[:, clear0] = 0; a[clear0] = 0
        occ = (a >= 4) & ~D3
        bx = line['boxes']; fr = tuple(line['span']) if line.get('span') else freespan(occ, min(b[1] for b in bx), max(b[3] for b in bx), min(b[0] for b in bx), max(b[2] for b in bx), p.shape[1])
        C = coverage(p.shape, [b[:5] for b in bx], fr)
        O = ndimage.grey_dilation(C, size=(3, 3)); S2 = ndimage.grey_dilation(C, size=(5, 5))
        O[others] = 0; S2[others] = 0                          # no edge or shadow over a neighbouring light shape
        # paint shadow, edge, fill (alpha 0..1, colour 0..15) over what is there
        def over(K, a, col, al):
            A = a / 15; na = al + A * (1 - al)
            nK = np.stack([np.where(na > 0, (col[c] * al + K[c] * A * (1 - al)) / np.maximum(na, 1e-6), 0) for c in range(3)])
            return nK, na * 15
        if SHADOW: K, a = over(K, a, s_g, S2 * s_a / 15)
        K, a = over(K, a, o_g, O * o_a / 15)
        K, a = over(K, a, fg, C)
    else:
        fg = med(K, M & full, 2)
        # clear the letters and their soft edge: whatever near them is darker than the box (not other dark shapes)
        ring4 = ndimage.binary_dilation(M, iterations=4) & ~D2
        bgv = mode(g[ring4 & (a >= 8)].astype(int)) if (ring4 & (a >= 8)).any() else 13
        otherd = ndimage.binary_dilation(~M & (g <= 5) & (a >= 8) & ~D1, iterations=1)
        near = np.zeros(p.shape, bool)                         # the letters' own area, not the box's edge
        bx_ = line['boxes']
        near[max(0, min(b[1] for b in bx_) - 1):max(b[3] for b in bx_) + 1, max(0, min(b[0] for b in bx_) - 1):max(b[2] for b in bx_) + 1] = True
        E = (D1 & ~(~M & (g >= 9))) | (D2 & (g < bgv) & ~otherd & near)
        fill_from_nearest(K, a, E)
        bx = line['boxes']
        inside = ndimage.binary_fill_holes(ndimage.binary_dilation(M, iterations=2) | ((a >= 8) & (g >= 9)))
        occ = ~inside
        fr = tuple(line['span']) if line.get('span') else freespan(occ, min(b[1] for b in bx), max(b[3] for b in bx), min(b[0] for b in bx), max(b[2] for b in bx), p.shape[1], margin=False)
        C = coverage(p.shape, [b[:5] for b in bx], fr)
        K = np.stack([K[c] * (1 - C) + fg[c] * C for c in range(3)]); a = np.maximum(a, 15 * C)
    touched = (np.abs(K - K0) > 1e-3).any(axis=0) | (np.abs(a - a0) > 1e-3)     # pixels we changed (the others stay exactly)
    if LEVELS < 16:
        st = 15 / (LEVELS - 1); K = np.round(K / st) * st; a = np.round(a / st) * st
    Ki = np.clip(np.round(K), 0, 15).astype(np.uint16); ai = np.clip(np.round(a), 0, 15).astype(np.uint16)
    Ki[:, ai == 0] = 0
    new = (Ki[0] << 12) | (Ki[1] << 8) | (Ki[2] << 4) | ai
    region = ndimage.binary_dilation(M, iterations=6) | ndimage.binary_dilation(C > 0.004, iterations=3)
    out = np.where(region & touched, new, p0).astype(np.uint16)
    return clean(out)
