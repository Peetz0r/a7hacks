/* a7hacks - the root side of Peetz0r's α7-hacks (ILCE-7M2 firmware 4.01 only). Shipped in the app as its "native
 * library" liba7hacks.so, so that Android installs it executable; the app runs it as root.
 *
 * Usage (as root):
 *   liba7hacks.so --lens-write id 0               manual lens EXIF info off (the camera behaves as without us)
 *   liba7hacks.so --lens-write id 1 fnum fden anum aden mnum mden [namehex]
 *       focal length fnum/fden, F-number anum/aden, minimum F-number (MaxApertureValue) mnum/mden,
 *       lens name as hex bytes (at most 64). Numbers are decimal.
 *   liba7hacks.so --lens-read id                  show the block
 *   liba7hacks.so --exposure-patcher              run forever: exposure focal length (see below)
 *   liba7hacks.so --label-patcher                 run forever: menu label (see below)
 *   liba7hacks.so --label-find [--all]            menu label, read only: show where the label is
 *   liba7hacks.so --font-patcher [--text-only]    run forever: UI font, icon and picture lettering, accent colour
 *                                                 (--text-only: the UI font only)
 *   liba7hacks.so --font-restore                  UI font: put the camera's own back
 *   liba7hacks.so --memory-dump path [--all]      copy the UI's own data to path (for study elsewhere;
 *                                                 --all: and the driver memory it has mapped, RAM pages only)
 *   liba7hacks.so --glyph-cache [--mark-stale | --list] [--all]
 *                                                 UI font: show the UI's glyph cache (--mark-stale: and mark it
 *                                                 stale; --list: and list, with pictures, the glyphs drawn since;
 *                                                 --all: look in every process, not just the UI)
 *
 * Manual lens EXIF info (--lens-write, --lens-read): the camera fills EXIF LensModel / FocalLength / FNumber /
 * MaxApertureValue from this block when no electronic lens is attached (it is what CameraEx.setExifInfo()
 * sets). It lives in av-cam's RAM, so a reboot clears it. Before touching anything the program checks that the
 * av-cam code around the block is exactly what it was written for, and that the block looks sane; otherwise it
 * writes nothing. id is a number the caller chooses. The answer is one line, printed and also written as
 * "id answer" to /android/dev/a7hacks.st (RAM), where the app picks it up:
 *   same | written ... | disabled | off | block ... | badsig | badblk ... | error ...
 *
 * Exposure focal length (--exposure-patcher): with ISO AUTO "Standard" the camera picks the slowest shutter
 * speed from the lens's focal length, which is 0 without an electronic lens (it then falls back to 1/60). The
 * exposure code fetches that focal length with one call at 0x234b96; the exposure patcher replaces that call
 * with a load from a 2-byte mailbox (unused alignment padding at 0x233ef6) holding the SteadyShot focal length,
 * read from the sampler's /android/dev/a7hacks.ss. Four times a second it checks the lens mount: as soon as an
 * electronic lens is attached (or the SteadyShot setting can't be read) it puts the original call back, so such
 * lenses always use their own focal length. Enabling writes the mailbox before the call site, disabling
 * restores the call site before the mailbox. Status lines (on N | lens | off | badsig | badsite | error ...) go
 * to /android/dev/a7hacks.ex when they change.
 *
 * Menu label (--label-patcher): the English menu item for the SteadyShot focal length reads "SteadyS. Focal
 * Len.". The camera UI (im.elf, which runs libObj.so) reads its strings from /usr/share/app/string_english.uxc
 * into its own memory once per boot (with O_DIRECT, so there's no page-cache copy). The label patcher finds that
 * copy and writes "Manual Focal Length" over the text: the same length, so nothing else moves. A string record
 * is a u32 id, the text and a NUL; we only touch a complete record (id 0x1e04f85a + the exact old text). The
 * UI's memory is reached via /proc/PID/pagemap (virtual -> physical) and /dev/mem; only that process's pages
 * are read, and only addresses in RAM. The UI only loads the menu strings when it needs them (the first time the
 * menu opens, say), so until the label turns up the patcher looks every 2 s at just the memory the UI has newly
 * taken into use (pages that weren't there, or sat in another frame, at the previous look), and through all of
 * it every 10..60 s. Once found, it checks the copies it knows about every 2 s (and renames a fresh one again),
 * and looks for other copies every 5 minutes. Status (on N | waiting MB | noui | error ...) goes to
 * /android/dev/a7hacks.ui. --label-find does the search once without writing and lists what it found;
 * "--label-find --all" looks in every process.
 *
 * UI font (--font-patcher): the camera UI draws Latin, Greek and Cyrillic text with Univers, embedded in
 * /usr/share/app/UniversOTS-SJ_wIcon.ltt, and reads the outlines of the characters from that file as it needs
 * them (through the kernel's file cache, 1 KB at a time). The font patcher maps the file and locks it in RAM, so
 * its cached pages can't be dropped and re-read from flash, finds where those pages are (/proc/self/pagemap)
 * and writes Comic Relief outlines over the Univers ones through /dev/mem (fontpatch.h, made by font/build.py).
 * Every changed character keeps its place, its byte range and its width, so the UI can't tell, and any mix of
 * old and new data is still a valid font. Nothing reaches flash: the kernel treats the pages as unchanged and
 * /usr is read-only; a reboot gives Univers back, and so does --font-restore (after the patcher has been
 * stopped), which drops the file's pages from the file cache so they are read from flash again. Before writing
 * anything the patcher checks that every range holds the original bytes (by hash) or already the new ones. The
 * UI's font engine also keeps images of characters it has drawn, and the camera boots from a memory snapshot in
 * which that cache already holds Univers ones, so after writing the patcher marks the cached images stale and
 * the engine draws them again from the file (see "the UI's glyph cache" below); --font-restore does the same
 * after putting Univers back. The icon font /usr/share/app/Sony_DI_Icons.ttf gets the same treatment: the
 * lettering inside its icons (AWB, MF, ISO ...) is redrawn in Comic Relief Bold, each icon keeping its own bytes
 * (font/icons/); so do the camera's pictures (pics.c), and the orange accent colour turns green (pictures and
 * color_cmn.uxc). --text-only leaves the icons, pictures and colours alone, and so does the patcher for a file
 * that isn't exactly the one this was made for (status gets "noicons" / "nopics"). Status (on N [cM | nocache]
 * [tK | nolabels] [pP | nopics] [aA] [noicons] | partial N/M | off [pics] | badfile | error ...; M = cached
 * characters marked) goes to /android/dev/a7hacks.ft. A stop (SIGTERM, the app's Restart) is held off while
 * the patcher writes, so nothing is left half written.
 *
 * Static, no libc: build with
 *   clang --target=arm-linux-gnueabi -march=armv7-a -static -nostdlib -ffreestanding -Os -fuse-ld=lld \
 *         -o ../app/src/main/jniLibs/armeabi/liba7hacks.so a7hacks.c
 * (named lib*.so so that Android installs it, executable, with the app's native libraries)
 */
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned short u16;

#ifndef HOST
static long sc(long n, long a, long b, long c, long d, long e) {
    register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b; register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d; register long r4 __asm__("r4") = e;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "memory");
    return r0;
}
static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b; register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d; register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5) : "memory");
    return r0;
}
#define STATUS "/android/dev/a7hacks.st"
#define EXSTATUS "/android/dev/a7hacks.ex"
#define UISTATUS "/android/dev/a7hacks.ui"
#define FTSTATUS "/android/dev/a7hacks.ft"
#define FONTFILE "/usr/share/app/UniversOTS-SJ_wIcon.ltt"
#define ICONFILE "/usr/share/app/Sony_DI_Icons.ttf"
#define PICDIR "/usr/share/app/"
#define DROPCACHES "/proc/sys/vm/drop_caches"
#define SSFILE "/android/dev/a7hacks.ss"
#define RAM_LO 0x80000000u     /* physical RAM: 1 GB from 0x80000000 */
#define RAM_SIZE 0x40000000u
#else
long sc(long n, long a, long b, long c, long d, long e);   /* test build: provided by the test harness */
long sc6(long n, long a, long b, long c, long d, long e, long f);
#define STATUS "a7hacks.st"
#define EXSTATUS "a7hacks.ex"
#define UISTATUS "a7hacks.ui"
#define FTSTATUS "a7hacks.ft"
#ifndef FONTFILE
#define FONTFILE "font.ltt"
#endif
#ifndef ICONFILE
#define ICONFILE "icons.ttf"
#endif
#ifndef PICDIR
#define PICDIR ""
#endif
#define DROPCACHES "drop_caches"
#define SSFILE "a7hacks.ss"
#define RAM_LO 0u
#define RAM_SIZE 0xfffff000u
#endif
#define SYS_EXIT 1
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_GETPID 20
#define SYS_RENAME 38
#define SYS_LLSEEK 140
#define SYS_NANOSLEEP 162
#define SYS_GETDENTS64 217
#define SYS_READLINK 85
#define SYS_MLOCK 150
#define SYS_MUNMAP 91
#define SYS_MMAP2 192
#define SYS_NICE 34
#define SYS_RT_SIGPROCMASK 175

/* the answer line, collected here and written out at the end */
static char msg[2048];
static int mlen;
static void out(const char *s) { while (*s && mlen < (int)sizeof msg - 2) msg[mlen++] = *s++; }
static void outb(const u8 *s, int n) {
    for (int i = 0; i < n && mlen < (int)sizeof msg - 2; i++) msg[mlen++] = (s[i] >= 32 && s[i] < 127) ? s[i] : '?';
}
static void outn(u32 v) {
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v && i > 0);
    out(b + i);
}
static int dec(const char *s, u32 *v) {
    u32 x = 0; if (!*s) return 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return 0; x = x * 10 + (*s - '0'); }
    *v = x; return 1;
}
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c |= 32; if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* physical addresses (av-cam is loaded at 0x83541000) */
#define BLOCK 0x83f22c10u   /* av-cam settings + 0x5ac */
#define BLEN 0x60           /* u32 state, name[0x41], pad[3], 6 x u32 */
#define NAME 4
#define NAMELEN 0x41
#define NUMS 0x48

struct sig { u32 addr; int n; const u8 *bytes; };
/* command 0x4102 handler (stores into the block) */
static const u8 sig1[] = {0x38,0xb5,0x04,0x46,0x36,0x4d,0x41,0x22,0x03,0x7a,0x04,0xf1,0x09,0x01,0x7d,0x44,
                          0x05,0xf5,0xb6,0x60,0xc5,0xf8,0xac,0x35,0x27,0xf2,0x8e,0xec,0x94,0xf8,0x4b,0x20};
/* capture setup (reads the block) */
static const u8 sig2[] = {0xd3,0xf8,0xf8,0x25,0x00,0x92,0x03,0xf5,0xb6,0x62,0xd3,0xf8,0xac,0x15,0xd3,0xf8,
                          0xf4,0x35,0x15,0xf3,0x06,0xfb,0x07,0xa8};
/* EXIF builder: "use the manual lens info?" */
static const u8 sig3[] = {0x08,0xb5,0xcd,0xf7,0xd9,0xf2,0x60,0xb1,0xcd,0xf7,0x0e,0xf3,0x02,0x28,0x07,0xdc,
                          0xf4,0xf4,0x6a,0xfe,0xa0,0xf1,0x01,0x03,0x58,0x42,0x40,0xeb,0x03,0x00,0x08,0xbd};
static const struct sig sigs[] = {
    {0x837cf924u, sizeof sig1, sig1}, {0x83827560u, sizeof sig2, sig2}, {0x83b3cb08u, sizeof sig3, sig3},
};

static int fd = -1;
static int pread_(u32 a, u8 *buf, int n) {
    unsigned long long res;
    if (sc(SYS_LLSEEK, fd, 0, a, (long)&res, 0) < 0) return -1;      /* _llseek(fd, 0, a) */
    return sc(SYS_READ, fd, (long)buf, n, 0, 0) == n ? 0 : -1;
}
static int pwrite_(u32 a, const u8 *buf, int n) {
    unsigned long long res;
    if (sc(SYS_LLSEEK, fd, 0, a, (long)&res, 0) < 0) return -1;
    return sc(SYS_WRITE, fd, (long)buf, n, 0, 0) == n ? 0 : -1;
}
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static void wr32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static int sane(const u8 *b) {
    if (rd32(b) > 1) return 0;
    int nul = 0;
    for (int i = 0; i < NAMELEN; i++) if (!b[NAME + i]) { nul = 1; break; }
    if (!nul) return 0;
    for (int k = 1; k < 6; k += 2) {                 /* the three denominators */
        u32 d = rd32(b + NUMS + 4 * k);
        if (d == 0 || d > 100000) return 0;
    }
    return 1;
}

static void show(const u8 *b) {
    out(" state="); outn(rd32(b));
    out(" focal="); outn(rd32(b + NUMS)); out("/"); outn(rd32(b + NUMS + 4));
    out(" f="); outn(rd32(b + NUMS + 8)); out("/"); outn(rd32(b + NUMS + 12));
    out(" fmin="); outn(rd32(b + NUMS + 16)); out("/"); outn(rd32(b + NUMS + 20));
    out(" name=\""); int n = 0; while (n < NAMELEN - 1 && b[NAME + n]) n++;
    outb(b + NAME, n); out("\"");
}

/* exposure focal length (mode D) */
#define MOUNT 0x83f110a8u      /* lens mount type: 0 = no electronic lens (FE 50: 4) */
#define SITE 0x83775b96u       /* "bl 0x1e2f98" in the exposure code's lens update */
#define MAILBOX 0x83774ef6u    /* alignment padding after a "bx lr" */
static const u8 site_orig[] = {0xae,0xf7,0xff,0xf9};
static const u8 site_patch[] = {0x3f,0xf8,0xa2,0x0c};        /* ldrh.w r0, [pc, #-0xca2] -> MAILBOX */
static const u8 mailbox_orig[] = {0x00,0xbf};
static const u8 ctx1[] = {0xe3,0x58,0x1a,0x70,0xba,0xf0,0xf5,0xff};   /* 8 bytes before SITE */
static const u8 ctx2[] = {0x06,0xf0,0x93,0xf9};                       /* 4 bytes after SITE */
static const u8 ctx3[] = {0x12,0x88,0x1a,0x80,0x70,0x47};             /* 6 bytes before MAILBOX */
static const u8 ctx4[] = {0x9a,0x48,0x88,0x00,0xa2,0x48,0x88,0x00};   /* 8 bytes after MAILBOX */

static int same(const u8 *a, const u8 *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }

static int checksigs(void) {
    u8 buf[64];
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        if (pread_(sigs[i].addr, buf, sigs[i].n) < 0) return -1;
        if (!same(buf, sigs[i].bytes, sigs[i].n)) return 0;
    }
    return 1;
}

/* writes one status line to path (via path.t and a rename, so readers never see half a line) */
static void statusto(const char *path, const char *tmp, const char *s) {
    int f = sc(SYS_OPEN, (long)tmp, 01 | 0100 | 01000, 0644, 0, 0);
    if (f < 0) return;
    int n = 0; while (s[n]) n++;
    sc(SYS_WRITE, f, (long)s, n, 0, 0);
    sc(SYS_WRITE, f, (long)"\n", 1, 0, 0);
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    sc(SYS_RENAME, (long)tmp, (long)path, 0, 0, 0);
}
static void exstatus(const char *s) { statusto(EXSTATUS, EXSTATUS ".t", s); }

/* SteadyShot focal length from the sampler's bk.elf output ("... data:\n1C,00,"), 0 if unavailable */
static u32 steadyshot(void) {
    char t[256];
    int f = sc(SYS_OPEN, (long)SSFILE, 0, 0, 0, 0);
    if (f < 0) return 0;
    int n = sc(SYS_READ, f, (long)t, sizeof t - 1, 0, 0);
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    if (n <= 0) return 0;
    while (n > 0 && (t[n - 1] == '\n' || t[n - 1] == '\r' || t[n - 1] == ' ' || t[n - 1] == '\t')) n--;
    int s = n;
    while (s > 0 && t[s - 1] != '\n' && t[s - 1] != ' ' && t[s - 1] != '\t') s--;
    /* expect "LO,HI," or "LO,HI" */
    if (n - s < 5 || t[s + 2] != ',') return 0;
    int a = hexv(t[s]), b = hexv(t[s + 1]), c = hexv(t[s + 3]), d = hexv(t[s + 4]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return 0;
    u32 v = (u32)((c << 4 | d) << 8 | (a << 4 | b));
    return v < 10000 ? v : 0;
}

static int exposure(void) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
    if (fd < 0) { exstatus("error open /dev/mem"); out("error open /dev/mem"); return 2; }
    int ok = checksigs();
    u8 b1[8], b2[6], b3[8], b4[4];
    if (ok > 0) {
        if (pread_(SITE - 8, b1, 8) < 0 || pread_(SITE + 4, b4, 4) < 0 ||
            pread_(MAILBOX - 6, b2, 6) < 0 || pread_(MAILBOX + 2, b3, 8) < 0) ok = -1;
        else if (!same(b1, ctx1, 8) || !same(b4, ctx2, 4) || !same(b2, ctx3, 6) || !same(b3, ctx4, 8)) ok = 0;
    }
    if (ok < 0) { exstatus("error read"); out("error read"); return 2; }
    if (!ok) { exstatus("badsig"); out("badsig"); return 3; }
    char last[24]; last[0] = 0;
    for (;;) {
        u8 site[4], mbx[2], mnt[4];
        if (pread_(SITE, site, 4) < 0 || pread_(MAILBOX, mbx, 2) < 0 || pread_(MOUNT, mnt, 4) < 0) {
            exstatus("error read"); out("error read"); return 2;
        }
        int patched = same(site, site_patch, 4);
        if (!patched && !same(site, site_orig, 4)) { exstatus("badsite"); out("badsite"); return 3; }
        u32 mm = steadyshot();
        int lens = rd32(mnt) != 0;
        u32 want = (!lens && mm > 0) ? mm : 0;
        char st[24]; int k = 0;
        if (want) {
            u8 v[2] = {(u8)want, (u8)(want >> 8)};
            if (!same(mbx, v, 2) && pwrite_(MAILBOX, v, 2) < 0) { exstatus("error write"); out("error write"); return 2; }
            if (!patched && pwrite_(SITE, site_patch, 4) < 0) { exstatus("error write"); out("error write"); return 2; }
            const char *p = "on "; while (*p) st[k++] = *p++;
            char d[8]; int i = 7; d[i] = 0; u32 x = want;
            do { d[--i] = '0' + x % 10; x /= 10; } while (x && i > 0);
            while (d[i]) st[k++] = d[i++];
        } else {
            if (patched && pwrite_(SITE, site_orig, 4) < 0) { exstatus("error write"); out("error write"); return 2; }
            if (!same(mbx, mailbox_orig, 2) && pwrite_(MAILBOX, mailbox_orig, 2) < 0) { exstatus("error write"); out("error write"); return 2; }
            const char *p = lens ? "lens" : "off"; while (*p) st[k++] = *p++;
        }
        st[k] = 0;
        int changed = 0;
        for (int i = 0; ; i++) { if (st[i] != last[i]) { changed = 1; break; } if (!st[i]) break; }
        if (changed) { exstatus(st); for (int i = 0; i <= k; i++) last[i] = st[i]; }
#ifdef HOST
        return 0;      /* test build: one pass */
#endif
        long ts[2] = {0, 250000000};
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    }
}

/* ---- menu label (--label-patcher and --label-find) ---- */
#define LREC 24                /* one string record: u32 id + 19 characters + NUL */
static const u8 lab_old[LREC] = {0x5a,0xf8,0x04,0x1e,'S','t','e','a','d','y','S','.',' ','F','o','c','a','l',' ','L','e','n','.',0};
static const u8 lab_new[LREC] = {0x5a,0xf8,0x04,0x1e,'M','a','n','u','a','l',' ','F','o','c','a','l',' ','L','e','n','g','t','h',0};

static void outx(u32 v) {
    char b[9]; for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[8] = 0; out(b);
}
/* "/proc/<pid>/<name>" */
static void procpath(char *b, int pid, const char *name) {
    const char *p = "/proc/"; int k = 0;
    while (*p) b[k++] = *p++;
    char d[12]; int i = 11; d[i] = 0; u32 x = (u32)pid;
    do { d[--i] = '0' + x % 10; x /= 10; } while (x && i > 0);
    while (d[i]) b[k++] = d[i++];
    b[k++] = '/';
    while (*name) b[k++] = *name++;
    b[k] = 0;
}

/* /proc/<pid>/pagemap (one 64-bit entry per virtual page), kept open for the last process asked about */
static int pm_fd = -1, pm_pid = -1;
static int pm_open(int pid) {
    if (pid == pm_pid && pm_fd >= 0) return pm_fd;
    if (pm_fd >= 0) sc(SYS_CLOSE, pm_fd, 0, 0, 0, 0);
    char path[40]; procpath(path, pid, "pagemap");
    pm_fd = sc(SYS_OPEN, (long)path, 0, 0, 0, 0);
    pm_pid = pm_fd >= 0 ? pid : -1;
    return pm_fd;
}
/* physical address from a pagemap entry; 0 if the page isn't present or isn't in RAM */
static u32 pm_phys(const u8 *e, u32 va) {
    if (!(e[7] & 0x80)) return 0;                     /* bit 63: present */
    u32 lo = rd32(e), hi = rd32(e + 4) & 0x007fffff;  /* bits 0..54: page frame number */
    if (hi || lo >= 0x100000) return 0;
    u32 pa = lo << 12 | (va & 0xfff);
    if ((u32)(pa - RAM_LO) >= RAM_SIZE) return 0;
    return pa;
}
/* 1 = *pa set, 0 = page not there, -1 = can't look up the process's pages */
static int v2p(int pid, u32 va, u32 *pa) {
    int f = pm_open(pid); if (f < 0) return -1;
    unsigned long long res; u8 e[8];
    if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) < 0) return -1;
    if (sc(SYS_READ, f, (long)e, 8, 0, 0) != 8) return -1;
    *pa = pm_phys(e, va);
    return *pa ? 1 : 0;
}
/* reads (wr = 0) or writes n bytes at virtual address va of process pid, page by page:
 * 1 = done, 0 = a page isn't there, -1 = can't look up the process's pages, -2 = /dev/mem failed */
static int vrw(int pid, u32 va, u8 *b, int n, int wr) {
    while (n > 0) {
        int k = 4096 - (int)(va & 0xfff); if (k > n) k = n;
        u32 pa; int r = v2p(pid, va, &pa); if (r <= 0) return r;
        if ((wr ? pwrite_(pa, b, k) : pread_(pa, b, k)) < 0) return -2;
        va += k; b += k; n -= k;
    }
    return 1;
}

/* the label records found so far */
#define MAXHITS 8
static int hpid[MAXHITS]; static u32 hva[MAXHITS]; static int nhits;
static void addhit(int pid, u32 va) {
    for (int i = 0; i < nhits; i++) if (hpid[i] == pid && hva[i] == va) return;
    if (nhits < MAXHITS) { hpid[nhits] = pid; hva[nhits] = va; nhits++; }
}
static void delhit(int i) { for (; i + 1 < nhits; i++) { hpid[i] = hpid[i + 1]; hva[i] = hva[i + 1]; } nhits--; }

/* the mappings of one process worth looking through, from /proc/PID/maps */
#define MAXRNG 4000
static u32 rs[MAXRNG], rend[MAXRNG];
static int nrng, isui;
static int heaponly, heapk;     /* heaponly: take only the process's own data (see the font cache); heapk: its [heap] */
static const char *hexp(const char *p, const char *end, u32 *v) {
    u32 x = 0; int n = 0;
    while (p < end && hexv(*p) >= 0) { if (++n > 8) return 0; x = x << 4 | (u32)hexv(*p); p++; }
    *v = x; return n ? p : 0;
}
static int starts(const char *p, int n, const char *s) {
    for (int k = 0; s[k]; k++) if (k >= n || p[k] != s[k]) return 0;
    return 1;
}
/* "start-end perms offset dev inode   path" */
static void maps_line(const char *p, const char *end) {
    u32 s, e;
    if (!(p = hexp(p, end, &s)) || p >= end || *p++ != '-') return;
    if (!(p = hexp(p, end, &e)) || p >= end || *p++ != ' ') return;
    if (end - p < 5 || p[4] != ' ') return;
    char r = p[0], w = p[1]; p += 4;
    for (int f = 0; f < 3; f++) { while (p < end && *p == ' ') p++; while (p < end && *p != ' ') p++; }
    while (p < end && *p == ' ') p++;
    int n = (int)(end - p);
    if (n >= 10 && starts(end - 10, 10, "/libObj.so")) isui = 1;
    if (r != 'r' || e <= s) return;
    if (heaponly == 2 && n && starts(p, n, "/dev/")) {     /* M a: driver memory too (only pages in RAM get read) */
        if (nrng < MAXRNG) { rs[nrng] = s; rend[nrng] = e; nrng++; }
        return;
    }
    if (heaponly) {                /* writable and not a file: the [heap] and anonymous mappings */
        if (w != 'w' || (n && !starts(p, n, "[heap]"))) return;
        if (n && nrng < MAXRNG) heapk = nrng;
    }
    /* device mappings could be anything, and reading them could have side effects */
    if (starts(p, n, "/dev/") && !starts(p, n, "/dev/zero") && !starts(p, n, "/dev/shm/")) return;
    if (starts(p, n, "[vectors]")) return;
    if (nrng < MAXRNG) { rs[nrng] = s; rend[nrng] = e; nrng++; }
}
static char mbuf[8192];
/* fills rs/rend/nrng and isui from /proc/PID/maps; -1 if it can't be read */
static int readmaps(int pid) {
    char path[40]; procpath(path, pid, "maps");
    nrng = 0; isui = 0; heapk = -1;
    int f = sc(SYS_OPEN, (long)path, 0, 0, 0, 0);
    if (f < 0) return -1;
    int have = 0;
    for (;;) {
        int r = sc(SYS_READ, f, (long)(mbuf + have), (int)sizeof mbuf - have, 0, 0);
        if (r <= 0) break;
        have += r;
        int st = 0;
        for (int i = 0; i < have; i++) if (mbuf[i] == '\n') { maps_line(mbuf + st, mbuf + i); st = i + 1; }
        for (int i = st; i < have; i++) mbuf[i - st] = mbuf[i];
        have -= st;
        if (have == (int)sizeof mbuf) have = 0;       /* a line this long isn't one we want */
    }
    if (have) maps_line(mbuf, mbuf + have);
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    return 0;
}

static u8 pg[32 + 4096 + 32];  /* the page being searched, with up to LREC-1 bytes of its neighbours on each side */
static u8 pme[8 * 512];
static u32 scanned;            /* pages looked at by the last search */

/* The present pages seen by the last walk through the UI's memory, sorted by address, so that the next walk
 * can tell which pages are new (or now sit in a different frame): that is where freshly loaded strings land.
 * Bit 0 of a stored frame address marks a page that was new last time: it gets one more look, in case it
 * was still being filled. */
#define MAXPG 65536
static u32 pgva[2][MAXPG], pgpa[2][MAXPG];
static int pgn[2], pgcur, pgok, pgpid = -1;

static void findin(int pid, u32 va, int from, int to) {
    for (int j = from; j <= to; j++)
        if (pg[j] == 0x5a && (same(pg + j, lab_old, LREC) || same(pg + j, lab_new, LREC)))
            addhit(pid, va - 32 + (u32)j);
}

/* walks the present pages of the mappings in rs/rend of process pid looking for the label record (old or
 * new text): full = in every page, otherwise only in the pages that are new since the last walk */
static int scanpid(int pid, int full) {
    int f = pm_open(pid); if (f < 0) return -1;
    int prev = pgcur, cur = pgcur ^ 1, oi = 0, nn = 0, over = 0;
    int inc = !full && pgok && pgpid == pid;
    for (int k = 0; k < nrng; k++) {
        int carry = 0;         /* full: pg starts with the last LREC-1 bytes of the page before */
        u32 va = rs[k];
        while (va < rend[k]) {
            u32 np = (rend[k] - va) >> 12; if (np > 512) np = 512;
            unsigned long long res;
            if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) < 0) break;
            int got = sc(SYS_READ, f, (long)pme, (long)(np * 8), 0, 0) / 8;
            if (got <= 0) break;
            for (int i = 0; i < got; i++, va += 4096) {
                u32 pa = pm_phys(pme + 8 * i, va);
                if (!pa) { carry = 0; continue; }
                int fresh = 0;     /* 2 = new page, 1 = new last time */
                if (inc) {
                    while (oi < pgn[prev] && pgva[prev][oi] < va) oi++;
                    if (oi >= pgn[prev] || pgva[prev][oi] != va || (pgpa[prev][oi] & ~1u) != pa) fresh = 2;
                    else if (pgpa[prev][oi] & 1) fresh = 1;
                }
                if (nn < MAXPG) { pgva[cur][nn] = va; pgpa[cur][nn] = pa | (fresh == 2); nn++; } else over = 1;
                if (full) {
                    if (pread_(pa, pg + 32, 4096) < 0) { carry = 0; continue; }
                    scanned++;
                    findin(pid, va, carry ? 32 - (LREC - 1) : 32, 32 + 4096 - LREC);
                    for (int j = 0; j < LREC - 1; j++) pg[32 - (LREC - 1) + j] = pg[32 + 4096 - (LREC - 1) + j];
                    carry = 1;
                } else if (fresh) {
                    if (pread_(pa, pg + 32, 4096) < 0) continue;
                    scanned++;
                    int from = 32, to = 32 + 4096 - LREC;     /* plus records that cross into a neighbour */
                    if (vrw(pid, va - (LREC - 1), pg + 32 - (LREC - 1), LREC - 1, 0) == 1) from = 32 - (LREC - 1);
                    if (vrw(pid, va + 4096, pg + 32 + 4096, LREC - 1, 0) == 1) to = 32 + 4096 - 1;
                    findin(pid, va, from, to);
                }
            }
        }
    }
    pgcur = cur; pgn[cur] = nn; pgok = !over; pgpid = pid;
    return 0;
}

/* the processes to look in: the camera UI (the one running libObj.so), or with all every process but us */
#define MAXP 512
static int plist[MAXP], np_;
static char dbuf[4096];
static void findprocs(int all) {
    if (!all) {                    /* the one(s) from last time, if they're still there */
        int keep = 0;
        for (int i = 0; i < np_; i++) if (readmaps(plist[i]) == 0 && isui) plist[keep++] = plist[i];
        np_ = keep;
        if (np_) return;
    }
    np_ = 0;
    int d = sc(SYS_OPEN, (long)"/proc", 0, 0, 0, 0);
    if (d < 0) return;
    int me = sc(SYS_GETPID, 0, 0, 0, 0, 0);
    for (;;) {
        int n = sc(SYS_GETDENTS64, d, (long)dbuf, sizeof dbuf, 0, 0);
        if (n <= 0) break;
        for (int o = 0; o + 19 < n; ) {
            int reclen = (u8)dbuf[o + 16] | (u8)dbuf[o + 17] << 8;
            const char *name = dbuf + o + 19;
            int pid = 0, ok = *name != 0;
            for (const char *c = name; *c; c++) { if (*c < '0' || *c > '9') { ok = 0; break; } pid = pid * 10 + (*c - '0'); }
            if (ok && pid != me && np_ < MAXP && readmaps(pid) == 0 && (all ? nrng > 0 : isui)) plist[np_++] = pid;
            if (reclen <= 0) break;
            o += reclen;
        }
    }
    sc(SYS_CLOSE, d, 0, 0, 0, 0);
}
/* finds the label in those processes (adds to the hits); returns how many processes it looked in */
static int search(int all) {
    findprocs(all);
    scanned = 0;
    for (int i = 0; i < np_; i++) if (readmaps(plist[i]) == 0) scanpid(plist[i], 1);
    return np_;
}

/* what a hit holds now: 2 = the new text, 3 = the old text, 0 = something else / gone, -1/-2 errors (vrw) */
static int labstate(int pid, u32 va) {
    u8 b[LREC];
    int r = vrw(pid, va, b, LREC, 0);
    if (r <= 0) return r;
    return same(b, lab_new, LREC) ? 2 : same(b, lab_old, LREC) ? 3 : 0;
}
/* like labstate, but writes the new text over the old: 1 = renamed just now */
static int labfix(int pid, u32 va) {
    int r = labstate(pid, va);
    if (r != 3) return r;
    r = vrw(pid, va + 4, (u8 *)lab_new + 4, LREC - 4, 1);    /* the text only, the id stays */
    if (r <= 0) return r;
    return labstate(pid, va) == 2 ? 1 : -2;
}

static void uistatus(const char *s) { statusto(UISTATUS, UISTATUS ".t", s); }
static int scat(char *b, int k, const char *s) { while (*s && k < 38) b[k++] = *s++; b[k] = 0; return k; }
static int scatn(char *b, int k, u32 v) {
    char d[12]; int i = 11; d[i] = 0;
    do { d[--i] = '0' + v % 10; v /= 10; } while (v && i > 0);
    return scat(b, k, d + i);
}

#ifdef HOST
#ifndef HOST_TICKS
#define HOST_TICKS 1
#endif
static int hostticks;
#endif
static int label(void) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
    if (fd < 0) { uistatus("error open /dev/mem"); out("error open /dev/mem"); return 2; }
    char last[40]; last[0] = 0;
    u32 next = 0, wait = 5, mb = 0;        /* in 2 s ticks: search again after 10, 20, 40, 60, 60 ... s */
    int procs = 0;
    for (u32 t = 0; ; t++) {
        if (t >= next) {
            procs = search(0);
            mb = scanned >> 8;
            if (!procs) next = t + 5;                       /* no UI yet */
            else if (nhits) { next = t + 150; wait = 5; }   /* found: look for other copies every 5 minutes */
            else { next = t + wait; wait = wait < 15 ? wait * 2 : 30; }
        } else if (!nhits && np_ == 1 && readmaps(plist[0]) == 0 && isui) {
            /* in between: look only at memory the UI has just taken into use (where newly loaded strings are) */
            scanpid(plist[0], 0);
            if (nhits) { next = t + 150; wait = 5; }
        }
        int on = 0, err = 0;
        for (int i = 0; i < nhits; ) {
            int r = labfix(hpid[i], hva[i]);
            if (r == -2) { err = 1; i++; continue; }
            if (r <= 0) { delhit(i); if (!nhits) { next = t + 1; wait = 5; } continue; }
            on++; i++;
        }
        char st[40]; int k = 0; st[0] = 0;
        if (err) k = scat(st, k, "error write");
        else if (on) { k = scat(st, k, "on "); k = scatn(st, k, (u32)on); }
        else if (procs) { k = scat(st, k, "waiting "); k = scatn(st, k, mb); }
        else k = scat(st, k, "noui");
        int changed = 0;
        for (int i = 0; ; i++) { if (st[i] != last[i]) { changed = 1; break; } if (!st[i]) break; }
        if (changed) { uistatus(st); for (int i = 0; i <= k; i++) last[i] = st[i]; }
#ifdef HOST
        out(st); out(" | ");
        if (++hostticks >= HOST_TICKS) return 0;      /* test build: a few passes */
#endif
        long ts[2] = {2, 0};
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    }
}

/* S: search once, change nothing, list what was found */
static int labshow(int all) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    int procs = search(all);
    out("label: "); outn((u32)nhits); out(" found in "); outn(scanned >> 8); out(" MB of ");
    outn((u32)procs); out(all ? " processes" : " UI process(es)");
    for (int i = 0; i < nhits; i++) {
        u32 pa = 0; v2p(hpid[i], hva[i], &pa);
        int s = labstate(hpid[i], hva[i]);
        out("; pid "); outn((u32)hpid[i]); out(" va "); outx(hva[i]); out(" pa "); outx(pa);
        out(s == 2 ? " new" : s == 3 ? " old" : " gone");
    }
    return 0;
}

/* ---- UI font (mode F) ---- */
#include "fontpatch.h"
/* the font's license notice travels with its outlines ("strings liba7hacks.so" shows it); full text: font/OFL-ComicRelief.txt */
__attribute__((used)) static const char fontnotice[] =
    "Font outlines from Comic Relief (Regular and Bold), Copyright 2013 The Comic Relief Project Authors "
    "(https://github.com/loudifier/Comic-Relief), licensed under the SIL Open Font License, Version 1.1 "
    "(https://openfontlicense.org); modified to fit the camera's Univers font and the lettering of its icons.";
/* the files we change: the UI font, and the icon font (whose icons have lettering in them) */
static u32 fp_src[FP_COUNT], ip_src[IP_COUNT];
static struct pf {
    const char *path; u32 size; const u8 *head; int count;
    const u32 *off; const u16 *len, *dlen; const u32 *oldhash; const u8 *data;
    u32 *src;                     /* where each range's new bytes start in data */
    const u8 *map;                /* the file, mapped shared and read-only (and locked in RAM while the font patcher runs) */
} pfs[] = {
    {FONTFILE, FP_SIZE, fp_head, FP_COUNT, fp_off, fp_len, fp_dlen, fp_oldhash, fp_new, fp_src, 0},
    {ICONFILE, IP_SIZE, ip_head, IP_COUNT, ip_off, ip_len, ip_dlen, ip_oldhash, ip_new, ip_src, 0},
};
#define NPF ((int)(sizeof pfs / sizeof pfs[0]))
static u8 act[NPF];               /* the files the font patcher looks after (--text-only: the UI font only) */
static int mypid;

static void ftstatus(const char *s) { statusto(FTSTATUS, FTSTATUS ".t", s); }

/* maps file f (lock: and locks it, so its file-cache pages stay where they are): 0 ok, <0 error */
static int fontmap(int f, int lock) {
    struct pf *P = &pfs[f];
    int h = sc(SYS_OPEN, (long)P->path, 0, 0, 0, 0);
    if (h < 0) return -1;
    unsigned long long end = 0;
    if (sc(SYS_LLSEEK, h, 0, 0, (long)&end, 2) < 0 || end != P->size) { sc(SYS_CLOSE, h, 0, 0, 0, 0); return -2; }
    long a = sc6(SYS_MMAP2, 0, P->size, 1 /* PROT_READ */, 1 /* MAP_SHARED */, h, 0);
    sc(SYS_CLOSE, h, 0, 0, 0, 0);
    if ((unsigned long)a >= (unsigned long)-4095L) return -3;
    P->map = (const u8 *)a;
    if (lock && sc(SYS_MLOCK, a, P->size, 0, 0, 0) < 0) return -4;
    if (!same(P->map, P->head, 16)) return -2;
    u32 k = 0;
    for (int i = 0; i < P->count; i++) { P->src[i] = k; k += P->dlen[i]; }
    return 0;
}
static void fontunmap(int f) { if (pfs[f].map) sc(SYS_MUNMAP, (long)pfs[f].map, pfs[f].size, 0, 0, 0); pfs[f].map = 0; }
static u32 fnv(const u8 *p, int n) { u32 h = 0x811c9dc5u; for (int i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u; return h; }
/* range i of file f in RAM: 1 = new bytes, 0 = the original bytes (by their hash), -1 = something else */
static int frange(int f, int i) {
    const struct pf *P = &pfs[f];
    const u8 *p = P->map + P->off[i];
    int n = P->dlen[i], z = 1;
    for (int k = n; k < P->len[i]; k++) if (p[k]) { z = 0; break; }
    if (z && same(p, P->data + P->src[i], n)) return 1;
    if (fnv(p, P->len[i]) == P->oldhash[i]) return 0;
    return -1;
}
/* writes the new bytes of range i of file f into the file's pages, via their physical addresses */
static const u8 zeros[1024];
/* holds a stop (kill: SIGTERM, as the app's Restart sends; and SIGINT) off while on, so a write is never left
 * half done: the signal waiting takes effect as soon as the write is complete */
static void hold(int on) {
    u32 set[2] = { 1u << (15 - 1) | 1u << (2 - 1), 0 };
    sc(SYS_RT_SIGPROCMASK, on ? 0 /* SIG_BLOCK */ : 1 /* SIG_UNBLOCK */, (long)set, 0, 8, 0);
}
static int fset1(int f, int i);
static int fset(int f, int i) { hold(1); int r = fset1(f, i); hold(0); return r; }
static int fset1(int f, int i) {
    const struct pf *P = &pfs[f];
    u32 va = (u32)(unsigned long)(P->map + P->off[i]);
    if (vrw(mypid, va, (u8 *)P->data + P->src[i], P->dlen[i], 1) <= 0) return -1;
    for (int k = P->dlen[i]; k < P->len[i]; ) {
        int n = P->len[i] - k; if (n > (int)sizeof zeros) n = sizeof zeros;
        if (vrw(mypid, va + k, (u8 *)zeros, n, 1) <= 0) return -1;
        k += n;
    }
    return frange(f, i) == 1 ? 0 : -1;
}

#define PIC_BATCH 100                   /* pictures per pass of the font patcher's loop (a pass a second while there are more) */
#include "pics.c"

/* The UI reads the fonts through buffered file reads (1 KB at a time, at 1 KB boundaries). A character that
 * crosses the edge of the block sitting in its buffer could come out half old and half new, so ranges that
 * cross the edge of such a block wait until the UI has moved on. The block ends at the read position.
 * winf says which of our files each read position is in. */
#define MAXWIN 16
static u32 win[MAXWIN];
static int winf[MAXWIN], nwin;
static char dbuf2[2048];
static void findwindows(void) {
    nwin = 0;
    findprocs(0);
    for (int k = 0; k < np_; k++) {
        char path[64], link[128];
        procpath(path, plist[k], "fd");
        int d = sc(SYS_OPEN, (long)path, 0, 0, 0, 0);
        if (d < 0) continue;
        for (;;) {
            int n = sc(SYS_GETDENTS64, d, (long)dbuf2, sizeof dbuf2, 0, 0);
            if (n <= 0) break;
            for (int o = 0; o + 19 < n; ) {
                int reclen = (u8)dbuf2[o + 16] | (u8)dbuf2[o + 17] << 8;
                const char *name = dbuf2 + o + 19;
                if (*name >= '0' && *name <= '9') {
                    char fdp[48]; int j = 0;
                    const char *q = "fd/"; while (*q) fdp[j++] = *q++;
                    for (q = name; *q && j < 40; ) fdp[j++] = *q++;
                    fdp[j] = 0;
                    procpath(path, plist[k], fdp);
                    int l = sc(SYS_READLINK, (long)path, (long)link, sizeof link - 1, 0, 0);
                    for (int f = 0; f < NPF; f++) {
                        if (!act[f]) continue;
                        const char *want = pfs[f].path; int m = 0; while (want[m]) m++;
                        if (l != m || !same((const u8 *)link, (const u8 *)want, m) || nwin >= MAXWIN) continue;
                        char info[48]; int t = 0;
                        q = "fdinfo/"; while (*q) info[t++] = *q++;
                        for (q = name; *q && t < 44; ) info[t++] = *q++;
                        info[t] = 0;
                        procpath(path, plist[k], info);
                        int h = sc(SYS_OPEN, (long)path, 0, 0, 0, 0);
                        if (h >= 0) {
                            char b[128]; int r = sc(SYS_READ, h, (long)b, sizeof b - 1, 0, 0);
                            sc(SYS_CLOSE, h, 0, 0, 0, 0);
                            if (r > 5 && same((const u8 *)b, (const u8 *)"pos:", 4)) {
                                u32 v = 0; int z = 4;
                                while (z < r && (b[z] == ' ' || b[z] == '\t')) z++;
                                while (z < r && b[z] >= '0' && b[z] <= '9') v = v * 10 + (b[z++] - '0');
                                win[nwin] = v; winf[nwin] = f; nwin++;
                            }
                        }
                    }
                }
                if (reclen <= 0) break;
                o += reclen;
            }
        }
        sc(SYS_CLOSE, d, 0, 0, 0, 0);
    }
}
static int crossesedge(int f, int i) {
    u32 a = pfs[f].off[i], b = a + pfs[f].len[i];
    for (int w = 0; w < nwin; w++) {
        if (winf[w] != f) continue;
        u32 hi = win[w], lo = hi >= 1024 ? hi - 1024 : 0;
        int ina = a >= lo && a < hi, inb = b - 1 >= lo && b - 1 < hi;
        if (ina != inb) return 1;
        if (a < lo && b > hi) return 1;
    }
    return 0;
}
/* does the block in a read buffer of file f that ends at pos hold part of a changed character? */
static int winpatched(int f, u32 pos) {
    u32 lo = pos >= 1024 ? pos - 1024 : 0;
    for (int i = 0; i < pfs[f].count; i++) if (pfs[f].off[i] < pos && pfs[f].off[i] + pfs[f].len[i] > lo) return 1;
    return 0;
}

/* The UI's glyph cache. The camera starts from a memory snapshot that Sony made after the UI had drawn a few
 * screens, so the font engine (Monotype iType, inside libObj.so) begins with a cache of Univers glyph images for
 * the characters on those screens and would go on using them instead of the file. Once the file has the new
 * outlines, the font patcher marks every entry in that cache as stale: it sets the top bit of the entry's flags word. Lookups
 * compare that word in full and only ever ask for flags within 0x005dbfff, and nothing else reads it (it isn't
 * even used while an entry sits free in the engine's pool), so the next lookup misses and the engine draws the
 * character afresh from the file. The marked entries stay where they are, unused, until the engine wants
 * their room and frees them itself; nothing in its lists is moved or freed by us.
 * The cache hangs off the engine's server structure: 1021 hash buckets at +0x1c, its heap limit at +0x1010
 * and bytes in use at +0x1014, and +0x104c holds 0x4012c from when the server is set up. An entry: u16 type
 * at +4 (0x7fxx), u16 glyph at +6, u32 flags at +8, the glyph image at +12 (whose first word points back at
 * the entry), the scaled font at +16, the next entry in its bucket at +0x2c. */
#define FS_HASH 1021
#define SRV_BUCKETS 0x1c
#define SRV_HEAP 0x1010
#define SRV_USED 0x1014
#define SRV_TAG 0x104c
#define SRV_TAGVAL 0x0004012cu
#define SRV_SIZE 0x1064
#define CE_SIZE 0x34
#define CE_TYPE 4
#define CE_FLAGS 8
#define CE_DATA 12
#define CE_NEXT 0x2c
#define MAXSRV 4
static u32 srv[MAXSRV]; static int nsrv, srvpid = -1;
static u8 sbuf[SRV_SIZE];

/* in the mappings read last (readmaps with heaponly)? */
static int inrng(u32 a) { for (int k = 0; k < nrng; k++) if (a >= rs[k] && a < rend[k]) return 1; return 0; }
/* is there a font server at va? (leaves it in sbuf) */
static int srvok(int pid, u32 va) {
    if (vrw(pid, va, sbuf, SRV_SIZE, 0) != 1) return 0;
    u32 heap = rd32(sbuf + SRV_HEAP), used = rd32(sbuf + SRV_USED);
    if (rd32(sbuf + SRV_TAG) != SRV_TAGVAL || heap < 0x10000 || heap > 0x10000000 || used > heap) return 0;
    for (int i = 0; i < FS_HASH; i++) {
        u32 b = rd32(sbuf + SRV_BUCKETS + 4 * i);
        if (b && ((b & 3) || !inrng(b))) return 0;
    }
    return 1;
}
static void srvscan(int pid, int k) {
    int f = pm_open(pid); if (f < 0) return;
    u32 va = rs[k];
    while (va < rend[k] && nsrv < MAXSRV) {
        u32 np = (rend[k] - va) >> 12; if (np > 512) np = 512;
        unsigned long long res;
        if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) < 0) return;
        int got = sc(SYS_READ, f, (long)pme, (long)(np * 8), 0, 0) / 8;
        if (got <= 0) return;
        for (int i = 0; i < got; i++, va += 4096) {
            u32 pa = pm_phys(pme + 8 * i, va);
            if (!pa || pread_(pa, pg, 4096) < 0) continue;
            for (int o = 0; o < 4096 && nsrv < MAXSRV; o += 4) {
                if (rd32(pg + o) != SRV_TAGVAL || va + o < rs[k] + SRV_TAG) continue;
                u32 x = va + o - SRV_TAG; int dup = 0;
                for (int s = 0; s < nsrv; s++) if (srv[s] == x) dup = 1;
                if (!dup && srvok(pid, x)) srv[nsrv++] = x;
            }
        }
    }
}
/* finds the font servers in the UI's data (readmaps with heaponly first): the [heap], then the rest */
static void findservers(int pid) {
    nsrv = 0; srvpid = pid;
    if (heapk >= 0) srvscan(pid, heapk);
    for (int k = 0; k < nrng && nsrv < MAXSRV; k++) if (k != heapk) srvscan(pid, k);
}
/* walks one server's cache: counts its entries (and the stale ones), and with mark marks the others stale.
 * Everything is checked before it is used, and a bucket is left alone at the first thing that doesn't fit
 * (the UI may be changing it right then): returns 1 if that happened */
static int walkcache(int pid, u32 s, int mark, u32 *cnt, u32 *stale, u32 *marked) {
    if (vrw(pid, s, sbuf, SRV_SIZE, 0) != 1 || rd32(sbuf + SRV_TAG) != SRV_TAGVAL) return 1;
    int broken = 0;
    for (int i = 0; i < FS_HASH; i++) {
        u32 e = rd32(sbuf + SRV_BUCKETS + 4 * i);
        for (int n = 0; e; n++) {
            u8 ce[CE_SIZE], d[4];
            if (n >= 512 || (e & 3) || !inrng(e) || vrw(pid, e, ce, CE_SIZE, 0) != 1) { broken = 1; break; }
            u32 data = rd32(ce + CE_DATA);
            if (ce[CE_TYPE + 1] != 0x7f || !data || (data & 3) || !inrng(data) ||
                vrw(pid, data, d, 4, 0) != 1 || rd32(d) != e) { broken = 1; break; }
            (*cnt)++;
            if (ce[CE_FLAGS + 3] & 0x80) (*stale)++;
            else if (mark) {
                u8 v = ce[CE_FLAGS + 3] | 0x80;          /* one byte: the UI sees the old or the new word */
                if (vrw(pid, e + CE_FLAGS + 3, &v, 1, 1) == 1) (*marked)++;
            }
            e = rd32(ce + CE_NEXT);
        }
    }
    return broken;
}
/* marks the UI's cached glyphs stale: how many it marked, -1 if it found no cache */
static int flushcache(void) {
    findprocs(0);
    int found = 0; u32 marked = 0;
    for (int k = 0; k < np_; k++) {
        int pid = plist[k];
        heaponly = 1; int r = readmaps(pid); heaponly = 0;
        if (r < 0) continue;
        int ok = srvpid == pid && nsrv > 0;
        for (int s = 0; ok && s < nsrv; s++) if (!srvok(pid, srv[s])) ok = 0;
        if (!ok) findservers(pid);
        if (!nsrv) continue;
        found = 1;
        for (int pass = 0; pass < 3; pass++) {       /* again if the UI was busy with a bucket */
            int broken = 0;
            for (int s = 0; s < nsrv; s++) { u32 c = 0, st = 0; broken |= walkcache(pid, srv[s], 1, &c, &st, &marked); }
            if (!broken) break;
        }
    }
    return found ? (int)marked : -1;
}
/* The UI's label cache (libra in libObj.so). Every text it draws (a menu item, a page number, the lettering of an
 * icon) is drawn once into a texture and kept: when the same text is wanted again, the texture is used, so a font
 * change never reaches it. The cache: 16 bucket heads, +0x48 bytes in use. An entry (0x28 bytes): +0 the next in its
 * bucket, +4 the cache, +8 0x80, +9 1 = drawn text (2 = a picture from the UI's files), +0xc u16 users, +0x10 its key
 * record. A lookup only walks the chain of bucket (key id & 15) and compares (key id & 0x3ffffff, plus some bits
 * from the key's source); a text key's id has 1 in bits 24..25. So a drawn text that is taken out of its bucket's
 * chain is never found again: the UI draws it once more (in the font it has now) and keeps that instead. The old
 * entry stays where it also is, on the list of unused entries, until the cache throws it out the usual way
 * (taking it out of its bucket finds nothing there and goes on). Only entries nobody uses are taken out; the ones
 * in use right then are remembered (entry and key id) and taken out once they aren't. */
#define TC_HEADS 16
#define TC_USED 0x48
#define TC_SIZE 0x58
#define TE_SIZE 0x28
#define TE_OWNER 4
#define TE_TAG 8
#define TE_KIND 9
#define TE_USERS 0xc
#define TE_KEY 0x10
#define MAXOLD 512
#define MAXCAND 64
static u32 tcm; static int tcpid = -1;
static u32 oldv[MAXOLD], oldk[MAXOLD]; static u8 olds[MAXOLD]; static int nold;
static u8 tbuf[TC_SIZE];
static u32 tctaken;               /* entries taken out since the font patcher started */

/* is there a label cache at m? (tcwhy: why not, for C) */
static int tcwhy;
static int tcok(int pid, u32 m) {
    tcwhy = 1; if (!m || (m & 3) || !inrng(m)) return 0;
    tcwhy = 2; if (vrw(pid, m, tbuf, TC_SIZE, 0) != 1) return 0;
    tcwhy = 3; if (rd32(tbuf + TC_USED) > 0x10000000u) return 0;
    int n = 0;
    for (int i = 0; i < TC_HEADS; i++) {
        u32 e = rd32(tbuf + 4 * i); u8 te[TE_KIND + 1];
        if (!e) continue;
        tcwhy = 10 + i * 10 + 1; if ((e & 3) || !inrng(e)) return 0;
        tcwhy++; if (vrw(pid, e, te, sizeof te, 0) != 1) return 0;
        tcwhy++; if (rd32(te + TE_OWNER) != m) return 0;
        n++;
    }
    tcwhy = 4; if (n < 4) return 0;
    tcwhy = 0; return 1;
}
/* finds it: every entry names its cache, so the address most entries name (in the [heap], else anywhere) */
static u32 cm[MAXCAND], cn[MAXCAND], tcmatch, tcpages; static int tcnc;
static void tccount(int pid, int k, int *nc) {
    int f = pm_open(pid); if (f < 0) return;
    u32 va = rs[k];
    while (va < rend[k]) {
        u32 np = (rend[k] - va) >> 12; if (np > 512) np = 512;
        unsigned long long res;
        if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) < 0) return;
        int got = sc(SYS_READ, f, (long)pme, (long)(np * 8), 0, 0) / 8;
        if (got <= 0) return;
        for (int i = 0; i < got; i++, va += 4096) {
            u32 pa = pm_phys(pme + 8 * i, va);
            if (!pa || pread_(pa, pg, 4096) < 0) continue;
            tcpages++;
            for (int o = 0; o + TE_SIZE <= 4096; o += 4) {
                if (pg[o + TE_TAG] != 0x80 || (pg[o + TE_KIND] != 1 && pg[o + TE_KIND] != 2)) continue;
                u32 m = rd32(pg + o + TE_OWNER);
                if (!m || (m & 3)) continue;
                tcmatch++;
                int c = 0; while (c < *nc && cm[c] != m) c++;
                if (c < *nc) cn[c]++;
                else if (*nc < MAXCAND) { cm[*nc] = m; cn[*nc] = 1; (*nc)++; }
                else {                       /* full: the least counted one makes room (and passes on its count) */
                    int lo = 0; for (int x = 1; x < MAXCAND; x++) if (cn[x] < cn[lo]) lo = x;
                    cm[lo] = m; cn[lo]++;
                }
            }
        }
    }
}
static int tcpick(int pid, int nc) {
    tcnc = nc;
    for (;;) {
        int b = -1;
        for (int c = 0; c < nc; c++) if (!(cn[c] >> 31) && cn[c] >= 8 && (b < 0 || cn[c] > cn[b])) b = c;
        if (b < 0) return 0;
        u32 keep = cn[b];
        if (tcok(pid, cm[b])) { tcm = cm[b]; tcpid = pid; return 1; }
        cn[b] = 0x80000000u | tcwhy << 20 | (keep & 0xfffff);       /* tried: why not, and its count (for C) */
    }
}
static void findlabels(int pid) {
    tcm = 0; tcpid = -1; int nc = 0; tcmatch = tcpages = 0;
    if (heapk >= 0) { tccount(pid, heapk, &nc); if (tcpick(pid, nc)) return; }
    nc = 0;
    for (int k = 0; k < nrng; k++) tccount(pid, k, &nc);
    tcpick(pid, nc);
}
static int isold(u32 e, u32 id) { for (int i = 0; i < nold; i++) if (oldv[i] == e && oldk[i] == id) return i; return -1; }
static void dropold(int i) { for (; i + 1 < nold; i++) { oldv[i] = oldv[i + 1]; oldk[i] = oldk[i + 1]; olds[i] = olds[i + 1]; } nold--; }
/* walks the cache. fresh: every drawn text there now is out of date; otherwise only the remembered ones.
 * Counts the drawn texts and the ones taken out; 1 if a chain didn't look right (the UI may be changing it) */
static int tcwalk(int pid, int fresh, u32 *txt, u32 *taken) {
    int broken = 0;
    u32 want = fresh ? 0xffff : 0;   /* the buckets to walk: all, or those of the remembered entries (fresh 2: count only) */
    for (int i = 0; i < nold; i++) { olds[i] = 0; want |= (oldk[i] >> 24) == 0x1a ? 0xffff : 1u << (oldk[i] & 15); }
    for (int i = 0; i < TC_HEADS; i++) {
        if (!(want >> i & 1)) continue;
        u32 link = tcm + 4 * (u32)i, lw;
        if (vrw(pid, link, (u8 *)&lw, 4, 0) != 1) { broken = 1; continue; }
        u32 e = lw;
        for (int n = 0; e; n++) {
            u8 te[TE_SIZE];
            if (n >= 1000 || (e & 3) || !inrng(e) || vrw(pid, e, te, TE_SIZE, 0) != 1 ||
                rd32(te + TE_OWNER) != tcm) { broken = 1; break; }   /* (+8 is mostly 0x80, but not always) */
            u32 next = rd32(te);
            if (te[TE_KIND] == 2 && picflush) {          /* a picture: its key is its record in the mapped file */
                u32 key = rd32(te + TE_KEY), id; u8 kb[4];
                if (!(key & 3) && vrw(pid, key, kb, 4, 0) == 1 && ispic(id = rd32(kb))) {
                    (*txt)++;
                    int o = isold(e, id);
                    if (o >= 0) olds[o] = 1;
                    if (fresh == 1 || o >= 0) {
                        if ((te[TE_USERS] | te[TE_USERS + 1]) == 0 && te[TE_TAG] == 0x80) {
                            u32 cur, nv = next;
                            if (vrw(pid, link, (u8 *)&cur, 4, 0) == 1 && cur == e && vrw(pid, link, (u8 *)&nv, 4, 1) == 1) {
                                (*taken)++;
                                if (o >= 0) dropold(o);
                                e = next;
                                continue;
                            }
                        } else if (o < 0 && nold < MAXOLD) { oldv[nold] = e; oldk[nold] = id; olds[nold] = 1; nold++; }
                    }
                }
            } else if (te[TE_KIND] == 1) {
                u32 key = rd32(te + TE_KEY), id; u8 kb[4];
                if ((key & 3) || !inrng(key) || vrw(pid, key, kb, 4, 0) != 1) { broken = 1; break; }
                id = rd32(kb);
                if ((id >> 24 & 3) == 1 && (id & 15) == (u32)i) {
                    (*txt)++;
                    int o = isold(e, id);
                    if (o >= 0) olds[o] = 1;
                    if (fresh == 1 || o >= 0) {
                        if ((te[TE_USERS] | te[TE_USERS + 1]) == 0 && te[TE_TAG] == 0x80) {   /* unused, settled */
                            u32 cur, nv = next;          /* one aligned word: the UI sees the old or the new link */
                            if (vrw(pid, link, (u8 *)&cur, 4, 0) == 1 && cur == e && vrw(pid, link, (u8 *)&nv, 4, 1) == 1) {
                                (*taken)++;
                                if (o >= 0) dropold(o);
                                e = next;
                                continue;                /* the link now points past it */
                            }
                        } else if (o < 0 && nold < MAXOLD) { oldv[nold] = e; oldk[nold] = id; olds[nold] = 1; nold++; }
                    }
                }
            }
            link = e; e = next;
        }
    }
    if (!broken) for (int i = nold - 1; i >= 0; i--) if (!olds[i]) dropold(i);   /* gone (thrown out, or reused) */
    return broken;
}
/* takes the out-of-date drawn texts out of the UI's label cache: how many, -1 if it found no cache */
static int labelflush(int fresh) {
    findprocs(0);
    int found = 0; u32 taken = 0;
    for (int k = 0; k < np_; k++) {
        int pid = plist[k];
        heaponly = 1; int r = readmaps(pid); heaponly = 0;
        if (r < 0) continue;
        if (!(tcpid == pid && tcok(pid, tcm))) { if (tcpid != pid) nold = 0; findlabels(pid); }
        if (!tcm) continue;
        found = 1;
        for (int pass = 0; pass < 3; pass++) { u32 t = 0; if (!tcwalk(pid, fresh, &t, &taken)) break; }
        break;                                  /* there is one UI */
    }
    if (!found) { nold = 0; return -1; }
    tctaken += taken;
    return (int)taken;
}

/* M path: copies the UI's own data (its [heap] and anonymous mappings) to path, for looking at elsewhere.
 * Per mapping: u32 start, u32 end, then its bytes (pages that aren't there as zeros). Reads only. */
static int memdump(const char *path, int dev) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    findprocs(0);
    if (!np_) { out("noui"); return 1; }
    int pid = plist[0];
    heaponly = dev ? 2 : 1; int r = readmaps(pid); heaponly = 0;
    if (r < 0) { out("error maps"); return 2; }
    int o = sc(SYS_OPEN, (long)path, 01 | 0100 | 01000, 0644, 0, 0);
    if (o < 0) { out("error open output"); return 2; }
    int f = pm_open(pid); u32 total = 0;
    for (int k = 0; k < nrng; k++) {
        u8 hd[8]; wr32(hd, rs[k]); wr32(hd + 4, rend[k]);
        sc(SYS_WRITE, o, (long)hd, 8, 0, 0);
        for (u32 va = rs[k]; va < rend[k]; va += 4096) {
            u8 e[8]; unsigned long long res; u32 pa = 0;
            if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) >= 0 && sc(SYS_READ, f, (long)e, 8, 0, 0) == 8)
                pa = pm_phys(e, va);
            if (!pa || pread_(pa, pg, 4096) < 0) for (int i = 0; i < 4096; i++) pg[i] = 0;
            if (sc(SYS_WRITE, o, (long)pg, 4096, 0, 0) != 4096) { sc(SYS_CLOSE, o, 0, 0, 0, 0); out("error write"); return 2; }
            total++;
        }
    }
    sc(SYS_CLOSE, o, 0, 0, 0, 0);
    out("pid "); outn((u32)pid); out(": "); outn((u32)nrng); out(" mappings, "); outn(total / 256); out(" MB");
    return 0;
}

/* --glyph-cache --list: lists the cached glyphs that aren't stale (drawn since the last --glyph-cache --mark-stale / --font-patcher), with their font's name and a
 * picture of each, straight to stdout. A glyph image: +24 width, +26 height, +28 bytes per row, +32 bits per
 * pixel, the pixels from +36; a scaled font's first word points at its font, whose first word points at its name */
static void say(const char *s) { int n = 0; while (s[n]) n++; sc(SYS_WRITE, 1, (long)s, n, 0, 0); }
static void sayn(u32 v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v && i > 0); say(b + i); }
static void sayx(u32 v) { char b[9]; for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[8] = 0; say(b); }
static u8 gbuf[0x1400 + 64];
static void cachelist(int pid, u32 s) {
    if (vrw(pid, s, sbuf, SRV_SIZE, 0) != 1) return;
    for (int i = 0; i < FS_HASH; i++) {
        u32 e = rd32(sbuf + SRV_BUCKETS + 4 * i);
        for (int n = 0; e && n < 512; n++) {
            u8 ce[CE_SIZE];
            if ((e & 3) || !inrng(e) || vrw(pid, e, ce, CE_SIZE, 0) != 1 || ce[CE_TYPE + 1] != 0x7f) break;
            u32 data = rd32(ce + CE_DATA), sf = rd32(ce + 16);
            e = rd32(ce + CE_NEXT);
            if (ce[CE_FLAGS + 3] & 0x80) continue;
            say("glyph "); sayn(ce[6] | ce[7] << 8); say(" type "); sayx(ce[4] | ce[5] << 8); say(" scaled font "); sayx(sf);
            u8 t[48]; u32 fo, nm;
            if (inrng(sf) && vrw(pid, sf, t, 4, 0) == 1 && inrng(fo = rd32(t)) && vrw(pid, fo, t, 4, 0) == 1 &&
                inrng(nm = rd32(t)) && vrw(pid, nm, t, 47, 0) == 1) {
                t[47] = 0; for (int k = 0; t[k]; k++) if (t[k] < 32 || t[k] > 126) { t[k] = 0; break; }
                say(" font \""); say((const char *)t); say("\"");
            }
            if (!inrng(data) || vrw(pid, data, gbuf, 36, 0) != 1) { say("\n"); continue; }
            u32 size = rd32(gbuf + 4); int w = (short)(gbuf[24] | gbuf[25] << 8), h = (short)(gbuf[26] | gbuf[27] << 8);
            int bpl = (short)(gbuf[28] | gbuf[29] << 8), bpp = gbuf[32];
            say(" "); sayn((u32)w); say("x"); sayn((u32)h); say(" "); sayn((u32)bpp); say("bpp\n");
            if (w <= 0 || h <= 0 || w > 80 || h > 80 || (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) ||
                bpl * 8 < w * bpp || size > sizeof gbuf || 36 + (u32)(bpl * h) > size || vrw(pid, data, gbuf, (int)size, 0) != 1) continue;
            for (int y = 0; y < h; y++) {
                char row[84]; int k = 0; row[k++] = '|';
                for (int x = 0; x < w; x++) {
                    int bit = x * bpp, v = (gbuf[36 + y * bpl + bit / 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1);
                    row[k++] = " .:-=+*#%@"[bpp == 1 ? v * 9 : bpp == 2 ? v * 3 : bpp == 4 ? v * 9 / 15 : v * 9 / 255];
                }
                row[k++] = '\n'; row[k] = 0; say(row);
            }
        }
    }
}

/* --glyph-cache: show the UI's glyph cache and label cache; --mark-stale: and mark the glyphs stale and take the drawn labels out
 * (the ones not in use right then); --list: list the glyphs drawn since */
static int cacheall;
static int cacheshow(int mark) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", mark == 1 ? 2 : 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    findprocs(cacheall);
    if (!np_) { out("noui"); return 1; }
    int shown = 0;
    for (int k = 0; k < np_; k++) {
        int pid = plist[k];
        heaponly = 1; int r = readmaps(pid); heaponly = 0;
        if (r < 0) continue;
        findservers(pid);
        if (cacheall && !nsrv) continue;
        if (shown++) out("; ");
        out("pid "); outn((u32)pid);
        { char path[40], nm[20]; procpath(path, pid, "comm"); int h = sc(SYS_OPEN, (long)path, 0, 0, 0, 0);
          if (h >= 0) { int r = sc(SYS_READ, h, (long)nm, sizeof nm - 1, 0, 0); sc(SYS_CLOSE, h, 0, 0, 0, 0);
                        if (r > 0) { nm[r] = 0; for (int i = 0; nm[i]; i++) if (nm[i] == '\n') nm[i] = 0; out(" ("); out(nm); out(")"); } } }
        out(": "); outn((u32)nsrv); out(" font cache(s)");
        for (int s = 0; s < nsrv; s++) {
            u32 c = 0, st = 0, m = 0;
            int broken = walkcache(pid, srv[s], mark == 1, &c, &st, &m);
            out(", at "); outx(srv[s]); out(" heap "); outn(rd32(sbuf + SRV_USED)); out("/"); outn(rd32(sbuf + SRV_HEAP));
            out(" glyphs "); outn(c); out(" stale "); outn(st);
            if (mark == 1) { out(" marked "); outn(m); }
            if (broken) out(" (busy)");
            if (mark == 2) cachelist(pid, srv[s]);
        }
        if (cacheall && !nsrv) continue;
        findlabels(pid);
        if (!tcm) {          /* what the search saw: pages, entry-like spots, then the most named addresses */
            out(", no label cache (heap "); outn((u32)heapk); out(" ranges "); outn((u32)nrng); out(" pages "); outn(tcpages);
            out(" spots "); outn(tcmatch); out(":");
            for (int r = 0; r < 6; r++) {
                int b = -1;
                for (int c = 0; c < tcnc; c++) if ((cn[c] & 0xfffff) && (b < 0 || (cn[c] & 0xfffff) > (cn[b] & 0xfffff))) b = c;
                if (b < 0) break;
                out(" "); outx(cm[b]); out("x"); outn(cn[b] & 0xfffff);
                if (cn[b] >> 31) { out("/"); outn(cn[b] >> 20 & 0x7ff); }
                cn[b] = 0;
            }
            out(")"); continue;
        }
        u32 t = 0, taken = 0; nold = 0; int broken = tcwalk(pid, mark == 1 ? 1 : 2, &t, &taken);
        out(", labels at "); outx(tcm); out(" bytes "); outn(rd32(tbuf + TC_USED)); out(" texts "); outn(t);
        if (mark == 1) { out(" taken out "); outn(taken); out(" in use "); outn((u32)nold); }
        if (broken) out(" (busy)");
    }
    return 0;
}

/* --font-restore: the original bytes aren't in this program, so drop the files' pages from the file cache instead (once
 * nobody has them locked); the UI's next reads then come from flash again. Returns how many ranges are still new. */
static int picsleft;            /* --font-restore: pictures still new (the UI keeps the picture files mapped, so their pages
                                   only go at the next start) */
static int fontoff(void) {
    int left = 0;
    for (int tries = 0; tries < 10; tries++) {
        left = 0; picsleft = picsnew();
        for (int f = 0; f < NPF; f++) {
            int r = fontmap(f, 0);
            if (r < 0) { if (f == 0) return r == -2 ? -2 : -1; continue; }
            for (int i = 0; i < pfs[f].count; i++) {
                int st = frange(f, i);
                if (st < 0 && f == 0) left = -2; else if (st == 1 && left >= 0) left++;
            }
            fontunmap(f);
        }
        if (left <= 0) return left;
        int h = sc(SYS_OPEN, (long)DROPCACHES, 1, 0, 0, 0);
        if (h >= 0) { sc(SYS_WRITE, h, (long)"1\n", 2, 0, 0); sc(SYS_CLOSE, h, 0, 0, 0, 0); }
        long ts[2] = {0, 300000000};
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);   /* a just-stopped font patcher may still hold the lock for a moment */
    }
    return left;
}

/* --font-patcher: put the new outlines in place and keep them there (runs forever); icons: the icon lettering and the
 * pictures with lettering in them too (pics.c); --font-restore: put the camera's own fonts back (the pictures stay
 * until the next start: the UI keeps their files mapped) */
static int font(int on, int icons) {
    if (!on) {
        ftstatus("restoring");
        mypid = sc(SYS_GETPID, 0, 0, 0, 0, 0);
        fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
        if (fd >= 0) colours(0);            /* the text accent colour back to Sony's */
        int left = fontoff();
        if (left == 0) {                 /* and the UI's cached Comic glyphs go stale */
            fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
            if (fd >= 0) { flushcache(); labelflush(1); }   /* and the labels drawn in Comic get drawn again */
        }
        const char *e = left == 0 ? (picsleft ? "off pics" : "off") : left == -2 ? "badfile" : left < 0 ? "error open font" : "error still in RAM";
        ftstatus(e); out(e); return left == 0 ? 0 : 2;
    }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
    if (fd < 0) { ftstatus("error open /dev/mem"); out("error open /dev/mem"); return 2; }
    int r = fontmap(0, 1);
    if (r < 0) {
        const char *e = r == -1 ? "error open font" : r == -2 ? "badfile" : r == -3 ? "error map" : "error lock";
        ftstatus(e); out(e); return r == -2 ? 3 : 2;
    }
    mypid = sc(SYS_GETPID, 0, 0, 0, 0, 0);
    for (int i = 0; i < pfs[0].count; i++)
        if (frange(0, i) < 0) { ftstatus("badfile"); out("badfile"); return 3; }   /* not the file we know: touch nothing */
    /* the icons and the pictures: each file only if it is exactly the one we know too */
    int noicons = !icons, pics = 0;
    act[0] = 1; act[1] = 0;
    if (icons) {
        int ok = fontmap(1, 1) == 0;
        for (int i = 0; ok && i < pfs[1].count; i++) if (frange(1, i) < 0) ok = 0;
        if (ok) act[1] = 1; else { noicons = 1; fontunmap(1); }
        pics = picsopen();
    }
    int total = 0;
    for (int f = 0; f < NPF; f++) if (act[f]) total += pfs[f].count;
    picflush = 0;                     /* the label store gives up its copies of our pictures once all are in */
    int pics_state = 0;               /* 2: all our pictures are in */
    sc(SYS_NICE, 19, 0, 0, 0, 0);     /* the camera comes first: unpacking and packing pictures takes a while */
    char last[40]; last[0] = 0;
    int cache = -2;                  /* glyphs marked stale by the last flush; -1: no cache found; -2: none yet */
    int labels = -2;                 /* -1: no label cache found */
    u32 fwin[MAXWIN]; int fwf[MAXWIN], nfwin = -1; u32 fwt = 0;   /* read positions at that flush (-1: no recheck) */
    for (u32 t = 0; ; t++) {
        int done = 0, left = 0, wrote = 0; u32 pdone = 0;
        for (int f = 0; f < NPF; f++) if (act[f]) for (int i = 0; i < pfs[f].count; i++)
            { if (frange(f, i) == 1) done++; else left++; }
        if (left) {
            findwindows();
            for (int f = 0; f < NPF; f++) if (act[f]) for (int i = 0; i < pfs[f].count; i++) {
                if (frange(f, i) == 1) continue;
                if (crossesedge(f, i) && t < 120) continue;      /* waits up to 2 minutes for the UI to move on */
                if (fset(f, i) < 0) { ftstatus("error write"); out("error write"); return 2; }
                done++; left--; wrote = 1;
            }
        }
        int acc = pics ? colours(1) : 0;      /* the text accent colour too */
        int pleft = 0;
        if (pics && pics_state != 2) {         /* a batch at a time; once all are in, the label store's copies go */
            pdone = picsdo(PIC_BATCH, &pleft);
            if (!pleft) { pics_state = 2; picflush = 1; if (!wrote) { int l = labelflush(1); if (l >= 0) labels = l; } }
        } else if (pics) { int z; pdone = picsdo(0, &z); }
        if (wrote) {
            /* the UI's cached glyphs are now out of date. A block sitting in one of its read buffers still has
             * the old bytes, so characters drawn from it come out old once more: flush again when every such
             * buffer has moved on (the UI keeps a file open more than once, and some of those never move) */
            findwindows();
            cache = flushcache();
            labels = labelflush(1);          /* the labels drawn so far: drawn again when next wanted */
            nfwin = 0;
            if (cache >= 0)
                for (int w = 0; w < nwin; w++) if (winpatched(winf[w], win[w])) { fwin[nfwin] = win[w]; fwf[nfwin] = winf[w]; nfwin++; }
            if (nfwin) fwt = t; else nfwin = -1;
        } else if (nfwin >= 0) {
            findwindows();
            int moved = 1;
            for (int w = 0; w < nfwin; w++) for (int v = 0; v < nwin; v++) if (fwin[w] == win[v] && fwf[w] == winf[v]) moved = 0;
            if (moved) { int c = flushcache(); if (c >= 0) { cache = c; labels = labelflush(1); } nfwin = -1; }
            else if (t - fwt > 360) nfwin = -1;               /* not within an hour or so: leave it */
        }
        if (nold && labels >= 0) labelflush(0);   /* the out-of-date labels that were in use: once they aren't */
        char st[40]; int k = 0;
        if (!left) {
            k = scat(st, k, "on "); k = scatn(st, k, (u32)done);
            if (cache == -1) k = scat(st, k, " nocache");
            else if (cache >= 0) { k = scat(st, k, " c"); k = scatn(st, k, (u32)cache); }
            if (labels == -1) k = scat(st, k, " nolabels");
            else if (labels >= 0) { k = scat(st, k, " t"); k = scatn(st, k, tctaken); }
            if (pics) { k = scat(st, k, " p"); k = scatn(st, k, pdone); }
            if (pics && acc > 0) { k = scat(st, k, " a"); k = scatn(st, k, (u32)acc); }
            else if (icons) k = scat(st, k, " nopics");
            if (noicons && icons) k = scat(st, k, " noicons");
        }
        else { k = scat(st, k, "partial "); k = scatn(st, k, (u32)done); k = scat(st, k, "/"); k = scatn(st, k, (u32)total); }
        int changed = 0;
        for (int i = 0; ; i++) { if (st[i] != last[i]) { changed = 1; break; } if (!st[i]) break; }
        if (changed) { ftstatus(st); for (int i = 0; i <= k; i++) last[i] = st[i]; }
#ifdef HOST
        out(st); out(" | ");
        if (++hostticks >= HOST_TICKS) return 0;
#endif
        long ts[2] = {left || pleft ? 1 : 10, 0};
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    }
}

static int eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int run(int argc, char **argv) {
    const char *o = argc >= 2 ? argv[1] : "";
    const char *o2 = argc >= 3 ? argv[2] : "";
    if (argc == 2 && eq(o, "--exposure-patcher")) return exposure();
    if (argc == 2 && eq(o, "--label-patcher")) return label();
    if (eq(o, "--font-patcher") && (argc == 2 || (argc == 3 && eq(o2, "--text-only")))) return font(1, argc == 2);
    if (argc == 2 && eq(o, "--font-restore")) return font(0, 1);
    if (eq(o, "--memory-dump") && (argc == 3 || (argc == 4 && eq(argv[3], "--all")))) return memdump(argv[2], argc == 4);
    if (eq(o, "--glyph-cache")) {
        int mode = 0;
        for (int i = 2; i < argc; i++) {
            if (eq(argv[i], "--mark-stale")) mode = 1;
            else if (eq(argv[i], "--list")) mode = 2;
            else if (eq(argv[i], "--all")) cacheall = 1;
            else { out("error usage"); return 1; }
        }
        return cacheshow(mode);
    }
    if (eq(o, "--label-find") && (argc == 2 || (argc == 3 && eq(o2, "--all")))) return labshow(argc == 3);
    char m = eq(o, "--lens-write") ? 'L' : eq(o, "--lens-read") ? 'R' : 0;
    if (!m || argc < 3) { out("error usage"); return 1; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", m == 'L' ? 2 : 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    u8 buf[64];
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        if (pread_(sigs[i].addr, buf, sigs[i].n) < 0) { out("error read"); return 2; }
        for (int k = 0; k < sigs[i].n; k++) if (buf[k] != sigs[i].bytes[k]) { out("badsig"); return 3; }
    }
    u8 cur[BLEN];
    if (pread_(BLOCK, cur, BLEN) < 0) { out("error read"); return 2; }
    if (!sane(cur)) { out("badblk"); show(cur); return 3; }
    if (m == 'R') { out("block"); show(cur); return 0; }

    u32 st;
    if (argc < 4 || !dec(argv[3], &st) || st > 1) { out("error args"); return 1; }
    if (st == 0) {
        if (rd32(cur) == 0) { out("off"); return 0; }
        u8 z[4] = {0, 0, 0, 0};
        if (pwrite_(BLOCK, z, 4) < 0) { out("error write"); return 2; }
        out("disabled"); return 0;
    }
    if (argc < 10) { out("error args"); return 1; }
    u8 nb[BLEN];
    for (int i = 0; i < BLEN; i++) nb[i] = cur[i];
    for (int i = 0; i < 6; i++) {
        u32 v;
        if (!dec(argv[4 + i], &v)) { out("error args"); return 1; }
        wr32(nb + NUMS + 4 * i, v);
    }
    for (int k = 1; k < 6; k += 2) if (rd32(nb + NUMS + 4 * k) == 0) { out("error zero denominator"); return 1; }
    for (int i = 0; i < NAMELEN; i++) nb[NAME + i] = 0;
    if (argc > 10) {
        const char *h = argv[10]; int n = 0;
        while (h[0] && h[1] && n < NAMELEN - 1) {
            int a = hexv(h[0]), c = hexv(h[1]);
            if (a < 0 || c < 0) { out("error name"); return 1; }
            nb[NAME + n++] = (u8)(a << 4 | c); h += 2;
        }
    }
    wr32(nb, 1);
    int same = 1;
    for (int i = 0; i < BLEN; i++) if (nb[i] != cur[i]) { same = 0; break; }
    if (same) { out("same"); return 0; }
    /* contents first, then switch it on */
    if (pwrite_(BLOCK + 4, nb + 4, BLEN - 4) < 0 || pwrite_(BLOCK, nb, 4) < 0) { out("error write"); return 2; }
    u8 chk[BLEN];
    if (pread_(BLOCK, chk, BLEN) < 0) { out("error read"); return 2; }
    for (int i = 0; i < BLEN; i++) if (chk[i] != nb[i]) { out("error verify"); return 2; }
    out("written"); show(nb);
    return 0;
}

/* prints the answer and leaves "id answer" in STATUS (written to a temporary name, then renamed) */
static int finish(int argc, char **argv, int code) {
    msg[mlen++] = '\n';
    sc(SYS_WRITE, 1, (long)msg, mlen, 0, 0);
    if (argc >= 3 && (eq(argv[1], "--lens-write") || eq(argv[1], "--lens-read"))) {
        int f = sc(SYS_OPEN, (long)STATUS ".t", 01 | 0100 | 01000, 0644, 0, 0);   /* O_WRONLY|O_CREAT|O_TRUNC */
        if (f >= 0) {
            int n = 0; while (argv[2][n] && n < 12) n++;
            sc(SYS_WRITE, f, (long)argv[2], n, 0, 0);
            sc(SYS_WRITE, f, (long)" ", 1, 0, 0);
            sc(SYS_WRITE, f, (long)msg, mlen, 0, 0);
            sc(SYS_CLOSE, f, 0, 0, 0, 0);
            sc(SYS_RENAME, (long)STATUS ".t", (long)STATUS, 0, 0, 0);
        }
    }
    return code;
}

#ifndef HOST
void _start_c(long *sp) {
    int argc = (int)sp[0]; char **argv = (char **)(sp + 1);
    sc(SYS_EXIT, finish(argc, argv, run(argc, argv)), 0, 0, 0, 0);
}
__asm__(".global _start\n_start:\n mov r0, sp\n bl _start_c\n");
#else
int a7hacks_run(int argc, char **argv) {
    fd = -1; mlen = 0; nhits = 0; np_ = 0; pm_fd = -1; pm_pid = -1; pgok = 0; pgpid = -1; pgn[0] = pgn[1] = 0;
    hostticks = 0; nsrv = 0; srvpid = -1; heaponly = 0;
    for (int f = 0; f < NPF; f++) { pfs[f].map = 0; act[f] = 0; }
    picsreset();
    tcm = 0; tcpid = -1; nold = 0; tctaken = 0;
    return finish(argc, argv, run(argc, argv));
}
#endif
