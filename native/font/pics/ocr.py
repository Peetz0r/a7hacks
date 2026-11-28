import numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw, ImageFont
UNIV = __import__('os').environ.get('UNIVERS', 'univers.ttf')   # the camera's Univers, from UniversOTS-SJ_wIcon.ltt (see ../build.py)
CHARS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-/%'
def sysfont(name):
    """a font of the system's by its file name, wherever the distribution keeps it (Debian, Fedora, ...)"""
    import glob
    return (glob.glob('/usr/share/fonts/**/' + name, recursive=True) or [name])[0]
FONTS = [UNIV, sysfont('LiberationSansNarrow-Bold.ttf'), sysfont('DejaVuSansCondensed-Bold.ttf')]
_tpl = {}; _src = {}
def tpl(c, h, w, fi=0):
    """character c rendered in font fi, cropped to its ink and stretched to w x h (float 0..1)"""
    key = (c, h, w, fi)
    if key in _tpl: return _tpl[key]
    if (c, fi) not in _src:
        f = ImageFont.truetype(FONTS[fi], 200)
        im = Image.new('L', (400, 300), 0); ImageDraw.Draw(im).text((50, 20), c, font=f, fill=255)
        a = np.array(im); ys, xs = np.nonzero(a > 127)
        _src[(c, fi)] = Image.fromarray(a[ys.min():ys.max() + 1, xs.min():xs.max() + 1])
    t = np.array(_src[(c, fi)].resize((w, h), Image.BILINEAR)) / 255.0
    _tpl[key] = t; return t
def natural_aspect(c):
    tpl(c, 10, 10); im = _src[(c, 0)]; return im.size[0] / im.size[1]
def split(p):
    """p: RGBA4444 array -> gray (0..15), alpha (0..15)"""
    return (p >> 12) & 15, p & 15
def components(p, light=True):
    g, a = split(p)
    m = (a >= 8) & ((g >= 9) if light else (g <= 5))
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    out = []
    for i, sl in enumerate(ndimage.find_objects(lab)):
        comp = (lab[sl] == i + 1)
        out.append(dict(y0=sl[0].start, y1=sl[0].stop, x0=sl[1].start, x1=sl[1].stop, mask=comp, area=int(comp.sum())))
    return out
def classify(comp):
    """best (score, char) for one component"""
    h, w = comp['mask'].shape
    if h < 5 or w < 1: return (0, None)
    m = comp['mask'].astype(float)
    best = (0, None)
    for c in CHARS:
        na = natural_aspect(c); asp = w / h
        if not (0.45 * na <= asp <= 1.35 * na): continue
        for fi in range(len(FONTS)):
            t = tpl(c, h, w, fi)
            inter = np.minimum(m, t).sum(); uni = np.maximum(m, t).sum()
            s = inter / uni if uni else 0
            if s > best[0]: best = (s, c)
    return best

FOLD = {c: c.upper() for c in 'cosuvwxzpji'}       # same shape in both cases, apart from size
def classify2(comp):
    s, c = classify(comp)
    return s, (FOLD.get(c, c) if c else None)
def lines(p):
    """text lines found in p: list of dict(chars=[comp...], polarity, text, score)"""
    found = []
    for light in (True, False):
        comps = [c for c in components(p, light) if c['area'] >= 6 and 6 <= c['y1'] - c['y0'] <= 90]
        for c in comps:
            c['score'], c['ch'] = classify2(c); c['light'] = light
        comps = [c for c in comps if c['ch'] and c['score'] >= 0.55]
        comps.sort(key=lambda c: c['x0'])
        used = set()
        for i, c in enumerate(comps):
            if i in used: continue
            line = [c]; used.add(i)
            for j in range(i + 1, len(comps)):
                if j in used: continue
                d = comps[j]; last = line[-1]
                top = min(x['y0'] for x in line); bot = max(x['y1'] for x in line); H = bot - top
                ov = min(bot, d['y1']) - max(top, d['y0'])
                if ov >= 0.5 * min(H, d['y1'] - d['y0']) and d['x0'] - last['x1'] <= 0.6 * H + 2:
                    line.append(d); used.add(j)
            found.append(line)
    out = []
    for line in found:
        H = max(c['y1'] - c['y0'] for c in line)
        txt = ''
        for c in line:
            ch = c['ch']
            if ch.lower() in 'cosuvwxzp' and (c['y1'] - c['y0']) < 0.82 * H: ch = ch.lower()
            txt += ch
        sc = sum(c['score'] for c in line) / len(line)
        out.append(dict(chars=line, text=txt, score=sc, light=line[0]['light'], H=H))
    return out
def accept(l):
    t = l['text']
    if len(t) >= 2: return l['score'] >= 0.62
    if t == '1': return l['score'] >= 0.88
    return l['score'] >= 0.78 and t not in 'Il-|'

def good_lines(p):
    """accepted lines, dropping light ones that overlap dark ones (text cut into a box) or better lines"""
    L = [l for l in lines(p) if accept(l)]
    def bb(l): return (min(c['x0'] for c in l['chars']), min(c['y0'] for c in l['chars']), max(c['x1'] for c in l['chars']), max(c['y1'] for c in l['chars']))
    def ov(a, b): return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
    out = []
    for l in sorted(L, key=lambda l: (not (not l['light']), -l['score'] * len(l['text']))):
        if any(ov(bb(l), bb(m)) for m in out): continue
        out.append(l)
    return out

OPAQUE_OK = True                        # redraw light lettering on filled badges too
AMB = {'I': 'I1li', 'l': 'I1li', '1': 'I1li', 'i': 'I1li', 'O': 'O0o', '0': 'O0o', 'o': 'O0o', 'S': 'S5s', '5': 'S5s',
       's': 'S5s', 'B': 'B8', '8': 'B8', 'Z': 'Z2z', 'z': 'Z2z', '2': 'Z2z', 'J': 'Jj', 'j': 'Jj'}
LOWERABLE = set('cosuvwxzpji')
def finalize(line):
    """line from the scan (text, boxes=[(x0,y0,x1,y1,ch,score)]): settles look-alike characters from their
    neighbours and position; None if it is only bars and strokes (pause signs, frames, ...)"""
    bx = [list(b) for b in line['boxes']]
    top = min(b[1] for b in bx); bot = max(b[3] for b in bx)
    capH = max(b[3] - b[1] for b in bx)
    def kind(ch):
        return 'd' if ch.isdigit() else ('u' if ch.isupper() else ('l' if ch.islower() else 's'))
    # lower case: starts clearly below the top of the capitals
    for b in bx:
        if b[4].upper() in LOWERABLE or b[4].lower() in LOWERABLE:
            if b[1] >= top + 0.28 * capH and b[4].isalpha(): b[4] = b[4].lower()
    fixed = [b[4] for b in bx]
    for i, b in enumerate(bx):
        ch = b[4]
        if ch not in AMB: continue
        near = [bx[j][4] for j in (i - 1, i + 1) if 0 <= j < len(bx) and bx[j][4] not in AMB]
        if not near: near = [x[4] for x in bx if x[4] not in AMB]
        k = max(set(map(kind, near)), key=lambda t: sum(kind(x) == t for x in near)) if near else None
        opts = AMB[ch]
        if k == 'd': fixed[i] = next((o for o in opts if o.isdigit()), ch)
        elif k == 'u': fixed[i] = next((o for o in opts if o.isupper()), ch)
        elif k == 'l':
            low = [o for o in opts if o.islower()]
            if set(low) == {'l', 'i'}:
                fixed[i] = 'i' if (b[3] - b[1]) < 0.85 * capH else 'l'
            elif low: fixed[i] = low[0]
    for b, ch in zip(bx, fixed): b[4] = ch
    text = ''.join(fixed)
    if all(c in 'Il1i|/-+' for c in text) and not (text.isdigit() and line['score'] >= 0.8): return None
    return dict(line, boxes=[tuple(b) for b in bx], text=text)

def nrm(s): return s.replace('0', 'O').replace('1', 'I').replace('l', 'I').replace('i', 'I').upper()
def finalize2(line, p=None, force=False):
    """the line's text, from the shapes (positions, sizes, case where the shapes differ) and Tesseract (letter
    or digit); None unless both readings agree (up to look-alikes), it is more than bars and strokes, and (with
    the picture p) no letter-sized shape right next to it was left out. Decimal points and colons (too small to
    match) are put back in."""
    t = line.get('tess', '')
    if not t or (not force and nrm(t) != nrm(line['text'])): return None
    bx = [list(b) for b in line['boxes']]
    if len(bx) != len(t): return None
    ours = [b[4] for b in bx]
    # pass 1: digit or letter
    cls = []
    for i, tc in enumerate(t):
        if tc.isdigit(): cls.append(tc); continue
        if tc in 'Oo' and ((i and cls[-1].isdigit()) or (i + 1 < len(t) and t[i + 1].isdigit())): cls.append('0'); continue
        if tc in 'Il' and i and cls[-1].isdigit(): cls.append('1'); continue
        cls.append(tc)
    # pass 2: case. Letters whose shapes differ by case: as matched. The others (c o s u v w x z p, and I l i):
    # lower case when it starts clearly below the tallest letter of its word
    out = []
    for i, (b, tc) in enumerate(zip(bx, cls)):
        if not tc.isalpha(): out.append(tc); continue
        j0 = i
        while j0 > 0 and cls[j0 - 1].isalpha(): j0 -= 1
        j1 = i
        while j1 + 1 < len(cls) and cls[j1 + 1].isalpha(): j1 += 1
        wtop = min(bx[j][1] for j in range(j0, j1 + 1)); wH = max(bx[j][3] - bx[j][1] for j in range(j0, j1 + 1))
        low = b[1] >= wtop + 0.2 * wH
        word = cls[j0:j1 + 1]
        lowerctx = sum(1 for c in word if c.islower()) > sum(1 for c in word if c.isupper())
        if tc.upper() in 'IL' and tc != 'L':
            # a capital I starting a word (Icon, Invalid) as Tesseract read it; otherwise l in lower case words
            ch = 'i' if low and (b[3] - b[1]) < 0.85 * wH else ('I' if tc == 'I' and i == j0 and j1 > j0 else
                 'l' if lowerctx or (i > j0 and out[-1].islower()) else 'I')
        elif tc.lower() in 'cosuvwxzp': ch = tc.lower() if low else tc.upper()
        # the shape gives the case (the letter itself is Tesseract's: the shapes take R for B, H or P)
        elif ours[i].isalpha() and ours[i].lower() not in 'cosuvwxzpij': ch = tc.upper() if ours[i].isupper() else tc.lower()
        else: ch = tc
        out.append(ch)
    for b, ch in zip(bx, out): b[4] = ch
    text = ''.join(out)
    if all(c in 'Il1i|/-+' for c in text) and not text.isdigit(): return None
    capH = max(b[3] - b[1] for b in bx)
    if p is not None and line['light']:
        # light lettering on an opaque box (the creative style badges): not handled well yet, leave it
        from scipy import ndimage as _nd
        M = np.zeros(p.shape, bool)
        for b in bx: M[b[1]:b[3], b[0]:b[2]] = True
        ring = _nd.binary_dilation(M, iterations=5) & ~_nd.binary_dilation(M, iterations=3)
        if ring.any() and ((p[ring] & 15) >= 12).mean() > 0.5:
            # light lettering on a filled badge: only whole, confident words (a stray letter there is glaring)
            if not OPAQUE_OK or sum(b[4].isalpha() for b in bx) < 3 or line['score'] < 0.72: return None
    if p is not None:
        comps = components(p, line['light'])
        have = {tuple(b[:4]) for b in bx}
        top = min(b[1] for b in bx); bot = max(b[3] for b in bx)
        x0 = min(b[0] for b in bx); x1 = max(b[2] for b in bx)
        small = []
        for c in comps:
            key = (c['x0'], c['y0'], c['x1'], c['y1'])
            if key in have: continue
            h = c['y1'] - c['y0']; w = c['x1'] - c['x0']
            vov = min(bot, c['y1']) - max(top, c['y0'])
            if h <= max(2, capH // 4) + 1 and w <= max(2, capH // 4) + 1 and x0 < c['x0'] and c['x1'] < x1 and vov > 0:
                small.append(c); continue
            if h >= 0.5 * capH and vov >= 0.5 * h and (x0 - 0.7 * capH <= c['x1'] <= x0 or x1 <= c['x0'] <= x1 + 0.7 * capH):
                return None
            # a shape that merges into the line's first or last letter (a letter fused with a frame): unsure
            if vov >= 0.5 * capH and c['x0'] < x0 <= c['x1'] < x1 and h < 3 * capH:
                return None
        extra = []
        for c in small:                       # on the baseline, after a digit: '.'; with another above it: ':'
            if c['y1'] < bot - 2: continue
            before = [b for b in bx if b[2] <= c['x0']]
            if not before or not max(before, key=lambda b: b[2])[4].isdigit(): continue
            above = [d for d in small if d is not c and abs(d['x0'] - c['x0']) <= 1 and d['y1'] <= c['y0']]
            if above:
                d = above[0]; extra.append((c['x0'], d['y0'], c['x1'], c['y1'], ':', 1.0))
            else: extra.append((c['x0'], c['y0'], c['x1'], c['y1'], '.', 1.0))
        if extra:
            allb = sorted([tuple(b) for b in bx] + extra, key=lambda b: b[0])
            return dict(line, boxes=allb, text=''.join(b[4] for b in allb))
    return dict(line, boxes=[tuple(b) for b in bx], text=text)

def confirmed_lines(scanned, p, settled=()):
    """the lines of a picture to redraw: confirmed ones and settled ones (read by a person, or by the confirmed
    vocabulary), unless lettering that wasn't confirmed sits right next to them (a half-edited label would be
    worse than none)"""
    def bb(l): return (min(b[0] for b in l['boxes']), min(b[1] for b in l['boxes']), max(b[2] for b in l['boxes']), max(b[3] for b in l['boxes']))
    # a badge: all known (or, with no boxes, a picture left alone)
    if any(s.get('exclusive') for s in settled): return [s for s in settled if s.get('exclusive') and s.get('boxes')]
    good, bad = list(settled), []
    from_ = {(s.get('tess'), s.get('orig')) for s in settled}       # the scanned lines they were made from
    for s in settled: from_ |= set(s.get('covers', ()))
    for l in scanned:
        if (l.get('tess'), l['text']) in from_: continue
        f = finalize2(l, p)
        (good if f else bad).append(f or l)
    # dark 'lines' that are only the holes of light letters (the insides of 0s) aren't lettering next to anything
    lb = [bb(f) for f in good + bad if f['light']]          # (and the gaps between them: inside a light word's outline)
    def holes(f): return not f['light'] and all(any(c[0] <= b[0] and b[2] <= c[2] and c[1] <= b[1] and b[3] <= c[3] for c in lb) for b in f['boxes'])
    bad = [f for f in bad if not holes(f)]
    out = []
    for f in good:
        x0, y0, x1, y1 = bb(f)
        # a few small upright strokes are as likely a bit of an icon as letters (the face frame's "IL")
        if 'settled' not in f and y1 - y0 < 12 and set(f['text'].replace(' ', '')) <= set('IlL1|ij'): continue
        # a lone ring is as likely part of an icon (the pupil of an eye) as an O
        if 'settled' not in f and f['text'].strip() in ('O', 'o'): continue
        if not f.get('free') and any(b[0] < x1 + 4 and x0 - 4 < b[2] and b[1] < y1 + 4 and y0 - 4 < b[3] for b in map(bb, bad)): continue
        out.append(f)
    # a dark "letter" inside light lettering is a gap in that lettering, not a letter of its own
    lights = [bb(f) for f in good + bad if f['light']]
    def hit(f): return any(b[0] < c[2] and c[0] < b[2] and b[1] < c[3] and c[1] < b[3] for b in f['boxes'] for c in lights)
    return [f for f in out if f['light'] or 'settled' in f or not hit(f)]
