"""The accent colour: Sony's orange turned another hue (HUE degrees, default 75: green). A table from each
12-bit colour (RGB444) to its new colour: the warm ones (red through orange to amber) turned by HUE, all
others (greys, blues, ...) as they were. Applied to whole pictures that use the orange accent; not to ones
that are red for a reason (the compass needle, Part R, the warning frames) nor to photos (the sample
pictures). The camera applies the same table (picpatch.h: pic_hue) to the pictures marked for it, so only
our own lettering goes into the patch as pixels."""
import colorsys, os
import numpy as np
HUE = int(os.environ.get('HUE', '75'))
WARM = (340, 70)                       # hues counted as warm: 340..360 and 0..70 degrees

def _lut():
    t = np.arange(4096, dtype=np.uint16)
    if not HUE: return t
    for c in range(4096):
        r, g, b = (c >> 8 & 15) / 15, (c >> 4 & 15) / 15, (c & 15) / 15
        h, s, v = colorsys.rgb_to_hsv(r, g, b)
        if s == 0 or not (h * 360 >= WARM[0] or h * 360 < WARM[1]): continue
        r2, g2, b2 = colorsys.hsv_to_rgb((h + HUE / 360) % 1, s, v)
        t[c] = int(round(r2 * 15)) << 8 | int(round(g2 * 15)) << 4 | int(round(b2 * 15))
    return t
LUT = _lut()

# Pictures that warn (the empty battery): Sony made them orange, next to a red that would have clashed; with the
# accent green they become red instead: the warm hues pulled down by RED degrees (not below 0: red stays red).
RED = int(os.environ.get('RED', '30'))
REDS = {0x205c01b, 0x20e8011}                  # resource ids: the empty battery (image_cmn_43, image_EVF)
def _redlut():
    t = np.arange(4096, dtype=np.uint16)
    if not HUE: return t
    for c in range(4096):
        r, g, b = (c >> 8 & 15) / 15, (c >> 4 & 15) / 15, (c & 15) / 15
        h, s, v = colorsys.rgb_to_hsv(r, g, b)
        if s == 0 or not (h * 360 < WARM[1]): continue
        r2, g2, b2 = colorsys.hsv_to_rgb(max(0.0, h - RED / 360), s, v)
        t[c] = int(round(r2 * 15)) << 8 | int(round(g2 * 15)) << 4 | int(round(b2 * 15))
    return t
REDLUT = _redlut()

def apply(p, red=False):
    """the picture (uint16 RGBA4444) with the table applied (alpha kept); red: the warning table instead"""
    return (((REDLUT if red else LUT)[p >> 4].astype(np.uint32) << 4) | (p & 15)).astype(np.uint16)

def hsv(p):
    a = p & 15; R = ((p >> 12) & 15).astype(float); G = ((p >> 8) & 15).astype(float); B = ((p >> 4) & 15).astype(float)
    mx = np.maximum(np.maximum(R, G), B); mn = np.minimum(np.minimum(R, G), B); d = mx - mn
    h = np.zeros_like(mx); nz = d > 0
    rr = nz & (mx == R); gg = nz & (mx == G) & ~rr; bb = nz & ~rr & ~gg
    h[rr] = ((G - B)[rr] / d[rr]) % 6; h[gg] = (B - R)[gg] / d[gg] + 2; h[bb] = (R - G)[bb] / d[bb] + 4
    return h * 60, np.where(mx > 0, d / np.maximum(mx, 1), 0), mx, a

def wants(p):
    """does this picture use the orange accent (and isn't a photo, nor red for a reason)?"""
    if not HUE: return False
    h, s, v, a = hsv(p)
    m = (a >= 8) & (s >= 0.35) & (v >= 6)
    n = int(m.sum())
    if n < 6: return False
    hm = h[m]
    orange = ((hm >= 15) & (hm < 55)).mean()
    other = ((hm >= WARM[1]) & (hm < WARM[0])).mean()
    if orange < 0.25: return False                     # red ones (or not warm at all)
    # a photo (the sample pictures, the lens backgrounds): many colours (not a thin gradient like a scroll
    # bar), or a fair number with many muted warm tones (skin, wood, sunsets)
    nc = len(np.unique(p[a >= 8] >> 4))
    mv = (a >= 8) & (v >= 3)
    muted = ((s > 0) & (s < 0.35) & mv).sum() / max(1, mv.sum())
    if nc >= 120 and min(p.shape) > 48: return False
    if nc >= 100 and muted >= 0.3: return False
    if other >= 0.2 and nc >= 64: return False
    if muted >= 0.25 and nc >= 60: return False        # skin tone swatches
    if np.median(hm) >= 45: return False               # yellow things (the self-timer count), not the orange accent
    return True

def colours(raw):
    """the text accent colours: the entries of color_cmn.uxc (id 3a0940nn, then R, G, B, A) in the warm range,
    turned like the pictures (in full 8-bit precision). -> [(file offset of R, old RGBA bytes, new RGBA bytes)]"""
    import struct
    out = []
    i = 0x4c
    while HUE and i + 8 <= len(raw):
        cid = struct.unpack('<I', raw[i:i + 4])[0]
        r, g, b, a = raw[i + 4:i + 8]
        if cid >> 8 == 0x3a0940:
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            # the orange accent only (not the red, nor the green and blue that are there already)
            if s > 0.3 and 15 <= h * 360 < 55:
                r2, g2, b2 = colorsys.hsv_to_rgb((h + HUE / 360) % 1, s, v)
                out.append((i + 4, bytes([r, g, b, a]), bytes([round(r2 * 255), round(g2 * 255), round(b2 * 255), a])))
        i += 8
    return out

def styles(raw):
    """the text styles' colours: style_cmn.uxc. A style is a u32 id 2607a0nn, then u32 kind; kind 8 (text with an
    outline) has its RGBA at +12. The orange ones turned like colours() does -> [(offset, old, new)]"""
    import struct
    out = []
    for i in range(0x10, len(raw) - 16, 4):
        cid, kind = struct.unpack('<II', raw[i:i + 8])
        if cid >> 8 != 0x2607a0 or kind != 8: continue
        r, g, b, a = raw[i + 12:i + 16]
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if HUE and s > 0.3 and 15 <= h * 360 < 55:
            r2, g2, b2 = colorsys.hsv_to_rgb((h + HUE / 360) % 1, s, v)
            out.append((i + 12, bytes([r, g, b, a]), bytes([round(r2 * 255), round(g2 * 255), round(b2 * 255), a])))
    return out
