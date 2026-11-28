"""The creative style badges (Std., Vivid, ... with or without a number tab): a fixed set of words in a fixed
layout, so their lines are made from the shapes directly rather than from the scan, which reads them unevenly
(the i's dot apart from its stem, 'et' as one shape, the dark number on its light tab mostly missed)."""
import difflib
import numpy as np
from scipy import ndimage
from ocr import components, tpl, FONTS
from PIL import ImageFont
DOTTED = {'Std', 'Port', 'Land'}      # these end in a full stop
STYLES = ['Std', 'Vivid', 'Ntrl', 'Clear', 'Deep', 'Light', 'Port', 'Land', 'Sunset', 'Night', 'Autm', 'B/W', 'Sepia']
from render import COMIC
_f = ImageFont.truetype(COMIC, 100)

def style_of(lines):
    """(word, scanned line) if a scanned line reads as one of the style words"""
    best = (0, None, None)
    for l in lines:
        for s in STYLES:
            for t in (l.get('tess') or '', l['text']):
                # case counts (STD elsewhere is not Std.); I, l, 1, j and | are told apart by neither reading
                k = lambda x: x.replace('/', '').translate(str.maketrans('Il1j|i', 'llllll'))
                r = difflib.SequenceMatcher(None, k(t), k(s)).ratio() if k(t)[:1] == k(s)[:1] else 0   # from its first letter
                if r > best[0]: best = (r, s, l)
    return (best[1], best[2]) if best[0] >= 0.8 else (None, None)    # not Part (Partial Color), SftH

def ov(a0, a1, b0, b1): return min(a1, b1) - max(a0, b0)

def split_widths(n, ws, W):
    """cut W columns into len(ws) parts proportional to ws: the cut positions"""
    tot = sum(ws); cuts = []; acc = 0
    for w in ws[:-1]:
        acc += w; cuts.append(int(round(W * acc / tot)))
    return cuts

def digit(c):
    """(score, digit) for a shape: the best match among the digits only"""
    h, w = c['mask'].shape; m = c['mask'].astype(float); best = (0, None)
    for d in '0123456789':
        for fi in range(len(FONTS)):
            t = tpl(d, h, w, fi); u = np.maximum(m, t).sum()
            v = np.minimum(m, t).sum() / u if u else 0
            if v > best[0]: best = (v, d)
    return best

def lines_for(p, scanned):
    """the badge's lines (word, and number if it has one), or None if this isn't one or it doesn't add up"""
    word, l = style_of(scanned)
    if not word or not l['light']: return None
    by0 = min(b[1] for b in l['boxes']); by1 = max(b[3] for b in l['boxes'])
    bx0 = min(b[0] for b in l['boxes']); bx1 = max(b[2] for b in l['boxes'])
    H = by1 - by0
    comps = components(p, True)
    # the badge's frame: a light shape around the word, not much taller than it
    if not any(c['x0'] < bx0 and c['x1'] > bx1 and c['y0'] < by0 and c['y1'] > by1 and c['y1'] - c['y0'] <= 3 * H
               for c in comps): return None
    # letters: light shapes on the line's rows, not the frame or tab (too big) nor the sparkle (above)
    cand = [c for c in comps if c['y1'] - c['y0'] <= 1.5 * H and c['x1'] - c['x0'] <= 2.5 * H
            and ov(c['y0'], c['y1'], by0, by1) >= 0.5 * (c['y1'] - c['y0']) and bx0 - 2 <= c['x0'] and c['x1'] <= bx1 + 2 * H]
    small = [c for c in comps if c['y1'] - c['y0'] <= 0.4 * H and c['x1'] - c['x0'] <= 0.4 * H
             and bx0 - 2 <= c['x0'] and c['x1'] <= bx1 + 2 * H and by0 - 0.3 * H <= c['y0'] and c['y1'] <= by1 + 2]
    big = [c for c in cand if c['y1'] - c['y0'] > 0.4 * H]
    if not big: return None
    big.sort(key=lambda c: c['x0'])
    # an i's dot (a small shape above a stem) belongs to the stem; a small one on the baseline after the
    # last letter is a full stop
    groups = [[c] for c in big]
    dot = None
    for c in small:
        if c in big: continue
        g = next((g for g in groups if ov(c['x0'], c['x1'], g[0]['x0'], g[0]['x1']) > 0 and c['y1'] <= g[0]['y0'] + 1), None)
        if g: g.append(c); continue
        if word in DOTTED and c['x0'] >= max(x['x1'] for x in big) and c['y1'] >= by1 - 2: dot = c
    # the dimmed small ones (light letters packed on a light box) are left alone: too tight to redraw cleanly
    M = np.zeros(p.shape, bool)
    for c in big: M[c['y0']:c['y1'], c['x0']:c['x1']] |= c['mask']
    ring = ndimage.binary_dilation(M, iterations=2) & ~ndimage.binary_dilation(M, iterations=1)
    if ring.any() and ((((p >> 12) & 15) >= 9) & ((p & 15) >= 8))[ring].mean() > 0.25: return None
    letters = [ch for ch in word]
    if len(groups) > len(letters): return None
    # fewer shapes than letters: some are joined ('et'); cut the widest ones by the letters' widths
    assign = [1] * len(groups)
    while sum(assign) < len(letters):
        k = max(range(len(groups)), key=lambda i: (groups[i][0]['x1'] - groups[i][0]['x0']) / assign[i])
        assign[k] += 1
    boxes, mbox, i = [], [], 0
    for g, n in zip(groups, assign):
        for c in g: mbox.append((c['x0'], c['y0'], c['x1'], c['y1']))
        chs = letters[i:i + n]; i += n
        x0 = min(c['x0'] for c in g); x1 = max(c['x1'] for c in g); y0 = min(c['y0'] for c in g); y1 = max(c['y1'] for c in g)
        if n == 1: boxes.append((x0, y0, x1, y1, chs[0], 1.0)); continue
        c = g[0]; m = c['mask']; W = c['x1'] - c['x0']
        cuts = [0] + split_widths(n, [_f.getlength(ch) for ch in chs], W) + [W]
        for ch, a, b in zip(chs, cuts, cuts[1:]):
            rows = np.nonzero(m[:, a:b].any(axis=1))[0]
            if not len(rows): return None
            boxes.append((c['x0'] + a, c['y0'] + int(rows[0]), c['x0'] + b, c['y0'] + int(rows[-1]) + 1, ch, 1.0))
    if dot:
        boxes.append((dot['x0'], dot['y0'], dot['x1'], dot['y1'], '.', 1.0)); mbox.append((dot['x0'], dot['y0'], dot['x1'], dot['y1']))
    text = ''.join(b[4] for b in boxes)
    out = [dict(text=text, score=1.0, light=True, H=H, boxes=boxes, mbox=mbox, tess=l.get('tess'), sim=1.0,
                settled='badge', orig=l['text'], exclusive=True)]
    # the number: a dark shape left of the word, as tall as the letters (on the tab)
    wx0 = min(b[0] for b in boxes); capH = max(b[3] - b[1] for b in boxes)
    dk = [c for c in components(p, False) if c['x1'] <= wx0 and c['y1'] - c['y0'] >= 0.7 * capH
          and ov(c['y0'], c['y1'], by0, by1) >= 0.6 * capH and c['x1'] - c['x0'] <= capH]
    if len(dk) == 1:
        c = dk[0]
        s, d = digit(c)
        # a number sits on its light tab: light all around it
        M = np.zeros(p.shape, bool); M[c['y0']:c['y1'], c['x0']:c['x1']] = True
        ring = ndimage.binary_dilation(M, iterations=2) & ~M
        if ((((p >> 12) & 15) >= 9) & ((p & 15) >= 8))[ring].mean() < 0.7 or s < 0.5: return out
        out.append(dict(text=d or '?', score=s, light=False, H=c['y1'] - c['y0'],
                        boxes=[(c['x0'], c['y0'], c['x1'], c['y1'], d or '?', s)], tess=None, sim=1.0,
                        settled='badge', orig='', exclusive=True))
    return out

# the D-R / DRO / HDR boxes (with OFF, AUTO, Lv1.., 1.0EV.. below): dark letters on a light box. The scan reads
# them unevenly (D-R as "-B"), so they are found by shape: a light box with three dark shapes inside that match
# one of the three words.
BOXWORDS = ('D-R', 'DRO', 'HDR', 'ISO')
def best_of(c, chars):
    h, w = c['mask'].shape; m = c['mask'].astype(float); best = (0, None)
    for ch in chars:
        for fi in range(len(FONTS)):
            t = tpl(ch, h, w, fi); u = np.maximum(m, t).sum()
            v = np.minimum(m, t).sum() / u if u else 0
            if v > best[0]: best = (v, ch)
    return best

def box_lines(p, scanned):
    out = []
    dark = [c for c in components(p, False) if c['area'] >= 3]
    for b in components(p, True):
        bw, bh = b['x1'] - b['x0'], b['y1'] - b['y0']
        if bh < 8 or bw < 1.3 * bh or bw > 4 * bh: continue
        ins = sorted([c for c in dark if b['x0'] < c['x0'] and c['x1'] < b['x1'] and b['y0'] < c['y0'] and c['y1'] < b['y1']],
                     key=lambda c: c['x0'])
        if len(ins) != 3: continue
        capH = max(c['y1'] - c['y0'] for c in ins)
        if capH < 0.45 * bh: continue
        chs = []
        for c in ins:
            if c['y1'] - c['y0'] < 0.4 * capH: chs.append((1.0, '-')); continue
            s, ch = best_of(c, 'DROHB0IS5')
            chs.append((s, ch))
        word = ''.join(ch for _, ch in chs).replace('0', 'O').replace('5', 'S')
        if word not in BOXWORDS or min(s for s, _ in chs) < 0.5: continue
        boxes = [(c['x0'], c['y0'], c['x1'], c['y1'], ch, 1.0) for c, ch in zip(ins, word)]
        covers = [(l.get('tess'), l['text']) for l in scanned
                  if ov(min(x[0] for x in l['boxes']), max(x[2] for x in l['boxes']), b['x0'], b['x1']) > 0
                  and ov(min(x[1] for x in l['boxes']), max(x[3] for x in l['boxes']), b['y0'], b['y1']) > 0]
        out.append(dict(text=word, score=1.0, light=False, H=capH, boxes=boxes, tess=word, sim=1.0, settled='box',
                        orig='', free=True, covers=covers))
    return out

def number_line(p, l):
    """a light line Tesseract reads as a whole number (the ISO values, ...) whose digits the shape matcher lost
    or misread: the line's shapes, one per digit, all as tall as each other -> the line, or None"""
    t = (l.get('tess') or '').replace('O', '0').replace('o', '0')
    if not l['light'] or len(t) < 2 or not t.isdigit(): return None
    bx = l['boxes']
    y0 = min(b[1] for b in bx); y1 = max(b[3] for b in bx); x0 = min(b[0] for b in bx); x1 = max(b[2] for b in bx)
    H = y1 - y0
    cs = [c for c in components(p, True) if ov(c['y0'], c['y1'], y0, y1) >= 0.6 * (c['y1'] - c['y0'])
          and c['y1'] - c['y0'] >= 0.6 * H and c['y1'] - c['y0'] <= 1.3 * H and c['x1'] - c['x0'] <= 1.2 * H
          and x0 - H <= c['x0'] and c['x1'] <= x1 + H]
    cs.sort(key=lambda c: c['x0'])
    if len(cs) != len(t): return None
    hs = [c['y1'] - c['y0'] for c in cs]
    if max(hs) > 1.25 * min(hs): return None
    if any(cs[i + 1]['x0'] - cs[i]['x1'] > 0.6 * H for i in range(len(cs) - 1)): return None
    # a point or a colon between the digits (or a slash before them) means it isn't a whole number
    xs0, xs1 = cs[0]['x0'], cs[-1]['x1']
    have = {(c['x0'], c['y0'], c['x1'], c['y1']) for c in cs}
    for c in components(p, True):
        if (c['x0'], c['y0'], c['x1'], c['y1']) in have: continue
        if ov(c['y0'], c['y1'], y0, y1) > 0 and xs0 - H <= c['x0'] and c['x1'] <= xs1 and c['y1'] - c['y0'] < 0.6 * H: return None
    boxes = [(c['x0'], c['y0'], c['x1'], c['y1'], d, 1.0) for c, d in zip(cs, t)]
    return dict(l, boxes=boxes, text=t, settled='number', orig=l['text'], free=True)

def iso_line(p, lines):
    """an 'ISO' the scan missed (above or beside a number it read): three light shapes in a row, as tall as each
    other, matching I, S, O -> the line, or None"""
    used = {tuple(b[:4]) for l in lines for b in l['boxes']}
    cs = [c for c in components(p, True) if (c['x0'], c['y0'], c['x1'], c['y1']) not in used and c['y1'] - c['y0'] >= 6]
    cs.sort(key=lambda c: (c['y0'], c['x0']))
    for i in range(len(cs) - 2):
        tri = sorted(cs[i:i + 3], key=lambda c: c['x0'])
        hs = [c['y1'] - c['y0'] for c in tri]; H = max(hs)
        if max(hs) > 1.2 * min(hs) or max(c['y0'] for c in tri) - min(c['y0'] for c in tri) > 0.2 * H: continue
        if any(tri[k + 1]['x0'] - tri[k]['x1'] > 0.5 * H or tri[k + 1]['x0'] < tri[k]['x1'] for k in range(2)): continue
        # thin small letters match the (bold) reference shapes poorly, and S/5, O/0 look alike: those pairs count
        rd = [best_of(c, ch) for c, ch in zip(tri, 'ISO')]
        alt = [best_of(c, 'ISO0123456789')[1] for c in tri]
        if any(s < 0.4 for s, _ in rd) or alt[0] != 'I' or alt[1] not in 'S5' or alt[2] not in 'O0': continue
        # a bar right above or below (the limits of a value range): too tight to redraw, leave the picture
        ty0 = min(c['y0'] for c in tri); ty1 = max(c['y1'] for c in tri)
        if any(c['x1'] - c['x0'] > 2 * H and (abs(c['y0'] - ty1) <= 0.5 * H or abs(ty0 - c['y1']) <= 0.5 * H)
               for c in components(p, True)): return None
        boxes = [(c['x0'], c['y0'], c['x1'], c['y1'], ch, 1.0) for c, ch in zip(tri, 'ISO')]
        return dict(text='ISO', score=1.0, light=True, H=H, boxes=boxes, tess='ISO', sim=1.0, settled='word',
                    orig='', free=True)
    return None


def hi_line(p, scanned):
    """'HI' (ISO HI) the scan read with pieces of the ISO box in it: an H and an I side by side, as tall as each
    other, among the light shapes of the line Tesseract reads as HI -> the line, or None"""
    l = next((l for l in scanned if l['light'] and (l.get('tess') or '').upper() == 'HI'), None)
    if not l: return None
    cs = [c for c in components(p, True) if any(tuple(b[:4]) == (c['x0'], c['y0'], c['x1'], c['y1']) for b in l['boxes'])]
    cs.sort(key=lambda c: c['x0'])
    for i in range(len(cs)):
      for k in range(i + 1, len(cs)):
        h, j = cs[i], cs[k]
        H = h['y1'] - h['y0']
        if abs((j['y1'] - j['y0']) - H) > 0.15 * H or abs(j['y0'] - h['y0']) > 0.15 * H or j['x0'] - h['x1'] > 0.6 * H: continue
        if best_of(h, 'HIBO')[1] != 'H' or j['x1'] - j['x0'] > 0.45 * H: continue
        boxes = [(h['x0'], h['y0'], h['x1'], h['y1'], 'H', 1.0), (j['x0'], j['y0'], j['x1'], j['y1'], 'I', 1.0)]
        return dict(l, boxes=boxes, text='HI', settled='word', orig=l['text'], free=True)
    return None

def warn_boxes(p):
    """Sony's warning signs in a picture: light boxes, about square, with a '!' cut out (two holes in the
    middle, the stem's above the dot's): their (x0, y0, x1, y1)"""
    out = []
    for c in components(p, True):
        h, w = c['y1'] - c['y0'], c['x1'] - c['x0']
        if h < 8 or not 0.85 <= w / h <= 1.25: continue
        f = ndimage.binary_fill_holes(c['mask'])
        if f.mean() < 0.85: continue                   # a box (rounded corners), not a letter with holes
        hl, n = ndimage.label(f & ~c['mask'])
        if n != 2: continue
        st, dt = sorted(ndimage.find_objects(hl), key=lambda s: s[0].start)
        if dt[0].start < st[0].stop or st[0].stop - st[0].start < 1.5 * (dt[0].stop - dt[0].start): continue
        if any(abs((s[1].start + s[1].stop) / 2 - w / 2) > 0.15 * w for s in (st, dt)): continue
        out.append((c['x0'], c['y0'], c['x1'], c['y1']))
    return out

def warn_lines(p, settled):
    """a '!' line of its own for each warning sign not already in a settled line (also beside an icon, where
    no lettering was read), staying where it is: render.py draws it as a rounded box with Comic Relief's '!'
    cut out"""
    out = []
    ex = any(l.get('exclusive') for l in settled)
    if ex and not any(l.get('boxes') for l in settled): return out          # a picture left alone on purpose
    for x0, y0, x1, y1 in warn_boxes(p):
        if any(b[4] == '!' and abs(b[0] - x0) <= 2 and abs(b[1] - y0) <= 2 for l in settled for b in l.get('boxes', ())): continue
        out.append(dict(text='!', score=1.0, light=True, H=y1 - y0, boxes=[(x0, y0, x1, y1, '!', 1.0)], tess='!',
                        sim=1.0, settled='known', orig='', free=True, span=(x0, x1), exclusive=ex))
    return out
