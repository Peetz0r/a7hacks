import sys as _sys
_HELP = """usage: python3 gsheet.py        (a check, not a step)
Sheets of the shape groups in shapes.pkl that have more than one member: g0.png, g1.png, ...; also writes
multi.pkl. Reads $ICONS (default Sony_DI_Icons.ttf).

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
import os, pickle, sys
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont, ImageChops
shapes, groups = pickle.load(open('shapes.pkl', 'rb'))
f = TTFont(os.environ.get('ICONS', 'Sony_DI_Icons.ttf')); glyf = f['glyf']
sm = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 11)
def contours(gname):
    coords, ends, _ = glyf[gname].getCoordinates(glyf); cs, s = [], 0
    for e in ends: cs.append([tuple(p) for p in coords[s:e + 1]]); s = e + 1
    return cs
def draw_shape(s, cw, ch):
    cs = contours(s['glyph']); polys = [cs[s['ci']]] + [cs[k] for k in s['kids']]
    x0, y0, x1, y1 = s['bb']; sc = min((cw - 8) / max(1, x1 - x0), (ch - 8) / max(1, y1 - y0), 0.06)
    im = Image.new('1', (cw, ch), 0)
    for p in polys:
        m = Image.new('1', (cw, ch), 0)
        ImageDraw.Draw(m).polygon([(4 + (x - x0) * sc, ch - 4 - (y - y0) * sc) for x, y in p], fill=1)
        im = ImageChops.logical_xor(im, m)
    return im
def sheet(ids, fn, cols=20, cw=60, ch=78):
    rows = (len(ids) + cols - 1) // cols
    out = Image.new('L', (cols * cw, rows * (ch + 14)), 255); d = ImageDraw.Draw(out)
    for i, gi in enumerate(ids):
        s = shapes[groups[gi][0]]; x = (i % cols) * cw; y = (i // cols) * (ch + 14)
        out.paste(0 if s['sign'] > 0 else 140, (x, y), draw_shape(s, cw, ch))
        d.text((x + 2, y + ch), '%d·%d' % (gi, len(groups[gi])), font=sm, fill=0)
        d.rectangle([x, y, x + cw - 1, y + ch + 13], outline=200)
    out.save(fn)
if __name__ == '__main__':
    multi = sorted([gi for gi in range(len(groups)) if len(groups[gi]) >= 2], key=lambda g: -len(groups[g]))
    for k in range(0, len(multi), 200):
        sheet(multi[k:k + 200], 'g%d.png' % (k // 200)); print('g%d.png' % (k // 200), len(multi[k:k + 200]))
    pickle.dump(multi, open('multi.pkl', 'wb'))
