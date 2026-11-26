"""Builds the picture patches. For every picture whose lettering was found and confirmed: the picture with
Comic Relief lettering. What goes into the patch is only what we drew: the pixels that differ from the
original (after the one change made everywhere, fully transparent pixels getting colour 0), in the order the
camera stores them. The camera unpacks the original from the file, applies the same change and our pixels,
and packs it with the same encoder (uxclz.c): mkpics checks that the result fits in the room the original
had, and records its length and a hash, which the camera checks before writing anything."""
import sys, pickle, json, subprocess, collections, os; sys.path.insert(0, '.')
import numpy as np
from uxc import *; from ocr import *; from render import *
import render, hue
from ocr import finalize2, confirmed_lines
from tile import tile
FILES = ['image_cmn_43.uxc', 'image_EVF.uxc', 'image_DSLR_Only_43.uxc', 'image_DSC_Only_43.uxc']
UXCLZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'uxclz')
def fnv(b):
    h = 0x811c9dc5
    for c in b: h = ((h ^ c) * 0x01000193) & 0xffffffff
    return h
def fixed(p, lines):
    q = clean(p)
    for l in lines: q = redraw(q, dict(light=l['light'], boxes=l['boxes'], mbox=l.get('mbox'), span=l.get('span')))
    return q
def to_raw(q):
    h, w = q.shape; aw, ah = (w + 7) & ~7, (h + 7) & ~7
    full = np.zeros((ah, aw), np.uint16); full[:h, :w] = q
    return tile(full)
def encode(raw, how='e'): return subprocess.run([UXCLZ, how], input=raw, capture_output=True, check=True).stdout
def runs(old, new):
    """the pixels of new that differ from old (u16 each): u16 skip, u16 count, count pixels; ends with 0, 0"""
    a = np.frombuffer(old, '<u2'); b = np.frombuffer(new, '<u2')
    diff = np.nonzero(a != b)[0]; out = bytearray(); pos = 0; i = 0
    while i < len(diff):
        j = i
        while j + 1 < len(diff) and diff[j + 1] - diff[j] <= 3 and j + 1 - i < 65535: j += 1   # short gaps: keep going
        start, end = int(diff[i]), int(diff[j]) + 1
        skip = start - pos
        while skip > 65535: out += (65535).to_bytes(2, 'little') + (0).to_bytes(2, 'little'); skip -= 65535
        out += skip.to_bytes(2, 'little') + (end - start).to_bytes(2, 'little') + b[start:end].astype('<u2').tobytes()
        pos = end; i = j + 1
    return bytes(out + b'\0\0\0\0')
if __name__ == '__main__':
    S = pickle.load(open('scan2.pkl', 'rb'))
    cat = pickle.load(open('cat.pkl', 'rb')); byh = {}
    for r in cat: byh.setdefault(r['h_'], r)
    want = {}
    OV = pickle.load(open('overrides.pkl', 'rb')) if os.path.exists('overrides.pkl') else {}   # read by a person / the vocabulary
    for s in S:
        if s['fn'] not in FILES: continue
        L = confirmed_lines(s['lines'], byh[s['h_']]['pic'], OV.get(s['h_'], ()))
        if L: want[s['h_']] = L
    for h, L in OV.items():                       # pictures the scan found nothing in, whose lettering is known
        if h not in want and h in byh and any(l.get('settled') == 'known' for l in L): want[h] = list(L)
    stats = collections.Counter(); done = {}; out = {fn: [] for fn in FILES}; texts = {}
    files = {fn: open(APP + fn, 'rb').read() for fn in FILES}
    huew = {}                                      # does the picture take the accent colour (hue.py)?
    for r in cat:
        if r['fn'] not in FILES: continue
        if r['h_'] not in huew: huew[r['h_']] = bool(hue.wants(r['pic']))
        if r['h_'] not in want and not huew[r['h_']]: continue
        if r['len'] > 0xffffff: continue
        if r['h_'] not in done:
            p = r['pic']; L = want.get(r['h_'])
            res = None
            for hu in ([1, 0] if huew[r['h_']] and L else [huew[r['h_']]]):
                red = bool(hu) and r['res'] in hue.REDS          # a warning: red instead of green
                blue = bool(hu) and hue.blue(r['res'], r['w'], r['h'])   # the level: blue instead of green
                tint = (lambda q, red=red, blue=blue: hue.apply(q, red, blue)) if hu else (lambda q: q)
                base = to_raw(tint(clean(p)))
                for lv in ((8, 6, 4) if L else (0,)):
                    render.LEVELS = lv or 8
                    q = tint(fixed(p, L) if L else clean(p)); raw = to_raw(q)
                    enc = encode(raw, 'q'); slow = 0                # the quick way where it fits (the camera is slow)
                    if len(enc) > r['len']: enc = encode(raw, 'e'); slow = 1
                    res = (enc, raw, lv, slow, base, hu)
                    if len(enc) <= r['len']: break
                if len(res[0]) <= r['len']: break
                if hu: stats['hue dropped (no room)'] += 1
            enc, raw, lv, slow, base, hu = res
            done[r['h_']] = (enc, runs(base, raw), lv, len(base), slow, hu | (2 if hu and red else 0) | (4 if hu and blue else 0))
            texts[r['h_']] = [l['text'] for l in L] if L else []
        enc, rn, lv, rawlen, slow, hu = done[r['h_']]
        if len(enc) > r['len']: stats['toobig'] += 1; continue
        f = files[r['fn']]; start = r['ro'] + 4 + r['off']
        old = f[start:start + r['len']]
        # raw: top bit = the camera applies the accent colour table first; the next one: the warning (red) table
        # instead; the one after: the level's (blue)
        out[r['fn']].append([start, r['len'], fnv(old), len(enc) | slow << 31, fnv(enc), rawlen | (hu & 1) << 31 | (hu >> 1 & 1) << 30 | (hu >> 2 & 1) << 29, rn.hex(), r['id'], r['res']])
        stats['ok'] += 1; stats['levels%d' % lv] += 1; stats['slow'] += slow; stats['hue'] += hu & 1; stats['red'] += hu >> 1 & 1; stats['blue'] += hu >> 2 & 1
        stats['lettering' if r['h_'] in want else 'colour only'] += 1
        if lv == 4: print('4 levels:', r['fn'], hex(r['res']))
    for fn in FILES:
        json.dump({'size': len(files[fn]), 'head': files[fn][:16].hex(), 'pics': sorted(out[fn])}, open(fn + '.pics.json', 'w'))
        print(fn, len(out[fn]), 'pictures', sum(len(x[6]) // 2 for x in out[fn]), 'bytes of our pixels')
    print(stats)
    pickle.dump(texts, open('texts.pkl', 'wb'))
