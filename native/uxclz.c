/* Sony's LZ77 as used for the pictures in the UI's .uxc files (and their firmware updates; cf. the Linux
 * kernel's lib/lz77): blocks of up to 4096 output bytes, each the type byte 0xf0, then groups of a flag byte
 * (bit 0 first: 0 = one literal byte, 1 = a match of two bytes: length code in the high nibble for lengths
 * 3..16, 32, 64; distance 1..4095 in the other 12 bits), ended by a match of distance 0.
 * The encoder picks the cheapest way to say each block (literal 9 bits, match 17 bits), so a picture whose
 * lettering we redraw still fits in the room the original had; or, much faster, the quick way (greedy), which
 * is used wherever that fits too, or else the same with a longer search (medium: most of the rest fit that way). Used by a7hacks (--font-patcher) and, built on its own with
 * -DUXCLZ_MAIN, by font/pics/mkpics.py, so both produce the same bytes. */
#ifndef UXCLZ_C
#define UXCLZ_C
static const unsigned char lz_lens[16] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};

/* unpacks src (n bytes) into dst (room cap): bytes written, or -1 if src isn't that format */
static int lz_decode(const unsigned char *src, int n, unsigned char *dst, int cap) {
    int i = 0, o = 0;
    while (i < n && o < cap) {
        if (src[i] != 0xf0) return o ? o : -1;       /* past the last block (padding) */
        int b0 = o; i++;
        for (;;) {
            if (i >= n) return -1;
            int fl = src[i++];
            for (int b = 0; b < 8; b++) {
                if (fl >> b & 1) {
                    if (i + 1 >= n) return -1;
                    int c0 = src[i], c1 = src[i + 1]; i += 2;
                    int len = lz_lens[c0 >> 4], d = (c0 & 15) << 8 | c1;
                    if (!d) goto next;
                    if (d > o - b0 || o + len > cap) return -1;
                    for (int k = 0; k < len; k++, o++) dst[o] = dst[o - d];
                } else {
                    if (i >= n || o >= cap) return -1;
                    dst[o++] = src[i++];
                }
            }
        }
next:   ;
    }
    return o;
}

#define LZ_CHAIN 96
static unsigned short lz_head[4096], lz_prev[4096], lz_start[4096];
static unsigned int lz_cost[4097];
static unsigned short lz_len[4096], lz_dist[4096];
/* packs one block (n <= 4096) at dst: bytes written */
static int lz_block(const unsigned char *s, int n, unsigned char *dst) {
    for (int h = 0; h < 4096; h++) lz_head[h] = 0xffff;
    for (int i = 0; i < n; i++) {
        lz_start[i] = 0xffff;
        if (i + 2 < n) {
            int h = (s[i] << 4 ^ s[i + 1] << 2 ^ s[i + 2] * 7) & 4095;
            lz_start[i] = lz_head[h]; lz_prev[i] = lz_head[h]; lz_head[h] = (unsigned short)i;
        }
    }
    lz_cost[n] = 0;
    for (int i = n - 1; i >= 0; i--) {
        unsigned int best = 9 + lz_cost[i + 1]; int bl = 1, bd = 0;
        int c = lz_start[i];
        for (int k = 0; c != 0xffff && k < LZ_CHAIN; k++, c = lz_prev[c]) {
            int d = i - c;
            if (d > 4095) break;
            int m = 0, lim = n - i < 64 ? n - i : 64;
            while (m < lim && s[c + m] == s[i + m]) m++;
            for (int L = 0; L < 16 && lz_lens[L] <= m; L++) {
                unsigned int v = 17 + lz_cost[i + lz_lens[L]];
                if (v < best || (v == best && lz_lens[L] > bl)) { best = v; bl = lz_lens[L]; bd = d; }
            }
            if (m == lim) break;     /* as long as a match can be here: one further back would cost the same, and
                                        the nearer one is kept on equal cost, so the result is the same (much faster
                                        on the flat, transparent parts) */
        }
        lz_cost[i] = best; lz_len[i] = (unsigned short)bl; lz_dist[i] = (unsigned short)bd;
    }
    int o = 0, i = 0, fpos = -1, nb = 8;
    dst[o++] = 0xf0;
    for (;;) {
        int last = i >= n;
        if (nb == 8) { fpos = o; dst[o++] = 0; nb = 0; }
        if (last) { dst[fpos] |= (unsigned char)(1 << nb); dst[o++] = 0; dst[o++] = 0; break; }
        if (lz_dist[i]) {
            int L = 0; while (lz_lens[L] != lz_len[i]) L++;
            dst[fpos] |= (unsigned char)(1 << nb);
            dst[o++] = (unsigned char)(L << 4 | lz_dist[i] >> 8); dst[o++] = (unsigned char)lz_dist[i];
            i += lz_len[i];
        } else dst[o++] = s[i++];
        nb++;
    }
    return o;
}
/* the quick way: at each place the longest match among the last few (chain) with the same 3 bytes (much
 * faster, a few per cent bigger) */
#define LZ_QCHAIN 16
#define LZ_MCHAIN 256
static int lz_block_quick(const unsigned char *s, int n, unsigned char *dst, int chain) {
    for (int h = 0; h < 4096; h++) lz_head[h] = 0xffff;
    int o = 0, i = 0, fpos = -1, nb = 8;
    dst[o++] = 0xf0;
    for (;;) {
        if (nb == 8) { fpos = o; dst[o++] = 0; nb = 0; }
        if (i >= n) { dst[fpos] |= (unsigned char)(1 << nb); dst[o++] = 0; dst[o++] = 0; break; }
        int bl = 0, bd = 0;
        if (i + 2 < n) {
            int h = (s[i] << 4 ^ s[i + 1] << 2 ^ s[i + 2] * 7) & 4095;
            int c = lz_head[h];
            int lim = n - i < 64 ? n - i : 64;
            for (int k = 0; c != 0xffff && k < chain; k++, c = lz_prev[c]) {
                if (bl && s[c + bl] != s[i + bl]) continue;   /* can't be longer than the best so far: the same result,
                                                                 without comparing it all */
                int m = 0;
                while (m < lim && s[c + m] == s[i + m]) m++;
                if (m > bl) { bl = m; bd = i - c; if (m == lim) break; }
            }
        }
        int step = 1;
        if (bl >= 3) {
            int L = 15; while (lz_lens[L] > bl) L--;
            dst[fpos] |= (unsigned char)(1 << nb);
            dst[o++] = (unsigned char)(L << 4 | bd >> 8); dst[o++] = (unsigned char)bd;
            step = lz_lens[L];
        } else dst[o++] = s[i];
        for (int k = i; k < i + step; k++)
            if (k + 2 < n) { int h = (s[k] << 4 ^ s[k + 1] << 2 ^ s[k + 2] * 7) & 4095; lz_prev[k] = lz_head[h]; lz_head[h] = (unsigned short)k; }
        i += step; nb++;
    }
    return o;
}
/* packs n bytes at dst (room: n + n / 8 + 16 per block is plenty): how LZ_CHEAPEST, LZ_QUICK or LZ_MEDIUM;
 * bytes written */
#define LZ_CHEAPEST 0
#define LZ_QUICK 1
#define LZ_MEDIUM 2
static int lz_encode(const unsigned char *s, int n, unsigned char *dst, int how) {
    int o = 0;
    for (int i = 0; i < n; i += 4096) {
        int b = n - i < 4096 ? n - i : 4096;
        o += how == LZ_CHEAPEST ? lz_block(s + i, b, dst + o) : lz_block_quick(s + i, b, dst + o, how == LZ_QUICK ? LZ_QCHAIN : LZ_MCHAIN);
    }
    return o;
}

#ifdef UXCLZ_MAIN
#include <stdio.h>
#include <stdlib.h>
/* uxclz e|q|m|d < in > out  (pack the cheapest way, the quick way, the medium way; unpack) */
int main(int argc, char **argv) {
    static unsigned char in[1 << 22], out[1 << 23];
    int n = (int)fread(in, 1, sizeof in, stdin);
    int m = argv[1][0] == 'd' ? lz_decode(in, n, out, sizeof out) : lz_encode(in, n, out, argv[1][0] == 'q' ? LZ_QUICK : argv[1][0] == 'm' ? LZ_MEDIUM : LZ_CHEAPEST);
    if (m < 0) return 1;
    fwrite(out, 1, (size_t)m, stdout);
    return 0;
}
#endif
#endif
