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
 * hue, green) first (or, with bit 30 too, pic_red: warnings such as the empty battery turn red), so only our lettering needs pixels here. To make the new picture we unpack the original from the file, give fully transparent pixels colour 0
 * (everywhere, so it packs tighter), put our pixels in, and pack it again; it is written only if it comes out
 * exactly as mkpics computed (length and hash), with zeros after it to the end of the area. Like the font, it
 * goes into the camera's in-memory copy of the file (whose pages we lock: only the ones that change, the main
 * file is 30 MB); a reboot brings back the original. */
#include "uxclz.c"
#include "picpatch.h"
static struct pp {
    const char *path; u32 size; const u8 *head; int count;
    const u32 *off, *len, *old, *nlen, *nhash, *raw, *run, *rlen; const u8 *runs;
    const u32 *rid; int nrid; const u32 *rix;
    const u8 *map; u8 on;
} pps[] = {
    {PICDIR "image_cmn_43.uxc", PC_SIZE, pc_head, PC_COUNT, pc_off, pc_len, pc_old, pc_nlen, pc_new, pc_raw, pc_run, pc_rlen, pc_runs, pc_rid, PC_NRID, pc_rix, 0, 0},
    {PICDIR "image_EVF.uxc", PE_SIZE, pe_head, PE_COUNT, pe_off, pe_len, pe_old, pe_nlen, pe_new, pe_raw, pe_run, pe_rlen, pe_runs, pe_rid, PE_NRID, pe_rix, 0, 0},
    {PICDIR "image_DSLR_Only_43.uxc", PD_SIZE, pd_head, PD_COUNT, pd_off, pd_len, pd_old, pd_nlen, pd_new, pd_raw, pd_run, pd_rlen, pd_runs, pd_rid, PD_NRID, pd_rix, 0, 0},
    {PICDIR "image_DSC_Only_43.uxc", PS_SIZE, ps_head, PS_COUNT, ps_off, ps_len, ps_old, ps_nlen, ps_new, ps_raw, ps_run, ps_rlen, ps_runs, ps_rid, PS_NRID, ps_rix, 0, 0},
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
    u32 raw = P->raw[i] & 0x3fffffffu, tint = P->raw[i] >> 31;   /* top bit: takes the accent colour */
    const u16 *tab = P->raw[i] >> 30 & 1 ? pic_red : pic_hue;     /* the next one: a warning, red instead */
    if (raw > PIC_MAXRAW || lz_decode(P->map + P->off[i], (int)P->len[i], pbuf, (int)raw) != (int)raw) return -1;
    for (u32 j = 0; j + 1 < raw; j += 2) if (!(pbuf[j] & 15)) pbuf[j] = pbuf[j + 1] = 0;   /* clear: colour 0 */
    if (tint)                                  /* Sony's orange in another hue: each pixel's colour by the table */
        for (u32 j = 0; j + 1 < raw; j += 2) {
            u32 px = pbuf[j] | (u32)pbuf[j + 1] << 8, c = tab[px >> 4];
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
/* is id one of the pictures we change (in a file the font patcher looks after)? -1 no, else k << 16 | i */
static int picfind(u32 id) {
    for (int k = 0; k < NPP; k++) {
        if (!pps[k].on) continue;
        int lo = 0, hi = pps[k].nrid - 1;
        while (lo <= hi) {
            int m = (lo + hi) >> 1; u32 v = pps[k].rid[m];
            if (v == id) return k << 16 | (int)pps[k].rix[m];
            if (v < id) lo = m + 1; else hi = m - 1;
        }
    }
    return -1;
}
static int ispic(u32 id) { return picfind(id) >= 0; }
/* The UI keeps the pictures it has unpacked (textures, in the GPU's layout: exactly the unpacked data) in its
 * label store, and a view that is alive keeps using its own (the menus' highlight bars, made at the factory
 * before the boot snapshot): those don't go when our picture comes in, so their pixels are brought up to date
 * where they are. entry: the store's entry for picture id (in process pid). Its texture: entry +0x18 -> +0x18
 * -> +4 -> a header: 0xabcdef, ..., +8 width | height << 16, ..., +0x18 the size in tiles (multiples of 8),
 * ..., +0x24 its physical address, +0x28 the picture's resource id. Only a texture that is exactly the
 * unpacked size of our picture, in RAM, gets written, only where it differs. 1 up to date, 0 not ours or not
 * a texture we know, -1 failed */
static u8 tpage[4096];
static int picpaint(int pid, u32 entry, u32 id) {
    int ki = picfind(id);
    if (ki < 0) return 0;
    int k = ki >> 16, i = ki & 0xffff;
    if (pbad[k][i] || picstate(k, i) != 1) return 0;
    u8 w[4]; u32 a = entry;
    static const u8 hops[3] = {0x18, 0x18, 4};
    for (int h = 0; h < 3; h++) {
        if (vrw(pid, a + hops[h], w, 4, 0) != 1) return 0;
        a = rd32(w);
        if (!a || (a & 3)) return 0;
    }
    u8 hd[0x2c];
    if (vrw(pid, a, hd, sizeof hd, 0) != 1 || rd32(hd) != 0xabcdefu) return 0;
    if ((rd32(hd + 0x28) & 0xffffff) != (id & 0xffffff)) return 0;
    u32 dim = rd32(hd + 0x18), aw = dim & 0xffff, ah = dim >> 16, pa = rd32(hd + 0x24);
    const struct pp *P = &pps[k];
    u32 raw = P->raw[i] & 0x3fffffffu;
    if (!aw || !ah || (aw & 7) || (ah & 7) || aw * ah * 2 != raw || raw > PIC_MAXRAW) return 0;
    if (pa < RAM_LO || pa - RAM_LO > RAM_SIZE - raw) return 0;
    if (lz_decode(P->map + P->off[i], (int)P->len[i], pbuf, (int)raw) != (int)raw) return -1;   /* ours, now */
    for (u32 o = 0; o < raw; o += sizeof tpage) {
        u32 n = raw - o < sizeof tpage ? raw - o : sizeof tpage;
        if (pread_(pa + o, tpage, (int)n) < 0) return -1;
        int same_ = 1;
        for (u32 j = 0; j < n; j++) if (tpage[j] != pbuf[o + j]) { same_ = 0; break; }
        if (!same_ && pwrite_(pa + o, pbuf + o, (int)n) < 0) return -1;
    }
    return 1;
}

/* The accent colour of text the UI draws itself (the orange headings, the "50mm" over the live view, ...): two
 * small tables the UI reads as it needs them, color_cmn.uxc (plain colours) and style_cmn.uxc (text styles: a
 * colour and an outline). Their orange entries get the same hue as the pictures (picpatch.h: col_*, sty_*), in
 * the camera's in-memory copy of the file (its page kept there while the font patcher runs); only if the file
 * is exactly the one we know. on: our colours, off: Sony's back. Returns how many entries are ours, or <0. */
static struct ct {
    const char *path; u32 size, hash; int n; const u32 *off, *old, *nw; const u8 *map;
} cts[] = {
    {PICDIR "color_cmn.uxc", COL_SIZE, COL_HASH, COL_N, col_off, col_old, col_new, 0},
    {PICDIR "style_cmn.uxc", STY_SIZE, STY_HASH, STY_N, sty_off, sty_old, sty_new, 0},
};
static int colours1(struct ct *C, int on) {
    if (!C->n) return 0;
    if (!C->map) {
        int h = sc(SYS_OPEN, (long)C->path, 0, 0, 0, 0);
        if (h < 0) return -1;
        unsigned long long end = 0;
        if (sc(SYS_LLSEEK, h, 0, 0, (long)&end, 2) < 0 || end != C->size) { sc(SYS_CLOSE, h, 0, 0, 0, 0); return -2; }
        long a = sc6(SYS_MMAP2, 0, C->size, 1 /* PROT_READ */, 1 /* MAP_SHARED */, h, 0);
        sc(SYS_CLOSE, h, 0, 0, 0, 0);
        if ((unsigned long)a >= (unsigned long)-4095L) return -3;
        sc(SYS_MLOCK, a, C->size, 0, 0, 0);
        C->map = (const u8 *)a;
    }
    /* the file as Sony made it, with ours put back: is it the one we know? */
    static u8 b[4096];
    if (C->size > sizeof b) return -2;
    for (u32 i = 0; i < C->size; i++) b[i] = C->map[i];
    for (int k = 0; k < C->n; k++) {
        u32 v = rd32(C->map + C->off[k]);
        if (v != C->old[k] && v != C->nw[k]) return -2;
        wr32(b + C->off[k], C->old[k]);
    }
    if (pfnv(b, C->size) != C->hash) return -2;
    int n = 0;
    hold(1);
    for (int k = 0; k < C->n; k++) {
        u32 want = on ? C->nw[k] : C->old[k];
        if (rd32(C->map + C->off[k]) != want) {
            u8 w[4]; wr32(w, want);
            if (vrw(mypid, (u32)(unsigned long)(C->map + C->off[k]), w, 4, 1) <= 0) { hold(0); return -3; }
        }
        n += on;
    }
    hold(0);
    if (!on) { sc(SYS_MUNMAP, (long)C->map, C->size, 0, 0, 0); C->map = 0; }
    return n;
}
static int colours(int on) {
    int n = 0, bad = 0;
    for (unsigned c = 0; c < sizeof cts / sizeof cts[0]; c++) { int r = colours1(&cts[c], on); if (r < 0) bad = r; else n += r; }
    return n ? n : bad;
}
static void picsreset(void) { for (int k = 0; k < NPP; k++) { pps[k].map = 0; pps[k].on = 0; } picflush = 0; for (unsigned c = 0; c < sizeof cts / sizeof cts[0]; c++) cts[c].map = 0; }
