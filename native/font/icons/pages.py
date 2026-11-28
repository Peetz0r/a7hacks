import sys as _sys
_HELP = """usage: python3 pages.py         (optional step 5, see README)
Before/after sheets of every icon icons.py changed: page0.png, page1.png, ... (from $ICONS, icons.ttf and
icons.done.json).

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
import os
import json, sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
SRC = os.environ.get('ICONS', 'Sony_DI_Icons.ttf')
f = TTFont(SRC); rev = {v: k for k, v in f.getBestCmap().items()}
done = json.load(open('icons.done.json'))
names = sorted([n for n in done if n in rev], key=lambda n: rev[n])
size = 34; fo = ImageFont.truetype(SRC, size); fn = ImageFont.truetype('icons.ttf', size)
sm = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 10)
cols, cw, ch = 5, 186, 58; per = cols * 22
for p in range(0, len(names), per):
    sub = names[p:p + per]; rows = (len(sub) + cols - 1) // cols
    im = Image.new('L', (cols * cw, rows * ch), 0); d = ImageDraw.Draw(im)
    for k, n in enumerate(sub):
        x = (k % cols) * cw; y = (k // cols) * ch; t = chr(rev[n])
        d.text((x + 3, y + 2), t, font=fo, fill=255); d.text((x + cw // 2, y + 2), t, font=fn, fill=255)
        d.text((x + 3, y + 45), '%04X %s' % (rev[n], done[n]), font=sm, fill=170)
        d.line([x + cw - 1, y, x + cw - 1, y + ch], fill=70)
    im.save('page%d.png' % (p // per)); print('page%d.png' % (p // per), len(sub))
