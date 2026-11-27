/* ---- the UI's pictures with lettering in them (part of --font-patcher; included by a7hacks.c) ----
 * Many things the camera shows as text are pictures Sony drew in Univers: the focal lengths in the SteadyShot
 * list, the menu page numbers, AWB, MF, ISO, ... They live in /usr/share/app/image_*.uxc, which the UI maps.
 * A picture's record gives where its data is (its own data area, a fixed number of bytes); the data is Sony's
 * LZ77 (uxclz.c), unpacked by the camera's hardware straight into the GPU's layout (RGBA4444 in 8x8 tiles).
 * picpatch.h (made by font/pics/mkpics.py and font/mkheader.py) has, per picture we change: where its data
 * area is and how long, a hash of the original data, the length and hash of the new data (top bit of the
 * length: packed the slow, cheapest way; the next one: the medium way; otherwise the quick way, which is most
 * of them), the size unpacked,
 * and our pixels: only the ones we drew (Comic Relief lettering, and what we cleared around the old one), as
 * runs (u16 skip, u16 count, the pixels; ending with 0, 0), themselves packed with the same LZ77. Pictures
 * in Sony's orange accent are marked (top bit of _raw): their colours go through pic_hue (the accent in another
 * hue, green) first (or, with bit 30 too, pic_red: warnings such as the empty battery turn red; with bit 29, pic_blue: the level's marks), so only our lettering needs pixels here. To make the new picture we unpack the original from the file, give fully transparent pixels colour 0
 * (everywhere, so it packs tighter), put our pixels in, and pack it again; it is written only if it comes out
 * exactly as mkpics computed (length and hash), with zeros after it to the end of the area. Like the font, it
 * goes into the camera's in-memory copy of the file (whose pages we lock: only the ones that change, the main
 * file is 30 MB); a reboot brings back the original. */
#include "uxclz.c"
#include "picpatch.h"
static struct pp {
    const char *path; u32 size; const u8 *head; int count;
    const u32 *off, *len, *old, *nlen, *nhash, *raw, *run, *rlen; const u8 *runs;
    const u32 *rid; int nrid; const u32 *rix, *ohash;
    const u8 *map; u8 on;
} pps[] = {
    {PICDIR "image_cmn_43.uxc", PC_SIZE, pc_head, PC_COUNT, pc_off, pc_len, pc_old, pc_nlen, pc_new, pc_raw, pc_run, pc_rlen, pc_runs, pc_rid, PC_NRID, pc_rix, pc_ohash, 0, 0},
    {PICDIR "image_EVF.uxc", PE_SIZE, pe_head, PE_COUNT, pe_off, pe_len, pe_old, pe_nlen, pe_new, pe_raw, pe_run, pe_rlen, pe_runs, pe_rid, PE_NRID, pe_rix, pe_ohash, 0, 0},
    {PICDIR "image_DSLR_Only_43.uxc", PD_SIZE, pd_head, PD_COUNT, pd_off, pd_len, pd_old, pd_nlen, pd_new, pd_raw, pd_run, pd_rlen, pd_runs, pd_rid, PD_NRID, pd_rix, pd_ohash, 0, 0},
    {PICDIR "image_DSC_Only_43.uxc", PS_SIZE, ps_head, PS_COUNT, ps_off, ps_len, ps_old, ps_nlen, ps_new, ps_raw, ps_run, ps_rlen, ps_runs, ps_rid, PS_NRID, ps_rix, ps_ohash, 0, 0},
};
#define NPP ((int)(sizeof pps / sizeof pps[0]))
static u8 pbad[NPP][PIC_MAXCOUNT];       /* pictures that didn't come out as expected: left alone */
static u8 pok[NPP][PIC_MAXCOUNT];        /* pictures found ours (their pages are locked, so they stay): not hashed again
                                            while the rest are being done */
static u8 pbuf[PIC_MAXRAW + 16], pout[PIC_MAXRAW + PIC_MAXRAW / 8 + 4096], prun[PIC_MAXRUNS + 16];
static int picflush;                     /* the label store also gives up its copies of the pictures we change */
/* how a picture is packed, from its _nlen: top bit the cheapest way, the next one the medium way, else quick */
#define PIC_HOW(nl) ((nl) >> 31 ? LZ_CHEAPEST : (nl) >> 30 & 1 ? LZ_MEDIUM : LZ_QUICK)

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
    u32 n = P->nlen[i] & 0x3fffffffu, z = 1;
    for (u32 j = n; j < P->len[i]; j++) if (p[j]) { z = 0; break; }
    if (z && pfnv(p, n) == P->nhash[i]) return 1;
    if (pfnv(p, P->len[i]) == P->old[i]) return 0;
    return -1;
}
/* the pixels of an unpacked picture (raw bytes, RGBA4444) the way we change them: fully transparent ones colour 0,
 * and with R's top bit the colours through the table (bit 30: pic_red, bit 29: pic_blue, else pic_hue); one pass
 * (a transparent pixel gets what the table gives colour 0, as when it was cleared first) */
static void picprep(u8 *b, u32 raw, u32 R) {
    const u16 *tab = R >> 30 & 1 ? pic_red : R >> 29 & 1 ? pic_blue : pic_hue;
    if (!(R >> 31)) { for (u32 j = 0; j + 1 < raw; j += 2) if (!(b[j] & 15)) b[j] = b[j + 1] = 0; return; }
    u32 c0 = tab[0]; u8 z0 = (u8)((c0 & 15) << 4), z1 = (u8)(c0 >> 4);
    for (u32 j = 0; j + 1 < raw; j += 2) {
        u32 a = b[j] & 15;
        if (!a) { b[j] = z0; b[j + 1] = z1; continue; }
        u32 c = tab[(b[j] | (u32)b[j + 1] << 8) >> 4];
        b[j] = (u8)((c & 15) << 4 | a); b[j + 1] = (u8)(c >> 4);
    }
}
/* makes picture i of file k ours: 0 done, -1 it didn't come out as expected (nothing written), -2 write error */
static int picset(int k, int i) {
    const struct pp *P = &pps[k];
    u32 raw = P->raw[i] & 0x1fffffffu;
    if (raw > PIC_MAXRAW || lz_decode(P->map + P->off[i], (int)P->len[i], pbuf, (int)raw) != (int)raw) return -1;
    picprep(pbuf, raw, P->raw[i]);             /* clear; Sony's orange in another hue (top bit of _raw) */
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
    int n = lz_encode(pbuf, (int)raw, pout, PIC_HOW(P->nlen[i]));
    if ((u32)n != (P->nlen[i] & 0x3fffffffu) || pfnv(pout, (u32)n) != P->nhash[i]) return -1;
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
        for (int i = 0; i < pps[k].count; i++) pbad[k][i] = pok[k][i] = 0;
        if (picmap(k, 1) < 0) continue;
        int ok = 1;
        for (int i = 0; ok && i < pps[k].count; i++) if (picstate(k, i) < 0) ok = 0;
        if (ok) { pps[k].on = 1; n++; } else picunmap(k);
    }
    return n;
}
/* puts our pictures in place where they aren't yet, at most budget of them this time: how many are ours;
 * *left: how many still to do. Hashing every picture each time cost more than the pictures themselves (15 MB,
 * four times a second), so while there is a budget only the ones not known to be ours yet are looked at, and
 * once the budget is spent the rest only counted; budget 0 (once all are in) looks at every one again. */
static int picfirstin;                   /* the first PIC_NFIRST of pic_ord (the ones seen most) are all in */
static u32 picsdo(int budget, int *left) {
    u32 done = 0; *left = 0; int check = budget == 0, firstleft = 0;
    for (int t = 0; t < PIC_NORD; t++) {            /* in the order of order.txt: the ones seen most first */
        int k = (int)(pic_ord[t] >> 16), i = (int)(pic_ord[t] & 0xffff), l0 = *left;
        if (!pps[k].on) continue;
        do {
            if (pbad[k][i]) break;
            if (pok[k][i] && !check) { done++; break; }
            if (!check && budget <= 0) { (*left)++; break; }
            int st = picstate(k, i);
            pok[k][i] = st == 1;
            if (st == 1) { done++; break; }
            if (st < 0) { pbad[k][i] = 1; break; }
            if (budget <= 0) { (*left)++; break; }
            budget--;
            int r = picset(k, i);
            if (r == 0) { done++; pok[k][i] = 1; } else pbad[k][i] = 1;
        } while (0);
        if (t < PIC_NFIRST && *left != l0) firstleft++;
    }
    picfirstin = !firstleft;
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
/* ---- textures the UI unpacked before our pictures came in ----
 * The UI unpacks a picture straight into its graphics memory (which it maps from /dev/stream), exactly as the
 * file has it unpacked (the GPU's tiled RGBA4444). Views made at the factory, before the boot snapshot (the
 * menus' highlight bars, the selected tab, the gradient behind the shutter speed, ...), keep the copies they made
 * then for as long as the camera runs and never read the file again. Such a copy is brought up to date where it
 * is: through our own mapping of /dev/stream (the way the UI reaches it, so the GPU sees the writes), and only
 * when it holds exactly Sony's picture (its hash, picpatch.h _ohash), so a header we misread costs nothing.
 * Two ways to find them: the label store's entries (picpaint, from tcwalk) and the views' own texture headers
 * (texpass in a7hacks.c). */
static int sfd = -2;                     /* /dev/stream, opened when first needed */
/* our picture id (k << 16 | i, from picfind) into the texture at physical address pa, tw x th pixels (whole
 * tiles): 1 written, 0 ours already or not Sony's picture or not one of ours, -1 failed */
static int texwrite(int ki, u32 pa, u32 tw, u32 th) {
    int k = ki >> 16, i = ki & 0xffff;
    const struct pp *P = &pps[k];
    if (pbad[k][i] || picstate(k, i) != 1) return 0;
    u32 raw = P->raw[i] & 0x1fffffffu;
    if (!tw || !th || (tw & 7) || (th & 7) || tw * th * 2 != raw || raw > PIC_MAXRAW || (pa & 3)) return 0;
    if (pa < RAM_LO || pa - RAM_LO > RAM_SIZE - raw) return 0;
    if (sfd == -2) sfd = sc(SYS_OPEN, (long)"/dev/stream", 2 | 0x101000 /* O_RDWR | O_SYNC */, 0, 0, 0);
    if (sfd < 0) return -1;
    if (lz_decode(P->map + P->off[i], (int)P->len[i], pbuf, (int)raw) != (int)raw) return -1;   /* ours */
    u32 base = pa & ~0xfffu, len = (pa - base + raw + 0xfff) & ~0xfffu;
    long m = sc6(SYS_MMAP2, 0, len, 3 /* PROT_READ | PROT_WRITE */, 1 /* MAP_SHARED */, sfd, (long)(base >> 12));
    if ((unsigned long)m >= (unsigned long)-4095L) return -1;
    volatile u8 *t = (volatile u8 *)m + (pa - base);
    u32 hsh = 0x811c9dc5u, same = 1;
    for (u32 j = 0; j < raw; j++) { u8 c = t[j]; hsh = (hsh ^ c) * 0x01000193u; if (c != pbuf[j]) same = 0; }
    int r = 0;
    if (!same && hsh == P->ohash[i]) {             /* Sony's picture, exactly: ours instead */
        hold(1);
        for (u32 j = 0; j < raw; j++) t[j] = pbuf[j];
        hold(0);
        r = 1;
        for (u32 j = 0; j < raw; j++) if (t[j] != pbuf[j]) { r = -1; break; }
    }
    sc(SYS_MUNMAP, m, len, 0, 0, 0);
    return r;
}
/* the label store's entry for picture id (in process pid), in use: its texture brought up to date. The texture:
 * entry +0x18 -> +0x18 -> +4 -> a header: 0xabcdef, ..., +0x18 the size in tiles, ..., +0x24 its physical
 * address, +0x28 the resource id. */
static int picpaint(int pid, u32 entry, u32 id) {
    int ki = picfind(id);
    if (ki < 0) return 0;
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
    u32 dim = rd32(hd + 0x18);
    return texwrite(ki, rd32(hd + 0x24), dim & 0xffff, dim >> 16);
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
/* The time in 24 hours (ISO 8601 with the Y-M-D date). Sony shows the time in 24 hours only with the date format
 * D-M-Y, in 12 (AM/PM) with the others; each place that shows a time decides that for itself:
 *  - playback, the line under the picture (viewUnified2.so, the ACT_ widget after ACT_IconNdFilterAct: time
 *    as "${v1:2}:${v2:02}${v3}", v3 AM/PM or, for D-M-Y, empty with the hour as it is): "cmp r3, #3" (the
 *    date format) becomes "cmp r3, r3", so always the D-M-Y way, Thumb at 0x2e489a;
 *  - playback's index (the grid; PAS_Index::reqScroll, viewUnified2.so): the chosen picture's date and time,
 *    per date format (a jump table, object +0xa88); each format's time part starts "add.w r5, r5, #0x718;
 *    ldr r1, =template": in Y-M-D, M-D-Y and M(Eng)-D-Y the ldr becomes "b" to D-M-Y's ldr (0x316cd4), which
 *    goes on the same way with the hour as it is (Thumb at 0x316a3e, 0x316b1a, 0x316c02);
 *  - ViewGpsInfo in viewUnified8.so (a picture's date, and its time as hour, minutes and AM/PM): after giving the date its order (setFormatType) it takes the 24 hour way only for format 3:
 *    "cmp r5, #3 ... beq" becomes "movs r5, #3 ... b" (r5 is used further on as for D-M-Y), Thumb at 0x26142
 *    and 0x26148;
 *  - ACT_IconLabelPbDatacode in viewUnified2.so (a time with seconds): a table in the read-only data, one byte
 *    per date format, 1 = 24 hours (Sony's 0, 0, 0, 1): Y-M-D's becomes 1.
 * Into the camera's in-memory copy of each library, like the colours (its page kept there while the font
 * patcher runs), only if the whole page is exactly the one we know (its hash with Sony's bytes in our places)
 * and each place holds Sony's bytes or ours. Written a byte at a time, in order (backwards when putting Sony's
 * back), chosen so that the UI, should it run that code just then, finds no half written instruction it could
 * trip over (at worst a wrong number or text for a moment); code is followed by an instruction cache flush. on: ours, off: Sony's.
 * Returns how many places are ours, or <0. */
static struct h24f { const char *path; u32 size, page, hash; const u8 *map; } h24fs[] = {
    {"/usr/lib/viewUnified8.so", 232804u, 0x26000u, 0xa6f67cb8u, 0},
    {"/usr/lib/viewUnified2.so", 5317568u, 0x40a000u, 0x6a85a2efu, 0},
    {"/usr/lib/viewUnified2.so", 5317568u, 0x2e4000u, 0xa6ae5667u, 0},
    {"/usr/lib/viewUnified2.so", 5317568u, 0x316000u, 0x95a0481du, 0},
    {"/usr/lib/viewUnified2.so", 5317568u, 0x30b000u, 0x4f4caeabu, 0},
    {"/usr/lib/viewUnified2.so", 5317568u, 0x48c000u, 0x39165da7u, 0},
    {PICDIR "viewStlrec.uxc", 19504u, 0x1000u, 0x7061d7feu, 0},
    {PICDIR "viewStlrec.uxc", 19504u, 0x3000u, 0xaf949fccu, 0},
    {"/usr/lib/viewUnified2.so", 5317568u, 0x11f000u, 0x5a8e6b1eu, 0},
};
/* kind: H24 the time in 24 hours (always); IRIS_T, IRIS_W the focal length where live view says F-- (only with a
 * lens that tells no aperture: see irisfl below), IRIS_W a code word written at once; IRIS_S always Sony's (it was
 * ours: the number written into "--"; any number there is taken back) */
#define H24 0
#define IRIS_T 1
#define IRIS_S 2
#define IRIS_W 3
#define LAY 4
static const struct h24p { int f; u32 off; u8 n, kind, old[4], nw[4]; } h24ps[] = {
    {0, 0x26142u, 2, H24, {0x03, 0x2d}, {0x03, 0x25}},     /* cmp r5, #3  ->  movs r5, #3 */
    {0, 0x26148u, 2, H24, {0x2d, 0xd0}, {0x2d, 0xe0}},     /* beq (24 hours)  ->  b */
    {1, 0x40a314u, 1, H24, {0x00}, {0x01}},                /* Y-M-D: 12 hours  ->  24 */
    {2, 0x2e489au, 2, H24, {0x03, 0x2b}, {0x9b, 0x42}},    /* cmp r3, #3  ->  cmp r3, r3 */
    {3, 0x316a3eu, 2, H24, {0xbb, 0x49}, {0x49, 0xe1}},    /* ldr r1, =template  ->  b 0x316cd4 (half: ldr r1, other) */
    {3, 0x316b1au, 2, H24, {0x84, 0x49}, {0xdb, 0xe0}},
    {3, 0x316c02u, 2, H24, {0x4a, 0x49}, {0x67, 0xe0}},
    {1, 0x40a978u, 4, IRIS_S, {'-', '-', 0, 0}, {0}},      /* "--" (was: the focal length, "135", 1..4 digits) */
    /* our code (in the zeros after the library's code): mm = Bkup_Read(0x010713ca), into the aperture's now unused
     * slot, and on into Sony's own "%d" with it (r6 = 0, as for "--"); if the read fails, the label as it was */
    {5, 0x48cfe0u, 4, IRIS_T, {0}, {0x06, 0x48, 0x07, 0xf1}},  /* ldr r0, =0x010713ca; add.w r1, r7, #0x5c */
    {5, 0x48cfe4u, 4, IRIS_T, {0}, {0x5c, 0x01, 0x5d, 0xf4}},
    {5, 0x48cfe8u, 4, IRIS_T, {0}, {0xc2, 0xef, 0x08, 0xb1}},  /* blx Bkup_Read (0xeaf6c); cbz r0, 1f */
    {5, 0x48cfecu, 4, IRIS_T, {0}, {0x7e, 0xf6, 0x59, 0xbb}},  /* b.w 0x30b6a2 (Sony's end of "--") */
    {5, 0x48cff0u, 4, IRIS_T, {0}, {0x00, 0x26, 0xb7, 0xf8}},  /* 1: movs r6, #0; ldrh.w r0, [r7, #0x5c] */
    {5, 0x48cff4u, 4, IRIS_T, {0}, {0x5c, 0x00, 0x7e, 0xf6}},  /* b.w 0x30b648 (snprintf(buf, 5, "%d", r0)) */
    {5, 0x48cff8u, 4, IRIS_T, {0}, {0x27, 0xbb, 0x00, 0xbf}},  /* nop */
    {5, 0x48cffcu, 4, IRIS_T, {0}, {0xca, 0x13, 0x07, 0x01}},  /* 0x010713ca: the SteadyShot focal length (mm) */
    {4, 0x30b5c4u, 4, IRIS_W, {0xdf, 0xf5, 0x1a, 0xe8}, {0x81, 0xf1, 0x0c, 0xfd}},   /* "--": blx memcpy  ->  bl 0x48cfe0 */
    {4, 0x30b844u, 4, IRIS_T, {0xf4, 0xe7, 0x04, 0x02}, {0xf6, 0xe7, 0x04, 0x02}},   /* F${v1:2}  ->  ${v1} (one byte) */
    {4, 0x30b754u, 2, IRIS_T, {0x02, 0xd9}, {0x02, 0xe0}},  /* its event filter: bls (events 0, 1)  ->  b (every event) */
    /* DISP's "Level" gets the histogram too. Each display mode of the shooting screen is a layout in viewStlrec.uxc
     * (the UI reads it when it builds that screen, no copy kept): slots, each naming one of master_camera's
     * sub-layouts by the hash of its class (LG_master_camera::Layout...). In the Level layouts (LCD: record 41,
     * EVF: record 55) a slot they don't need (water depth; the EVF has its own zoom bar besides the common one)
     * gets Sony's histogram, as in the Histogram layouts. (Not in another mode: the level widget is found by name
     * through a lookup compiled into each layout's class, and only the Level and For viewfinder ones know it;
     * the histogram is fed by its model, wherever it is.) */
    {6, 0x1f39u, 4, LAY, {0x5d, 0xa5, 0xb4, 0x55}, {0x19, 0xea, 0xb9, 0x70}},  /* WATERDEPTH1 -> HISTOGRAM_LL */
    {7, 0x36feu, 4, LAY, {0x3d, 0x60, 0xec, 0xaa}, {0x7d, 0xbd, 0xa4, 0xae}},  /* CNM_REC_ZOOMBAR -> EVF_HISTOGRAM_LR */
    /* and their dial guide is the Histogram layouts' one, which keeps the ISO dial above the histogram (the same
     * widgets, so the Level classes' lookups still find them, with the same per-slot setting) */
    {6, 0x1ec1u, 4, LAY, {0xc6, 0x82, 0x37, 0x4d}, {0x2c, 0x54, 0x8b, 0xed}},  /* DIALGUIDE1 -> DIALGUIDE_GRAPHIC1 */
    {7, 0x36c2u, 4, LAY, {0xf3, 0x5b, 0x5c, 0x5f}, {0x34, 0x5e, 0xe8, 0x86}},  /* EVF_DIALGUIDE1 -> EVF_DIALGUIDE_GRAPHIC1 */
    /* and the stlrec view switches the histogram's data on for the Level mode (4; 5 histogram, 6 for
     * viewfinder) too: its test of the layout mode (UserGetLayoutMode) becomes a bit test, (mask >> mode) & 1,
     * in the same bytes. Each goes in from its end, so until its first instruction is ours Sony's test still
     * decides (at worst the histogram goes on or off once). */
    {8, 0x11f19cu, 2, LAY, {0x01, 0x21}, {0x00, 0xbf}},                          /* LCD: 5, 6 -> 4, 5, 6 */
    {8, 0x11f198u, 4, LAY, {0x8c, 0xbf, 0x00, 0x21}, {0xc9, 0x07, 0xc9, 0x0f}},  /*   r1 = (0x70 >> mode) & 1: movs r1, #0x70; */
    {8, 0x11f194u, 4, LAY, {0x41, 0x1f, 0x01, 0x29}, {0x70, 0x21, 0xc1, 0x40}},  /*   lsrs r1, r0; lsls r1, #31; lsrs r1, #31; nop */
    {8, 0x11f1bau, 4, LAY, {0x41, 0xeb, 0x0c, 0x01}, {0x00, 0xbf, 0x00, 0xbf}},  /* EVF: 5 -> 4, 5 */
    {8, 0x11f1b6u, 4, LAY, {0xdc, 0xf1, 0x00, 0x01}, {0xc9, 0x07, 0xc9, 0x0f}},  /*   r1 = (0x30 >> mode) & 1, as above */
    {8, 0x11f1b2u, 4, LAY, {0xa0, 0xf1, 0x05, 0x0c}, {0x30, 0x21, 0xc1, 0x40}},
};
#define NH24F ((int)(sizeof h24fs / sizeof h24fs[0]))
#define NH24P ((int)(sizeof h24ps / sizeof h24ps[0]))
/* is q (n bytes) ours for place P: its new bytes, or for IRIS_S 1..4 digits padded with NULs */
static int h24ours(const struct h24p *P, const u8 *q) {
    if (P->kind != IRIS_S) { for (int j = 0; j < P->n; j++) if (q[j] != P->nw[j]) return 0; return 1; }
    int d = 0; while (d < P->n && q[d] >= '0' && q[d] <= '9') d++;
    if (!d) return 0;
    for (int j = d; j < P->n; j++) if (q[j]) return 0;
    return 1;
}
/* The live view's aperture (ACT_IconIris in viewUnified2.so) with a lens that tells none (a manual one) reads
 * "F--": the label's template F${v1:2} with v1 copied (memcpy) from a 4 byte string "--". For such a lens, ours:
 * the template, a word in that widget's own literal pool that its setup reads, becomes ${v1} (0x1e04e7f6, the
 * same in every language), and instead of copying "--" the widget reads the focal length set for SteadyShot
 * itself (our code above), so it reads "135" (with "mm" the label, sized for "F5.6", shrank it); with any other
 * lens, Sony's. The widget is built anew on a change of screen and when a menu (C4) closes, and takes its values
 * then and on its events (event id 0, 1 or 0x18000000, GetEventId at 0x30b74e; other events it ignores); for
 * such a lens its filter lets every event through. irisfl: that focal length (the helper's own reading; it only
 * decides ours or Sony's now), 0 for Sony's. */
static u32 irisfl, irisin;              /* irisin: the focal length the label has been given (status iN) */
static int clock24(int on) {
    static u8 b[4096];
    int ok[NH24F];
    for (int f = 0; f < NH24F; f++) {
        struct h24f *F = &h24fs[f]; ok[f] = 0;
        if (!F->map) {
            int h = sc(SYS_OPEN, (long)F->path, 0, 0, 0, 0);
            if (h < 0) continue;
            unsigned long long end = 0;
            if (sc(SYS_LLSEEK, h, 0, 0, (long)&end, 2) < 0 || end != F->size) { sc(SYS_CLOSE, h, 0, 0, 0, 0); continue; }
            long a = sc6(SYS_MMAP2, 0, 4096, 1 /* PROT_READ */, 1 /* MAP_SHARED */, h, (long)(F->page >> 12));
            sc(SYS_CLOSE, h, 0, 0, 0, 0);
            if ((unsigned long)a >= (unsigned long)-4095L) continue;
            sc(SYS_MLOCK, a, 4096, 0, 0, 0);
            F->map = (const u8 *)a;
        }
        /* the page as Sony made it, with ours put back: is it the one we know? */
        for (u32 i = 0; i < 4096; i++) b[i] = F->map[i];
        int good = 1;
        for (int k = 0; k < NH24P; k++) {
            const struct h24p *P = &h24ps[k];
            if (P->f != f) continue;
            const u8 *q = F->map + (P->off - F->page);
            int isold = 1;
            for (int j = 0; j < P->n; j++) if (q[j] != P->old[j]) isold = 0;
            if (!isold && !h24ours(P, q)) good = 0;
            for (int j = 0; j < P->n; j++) b[P->off - F->page + j] = P->old[j];
        }
        ok[f] = good && pfnv(b, 4096) == F->hash;
    }
    /* what each place should hold */
    int n = 0, bad = 0;
    /* Sony's back first, last place first (the template before its string); then ours, in order (the string
     * before the template), so a half done change is still a sensible text */
    for (int pass = 0; pass < 2; pass++)
        for (int x = 0; x < NH24P; x++) {
            int k = pass ? x : NH24P - 1 - x;
            const struct h24p *P = &h24ps[k];
            struct h24f *F = &h24fs[P->f];
            int mine = (P->kind == H24 || P->kind == LAY) ? on : P->kind == IRIS_S ? 0 : (on && irisfl > 0 && irisfl < 10000);
            if (mine != pass) continue;
            if (!ok[P->f]) { bad = -2; continue; }        /* not the library we know: leave it */
            const u8 *want = !mine ? P->old : P->nw;
            u8 *q = (u8 *)F->map + (P->off - F->page);
            int same = 1;
            for (int j = 0; j < P->n; j++) if (q[j] != want[j]) same = 0;
            if (!same) {
                int r = 1;
                hold(1);
                if (P->kind == IRIS_W) r = vrw(mypid, (u32)(unsigned long)q, (u8 *)want, P->n, 1);   /* one aligned word */
                else for (int j = 0; j < P->n && r > 0; j++) {  /* a byte at a time: low first, Sony's back high first */
                    int bb = mine ? j : P->n - 1 - j;
                    if (q[bb] != want[bb]) r = vrw(mypid, (u32)(unsigned long)(q + bb), (u8 *)want + bb, 1, 1);
                }
                sc(0xf0002 /* ARM cacheflush */, (long)q, (long)(q + P->n), 0, 0, 0);
                hold(0);
                if (r <= 0) { bad = -3; continue; }
                /* the call to our code is Sony's again: a moment before our code goes, should the UI be in it */
                if (P->kind == IRIS_W && !mine) { long ts[2] = {0, 200000000}; sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0); }
            }
            if (P->kind == H24 || P->kind == LAY) n += on;
        }
    irisin = 0;                                  /* every iris place ours: the label says so */
    { int t = 0, all = 0; for (int k = 0; k < NH24P; k++) { const struct h24p *P = &h24ps[k];
          if (P->kind != IRIS_T && P->kind != IRIS_W) continue;
          all++; if (h24fs[P->f].map && h24ours(P, h24fs[P->f].map + (P->off - h24fs[P->f].page))) t++; }
      if (t == all && on) irisin = irisfl; }
    if (!on) for (int f = 0; f < NH24F; f++) if (h24fs[f].map) { sc(SYS_MUNMAP, (long)h24fs[f].map, 4096, 0, 0, 0); h24fs[f].map = 0; }
    return n ? n : bad;
}
static void picsreset(void) { for (int k = 0; k < NPP; k++) { pps[k].map = 0; pps[k].on = 0; } picflush = 0; if (sfd >= 0) sc(SYS_CLOSE, sfd, 0, 0, 0, 0); sfd = -2; for (unsigned c = 0; c < sizeof cts / sizeof cts[0]; c++) cts[c].map = 0; for (int c = 0; c < NH24F; c++) h24fs[c].map = 0; }
