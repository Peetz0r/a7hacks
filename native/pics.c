/* ---- the UI's pictures with lettering in them (part of --font-patcher; included by a7hacks.c) ----
 * Many things the camera shows as text are pictures Sony drew in Univers: the focal lengths in the SteadyShot
 * list, the menu page numbers, AWB, MF, ISO, ... They live in /usr/share/app/image_*.uxc, which the UI maps.
 * A picture's record gives where its data is (its own data area, a fixed number of bytes); the data is Sony's
 * LZ77 (uxclz.c), unpacked by the camera's hardware straight into the GPU's layout (RGBA4444 in 8x8 tiles).
 * picpatch.h (made by font/pics/mkpics.py and font/mkheader.py) has, per picture we change: where its data
 * area is and how long, a hash of the original data, the length and hash of the new data (top bit of the
 * length: packed the slow, cheapest way; otherwise the quick way, which is most of them), the size unpacked,
 * and our pixels: only the ones we drew (Comic Relief lettering, and what we cleared around the old one), as
 * runs (u16 skip, u16 count, the pixels; ending with 0, 0), themselves packed with the same LZ77. Pictures
 * in Sony's orange accent are marked (top bit of _raw): their colours go through pic_hue (the accent in another
 * hue, green) first, so only our lettering needs pixels here. To make the new picture we unpack the original from the file, give fully transparent pixels colour 0
 * (everywhere, so it packs tighter), put our pixels in, and pack it again; it is written only if it comes out
 * exactly as mkpics computed (length and hash), with zeros after it to the end of the area. Like the font, it
 * goes into the camera's in-memory copy of the file (whose pages we lock: only the ones that change, the main
 * file is 30 MB); a reboot brings back the original. */
#include "uxclz.c"
#include "picpatch.h"
static struct pp {
    const char *path; u32 size; const u8 *head; int count;
    const u32 *off, *len, *old, *nlen, *nhash, *raw, *run, *rlen; const u8 *runs;
    const u32 *rid; int nrid;
    const u8 *map; u8 on;
} pps[] = {
    {PICDIR "image_cmn_43.uxc", PC_SIZE, pc_head, PC_COUNT, pc_off, pc_len, pc_old, pc_nlen, pc_new, pc_raw, pc_run, pc_rlen, pc_runs, pc_rid, PC_NRID, 0, 0},
    {PICDIR "image_EVF.uxc", PE_SIZE, pe_head, PE_COUNT, pe_off, pe_len, pe_old, pe_nlen, pe_new, pe_raw, pe_run, pe_rlen, pe_runs, pe_rid, PE_NRID, 0, 0},
    {PICDIR "image_DSLR_Only_43.uxc", PD_SIZE, pd_head, PD_COUNT, pd_off, pd_len, pd_old, pd_nlen, pd_new, pd_raw, pd_run, pd_rlen, pd_runs, pd_rid, PD_NRID, 0, 0},
    {PICDIR "image_DSC_Only_43.uxc", PS_SIZE, ps_head, PS_COUNT, ps_off, ps_len, ps_old, ps_nlen, ps_new, ps_raw, ps_run, ps_rlen, ps_runs, ps_rid, PS_NRID, 0, 0},
};
#define NPP ((int)(sizeof pps / sizeof pps[0]))
static u8 pbad[NPP][PIC_MAXCOUNT];       /* pictures that didn't come out as expected: left alone */
static u8 pbuf[PIC_MAXRAW + 16], pout[PIC_MAXRAW + PIC_MAXRAW / 8 + 4096], prun[PIC_MAXRUNS + 16];
static int picflush;                     /* the label store also gives up its copies of the pictures we change */

static u32 pfnv(const u8 *p, u32 n) { u32 h = 0x811c9dc5u; for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u; return h; }
static void picunmap(int k) { if (pps[k].map) sc(SYS_MUNMAP, (long)pps[k].map, pps[k].size, 0, 0, 0); pps[k].map = 0; }
/* maps picture file k (lock: and locks the pages that change): 0 ok, <0 error */
static int picmap(int k, int lock) {
    struct pp *P = &pps[k];
    if (!P->count) return -1;
    int h = sc(SYS_OPEN, (long)P->path, 0, 0, 0, 0);
    if (h < 0) return -1;
    unsigned long long end = 0;
    if (sc(SYS_LLSEEK, h, 0, 0, (long)&end, 2) < 0 || end != P->size) { sc(SYS_CLOSE, h, 0, 0, 0, 0); return -2; }
    long a = sc6(SYS_MMAP2, 0, P->size, 1 /* PROT_READ */, 1 /* MAP_SHARED */, h, 0);
    sc(SYS_CLOSE, h, 0, 0, 0, 0);
    if ((unsigned long)a >= (unsigned long)-4095L) return -3;
    P->map = (const u8 *)a;
    if (!same(P->map, P->head, 16)) { picunmap(k); return -2; }
    for (int i = 0; lock && i < P->count; i++) {
        u32 lo = P->off[i] & ~4095u, hi = (P->off[i] + P->len[i] + 4095u) & ~4095u;
        if (sc(SYS_MLOCK, a + lo, hi - lo, 0, 0, 0) < 0) { picunmap(k); return -4; }
    }
    return 0;
}
/* picture i of file k in RAM: 1 = ours, 0 = the original (by its hash), -1 = something else */
static int picstate(int k, int i) {
    const struct pp *P = &pps[k];
    const u8 *p = P->map + P->off[i];
    u32 n = P->nlen[i] & 0x7fffffffu, z = 1;
    for (u32 j = n; j < P->len[i]; j++) if (p[j]) { z = 0; break; }
    if (z && pfnv(p, n) == P->nhash[i]) return 1;
    if (pfnv(p, P->len[i]) == P->old[i]) return 0;
    return -1;
}
/* makes picture i of file k ours: 0 done, -1 it didn't come out as expected (nothing written), -2 write error */
static int picset(int k, int i) {
    const struct pp *P = &pps[k];
    u32 raw = P->raw[i] & 0x7fffffffu, tint = P->raw[i] >> 31;   /* top bit: takes the accent colour */
    if (raw > PIC_MAXRAW || lz_decode(P->map + P->off[i], (int)P->len[i], pbuf, (int)raw) != (int)raw) return -1;
    for (u32 j = 0; j + 1 < raw; j += 2) if (!(pbuf[j] & 15)) pbuf[j] = pbuf[j + 1] = 0;   /* clear: colour 0 */
    if (tint)                                  /* Sony's orange in another hue: each pixel's colour by the table */
        for (u32 j = 0; j + 1 < raw; j += 2) {
            u32 px = pbuf[j] | (u32)pbuf[j + 1] << 8, c = pic_hue[px >> 4];
            pbuf[j] = (u8)((c & 15) << 4 | (px & 15)); pbuf[j + 1] = (u8)(c >> 4);
        }
    u32 rl = P->rlen[i];
    if (rl > PIC_MAXRUNS || lz_decode(P->runs + P->run[i], 0x7fffffff, prun, (int)rl) != (int)rl) return -1;
    const u8 *q = prun, *qe = prun + rl;
    u32 pos = 0;
    for (;;) {
        if (q + 4 > qe) return -1;
        u32 skip = q[0] | q[1] << 8, cnt = q[2] | q[3] << 8; q += 4;
        if (!skip && !cnt) break;
        pos += skip;
        if ((pos + cnt) * 2 > raw || q + 2 * cnt > qe) return -1;
        for (u32 j = 0; j < cnt; j++, pos++, q += 2) { pbuf[2 * pos] = q[0]; pbuf[2 * pos + 1] = q[1]; }
    }
    int n = lz_encode(pbuf, (int)raw, pout, !(P->nlen[i] >> 31));      /* top bit: packed the cheapest way */
    if ((u32)n != (P->nlen[i] & 0x7fffffffu) || pfnv(pout, (u32)n) != P->nhash[i]) return -1;
    u32 va = (u32)(unsigned long)(P->map + P->off[i]);
    hold(1);                                   /* a stop waits until the picture is written whole */
    if (vrw(mypid, va, pout, n, 1) <= 0) { hold(0); return -2; }
    for (u32 j = (u32)n; j < P->len[i]; ) {
        u32 m = P->len[i] - j; if (m > sizeof zeros) m = sizeof zeros;
        if (vrw(mypid, va + j, (u8 *)zeros, (int)m, 1) <= 0) { hold(0); return -2; }
        j += m;
    }
    hold(0);
    return picstate(k, i) == 1 ? 0 : -2;
}
/* maps and locks the picture files that are exactly the ones we know: how many */
static int picsopen(void) {
    int n = 0;
    for (int k = 0; k < NPP; k++) {
        pps[k].on = 0;
        for (int i = 0; i < pps[k].count; i++) pbad[k][i] = 0;
        if (picmap(k, 1) < 0) continue;
        int ok = 1;
        for (int i = 0; ok && i < pps[k].count; i++) if (picstate(k, i) < 0) ok = 0;
        if (ok) { pps[k].on = 1; n++; } else picunmap(k);
    }
    return n;
}
/* puts our pictures in place where they aren't yet, at most budget of them this time: how many are ours;
 * *left: how many still to do */
static u32 picsdo(int budget, int *left) {
    u32 done = 0; *left = 0;
    for (int k = 0; k < NPP; k++) {
        if (!pps[k].on) continue;
        for (int i = 0; i < pps[k].count; i++) {
            if (pbad[k][i]) continue;
            int st = picstate(k, i);
            if (st == 1) { done++; continue; }
            if (st < 0) { pbad[k][i] = 1; continue; }
            if (budget <= 0) { (*left)++; continue; }
            budget--;
            int r = picset(k, i);
            if (r == 0) done++; else pbad[k][i] = 1;
        }
    }
    return done;
}
/* --font-restore: how many of our pictures are still in place (they go at the next start of the camera) */
static int picsnew(void) {
    int n = 0;
    for (int k = 0; k < NPP; k++) {
        if (picmap(k, 0) < 0) continue;
        for (int i = 0; i < pps[k].count; i++) if (picstate(k, i) == 1) n++;
        picunmap(k);
    }
    return n;
}
/* is id one of the pictures we change (in a file the font patcher looks after)? */
static int ispic(u32 id) {
    for (int k = 0; k < NPP; k++) {
        if (!pps[k].on) continue;
        int lo = 0, hi = pps[k].nrid - 1;
        while (lo <= hi) { int m = (lo + hi) >> 1; u32 v = pps[k].rid[m]; if (v == id) return 1; if (v < id) lo = m + 1; else hi = m - 1; }
    }
    return 0;
}
static const u8 *colmap;
static void picsreset(void) { for (int k = 0; k < NPP; k++) { pps[k].map = 0; pps[k].on = 0; } picflush = 0; colmap = 0; }

/* The accent colour of text the UI draws itself (the orange headings, ...): color_cmn.uxc, a small table the
 * UI reads as it needs a colour. Its orange entries get the same hue as the pictures (picpatch.h: col_*), in the
 * camera's in-memory copy of the file (its page kept there while the font patcher runs); only if the file is exactly the one we
 * know. on: our colours, off: Sony's back. Returns how many entries are ours, or <0. */
static int colours(int on) {
    if (!COL_N) return 0;
    if (!colmap) {
        int h = sc(SYS_OPEN, (long)PICDIR "color_cmn.uxc", 0, 0, 0, 0);
        if (h < 0) return -1;
        unsigned long long end = 0;
        if (sc(SYS_LLSEEK, h, 0, 0, (long)&end, 2) < 0 || end != COL_SIZE) { sc(SYS_CLOSE, h, 0, 0, 0, 0); return -2; }
        long a = sc6(SYS_MMAP2, 0, COL_SIZE, 1 /* PROT_READ */, 1 /* MAP_SHARED */, h, 0);
        sc(SYS_CLOSE, h, 0, 0, 0, 0);
        if ((unsigned long)a >= (unsigned long)-4095L) return -3;
        sc(SYS_MLOCK, a, COL_SIZE, 0, 0, 0);
        colmap = (const u8 *)a;
    }
    /* the file as Sony made it, with ours put back: is it the one we know? */
    u8 b[COL_SIZE];
    for (u32 i = 0; i < COL_SIZE; i++) b[i] = colmap[i];
    for (int k = 0; k < COL_N; k++) {
        u32 v = colmap[col_off[k]] | colmap[col_off[k] + 1] << 8 | colmap[col_off[k] + 2] << 16 | (u32)colmap[col_off[k] + 3] << 24;
        if (v != col_old[k] && v != col_new[k]) return -2;
        for (int j = 0; j < 4; j++) b[col_off[k] + j] = (u8)(col_old[k] >> (8 * j));
    }
    if (pfnv(b, COL_SIZE) != COL_HASH) return -2;
    int n = 0;
    hold(1);
    for (int k = 0; k < COL_N; k++) {
        u32 want = on ? col_new[k] : col_old[k];
        u32 v = colmap[col_off[k]] | colmap[col_off[k] + 1] << 8 | colmap[col_off[k] + 2] << 16 | (u32)colmap[col_off[k] + 3] << 24;
        if (v != want) {
            u8 w[4] = { (u8)want, (u8)(want >> 8), (u8)(want >> 16), (u8)(want >> 24) };
            if (vrw(mypid, (u32)(unsigned long)(colmap + col_off[k]), w, 4, 1) <= 0) { hold(0); return -3; }
        }
        n += on;
    }
    hold(0);
    if (!on) { sc(SYS_MUNMAP, (long)colmap, COL_SIZE, 0, 0, 0); colmap = 0; }
    return n;
}
