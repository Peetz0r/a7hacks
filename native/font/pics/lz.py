LENS = list(range(3, 17)) + [32, 64]
def block(d, i):
    """one block from d[i]: (bytes, index after it)"""
    if d[i] == 0x0f:
        n = d[i + 2] | d[i + 3] << 8; return bytes(d[i + 4:i + 4 + n]), i + 4 + n
    assert d[i] == 0xf0, (i, d[i])
    out = bytearray(); i += 1
    while True:
        fl = d[i]; i += 1
        for b in range(8):
            if fl >> b & 1:
                b0, b1 = d[i], d[i + 1]; i += 2
                n = LENS[b0 >> 4]; dist = (b0 & 15) << 8 | b1
                if dist == 0: return bytes(out), i
                for k in range(n): out.append(out[-dist])
            else:
                out.append(d[i]); i += 1
def decode(d, outlen=None):
    out = b''; i = 0; starts = []
    while i < len(d) and (outlen is None or len(out) < outlen):
        starts.append(i)
        b, i = block(d, i); out += b
    return out

# ---- encoder: same format, greedy longest match (lengths 3..16, 32, 64; distance 1..4095), 4 KB blocks
_CODE = {n: i for i, n in enumerate(LENS)}
def _block_enc(src):
    out = bytearray([0xf0]); items = []
    pos = 0; heads = {}
    n = len(src)
    while pos < n:
        best_len, best_d = 0, 0
        if pos + 3 <= n:
            key = src[pos:pos + 3]
            for cand in reversed(heads.get(key, [])[-64:]):
                d = pos - cand
                if d > 4095: break
                l = 0
                while l < 64 and pos + l < n and src[cand + l] == src[pos + l]: l += 1   # overlap allowed
                if l > best_len: best_len, best_d = l, d
                if l == 64: break
        if best_len >= 3:
            usable = max(L for L in LENS if L <= best_len)
            items.append((1, bytes([(_CODE[usable] << 4) | (best_d >> 8), best_d & 255])))
            step = usable
        else:
            items.append((0, src[pos:pos + 1])); step = 1
        for k in range(pos, min(pos + step, n - 2)):
            heads.setdefault(src[k:k + 3], []).append(k)
        pos += step
    items.append((1, b'\x00\x00'))
    for i in range(0, len(items), 8):
        grp = items[i:i + 8]
        out.append(sum(1 << b for b, (m, _) in enumerate(grp) if m))
        for _, b in grp: out += b
    return bytes(out)
def encode(raw):
    return b''.join(_block_enc(raw[i:i + 4096]) for i in range(0, len(raw), 4096))
