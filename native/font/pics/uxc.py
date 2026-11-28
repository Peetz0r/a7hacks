import os, struct, numpy as np, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lz import decode
from tile import untile
APP = os.environ.get('UXCAPP', '')   # the folder with the camera's image_*.uxc (its /usr/share/app; not included)
def records(fn):
    if not APP:
        sys.exit('set UXCAPP to the folder with the camera\'s image_*.uxc (its /usr/share/app): see README')
    f = open(os.path.join(APP, fn), 'rb').read()
    a = np.frombuffer(f[:len(f) // 4 * 4], dtype='<u4')
    idx = np.nonzero(((a[:-1] >> 24) == 0x1a) & (((a[1:] >> 24) == 0xe0) | ((a[1:] >> 24) == 0xe8)) & ((a[1:] & 0xffffff) == 0))[0]
    out = []
    for k in idx:
        ro = int(k) * 4
        rid, fl, size, ln, off = struct.unpack_from('<5I', f, ro)
        if fl >> 24 == 0xe8:     # a stretchable picture (highlight bars, boxes): 5 more words before its length, offset
            ln, off = struct.unpack_from('<2I', f, ro + 32)
        out.append(dict(ro=ro, id=rid, res=(rid & 0xffffff) | 0x02000000, flags=fl, w=size & 0xffff, h=size >> 16, len=ln, off=off))
    return f, out
def picture(f, r):
    """upright (top row first) RGBA4444 array h x w, or None"""
    if r['flags'] >> 24 not in (0xe0, 0xe8): return None
    d = f[r['ro'] + 4 + r['off']: r['ro'] + 4 + r['off'] + r['len']]
    aw, ah = (r['w'] + 7) & ~7, (r['h'] + 7) & ~7
    raw = decode(d, aw * ah * 2)
    if len(raw) != aw * ah * 2: return None
    return untile(raw, aw, ah)[:r['h'], :r['w']]          # rows top-down, padding right and below
