/* a7hacks - the root side of Peetz0r's α7-hacks (ILCE-7M2 firmware 4.01 only). Shipped in the app as its "native
 * library" liba7hacks.so, so that Android installs it executable; the app runs it as root.
 *
 * Usage (as root): see usage[] near the end of this file, or run it with --help.
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
 * Menu labels (--label-patcher): the English menu item for the SteadyShot focal length reads "SteadyS. Focal
 * Len.", and the peaking colour menu's third choice "Yellow" (its colour is ours: see pics.c, peaking). The
 * camera UI (im.elf, which runs libObj.so) reads its strings from /usr/share/app/string_english.uxc into its own
 * memory once per boot (with O_DIRECT, so there's no page-cache copy). The label patcher finds that copy and
 * writes "Manual Focal Length" and "Custom" over the texts: the same lengths, so nothing else moves. A string
 * record is a u32 id, the text and a NUL; we only touch a complete record (id 0x1e04f85a or 0x1e04e566 + the
 * exact old text). The UI's memory is reached via /proc/PID/pagemap (virtual -> physical) and /dev/mem; only
 * that process's pages are read, and only addresses in RAM. The UI only loads the menu strings when it needs
 * them (the first time the menu opens, say), so until both labels turn up the patcher looks every 10 s at just
 * the memory the UI has newly taken into use (pages that weren't there, or sat in another frame, at the previous
 * look), and through all of it after 10, 20, 40 ... s, then every 5 minutes. Once found, it checks the copies it
 * knows about every 2 s (and renames a fresh one again), and looks for other copies every 5 minutes. Status (on N | waiting MB | noui | error ...) goes to
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
 * color_cmn.uxc, style_cmn.uxc). --text-only leaves the icons, pictures and colours alone, and so does the patcher for a file
 * that isn't exactly the one this was made for (status gets "noicons" / "nopics"). Status (on N [cM | nocache]
 * [tK | nolabels] [pP[/T] | nopics] [aA] [noicons] | partial N/M [pP/T] | off [pics] | badfile | error ...; M = cached
 * characters marked; P/T: pictures done of how many, while they are still being done; hH: the time in 24 hours
 * (pics.c, clock24) in H places; iI: live view shows the focal length I instead of F--; kYYCBCR: focus peaking's
 * "Custom" colour (Sony's Yellow) is ours, in Y, Cb, Cr (from /android/dev/a7hacks.pk); z+M / z-M: files on an
 * exFAT card get M minutes east of UTC as their offset (pics.c, TZC; M from udate: time zone plus summer time,
 * so it follows a summer time switch within ~10 s); fF, sS: seconds from the
 * patcher's start until the font, and the pictures, were all in) goes to
 * /android/dev/a7hacks.ft. A stop (SIGTERM, the app's Restart) is held off while
 * the patcher writes, so nothing is left half written.
 *
 * Static, no libc; the app's Gradle build makes it (task buildHelper, into app/build/helper/armeabi) with
 *   clang --target=arm-linux-gnueabi -march=armv7-a -mfpu=none -mfloat-abi=soft -static -nostdlib \
 *         -ffreestanding -Os -fuse-ld=lld -o liba7hacks.so a7hacks.c
 * (-mfpu=none: no VFP/NEON instructions, which newer clang versions use by default; a helper with them crashed
 * the camera)
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
#define PKFILE "/android/dev/a7hacks.pk"
#define CKSTATUS "/android/dev/a7hacks.ck"
#define UDATE "/proc/udm/udate"
#define ALARMDEV "/dev/alarm"
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
#define PKFILE "a7hacks.pk"
#define CKSTATUS "a7hacks.ck"
#define UDATE "udate"
#define ALARMDEV "alarm"
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
#define SYS_IOCTL 54
#define SYS_GETTIMEOFDAY 78
#define SYS_CLOCK_GETTIME 263
#define SYS_SETTIMEOFDAY 79

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
#define LREC 24                /* the longest string record we look for: u32 id + 19 characters + NUL */
static const u8 lab_old[LREC] = {0x5a,0xf8,0x04,0x1e,'S','t','e','a','d','y','S','.',' ','F','o','c','a','l',' ','L','e','n','.',0};
static const u8 lab_new[LREC] = {0x5a,0xf8,0x04,0x1e,'M','a','n','u','a','l',' ','F','o','c','a','l',' ','L','e','n','g','t','h',0};
/* the peaking colour menu's third choice (its own record, after "Peaking Color"): its colour is ours (pics.c) */
static const u8 pkl_old[11] = {0x66,0xe5,0x04,0x1e,'Y','e','l','l','o','w',0};
static const u8 pkl_new[11] = {0x66,0xe5,0x04,0x1e,'C','u','s','t','o','m',0};
static const struct lab { int n; const u8 *old, *nw; } labs[] = {{LREC, lab_old, lab_new}, {11, pkl_old, pkl_new}};
#define NLAB ((int)(sizeof labs / sizeof labs[0]))

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
static int hpid[MAXHITS], hlab[MAXHITS]; static u32 hva[MAXHITS]; static int nhits;
static void addhit(int pid, u32 va, int l) {
    for (int i = 0; i < nhits; i++) if (hpid[i] == pid && hva[i] == va) return;
    if (nhits < MAXHITS) { hpid[nhits] = pid; hva[nhits] = va; hlab[nhits] = l; nhits++; }
}
/* every label found (at least one copy each)? */
static int labsfound(void) {
    for (int l = 0; l < NLAB; l++) { int f = 0; for (int i = 0; i < nhits; i++) if (hlab[i] == l) f = 1; if (!f) return 0; }
    return 1;
}
static void delhit(int i) { for (; i + 1 < nhits; i++) { hpid[i] = hpid[i + 1]; hva[i] = hva[i + 1]; hlab[i] = hlab[i + 1]; } nhits--; }

/* the mappings of one process worth looking through, from /proc/PID/maps */
#define MAXRNG 4000
static u32 rs[MAXRNG], rend[MAXRNG];
static int nrng, isui;
static int heaponly, heapk;     /* heaponly: take only the process's own data (see the font cache); heapk: its [heap] */
#define MAXSTREAM 64
static u32 stpa[MAXSTREAM], stlen[MAXSTREAM];   /* wantstream: the process's /dev/stream mappings (physical, length) */
static int nst, wantstream;
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
    char r = p[0], w = p[1], x = p[2]; p += 4;
    u32 pgo = 0;
    { const char *q = p; while (q < end && *q == ' ') q++; if (!hexp(q, end, &pgo)) pgo = 0; }
    for (int f = 0; f < 3; f++) { while (p < end && *p == ' ') p++; while (p < end && *p != ' ') p++; }
    while (p < end && *p == ' ') p++;
    int n = (int)(end - p);
    if (wantstream && n == 11 && starts(p, n, "/dev/stream") && nst < MAXSTREAM) { stpa[nst] = pgo; stlen[nst] = e - s; nst++; }
    /* the UI runs libObj.so (mapped executable); our font patcher maps one page of it too (read only, for the
     * peaking colour), and must not count: its memory holds our own label texts */
    if (x == 'x' && n >= 10 && starts(end - 10, 10, "/libObj.so")) isui = 1;
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
        for (int l = 0; l < NLAB; l++)
            if (pg[j] == labs[l].old[0] && (same(pg + j, labs[l].old, labs[l].n) || same(pg + j, labs[l].nw, labs[l].n)))
                addhit(pid, va - 32 + (u32)j, l);
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

/* what a hit (of label l) holds now: 2 = the new text, 3 = the old text, 0 = something else / gone, -1/-2 errors (vrw) */
static int labstate(int pid, u32 va, int l) {
    u8 b[LREC]; const struct lab *L = &labs[l];
    int r = vrw(pid, va, b, L->n, 0);
    if (r <= 0) return r;
    return same(b, L->nw, L->n) ? 2 : same(b, L->old, L->n) ? 3 : 0;
}
/* like labstate, but writes the new text over the old: 1 = renamed just now */
static int labfix(int pid, u32 va, int l) {
    int r = labstate(pid, va, l);
    if (r != 3) return r;
    r = vrw(pid, va + 4, (u8 *)labs[l].nw + 4, labs[l].n - 4, 1);    /* the text only, the id stays */
    if (r <= 0) return r;
    return labstate(pid, va, l) == 2 ? 1 : -2;
}

static void uistatus(const char *s) { statusto(UISTATUS, UISTATUS ".t", s); }
static int scat(char *b, int k, const char *s) { while (*s && k < 78) b[k++] = *s++; b[k] = 0; return k; }   /* status lines: up to 78 characters (buffers of 80) */
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
    u32 next = 0, wait = 5, mb = 0;        /* in 2 s ticks: search again after 10, 20, 40, 80, 160, 300, 300 ... s */
    int procs = 0;
    for (u32 t = 0; ; t++) {
        if (t >= next) {
            procs = search(0);
            mb = scanned >> 8;
            if (!procs) next = t + 5;                       /* no UI yet */
            else if (labsfound()) { next = t + 150; wait = 5; }   /* found: look for other copies every 5 minutes */
            else { next = t + wait; wait = wait < 75 ? wait * 2 : 150; }
        } else if (!labsfound() && t % 5 == 0 && np_ == 1 && readmaps(plist[0]) == 0 && isui) {
            /* every 10 s in between: look only at memory the UI has just taken into use (where newly loaded strings
             * are; the whole search, some 2.5 s of the CPU, backs off to every 5 minutes: it is for strings that land
             * in pages the UI already had) */
            scanpid(plist[0], 0);
            if (labsfound()) { next = t + 150; wait = 5; }
        }
        int on = 0, err = 0;
        for (int i = 0; i < nhits; ) {
            int r = labfix(hpid[i], hva[i], hlab[i]);
            if (r == -2) { err = 1; i++; continue; }
            if (r <= 0) { delhit(i); if (!nhits) { next = t + 1; wait = 5; } continue; }
            on++; i++;
        }
        char st[80]; int k = 0; st[0] = 0;
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
        int s = labstate(hpid[i], hva[i], hlab[i]);
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

#define PIC_BATCH 100                   /* pictures per pass of the font patcher's loop (passes 10 ms apart while there are more) */
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
                        /* in use (a view that stays keeps it): our pixels where it is, if it is Sony's picture;
                         * and it still goes, as below, once nothing uses it */
                        if (te[TE_USERS] | te[TE_USERS + 1]) picpaint(pid, e, id);
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

/* --screenshot [osd.png lv.png] both.png: Sony's own screen as PNGs. both.png always; osd.png and
 * lv.png when three names are given.
 * osd.png (RGBA, 640x480) is the OSD the UI (im.elf) draws over the live view, with its transparency (Android
 * too, when an app is in front: it reaches the panel through the same buffers). The UI draws it into two buffers
 * in /dev/stream, double buffered: its two mappings of exactly 640x480 x 16 bits, RGBA4444, rows top down. The one
 * on screen is in the display register DREG_OSD (found and checked 2026-11-27: a mark in the other one doesn't
 * show); the UI flips only when it redraws, so that one is copied in one go (~35 ms) and the register read again:
 * if it flipped meanwhile, the new one is copied (at most 10 tries). If
 * it names neither buffer, the old way: both are compared (for up to a second, 50 ms apart) until they agree; if they
 * never do (something blinking), the first one is written anyway and the answer says so.
 * lv.png (RGB, 640x426) is the live view: no Linux process maps it. The image processor writes it, double
 * buffered, as UYVY (4:2:2, full range) at fixed physical addresses in the /dev/stream pool (LVA, LVB; found
 * 2026-11-26 by watching which memory changes, see --memory-changes). The panel itself shows another, compressed
 * copy (2 buffers on the LCD at 30 frames a second, 3 in the EVF at 60), named by DREG_LV; each UYVY frame is
 * written ~10 ms before the panel flips to its compressed twin (--lv-timing), so at a flip the UYVY buffer written
 * last is the frame on screen, and that one is copied (lvshown). Without flips (playback, menus): lvgrab.
 * both.png (RGB, 640x480) is the OSD over the live view, the way the panel shows it: the live view at the top on
 * the LCD, 27 rows down in the EVF (told by the flips: 60 a second); black behind the OSD when the
 * live view isn't running (playback, Android). Peaking and zebra are in neither buffer (the display hardware adds them), so no PNG has them.
 * Read only: /dev/mem and /dev/stream are opened read only, and only those buffers, in RAM, are read. The PNGs
 * are written row by row (deflate's "stored" blocks, uncompressed), so only the live view and the OSD copy need a buffer. */
#define OSDW 640
#define OSDH 480
#define OSDLEN (OSDW * OSDH * 2)
#define LVW 640
#define LVH 426
#define LVLEN (LVW * LVH * 2)
#define LVA 0x93022000u
#define LVB 0x930ed800u
#define LVEVF 27                 /* the live view's first row on the EVF's screen */
#define DREG_PAGE 0xf3441000u    /* display registers (read uncached, never written here) */
#define DREG_LV 0x220            /* the compressed live view buffer the panel shows */
#define DREG_OSD 0x320           /* the OSD buffer the panel shows */
static u32 crctab[256];
static u32 crc(u32 c, const u8 *p, int n) {
    if (!crctab[1]) for (u32 i = 0; i < 256; i++) { u32 v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xedb88320u ^ v >> 1 : v >> 1; crctab[i] = v; }
    c = ~c; for (int i = 0; i < n; i++) c = crctab[(c ^ p[i]) & 255] ^ c >> 8; return ~c;
}
static void be32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
/* one PNG chunk: length, type, data, CRC of type and data; 0 if written */
static int chunk(int o, const char *type, const u8 *d, int n) {
    u8 h[8]; be32(h, (u32)n); for (int i = 0; i < 4; i++) h[4 + i] = (u8)type[i];
    u8 t[4]; be32(t, crc(crc(0, h + 4, 4), d, n));
    return sc(SYS_WRITE, o, (long)h, 8, 0, 0) == 8 && (!n || sc(SYS_WRITE, o, (long)d, n, 0, 0) == n) &&
           sc(SYS_WRITE, o, (long)t, 4, 0, 0) == 4 ? 0 : -1;
}
/* a PNG written row by row: pngopen, then for each row its pixels at prow + PROWPIX and pngrow, then pngclose.
 * One IDAT per row: (the zlib header,) a stored block of the row (filter byte 0, then the pixels), (the Adler-32). */
#define PROWPIX 8
static u8 rowa[OSDW * 2], rowb[OSDW * 2], prow[PROWPIX + OSDW * 4 + 4];
static u32 pngy, pngh, ad1, ad2;
static int pngopen(const char *path, u32 w, u32 h, int rgba) {
    int o = sc(SYS_OPEN, (long)path, 01 | 0100 | 01000, 0644, 0, 0);
    if (o < 0) return -1;
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    u8 ih[13]; be32(ih, w); be32(ih + 4, h); ih[8] = 8; ih[9] = rgba ? 6 : 2; ih[10] = ih[11] = ih[12] = 0;
    if (sc(SYS_WRITE, o, (long)sig, 8, 0, 0) != 8 || chunk(o, "IHDR", ih, 13) < 0) { sc(SYS_CLOSE, o, 0, 0, 0, 0); return -1; }
    pngy = 0; pngh = h; ad1 = 1; ad2 = 0;
    return o;
}
static int pngrow(int o, int n) {                /* n: the row's pixel bytes, at prow + PROWPIX */
    u8 *p = prow + PROWPIX - 6;
    int len = 1 + n, last = pngy == pngh - 1;
    p[0] = (u8)last; p[1] = (u8)len; p[2] = (u8)(len >> 8); p[3] = (u8)~len; p[4] = (u8)(~len >> 8); p[5] = 0;
    if (!pngy) { p -= 2; p[0] = 0x78; p[1] = 0x01; }
    for (u8 *c = prow + PROWPIX - 1; c < prow + PROWPIX + n; c++) { ad1 = (ad1 + *c) % 65521; ad2 = (ad2 + ad1) % 65521; }
    int m = (int)(prow + PROWPIX + n - p);
    if (last) { be32(p + m, ad2 << 16 | ad1); m += 4; }
    pngy++;
    return chunk(o, "IDAT", p, m);
}
static int pngclose(int o, int bad) {
    if (!bad && chunk(o, "IEND", 0, 0) < 0) bad = 1;
    sc(SYS_CLOSE, o, 0, 0, 0, 0);
    return bad ? -1 : 0;
}
/* the pixels in which the two buffers differ (read now); -1 if they can't be read */
static int osddiff(u32 a, u32 b) {
    int n = 0;
    for (int y = 0; y < OSDH; y++) {
        if (pread_(a + y * OSDW * 2, rowa, OSDW * 2) < 0 || pread_(b + y * OSDW * 2, rowb, OSDW * 2) < 0) return -1;
        for (int x = 0; x < OSDW * 2; x += 2) if (rowa[x] != rowb[x] || rowa[x + 1] != rowb[x + 1]) n++;
    }
    return n;
}
static u8 lvbuf[LVLEN] __attribute__((aligned(4)));
static u32 usnow(void);
static int eq(const char *a, const char *b);
/* the live view into lvbuf: 1 a whole frame, 2 a frame but the live view isn't running (no buffer changed in
 * 300 ms), 0 no whole frame, -1 can't map, -2 doesn't look written. The camera writes the two buffers in turn (30
 * frames a second, so each one is rewritten every ~66 ms and holds still for ~33 ms of that), and a read through
 * an uncached mapping takes ~70 ms: too slow. So one word in the middle of the other buffer is watched (uncached):
 * when it changes, the camera is writing that one and this one is whole; it's copied right then through a cached
 * mapping of /dev/mem (~18 ms; only read, so its cache lines stay clean and are never written back), and the copy
 * only counts when 64 words spread over the frame, read uncached, still match it (that also catches stale cache
 * lines). Two tries: A, then B. */
static int lvgrab(void) {
    u32 base = LVA & ~0xfffu, len = (LVB + LVLEN - base + 0xfff) & ~0xfffu;
    long m[2] = {-1, -1};
    for (int k = 0; k < 2; k++) {               /* 0 uncached (/dev/stream maps uncached), 1 cached (/dev/mem without O_SYNC) */
        int sfd = sc(SYS_OPEN, (long)(k ? "/dev/mem" : "/dev/stream"), k ? 0 : 0x101000 /* O_RDONLY (| O_SYNC) */, 0, 0, 0);
        if (sfd >= 0) { m[k] = sc6(SYS_MMAP2, 0, len, 1 /* PROT_READ */, 1 /* MAP_SHARED */, sfd, (long)(base >> 12)); sc(SYS_CLOSE, sfd, 0, 0, 0, 0); }
    }
    int r = -1;
    if ((unsigned long)m[0] < (unsigned long)-4095L && (unsigned long)m[1] < (unsigned long)-4095L) {
        const volatile u32 *u[2] = {(const volatile u32 *)(m[0] + (LVA - base)), (const volatile u32 *)(m[0] + (LVB - base))};
        const u32 *c[2] = {(const u32 *)(m[1] + (LVA - base)), (const u32 *)(m[1] + (LVB - base))};
        u32 aa = 0;
        for (u32 i = 0; i < 1024; i++) { u32 w = u[0][i]; for (int b = 0; b < 32; b += 8) if ((w >> b & 255) == 0xaa) aa++; }
        r = aa >= 4096 / 3 ? -2 : 0;
        for (int t = 0; t < 2 && !r; t++) {
            const volatile u32 *other = u[!t] + LVLEN / 8;
            u32 v = *other, t0 = usnow(), moved = 0;
            while (usnow() - t0 < 300000) if (*other != v) { moved = 1; break; }
            const u32 *src = c[t];
            u32 *dst = (u32 *)lvbuf;
            for (u32 i = 0; i < LVLEN / 4; i++) dst[i] = src[i];
            int ok = 1;
            for (u32 i = 0; i < 64; i++) { u32 k = i * (LVLEN / 4 / 64) + i; if (u[t][k] != dst[k]) ok = 0; }
            if (ok) r = moved ? 1 : 2;
        }
    }
    for (int k = 0; k < 2; k++) if ((unsigned long)m[k] < (unsigned long)-4095L) sc(SYS_MUNMAP, m[k], len, 0, 0, 0);
    return r;
}
/* The live view frame that's on screen into lvbuf: 1 done, 0 no whole frame in 500 ms, -1 can't map, 3 the panel
 * didn't flip in 300 ms (no live view: use lvgrab). At each flip of DREG_LV, the UYVY buffer whose middle word
 * changed last is the frame now shown: copied through a cached mapping of /dev/mem, kept when its middle word is
 * still the same afterwards and 64 words spread over it, read uncached, match the copy (the EVF rewrites LVB ~7 ms
 * after its flip: then the next flip is tried). *fast: the panel flips more often than every 25 ms (the EVF). */
static int lvshown(int *fast) {
    u32 base = LVA & ~0xfffu, len = (LVB + LVLEN - base + 0xfff) & ~0xfffu;
    long m[2] = {-1, -1}, rm = -1;
    int f = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (f >= 0) {
        m[0] = sc6(SYS_MMAP2, 0, len, 1, 1, f, (long)(base >> 12));
        rm = sc6(SYS_MMAP2, 0, 0x1000, 1, 1, f, (long)(DREG_PAGE >> 12));
        sc(SYS_CLOSE, f, 0, 0, 0, 0);
    }
    f = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);          /* cached, for the copy */
    if (f >= 0) { m[1] = sc6(SYS_MMAP2, 0, len, 1, 1, f, (long)(base >> 12)); sc(SYS_CLOSE, f, 0, 0, 0, 0); }
    int r = -1; *fast = 0;
    if ((unsigned long)m[0] < (unsigned long)-4095L && (unsigned long)m[1] < (unsigned long)-4095L && (unsigned long)rm < (unsigned long)-4095L) {
        const volatile u32 *u[2] = {(const volatile u32 *)(m[0] + (LVA - base)), (const volatile u32 *)(m[0] + (LVB - base))};
        const u32 *c[2] = {(const u32 *)(m[1] + (LVA - base)), (const u32 *)(m[1] + (LVB - base))};
        const volatile u32 *reg = (const volatile u32 *)(rm + DREG_LV);
        u32 mid[2] = {u[0][LVLEN / 8], u[1][LVLEN / 8]}, shown = *reg, t0 = usnow(), flipt = 0, flips = 0, short_ = 0;
        int lastk = -1;
        r = 0;
        for (;;) {
            u32 t = usnow() - t0;
            if (t > 500000 || (!flips && t > 300000)) { if (!flips) r = 3; break; }
            for (int k = 0; k < 2; k++) { u32 v = u[k][LVLEN / 8]; if (v != mid[k]) { mid[k] = v; lastk = k; } }
            u32 v = *reg;
            if (v == shown) continue;
            shown = v; flips++;
            if (flipt && t - flipt < 25000) short_++;
            flipt = t;
            if (lastk < 0) continue;                         /* no UYVY write seen yet: this flip's frame is unknown */
            int k = lastk; u32 was = mid[k];
            const u32 *src = c[k]; u32 *dst = (u32 *)lvbuf;
            for (u32 i = 0; i < LVLEN / 4; i++) dst[i] = src[i];
            int ok = u[k][LVLEN / 8] == was;
            for (u32 i = 0; i < 64 && ok; i++) { u32 j = i * (LVLEN / 4 / 64) + i; if (u[k][j] != dst[j]) ok = 0; }
            if (ok) { r = 1; break; }
            mid[k] = u[k][LVLEN / 8];
        }
        *fast = short_ > 0;
    }
    if ((unsigned long)m[0] < (unsigned long)-4095L) sc(SYS_MUNMAP, m[0], len, 0, 0, 0);
    if ((unsigned long)m[1] < (unsigned long)-4095L) sc(SYS_MUNMAP, m[1], len, 0, 0, 0);
    if ((unsigned long)rm < (unsigned long)-4095L) sc(SYS_MUNMAP, rm, 0x1000, 0, 0, 0);
    return r;
}
/* the shown OSD buffer, copied in one go (static, like lvbuf: the patchers running from this binary never touch it,
 * so it only takes RAM during a --screenshot) */
static u8 osdcopy[OSDLEN];
/* the OSD buffer the panel shows (DREG_OSD), read uncached; 0 if it can't be read */
static u32 osdshown(void) {
    int f = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (f < 0) return 0;
    long rm = sc6(SYS_MMAP2, 0, 0x1000, 1, 1, f, (long)(DREG_PAGE >> 12));
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    if ((unsigned long)rm >= (unsigned long)-4095L) return 0;
    u32 v = *(const volatile u32 *)(rm + DREG_OSD);
    sc(SYS_MUNMAP, rm, 0x1000, 0, 0, 0);
    return v;
}
static u8 clamp8(int v) { return (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }
/* live view pixel x of row y (in lvbuf) as RGB */
static void lvrgb(int x, int y, u8 *q) {
    const u8 *p = lvbuf + y * LVW * 2 + (x & ~1) * 2;
    int u = p[0] - 128, v = p[2] - 128, Y = p[1 + (x & 1) * 2];
    q[0] = clamp8(Y + (359 * v >> 8)); q[1] = clamp8(Y - ((88 * u + 183 * v) >> 8)); q[2] = clamp8(Y + (454 * u >> 8));
}
static int screenshot(int argc, char **argv) {
    int evf = 0, names = argc - 2;
    const char *path = names == 3 ? argv[2] : 0, *lvpath = names == 3 ? argv[3] : 0, *bothpath = argv[1 + names];
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    findprocs(0);
    if (!np_) { out("noui"); return 1; }
    nst = 0; wantstream = 1; int r = readmaps(plist[0]); wantstream = 0;
    if (r < 0) { out("error maps"); return 2; }
    u32 buf[2]; int nb = 0;
    for (int k = 0; k < nst; k++) if (stlen[k] == OSDLEN && nb < 3) { if (nb < 2) buf[nb] = stpa[k]; nb++; }
    if (nb != 2) { out("error "); outn((u32)nb); out(" OSD buffers"); return 2; }
    for (int k = 0; k < 2; k++)
        if ((buf[k] & 4095) || (u32)(buf[k] - RAM_LO) >= RAM_SIZE || RAM_SIZE - (buf[k] - RAM_LO) < OSDLEN) { out("error OSD buffer not in RAM"); return 2; }
    int d = 0, tries = 0, byreg = 0;
    u32 on = osdshown();
    if (on == buf[0] || on == buf[1]) {         /* the one on screen, copied (~35 ms) while the UI doesn't flip */
        u32 other = on == buf[0] ? buf[1] : buf[0];
        for (tries = 1; tries <= 10; tries++) {
            if (pread_(on, osdcopy, OSDLEN) < 0) { out("error read"); return 2; }
            u32 now = osdshown();
            if (now == on) { byreg = 1; break; }
            if (now != buf[0] && now != buf[1]) break;
            other = on; on = now;               /* it flipped: the new one */
        }
        if (byreg) { buf[0] = on; buf[1] = other; }
    }
    if (!byreg) {
        tries = 0;
        u32 t0 = usnow();
        for (;;) {                              /* a compare reads both buffers: ~150 ms */
            tries++;
            if ((d = osddiff(buf[0], buf[1])) < 0) { out("error read"); return 2; }
            if (!d || usnow() - t0 > 1000000) break;
            long ts[2] = {0, 50000000};
            sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
        }
    }
    /* the live view: with three names it must be there; with one, an old or missing frame means black behind
     * the OSD (playback, menus over a stopped live view, Android) */
    if ((u32)(LVA - RAM_LO) >= RAM_SIZE || RAM_SIZE - (LVB - RAM_LO) < LVLEN) { out("error live view not in RAM"); return 2; }
    int fast = 0, lv = lvshown(&fast), lvexact = lv == 1;
    if (lv == 3 || lv == 0) lv = lvgrab();                  /* no flips (no live view), or never a whole frame: the old way */
    if (lv == -1) { out("error map live view"); return 2; }
    if (!lv) { out("error live view: no whole frame (it changed while copied)"); return 2; }
    if (lvpath && lv == -2) { out("error no live view (the buffer was never written)"); return 2; }
    int bad = 0, changed = 0, o;
    if (lvexact && fast) evf = 1;                       /* 60 flips a second: the EVF */
    out("written");
    /* the OSD */
    if (path) {
        if ((o = pngopen(path, OSDW, OSDH, 1)) < 0) { out(" error open output"); return 2; }
        for (int y = 0; y < OSDH && !bad; y++) {
            if (byreg) { for (int i = 0; i < OSDW * 2; i++) rowa[i] = osdcopy[y * OSDW * 2 + i]; }
            else if (pread_(buf[0] + y * OSDW * 2, rowa, OSDW * 2) < 0 || pread_(buf[1] + y * OSDW * 2, rowb, OSDW * 2) < 0) { bad = 1; break; }
            u8 *q = prow + PROWPIX;
            for (int x = 0; x < OSDW * 2; x += 2) {
                u32 v = rowa[x] | rowa[x + 1] << 8;
                if (!d && !byreg && (rowa[x] != rowb[x] || rowa[x + 1] != rowb[x + 1])) changed++;
                *q++ = (u8)((v >> 12 & 15) * 17); *q++ = (u8)((v >> 8 & 15) * 17); *q++ = (u8)((v >> 4 & 15) * 17); *q++ = (u8)((v & 15) * 17);
            }
            if (pngrow(o, OSDW * 4) < 0) bad = 1;
        }
        if (pngclose(o, bad) < 0) { out(" error write"); return 2; }
        out(" "); out(path); out(",");
    }
    /* the live view */
    if (lvpath) {
        if ((o = pngopen(lvpath, LVW, LVH, 0)) < 0) { out(" error open output"); return 2; }
        for (int y = 0; y < LVH && !bad; y++) {
            for (int x = 0; x < LVW; x++) lvrgb(x, y, prow + PROWPIX + x * 3);
            if (pngrow(o, LVW * 3) < 0) bad = 1;
        }
        if (pngclose(o, bad) < 0) { out(" error write"); return 2; }
        out(" "); out(lvpath); out(",");
    }
    /* both: the OSD over the live view (black where there's none) */
    if ((o = pngopen(bothpath, OSDW, OSDH, 0)) < 0) { out(" error open output"); return 2; }
    int top = evf ? LVEVF : 0, live = lv == 1 || (lv == 2 && lvpath);
    for (int y = 0; y < OSDH && !bad; y++) {
        if (byreg) { for (int i = 0; i < OSDW * 2; i++) rowa[i] = osdcopy[y * OSDW * 2 + i]; }
        else if (pread_(buf[0] + y * OSDW * 2, rowa, OSDW * 2) < 0 || (!path && pread_(buf[1] + y * OSDW * 2, rowb, OSDW * 2) < 0)) { bad = 1; break; }
        u8 *q = prow + PROWPIX;
        for (int x = 0; x < OSDW; x++, q += 3) {
            u32 v = rowa[2 * x] | rowa[2 * x + 1] << 8, a = v & 15;
            if (!path && !d && !byreg && (rowa[2 * x] != rowb[2 * x] || rowa[2 * x + 1] != rowb[2 * x + 1])) changed++;
            u8 l[3] = {0, 0, 0};
            if (live && y >= top && y < top + LVH) lvrgb(x, y - top, l);
            for (int c = 0; c < 3; c++) q[c] = (u8)(((v >> (12 - 4 * c) & 15) * 17 * a + l[c] * (15 - a)) / 15);
        }
        if (pngrow(o, OSDW * 3) < 0) bad = 1;
    }
    if (pngclose(o, bad) < 0) { out(" error write"); return 2; }
    out(" "); out(bothpath);
    if (byreg) { out(" (OSD: the one shown"); if (tries > 1) { out(", "); outn((u32)tries); out(" tries"); } out(")"); }
    else if (d) { out(" (the OSD buffers never agreed: "); outn((u32)d); out(" pixels differ; this is the first one, "); outx(buf[0]); out(")"); }
    else if (changed) { out(" (the OSD changed while writing: "); outn((u32)changed); out(" pixels)"); }
    else { out(" ("); outn((u32)tries); out(tries == 1 ? " look)" : " looks)"); }
    if (lvexact) out(fast ? " (live view: the frame shown, EVF)" : " (live view: the frame shown, LCD)");
    if (lv == 2) out(lvpath ? " (live view not running: an old frame)" : " (no live view running)");
    if (lv == -2) out(" (no live view)");
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
/* the font patcher's status line (see the top of this file), written when it changed; st: the line */
/* The views' own textures (see pics.c, texwrite): each has a header in the UI's own data: ...,
 * width | height << 16 (whole tiles), 1, 0, its physical address, its resource id 0x02xxxxxx. Walks the UI's
 * [heap] and anonymous memory for such headers of our pictures, whose texture lies wholly inside one of the
 * UI's /dev/stream mappings. Returns how many textures changed, or <0 (can't). */
static int texd, texe;           /* textures brought up to date: at the end, and once the first pictures were in */
#define MAXTEX 1024
static u32 txpa[MAXTEX]; static int ntx;
static u8 tpg[4096];
static int texpaint(u32 res, u32 tpa, u32 dim) {
    int ki = picfind(0x1a000000u | (res & 0xffffff));
    if (ki < 0) return 0;
    u32 raw = pps[ki >> 16].raw[ki & 0xffff] & 0x1fffffffu;
    int in = 0;
    for (int s_ = 0; s_ < nst; s_++) if (tpa >= stpa[s_] && stlen[s_] >= raw && tpa - stpa[s_] <= stlen[s_] - raw) in = 1;
    if (!in) return 0;
    for (int j = 0; j < ntx; j++) if (txpa[j] == tpa) return 0;       /* seen already (a header seen twice) */
    if (ntx < MAXTEX) txpa[ntx++] = tpa;
    return texwrite(ki, tpa, dim & 0xffff, dim >> 16);
}
static int texpass(void) {
#ifdef HOST
    return 0;
#else
    findprocs(0);
    if (!np_) return -1;
    int pid = plist[0];
    nst = 0; wantstream = 1; heaponly = 1; int r = readmaps(pid); heaponly = 0; wantstream = 0;
    if (r < 0 || !nst) return -1;
    int f = pm_open(pid); if (f < 0) return -1;
    int changed = 0; ntx = 0;
    for (int k = 0; k < nrng; k++)
        for (u32 va = rs[k]; va < rend[k]; va += 4096) {
            u8 e[8]; unsigned long long res; u32 pa;
            if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) < 0 || sc(SYS_READ, f, (long)e, 8, 0, 0) != 8) continue;
            if (!(pa = pm_phys(e, va)) || pread_(pa, tpg, 4096) < 0) continue;
            for (int j = 16; j + 4 <= 4096; j += 4) {
                u32 id = rd32(tpg + j);
                if (id >> 24 != 2 || rd32(tpg + j - 12) != 1 || rd32(tpg + j - 8) != 0) continue;
                if (texpaint(id, rd32(tpg + j - 4), rd32(tpg + j - 16)) > 0) changed++;
            }
        }
    return changed;
#endif
}

/* --pic-census: which of our pictures the UI has unpacked right now (read only). Both kinds of texture header in
 * the UI's [heap] and anonymous memory: the views' own (as texpass) and the label store's (0xabcdef, as
 * picpaint). One line each: S (still Sony's picture) or N (not: ours, or changed since), the file (0 cmn, 1 EVF,
 * 2 DSLR, 3 DSC), the resource id, the size, the physical address; then "census N textures". For finding the
 * pictures that are on screen most (the patcher's order). */
static void say(const char *s);
static void sayn(u32 v);
static void sayx(u32 v);
static int census1(u32 res, u32 tpa, u32 dim) {
    int ki = picfind(0x1a000000u | (res & 0xffffff));
    if (ki < 0) return 0;
    int k = ki >> 16, i = ki & 0xffff;
    u32 raw = pps[k].raw[i] & 0x1fffffffu, tw = dim & 0xffff, th = dim >> 16;
    if (!tw || !th || tw * th * 2 != raw || raw > PIC_MAXRAW || (tpa & 3)) return 0;
    if (tpa < RAM_LO || tpa - RAM_LO > RAM_SIZE - raw) return 0;
    for (int j = 0; j < ntx; j++) if (txpa[j] == tpa) return 0;
    if (ntx < MAXTEX) txpa[ntx++] = tpa;
    if (pread_(tpa, pbuf, (int)raw) < 0) return 0;
    say(pfnv(pbuf, raw) == pps[k].ohash[i] ? "S " : "N "); sayn((u32)k); say(" ");
    sayx(0x02000000u | (res & 0xffffff)); say(" "); sayn(tw); say("x"); sayn(th); say(" "); sayx(tpa); say("\n");
    return 1;
}
static int census(void) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    for (int k = 0; k < NPP; k++) { pps[k].on = 0; if (picmap(k, 0) == 0) pps[k].on = 1; }
    findprocs(0);
    if (!np_) { out("noui"); return 1; }
    int pid = plist[0];
    nst = 0; heaponly = 1; int r = readmaps(pid); heaponly = 0;
    if (r < 0) { out("error maps"); return 2; }
    int f = pm_open(pid); if (f < 0) { out("error pagemap"); return 2; }
    int n = 0; ntx = 0;
    for (int k = 0; k < nrng; k++)
        for (u32 va = rs[k]; va < rend[k]; va += 4096) {
            u8 e[8]; unsigned long long res; u32 pa;
            if (sc(SYS_LLSEEK, f, 0, (long)((va >> 12) * 8), (long)&res, 0) < 0 || sc(SYS_READ, f, (long)e, 8, 0, 0) != 8) continue;
            if (!(pa = pm_phys(e, va)) || pread_(pa, tpg, 4096) < 0) continue;
            for (int j = 0; j + 4 <= 4096; j += 4) {
                u32 w = rd32(tpg + j);
                if (j >= 16 && w >> 24 == 2 && rd32(tpg + j - 12) == 1 && rd32(tpg + j - 8) == 0)
                    n += census1(w, rd32(tpg + j - 4), rd32(tpg + j - 16));
                else if (w == 0xabcdefu && j + 0x2c <= 4096)
                    n += census1(rd32(tpg + j + 0x28), rd32(tpg + j + 0x24), rd32(tpg + j + 0x18));
            }
        }
    out("census "); outn((u32)n); out(" textures");
    return 0;
}

/* --pic-bench: where the font patcher's time goes with the pictures, measured on the camera: each picture as it
 * is in RAM now (after a patch: ours) unpacked, cleared and coloured, our pixels put in, packed, hashed, and
 * "written" the patcher's way (physical address, /dev/mem) into a buffer of our own; at nice 19, like the
 * patcher. Writes nothing of the camera's. First: how many of the files' pages are in RAM (mincore). */
#define SYS_MINCORE 219
static u8 benchw[PIC_MAXRAW + PIC_MAXRAW / 8 + 4096];
static u32 usnow(void) {          /* microseconds, wrapping: only differences count */
    long ts[2]; ts[0] = ts[1] = 0;
    sc(SYS_CLOCK_GETTIME, 1, (long)ts, 0, 0, 0);
    return (u32)ts[0] * 1000000u + (u32)ts[1] / 1000u;
}
static int bench(void) {
    fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    mypid = sc(SYS_GETPID, 0, 0, 0, 0, 0);
    sc(SYS_NICE, 19, 0, 0, 0, 0);
    for (u32 j = 0; j < sizeof benchw; j += 4096) benchw[j] = 1;      /* its pages there */
    static u32 tt[6]; u32 t0 = usnow(), n = 0, bytes = 0, res = 0, pages = 0, slow = 0, nslow = 0;
    static u8 vec[8192];
    for (int k = 0; k < NPP; k++) {
        if (picmap(k, 0) < 0) continue;
        struct pp *P = &pps[k];
        u32 np = (P->size + 4095) / 4096;
        if (np <= sizeof vec && sc(SYS_MINCORE, (long)P->map, P->size, (long)vec, 0, 0) == 0)
            for (u32 j = 0; j < np; j++) { pages++; res += vec[j] & 1; }
        for (int i = 0; i < P->count; i++) {
            u32 R = P->raw[i], raw = R & 0x1fffffffu;
            u32 a = usnow();
            if (lz_decode(P->map + P->off[i], (int)P->len[i], pbuf, (int)raw) != (int)raw) continue;
            u32 b = usnow();
            picprep(pbuf, raw, R);
            u32 c = usnow();
            u32 rl = P->rlen[i];
            if (rl <= PIC_MAXRUNS && lz_decode(P->runs + P->run[i], 0x7fffffff, prun, (int)rl) == (int)rl) {
                const u8 *q = prun, *qe = prun + rl; u32 pos = 0;
                while (q + 4 <= qe) {
                    u32 sk = q[0] | q[1] << 8, cn = q[2] | q[3] << 8; q += 4;
                    if (!sk && !cn) break;
                    pos += sk;
                    if ((pos + cn) * 2 > raw || q + 2 * cn > qe) break;
                    for (u32 j = 0; j < cn; j++, pos++, q += 2) { pbuf[2 * pos] = q[0]; pbuf[2 * pos + 1] = q[1]; }
                }
            }
            u32 d = usnow();
            int m = lz_encode(pbuf, (int)raw, pout, PIC_HOW(P->nlen[i]));
            u32 e = usnow();
            volatile u32 h = pfnv(pout, (u32)m) ^ pfnv(P->map + P->off[i], P->len[i]); (void)h;
            u32 f = usnow();
            vrw(mypid, (u32)(unsigned long)benchw, pout, m, 1);
            u32 g = usnow();
            tt[0] += b - a; tt[1] += c - b; tt[2] += d - c; tt[3] += e - d; tt[4] += f - e; tt[5] += g - f;
            if (P->nlen[i] >> 31) { slow += e - d; nslow++; }
            n++; bytes += raw;
        }
        picunmap(k);
    }
    u32 all = usnow() - t0;
    static const char *nm[6] = {" unpack ", ", colour ", ", ours ", ", pack ", ", hash ", ", write "};
    out("bench "); outn(n); out(" pictures, "); outn(bytes >> 10); out(" KB, "); outn(all / 1000u); out(" ms:");
    for (int x = 0; x < 6; x++) { out(nm[x]); outn(tt[x] / 1000u); out(" ms"); }
    out(" (of which the "); outn(nslow); out(" packed the cheapest way: "); outn(slow / 1000u); out(" ms)");
    out("; pages in RAM before: "); outn(res); out("/"); outn(pages);
    return 0;
}

/* seconds since some fixed moment (CLOCK_MONOTONIC: not moved by --set-clock) */
static u32 secs(void) {
#ifdef HOST
    return 0;
#else
    long ts[2] = {0, 0};
    sc(SYS_CLOCK_GETTIME, 1, (long)ts, 0, 0, 0);
    return (u32)ts[0];
#endif
}
static u32 tookf, tookp;              /* seconds the patcher took to get the font, the pictures in (0: not yet) */

/* the focal length set for SteadyShot while the lens mount says no electronic lens (MOUNT: only once checksigs
 * found the camera's code the one we know), else 0 */
static u32 manualfl(void) {
    u8 mnt[4];
    u32 mm = steadyshot();
    return (pread_(MOUNT, mnt, 4) == 0 && rd32(mnt) == 0 && mm > 0 && mm < 10000) ? mm : 0;
}
/* The peaking colour for the menu's "Custom" (Sony's Yellow; pics.c): /android/dev/a7hacks.pk, written by the
 * app, "YYCBCR" (Y, Cb, Cr in hex, BT.601 as the camera uses them). None (or nonsense): Sony's Yellow. Sets pkon,
 * pkw; 1 = changed. */
static int pkread(void) {
    char t[16]; int on = 0; u8 w[4] = {0, 0, 0, 0};
    int f = sc(SYS_OPEN, (long)PKFILE, 0, 0, 0, 0);
    if (f >= 0) {
        int n = sc(SYS_READ, f, (long)t, sizeof t, 0, 0);
        sc(SYS_CLOSE, f, 0, 0, 0, 0);
        if (n >= 6) {
            on = 1;
            for (int i = 0; i < 6; i++) if (hexv(t[i]) < 0) on = 0;
            for (int i = 6; i < n; i++) if (t[i] != '\n' && t[i] != '\r' && t[i] != ' ') on = 0;
            if (on) for (int c = 0; c < 3; c++) w[2 - c] = (u8)(hexv(t[2 * c]) << 4 | hexv(t[2 * c + 1]));
        }
    }
    int ch = on != pkon || (on && (w[0] != pkw[0] || w[1] != pkw[1] || w[2] != pkw[2]));
    pkon = on; for (int i = 0; i < 4; i++) pkw[i] = on ? w[i] : 0;
    return ch;
}
/* While the camera shows peaking in the colour that was ours, the new one goes there at once too, so the app's
 * colour picker shows on the camera as it's moved: into av-cam's copies of the peaking settings it last got
 * (VDF's own per output, the display driver's latest-command holder and two more copies of it, the copy its
 * display task received, and that task's two states with two outputs each; each a word Y, Cb, Cr, colorimetry), which it applies again on its own (a switch
 * between LCD and EVF), and into the display hardware's colour register (channel A at 0xf3445148: Y, Cb, Cr in
 * bits 31..8). Each only if it holds exactly the colour that was ours (any other colour there, Sony's, or
 * Custom not chosen, stays: the camera takes the new one the next time it sets peaking), av-cam's only once
 * checksigs found its code the one we know (pkav). was, now: 0xYYCBCR. */
#define PKREG 0xf3445148u
/* av-cam's copies are found by their shape: in a peaking block a colour word (Y, Cb, Cr, colorimetry 0x40 or
 * 0x41) follows the words on/off (0, 1), level (1..11), 0, 0 and is followed by 0; taken only when it holds
 * exactly the colour that was ours. Where they were (two areas of av-cam's data) is looked through at every
 * change, all of av-cam's data once the colour has stayed put a moment (pkall). */
static u8 pkpg[4096];
static void pkavscan(u32 lo, u32 hi, u32 was, u32 now) {
    for (u32 a = lo; a < hi; a += 4096) {
        if (pread_(a, pkpg, 4096) < 0) return;
        for (u32 o = 16; o + 8 <= 4096; o += 4) {
            const u8 *c = pkpg + o;
            if (c[0] != (u8)(was >> 16) || c[1] != (u8)(was >> 8) || c[2] != (u8)was || (c[3] & 0xfe) != 0x40) continue;
            u32 on = rd32(c - 16), lv = rd32(c - 12);
            if (on > 1 || lv < 1 || lv > 11 || rd32(c - 8) || rd32(c - 4) || rd32(c + 4)) continue;
            u8 w[3] = {(u8)(now >> 16), (u8)(now >> 8), (u8)now};
            hold(1); pwrite_(a + o, w, 3); hold(0);
        }
    }
}
static int pkav;
/* and libObj's own copy (it doesn't look the colour up again until the menu changes): a table in the UI's heap of 8 records of 7 bytes (02 00 Y Cb Cr output on/off), one per output and state.
 * Found by looking for 8 records holding the colour that was ours, once the colour has stayed put a moment (then
 * kept, and checked again before each use). */
#define PKREC 7
#define PKNREC 8
static int pkuipid; static u32 pkuiva;
static int pktable(int pid, u32 va, u32 was) {       /* do the 8 records at va hold was? */
    u8 b[PKREC * PKNREC];
    if (vrw(pid, va, b, sizeof b, 0) <= 0) return 0;
    for (int i = 0; i < PKNREC; i++) {
        const u8 *r = b + i * PKREC;
        if (r[2] != (u8)(was >> 16) || r[3] != (u8)(was >> 8) || r[4] != (u8)was) return 0;
    }
    return 1;
}
static void pkfindui(u32 was, u32 now) {          /* the table holding was or (already) now */
    pkuipid = 0;
    findprocs(0);
    if (!np_) return;
    int pid = plist[0];
    heaponly = 1; int r = readmaps(pid); heaponly = 0;
    if (r < 0) return;
    for (int k = 0; k < nrng; k++)
        for (u32 va = rs[k]; va < rend[k]; va += 4096) {
            u32 pa; if (v2p(pid, va, &pa) <= 0) continue;
            if (pread_(pa, pg + 32, 4096) < 0) continue;
            u8 y0 = (u8)(was >> 16), y1 = (u8)(now >> 16);
            for (int j = 32; j + 2 + 3 <= 32 + 4096; j++) {
                u8 y = pg[j + 2];
                if (y != y0 && y != y1) continue;         /* the quick test first: most bytes go no further */
                for (int c = 0; c < 2; c++) {
                    u32 v = c ? now : was;
                    if (y == (u8)(v >> 16) && pg[j + 3] == (u8)(v >> 8) && pg[j + 4] == (u8)v
                        && pktable(pid, va + (u32)(j - 32), v)) { pkuipid = pid; pkuiva = va + (u32)(j - 32); return; }
                }
            }
        }
}
static void pkshow(u32 was, u32 now, int full) {
    /* the table we know holds the new colour already: nothing to look for (a sweep after the colour came in) */
    int have = pkuipid && pktable(pkuipid, pkuiva, now);
    if (!have && (!pkuipid || !pktable(pkuipid, pkuiva, was))) {
        pkuipid = 0;
        if (full) { pkfindui(was, now); have = pkuipid && pktable(pkuipid, pkuiva, now); }
    }
    if (!have && pkuipid) for (int i = 0; i < PKNREC; i++) {
        u8 w[3] = {(u8)(now >> 16), (u8)(now >> 8), (u8)now};
        hold(1); vrw(pkuipid, pkuiva + (u32)(i * PKREC) + 2, w, 3, 1); hold(0);
    }
    if (pkav && full) pkavscan(0x83dc0000u, 0x841c1000u, was, now);
    else if (pkav) { pkavscan(0x83ee0000u, 0x83f00000u, was, now); pkavscan(0x83fb0000u, 0x83fc0000u, was, now); }
    int f = sc(SYS_OPEN, (long)"/dev/mem", 0x101002 /* O_RDWR | O_SYNC */, 0, 0, 0);
    if (f < 0) return;
    long m = sc6(SYS_MMAP2, 0, 0x1000, 3 /* PROT_READ | PROT_WRITE */, 1 /* MAP_SHARED */, f, (long)(PKREG >> 12));
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    if ((unsigned long)m >= 0xfffff000ul) return;
    volatile u32 *r = (volatile u32 *)(m + (PKREG & 0xfffu));
    u32 v = *r;
    if (v >> 8 == (was & 0xffffff)) *r = (now & 0xffffff) << 8 | (v & 0xff);
    sc(SYS_MUNMAP, m, 0x1000, 0, 0, 0);
}
/* a new colour from the app: into the lookup (clock24), and onto the screen if it shows the one before */
static int h24n;
#define PKSONY 0x1d21092u                /* Sony's Yellow: Y 0xd2, Cb 0x10, Cr 0x92 */
static u32 pkfirst, pkchg;              /* the colour before a run of changes (for the full pass), when it last changed */
static u32 pkin_t;                      /* when ours came in (0: it isn't) */
static void pkupdate(void) {
    u32 was = pkin, now = secs();
    if (pkread()) {
        h24n = clock24(1);
        /* Sony's Yellow (only that menu colour gives it) stands for ours when ours comes in or goes */
        u32 from = was ? was : PKSONY, to = pkin ? pkin : PKSONY;
        if (from != to && (was || pkin)) {
            pkshow(from, to, 0);
            if (!pkfirst) pkfirst = from;
            pkchg = now;
        }
        pkin_t = pkin ? (was ? pkin_t : now) : 0;
        if (pkin_t == 0 && pkin) pkin_t = 1;
        return;
    }
    if (pkfirst && now - pkchg >= 1) {   /* the colour has stayed put a second: all of av-cam's data, libObj's table */
        u32 to = pkin ? pkin : PKSONY;
        if (pkfirst != to) pkshow(pkfirst, to, 1);
        pkfirst = 0;
    }
    /* Ours came in before the camera had set peaking up (at a cold boot: the helper starts before it does), so
     * Sony's Yellow can turn up afterwards: for two minutes after, every 2 s, Sony's Yellow found in the register
     * and av-cam's copies becomes ours (and libObj's table, a longer look, at 10, 30, 60 and 120 s) */
    if (pkin && pkin_t && !pkfirst) {
        u32 age = now - pkin_t;
        static u32 last = 0xffffffffu;
        if (age <= 120 && age != last && !(age & 1)) {
            last = age;
            pkshow(PKSONY, pkin, age == 10 || age == 30 || age == 60 || age == 120);
        }
    }
}
static void ftreport(char *st, char *last, int left, int done, int total, int cache, int labels, int pics,
                     u32 pdone, int pleft, int acc, int icons, int noicons) {
    int k = 0;
    if (!left) {
        k = scat(st, k, "on "); k = scatn(st, k, (u32)done);
        if (cache == -1) k = scat(st, k, " nocache");
        else if (cache >= 0) { k = scat(st, k, " c"); k = scatn(st, k, (u32)cache); }
        if (labels == -1) k = scat(st, k, " nolabels");
        else if (labels >= 0) { k = scat(st, k, " t"); k = scatn(st, k, tctaken); }
    } else { k = scat(st, k, "partial "); k = scatn(st, k, (u32)done); k = scat(st, k, "/"); k = scatn(st, k, (u32)total); }
    if (pics && (!left || pleft)) {   /* pictures done (and while some are still to come: of how many) */
        k = scat(st, k, " p"); k = scatn(st, k, pdone);
        if (pleft) { k = scat(st, k, "/"); k = scatn(st, k, pdone + (u32)pleft); }
    }
    if (!left) {
        if (pics && acc > 0) { k = scat(st, k, " a"); k = scatn(st, k, (u32)acc); }
        if (pics && h24n > 0) { k = scat(st, k, " h"); k = scatn(st, k, (u32)h24n); }
        if (pics && irisin > 0) { k = scat(st, k, " i"); k = scatn(st, k, irisin); }
        if (!pics && icons) k = scat(st, k, " nopics");
        if (pics && tzinon) { k = scat(st, k, tzin < 0 ? " z-" : " z+"); k = scatn(st, k, (u32)(tzin < 0 ? -tzin : tzin)); }
        if (pics && pkin) { k = scat(st, k, " k"); char x[7]; for (int i = 0; i < 6; i++) x[i] = "0123456789abcdef"[(pkin >> (20 - 4 * i)) & 15]; x[6] = 0; k = scat(st, k, x); }
        if (pics && texd >= 0) { k = scat(st, k, " g"); k = scatn(st, k, (u32)(texd + (texe > 0 ? texe : 0))); }
        if (noicons && icons) k = scat(st, k, " noicons");
        if (tookf) { k = scat(st, k, " f"); k = scatn(st, k, tookf); }
        if (tookp) { k = scat(st, k, " s"); k = scatn(st, k, tookp); }
    }
    int changed = 0;
    for (int i = 0; ; i++) { if (st[i] != last[i]) { changed = 1; break; } if (!st[i]) break; }
    if (changed) { ftstatus(st); for (int i = 0; i <= k; i++) last[i] = st[i]; }
}

static int udread(long *t, int *off);
/* the offset word for the files' times (pics.c, tzw): the camera's minutes east of UTC from udate, when it
 * knows them (its time is set), else none (Sony's "not valid" stays) */
static void tzupdate(void) {
    long t; int off;
    tzon = udread(&t, &off) == 1;
    if (!tzon) return;
    tzw[0] = 1; tzw[1] = 0; tzw[2] = (u8)off; tzw[3] = (u8)((u32)off >> 8);
}
static int font(int on, int icons) {
    if (!on) {
        ftstatus("restoring");
        mypid = sc(SYS_GETPID, 0, 0, 0, 0, 0);
        fd = sc(SYS_OPEN, (long)"/dev/mem", 2, 0, 0, 0);
        if (fd >= 0) { colours(0); clock24(0); }   /* the text accent colour and the 12 hour time back to Sony's */
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
    texd = -2; u32 texd2 = 0;         /* textures brought up to date (-2: not looked yet, -1: can't), next look */
    texe = -1;                        /* -1: the first pictures aren't all in yet */
    int mountok = checksigs() > 0;    /* the camera's code is the one we know: MOUNT (the lens mount) can be read */
    pkav = mountok;                   /* and the peaking colour copies in av-cam */
    u32 t0 = secs(); tookf = tookp = 0;   /* for the time it takes (fF, sS in the status) */
    sc(SYS_NICE, 19, 0, 0, 0, 0);     /* the camera comes first: unpacking and packing pictures takes a while */
    char last[80]; last[0] = 0;
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
                if (crossesedge(f, i) && secs() - t0 < 30) continue;    /* waits up to 30 s for the UI to move on (it
                                                                            mostly does within seconds; a buffer that stays
                                                                            put isn't being read from) */
                if (fset(f, i) < 0) { ftstatus("error write"); out("error write"); return 2; }
                done++; left--; wrote = 1;
            }
        }
        int acc = pics ? colours(1) : 0;      /* the text accent colour too */
        irisfl = pics && mountok ? manualfl() : 0;   /* a lens that tells no aperture: its focal length for F-- */
        if (pics) pkupdate();                 /* the peaking colour for Custom (and Sony's copies of it) */
        if (pics) tzupdate();                 /* the offset from UTC for the files' times */
        h24n = pics ? clock24(1) : 0;         /* and the time in 24 hours */
        int pleft = 0;
        if (pics && pics_state != 2) {         /* a batch at a time; once all are in, the label store's copies go */
            pdone = picsdo(PIC_BATCH, &pleft);
            if (texe < 0 && picfirstin && pleft) {   /* the ones seen most are in: those already on screen too, now */
                int r = texpass(); texe = r > 0 ? r : 0;
            }
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
            if (nfwin) fwt = secs(); else nfwin = -1;
        } else if (nfwin >= 0) {
            findwindows();
            int moved = 1;
            for (int w = 0; w < nfwin; w++) for (int v = 0; v < nwin; v++) if (fwin[w] == win[v] && fwf[w] == winf[v]) moved = 0;
            if (moved) { int c = flushcache(); if (c >= 0) { cache = c; labels = labelflush(1); } nfwin = -1; }
            else if (secs() - fwt > 3600) nfwin = -1;         /* not within an hour: leave it */
        }
        if (nold && labels >= 0) labelflush(0);   /* the out-of-date labels that were in use: once they aren't */
        if (!tookf && !left) { tookf = secs() - t0; if (!tookf) tookf = 1; }
        if (!tookp && pics && pics_state == 2) { tookp = secs() - t0; if (!tookp) tookp = 1; }
        char st[80];
        ftreport(st, last, left, done, total, cache, labels, pics, pdone, pleft, acc, icons, noicons);
#ifdef HOST
        out(st); out(" | ");
        if (++hostticks >= HOST_TICKS) return 0;
#endif
        if (pics_state == 2 && (texd == -2 || (texd2 && secs() >= texd2))) {   /* the pictures unpacked before ours came */
            int r = texpass();
            if (r >= 0) texd = (texd > 0 ? texd : 0) + r; else if (texd == -2) texd = r;
            texd2 = texd2 ? 0 : secs() + 600;        /* once more ten minutes later, then no more */
        }
        /* while pictures are left only a moment (nice 19 already lets the camera go first): a quarter second
         * after every batch was about 50 s of the patcher's time */
        long ts[2] = {pleft ? 0 : left ? 1 : 10, pleft ? 10000000 : 0};
        if (ts[0] == 10 && pics) {
            /* the 10 s between passes a second at a time (a quarter: the colour, while the app's picker moves): a
             * new focal length (the SteadyShot setting, read every second) gets to the label at once, so it's there
             * when the camera next draws it, and a new peaking colour to the camera */
            for (int w = 0; w < 40; w++) {
                long q[2] = {0, 250000000};
                sc(SYS_NANOSLEEP, (long)q, 0, 0, 0, 0);
                u32 nf = mountok && (w & 3) == 3 ? manualfl() : irisfl, pk = pkin;
                int ch = nf != irisfl;
                if (ch) { irisfl = nf; h24n = clock24(1); }
                pkupdate();
                if (ch || pk != pkin) {
                    char st2[80];
                    ftreport(st2, last, left, done, total, cache, labels, pics, pdone, pleft, acc, icons, noicons);
                }
            }
        } else sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    }
}

/* ---- the clock (--set-clock) ----
 * The camera keeps its date and time in its own driver (udate.ko: UTC, plus the time zone and summer time
 * offsets from the menu, counted on from a hardware timer), not in Linux's clock, which the boot snapshot
 * brings back at the second it was taken (65.5 s after 1970 began). The driver shows the local time in
 * /proc/udm/udate:
 *   ---------- Clock ----------
 *    [TIME]
 *     2026/11/22 14:5:3 [LOCAL TIME]
 *    [AREA]
 *     <TD>60 <DST>0 [VALID]
 * (TD, DST in minutes; local = UTC + TD + DST). Linux's clock is set to that UTC, through Android's alarm
 * driver (ANDROID_ALARM_SET_RTC: the way Android itself sets it, so its alarms keep up; there is no RTC chip
 * behind it here, so only RAM changes), or with settimeofday if that fails. To get it within a few tenths of a
 * second it waits for the driver's second to tick over. Answer (also in /android/dev/a7hacks.ck):
 *   clock set Y-M-D h:m:s +OFF (alarm | tod) | clock same +OFF | clock unset | clock error ...
 * (OFF: minutes east of UTC; the time shown is local). Left alone when it is already within 2 s. */
static int unum(const char **p, int *v) {
    const char *q = *p; int neg = 0, n = 0, d = 0;
    while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') q++;
    if (*q == '-') { neg = 1; q++; }
    while (*q >= '0' && *q <= '9' && d < 9) { n = n * 10 + (*q++ - '0'); d++; }
    if (!d) return 0;
    *v = neg ? -n : n; *p = q; return 1;
}
static const char *after(const char *s, const char *w) {
    for (; *s; s++) { int i = 0; while (w[i] && s[i] == w[i]) i++; if (!w[i]) return s + i; }
    return 0;
}
/* days since 1970-01-01 (years 1970..2099: every 4th a leap year) */
static long days70(int y, int m, int d) {
    static const short cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long n = (long)(y - 1970) * 365 + (y - 1969) / 4 + cum[m - 1] + d - 1;
    if (m > 2 && (y & 3) == 0) n++;
    return n;
}
/* the driver's local time as seconds since 1970 (local), and its offset in minutes:
 * 1 ok, 0 unset / no data, -1 can't read */
static int udread(long *t, int *off) {
    char b[512];
    int f = sc(SYS_OPEN, (long)UDATE, 0, 0, 0, 0);
    if (f < 0) return -1;
    int n = sc(SYS_READ, f, (long)b, sizeof b - 1, 0, 0);
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    if (n <= 0) return -1;
    b[n] = 0;
    const char *p = after(b, "[TIME]");
    int Y, M, D, h, mi, se, td = 0, dst = 0;
    if (!p || !unum(&p, &Y) || *p++ != '/' || !unum(&p, &M) || *p++ != '/' || !unum(&p, &D)
        || !unum(&p, &h) || *p++ != ':' || !unum(&p, &mi) || *p++ != ':' || !unum(&p, &se)) return -1;
    while (*p == ' ') p++;
    int local = after(p, "[LOCAL TIME]") == p + 12, utc = after(p, "[UTC]") == p + 5;
    if (!local && !utc) return 0;                       /* UNSET TIME / NO DATA */
    if (local) {
        const char *a = after(p, "<TD>"); if (!a || !unum(&a, &td)) return -1;
        a = after(p, "<DST>"); if (!a || !unum(&a, &dst)) return -1;
    }
    if (Y < 2014 || Y > 2099 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || se > 60) return -1;
    if (td < -24 * 60 || td > 24 * 60 || dst < 0 || dst > 120) return -1;
    *t = days70(Y, M, D) * 86400 + h * 3600 + mi * 60 + se;
    *off = td + dst;
    return 1;
}
static void outoff(int off) {
    out(off < 0 ? " -" : " +"); outn((u32)(off < 0 ? -off : off));
}
static void out2(int v) { char b[3] = {(char)('0' + v / 10), (char)('0' + v % 10), 0}; out(b); }
static int setclock(void) {
    long t, t1; int off, off1;
    int r = udread(&t, &off);
    if (r < 0) { out("clock error read " UDATE); return 2; }
    if (!r) { out("clock unset"); return 1; }
    long tv[2], tz[2];
    if (sc(SYS_GETTIMEOFDAY, (long)tv, (long)tz, 0, 0, 0) < 0) tv[0] = 0;
    long d = tv[0] + off * 60L - t;
    if (d >= -2 && d <= 2) { out("clock same"); outoff(off); return 0; }
    /* wait for the driver's second to tick over (at most ~1.2 s), so that we know it to ~20 ms */
    long ts[2] = {0, 20000000};
    for (int i = 0; i < 60; i++) {
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
        if (udread(&t1, &off1) != 1) { out("clock error read " UDATE); return 2; }
        if (t1 != t) { t = t1; off = off1; break; }
    }
    long utc = t - off * 60L;
    long now[2] = {utc, 10000000};                       /* timespec: half a poll into the new second */
    const char *how = " (alarm)";
    int a = sc(SYS_OPEN, (long)ALARMDEV, 2, 0, 0, 0);    /* O_RDWR */
    long e = a < 0 ? -1 : sc(SYS_IOCTL, a, 0x40086105, (long)now, 0, 0);   /* ANDROID_ALARM_SET_RTC */
    if (a >= 0) sc(SYS_CLOSE, a, 0, 0, 0, 0);
    if (e < 0) {
        long tod[2] = {utc, 10000};                      /* timeval */
        how = " (tod)";
        if (sc(SYS_SETTIMEOFDAY, (long)tod, 0, 0, 0, 0) < 0) { out("clock error set"); return 2; }
    }
    long day = t / 86400, sec = t % 86400;
    int Y = 1970; while (days70(Y + 1, 1, 1) <= day) Y++;
    int M = 1; while (M < 12 && days70(Y, M + 1, 1) <= day) M++;
    out("clock set "); outn(Y); out("-"); out2(M); out("-"); out2((int)(day - days70(Y, M, 1) + 1));
    out(" "); out2((int)(sec / 3600)); out(":"); out2((int)(sec / 60 % 60)); out(":"); out2((int)(sec % 60));
    outoff(off); out(how);
    return 0;
}

/* --memory-changes start len [ms]: which 64 KB blocks of physical memory change (for finding the live view's
 * buffers, which av-cam fills from inside the kernel: no Linux process maps them). start and len in hex, whole blocks, in RAM;
 * ms (decimal, default 100) between the two passes. Read only: /dev/mem is opened read only; each block is read
 * twice and hashed. Prints one line per MB: its address, then one character per block: C changed, 0 all zero,
 * a untouched since power-on (DRAM's 0xaa pattern with a few bits flipped), . the same both times; and the time
 * one pass took. */
#define MCBLK 0x10000u
static u8 mcbuf[MCBLK];
static u32 mchash[9216];                     /* 576 MB in blocks */
static u8 mckind[9216];
static int mcpass(u32 start, u32 nb, int second) {
    for (u32 b = 0; b < nb; b++) {
        if (pread_(start + b * MCBLK, mcbuf, MCBLK) < 0) return -1;
        u32 h = 2166136261u, zero = 0, aa = 0;
        for (u32 i = 0; i < MCBLK; i += 4) {
            u32 w = rd32(mcbuf + i);
            h = (h ^ w) * 16777619u;
            if (!w) zero++;
            for (int k = 0; k < 32; k += 8) if ((w >> k & 255) == 0xaa) aa++;
        }
        if (!second) {
            mchash[b] = h;
            mckind[b] = zero == MCBLK / 4 ? '0' : aa >= MCBLK / 4 ? 'a' : '.';
        } else if (h != mchash[b]) mckind[b] = 'C';
    }
    return 0;
}
static int memchanges(const char *s, const char *l, const char *m) {
    u32 start = 0, len = 0, ms = 100;
    for (const char *p = s; *p; p++) { int v = hexv(*p); if (v < 0) { out("error usage"); return 1; } start = start << 4 | (u32)v; }
    for (const char *p = l; *p; p++) { int v = hexv(*p); if (v < 0) { out("error usage"); return 1; } len = len << 4 | (u32)v; }
    if (m && (!dec(m, &ms) || ms > 10000)) { out("error usage"); return 1; }
    if (!*s || !*l || !len || (start | len) & (MCBLK - 1) || len / MCBLK > sizeof mchash / 4) { out("error usage"); return 1; }
    if (start - RAM_LO >= RAM_SIZE || RAM_SIZE - (start - RAM_LO) < len) { out("error not in RAM"); return 2; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    u32 nb = len / MCBLK, t0 = usnow();
    if (mcpass(start, nb, 0) < 0) { out("error read"); return 2; }
    u32 t1 = usnow();
    long ts[2] = {(long)(ms / 1000), (long)(ms % 1000) * 1000000};
    sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    if (mcpass(start, nb, 1) < 0) { out("error read"); return 2; }
    u32 per = MCBLK * 16 > len ? nb : 16;
    for (u32 b = 0; b < nb; b += per) {
        sayx(start + b * MCBLK); say(" ");
        sc(SYS_WRITE, 1, (long)(mckind + b), (long)(nb - b < per ? nb - b : per), 0, 0); say("\n");
    }
    out("pass "); outn((t1 - t0) / 1000); out(" ms");
    return 0;
}

/* --ui-find hex: where in the camera UI's own data (its [heap] and anonymous writable memory, pages in
 * RAM only) a byte string (1..16 bytes, in hex) is; prints each hit's virtual address and the 32 bytes around
 * it. Read only. Hits that cross a page edge are missed. */
static int uifind(const char *h) {
    u8 pat[16]; int n = 0;
    for (const char *p = h; *p && p[1]; p += 2) {
        if (n >= 16 || hexv(p[0]) < 0 || hexv(p[1]) < 0) { out("error usage"); return 1; }
        pat[n++] = (u8)(hexv(p[0]) << 4 | hexv(p[1]));
    }
    if (!n) { out("error usage"); return 1; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    findprocs(0);
    if (!np_) { out("noui"); return 2; }
    int pid = plist[0], hits = 0;
    heaponly = 1; readmaps(pid); heaponly = 0;
    for (int k = 0; k < nrng && hits < 64; k++)
        for (u32 va = rs[k]; va < rend[k] && hits < 64; va += 4096) {
            u32 pa; if (v2p(pid, va, &pa) <= 0) continue;
            if (pread_(pa, pg + 32, 4096) < 0) continue;
            for (int j = 32; j + n <= 32 + 4096 && hits < 64; j++) {
                if (!same(pg + j, pat, n)) continue;
                hits++;
                sayx(va + (u32)(j - 32)); say(":");
                int a = j - 16 < 32 ? 32 : j - 16, b = j + 16 > 32 + 4096 ? 32 + 4096 : j + 16;
                for (int i = a; i < b; i++) { say(i == j ? " [" : " "); char c[3] = {"0123456789abcdef"[pg[i] >> 4], "0123456789abcdef"[pg[i] & 15], 0}; say(c); }
                say("\n");
            }
        }
    out("hits "); outn((u32)hits);
    return 0;
}

/* --mem-find hex start len: where in physical RAM (start, len in hex, whole pages) a byte string (1..16
 * bytes) is; prints each hit's address and the 32 bytes around it (at most 64 hits). Read only. */
static int memfind(const char *h, const char *s0, const char *l0) {
    u8 pat[16]; int n = 0; u32 st = 0, ln = 0;
    for (const char *p = h; *p && p[1]; p += 2) {
        if (n >= 16 || hexv(p[0]) < 0 || hexv(p[1]) < 0) { out("error usage"); return 1; }
        pat[n++] = (u8)(hexv(p[0]) << 4 | hexv(p[1]));
    }
    for (const char *p = s0; *p; p++) { if (hexv(*p) < 0) { out("error usage"); return 1; } st = st << 4 | (u32)hexv(*p); }
    for (const char *p = l0; *p; p++) { if (hexv(*p) < 0) { out("error usage"); return 1; } ln = ln << 4 | (u32)hexv(*p); }
    if (!n || !ln || (st | ln) & 0xfff) { out("error usage"); return 1; }
    if (st - RAM_LO >= RAM_SIZE || RAM_SIZE - (st - RAM_LO) < ln) { out("error not in RAM"); return 2; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    int hits = 0;
    for (u32 a = st; a - st < ln && hits < 64; a += 4096) {
        if (pread_(a, pg + 32, 4096) < 0) { out("error read"); return 2; }
        for (int j = 32; j + n <= 32 + 4096 && hits < 64; j++) {
            if (!same(pg + j, pat, n)) continue;
            hits++;
            sayx(a + (u32)(j - 32)); say(":");
            int x = j - 16 < 32 ? 32 : j - 16, y = j + 16 > 32 + 4096 ? 32 + 4096 : j + 16;
            for (int i = x; i < y; i++) { say(i == j ? " [" : " "); char c[3] = {"0123456789abcdef"[pg[i] >> 4], "0123456789abcdef"[pg[i] & 15], 0}; say(c); }
            say("\n");
        }
    }
    out("hits "); outn((u32)hits);
    return 0;
}

/* --reg-read addr n: n 32-bit hardware registers from addr on (hex both; for the display hardware's peaking and
 * zebra registers at 0xf3445100 / 0xf3446100, set by av-cam). Read only: /dev/mem opened read only, one page mapped
 * uncached (O_SYNC) and read, and only I/O space (0xf0000000 and up, never RAM) within that one page. /dev/mem's
 * read() refuses addresses above RAM ("Bad address"), hence the mapping. Prints "addr value" per register. */
static int regread(const char *s, const char *l) {
    u32 a = 0, n = 0;
    for (const char *p = s; *p; p++) { int v = hexv(*p); if (v < 0) { out("error usage"); return 1; } a = a << 4 | (u32)v; }
    for (const char *p = l; *p; p++) { int v = hexv(*p); if (v < 0) { out("error usage"); return 1; } n = n << 4 | (u32)v; }
    if (!*s || !*l || !n || a & 3 || n > 1024) { out("error usage"); return 1; }
    if (a < 0xf0000000u || (a & 0xfffu) + n * 4 > 0x1000u) { out("error not in one page of I/O space"); return 2; }
    int f = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (f < 0) { out("error open /dev/mem"); return 2; }
    long m = sc6(SYS_MMAP2, 0, 0x1000, 1 /* PROT_READ */, 1 /* MAP_SHARED */, f, (long)(a >> 12));
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    if ((unsigned long)m >= 0xfffff000ul) { out("error mmap"); return 2; }
    const volatile u32 *r = (const volatile u32 *)(m + (a & 0xfffu));
    for (u32 i = 0; i < n; i++) { u32 v = r[i]; sayx(a + i * 4); say(" "); sayx(v); say("\n"); }
    sc(SYS_MUNMAP, m, 0x1000, 0, 0, 0);
    return 0;
}

/* --off-watch path seconds: a log of what changes around a switch-off (which suspends the camera to RAM, so the
 * log in /android/dev survives it), for the shutdown screen. Every 20 ms: both OSD buffers (every 8th row: a hash and
 * how many pixels aren't fully transparent), whether the live view runs, the UI's process state, the GPIO and
 * indicator (LEDs) lines of /proc/udm, and the display registers below ("R" lines). One line per change: ms since start (CLOCK_BOOTTIME, which counts through a
 * suspend), ms spent suspended so far, then the values; GPIO/LED lines that changed follow it. Read only. */
#define OWROWS 8
/* display registers (av-cam's display output, "VPH": the pages its code refers to most): read uncached; those that
 * change by themselves in the first 3 s count as noisy and are left out; read only until 1 s after the switch-off
 * (OSD blank, live view stopped), in case the display's clocks are stopped after that */
static const u32 owrp[] = {0xf3400000u, 0xf3440000u, 0xf3441000u, 0xf3445000u, 0xf3446000u};   /* 0xf3400000 first: the
                                                  display path's clock enables (0x15 -> 0 at the switch-off) */
#define OWNR (sizeof owrp / sizeof owrp[0])
static u32 owrv[OWNR][1024];
static u8 ownoisy[OWNR][1024];
static char owg[2][4096], owi[2][1024];
static int owlog = -1, owlines;
static void owsay(const char *s) { int n = 0; while (s[n]) n++; if (owlog >= 0) sc(SYS_WRITE, owlog, (long)s, n, 0, 0); }
static void owsayn(u32 v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v && i > 0); owsay(b + i); }
static void owsayx(u32 v) { char b[9]; for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[8] = 0; owsay(b); }
static int rdfile(const char *path, char *b, int max) {
    int f = sc(SYS_OPEN, (long)path, 0, 0, 0, 0), n = 0, r;
    if (f < 0) { b[0] = 0; return -1; }
    while (n < max - 1 && (r = sc(SYS_READ, f, (long)(b + n), max - 1 - n, 0, 0)) > 0) n += r;
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    b[n] = 0; return n;
}
static u32 msclock(int id) { long ts[2] = {0, 0}; sc(SYS_CLOCK_GETTIME, id, (long)ts, 0, 0, 0); return (u32)ts[0] * 1000u + (u32)ts[1] / 1000000u; }
/* the lines of b that differ from the same line of a (indicator: skips the clock line "now SYSCNT") */
static void owdiff(const char *tag, const char *a, const char *b) {
    while (*b) {
        const char *ea = a, *eb = b;
        while (*ea && *ea != '\n') ea++;
        while (*eb && *eb != '\n') eb++;
        int d = (ea - a) != (eb - b);
        for (const char *p = a, *q = b; !d && q < eb; p++, q++) if (*p != *q) d = 1;
        if (d && !(eb - b > 3 && b[0] == 'n' && b[1] == 'o' && b[2] == 'w')) {
            owsay("    "); owsay(tag); owsay(" "); sc(SYS_WRITE, owlog, (long)b, eb - b, 0, 0); owsay("\n");
        }
        a = *ea ? ea + 1 : ea; b = *eb ? eb + 1 : eb;
    }
}
static int offwatch(const char *path, const char *s) {
    u32 secs_ = 0;
    if (!dec(s, &secs_) || !secs_ || secs_ > 3600) { out("error usage"); return 1; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    findprocs(0);
    if (!np_) { out("noui"); return 1; }
    int pid = plist[0];
    nst = 0; wantstream = 1; int r = readmaps(pid); wantstream = 0;
    if (r < 0) { out("error maps"); return 2; }
    u32 buf[2]; int nb = 0;
    for (int k = 0; k < nst; k++) if (stlen[k] == OSDLEN && nb < 3) { if (nb < 2) buf[nb] = stpa[k]; nb++; }
    if (nb != 2) { out("error "); outn((u32)nb); out(" OSD buffers"); return 2; }
    for (int k = 0; k < 2; k++)
        if ((buf[k] & 4095) || (u32)(buf[k] - RAM_LO) >= RAM_SIZE || RAM_SIZE - (buf[k] - RAM_LO) < OSDLEN) { out("error OSD buffer not in RAM"); return 2; }
    if ((u32)(LVA - RAM_LO) >= RAM_SIZE || RAM_SIZE - (LVB - RAM_LO) < LVLEN) { out("error live view not in RAM"); return 2; }
    owlog = sc(SYS_OPEN, (long)path, 01 | 0100 | 01000, 0644, 0, 0);
    if (owlog < 0) { out("error open output"); return 2; }
    char stp[32]; procpath(stp, pid, "stat");
    char st[512];
    int bt = msclock(7) != 0 || msclock(1) == 0 ? 7 : 1;         /* CLOCK_BOOTTIME if the kernel has it */
    u32 b0 = msclock(bt), m0 = msclock(1), lvw[2] = {0, 0}, lvt = 0, last[8], cur = 0, pr = 0;
    for (int i = 0; i < 8; i++) last[i] = 0xffffffffu;
    owsay("# off-watch: OSD "); owsayx(buf[0]); owsay(" "); owsayx(buf[1]); owsay(", UI pid "); owsayn((u32)pid);
    const volatile u32 *rg[OWNR];
    int rf = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (rf < 0) { owsay("error open /dev/mem for registers\n"); sc(SYS_CLOSE, owlog, 0, 0, 0, 0); out("error registers"); return 2; }
    for (u32 k = 0; k < OWNR; k++) {
        long m = sc6(SYS_MMAP2, 0, 0x1000, 1 /* PROT_READ */, 1 /* MAP_SHARED */, rf, (long)(owrp[k] >> 12));
        if ((unsigned long)m >= 0xfffff000ul) { owsay("error mmap registers\n"); sc(SYS_CLOSE, owlog, 0, 0, 0, 0); out("error registers"); return 2; }
        rg[k] = (const volatile u32 *)m;
        for (u32 i = 0; i < 1024; i++) { owrv[k][i] = rg[k][i]; ownoisy[k][i] = 0; }
    }
    sc(SYS_CLOSE, rf, 0, 0, 0, 0);
    int regs = 1, armed = 0; u32 offat = 0, learned = 0;
    owsay(bt == 7 ? ", boottime\n# ms slept | OSD A hash shown | OSD B hash shown | lv | UI state cpu\n" : ", NO boottime (monotonic only)\n");
    for (;;) {
        u32 now = msclock(bt), mono = msclock(1), v[8];
        if (now - b0 > secs_ * 1000u || owlines > 3000) break;
        for (int k = 0; k < 2; k++) {
            u32 h = 2166136261u, shown = 0;
            for (int y = 0; y < OSDH; y += OWROWS) {
                if (pread_(buf[k] + y * OSDW * 2, rowa, OSDW * 2) < 0) { owsay("error read OSD\n"); goto done; }
                for (int x = 0; x < OSDW * 2; x += 2) { u32 p = rowa[x] | rowa[x + 1] << 8; h = (h ^ p) * 16777619u; if (p & 15) shown++; }
            }
            v[2 * k] = h; v[2 * k + 1] = shown;
        }
        int lvmoved = 0;
        for (int k = 0; k < 2; k++) { u8 w[4]; if (pread_((k ? LVB : LVA) + LVLEN / 2, w, 4) == 0 && rd32(w) != lvw[k]) { lvw[k] = rd32(w); lvmoved = 1; } }
        if (lvmoved) lvt = now;
        v[4] = now - lvt < 300;                                  /* the live view ran in the last 300 ms */
        int n = rdfile(stp, st, sizeof st);
        char state = '?'; u32 ticks = 0;
        if (n > 0) {                                             /* "pid (comm) S ... field 14 utime 15 stime" */
            const char *p = st; while (*p && *p != ')') p++;
            if (*p) { p += 2; state = *p; int f = 3;
                while (*p && f < 14) { if (*p == ' ') f++; p++; }
                u32 a = 0, b = 0; while (*p >= '0' && *p <= '9') a = a * 10 + (u32)(*p++ - '0');
                if (*p == ' ') p++; while (*p >= '0' && *p <= '9') b = b * 10 + (u32)(*p++ - '0');
                ticks = a + b; }
        }
        int rch = 0;
        if (v[4]) armed = 1;
        if (armed && !offat && !v[4] && (!v[1] || !v[3])) offat = now ? now : 1;
        if (regs && offat && now - offat > 1000) { regs = 0; owsay("# registers: no more reads (1 s after the switch-off)\n"); }
        if (regs) {
            int learn = now - b0 < 3000;
            if (!learn && !learned) { learned = 1; u32 nn = 0; for (u32 k = 0; k < OWNR; k++) for (u32 i = 0; i < 1024; i++) nn += ownoisy[k][i];
                owsay("# registers: "); owsayn(nn); owsay(" noisy ones left out; watching the rest\n"); }
            for (u32 k = 0; k < OWNR; k++) for (u32 i = 0; i < 1024; i++) {
                u32 x = rg[k][i];
                if (x == owrv[k][i]) continue;
                if (learn) ownoisy[k][i] = 1;
                else if (!ownoisy[k][i]) {
                    if (!rch) { owsay("    R at "); owsayn(now - b0); owsay(":\n"); }
                    rch = 1; owsay("    R "); owsayx(owrp[k] + i * 4); owsay(" "); owsayx(owrv[k][i]); owsay(" > "); owsayx(x); owsay("\n");
                }
                owrv[k][i] = x;
            }
        }
        v[5] = (u32)state; v[6] = (int)((now - b0) - (mono - m0)) < 50 ? 0 : (now - b0) - (mono - m0);   /* ms suspended (the clocks differ by a ms) */
        rdfile("/proc/udm/gpio", owg[cur], sizeof owg[0]);
        rdfile("/proc/udm/indctr", owi[cur], sizeof owi[0]);
        int ch = 0;
        for (int i = 0; i < 6; i++) if (v[i] != last[i]) ch = 1;   /* not the cpu ticks */
        if (v[6] != last[6]) ch = 1;
        int gch = 0, ich = 0;
        if (owlines) {
            for (int i = 0; owg[cur][i] || owg[!cur][i]; i++) if (owg[cur][i] != owg[!cur][i]) { gch = 1; break; }
            const char *a = owi[cur], *b = owi[!cur];            /* the indicator file, without its clock line */
            for (;;) {
                while (*a == 'n' && a[1] == 'o' && a[2] == 'w') { while (*a && *a != '\n') a++; if (*a) a++; }
                while (*b == 'n' && b[1] == 'o' && b[2] == 'w') { while (*b && *b != '\n') b++; if (*b) b++; }
                if (*a != *b) { ich = 1; break; }
                if (!*a) break;
                a++; b++;
            }
        }
        if (ch || gch || ich || !owlines) {
            owsayn(now - b0); owsay(" "); owsayn(v[6]);
            owsay(" | "); owsayx(v[0]); owsay(" "); owsayn(v[1]); owsay(" | "); owsayx(v[2]); owsay(" "); owsayn(v[3]);
            owsay(" | "); owsay(v[4] ? "lv" : "--"); owsay(" | "); char sc_[2] = {state, 0}; owsay(sc_); owsay(" "); owsayn(ticks - pr);
            owsay("\n");
            if (!owlines) { owsay("    gpio at start:\n"); owdiff("G", "", owg[cur]); owsay("    indctr at start:\n"); owdiff("I", "", owi[cur]); }
            else { if (gch) owdiff("G", owg[!cur], owg[cur]); if (ich) owdiff("I", owi[!cur], owi[cur]); }
            owlines++; pr = ticks;
            for (int i = 0; i < 7; i++) last[i] = v[i];
        }
        cur = !cur;
        long ts[2] = {0, 20000000};
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    }
done:
    for (u32 k = 0; k < OWNR; k++) sc(SYS_MUNMAP, (long)rg[k], 0x1000, 0, 0, 0);
    owsay("# end\n");
    sc(SYS_CLOSE, owlog, 0, 0, 0, 0);
    out("logged "); outn((u32)owlines); out(" lines to "); out(path);
    return 0;
}

/* --osd-mark shown|hidden seconds: a test of the display register 0xf3441320, which holds the OSD buffer the
 * display shows. Paints a 48x48 magenta square in the middle of the shown (or the other) OSD buffer, through our own
 * mapping of /dev/stream, and after `seconds` puts the pixels back (only where our magenta still is). The UI flips
 * its two buffers when it redraws, so on a still screen the shown one stays shown. Prints the register before and
 * after. */
#define MARK 48
static u16 marksave[MARK * MARK];
static int osdmark(const char *which, const char *s) {
    u32 secs_ = 0, want = eq(which, "shown") ? 1 : eq(which, "hidden") ? 2 : 0;
    if (!want || !dec(s, &secs_) || !secs_ || secs_ > 60) { out("error usage"); return 1; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    findprocs(0);
    if (!np_) { out("noui"); return 1; }
    nst = 0; wantstream = 1; int r = readmaps(plist[0]); wantstream = 0;
    if (r < 0) { out("error maps"); return 2; }
    u32 buf[2]; int nb = 0;
    for (int k = 0; k < nst; k++) if (stlen[k] == OSDLEN && nb < 3) { if (nb < 2) buf[nb] = stpa[k]; nb++; }
    if (nb != 2) { out("error "); outn((u32)nb); out(" OSD buffers"); return 2; }
    int rf = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (rf < 0) { out("error open /dev/mem"); return 2; }
    long rm = sc6(SYS_MMAP2, 0, 0x1000, 1, 1, rf, (long)(0xf3441000u >> 12));
    sc(SYS_CLOSE, rf, 0, 0, 0, 0);
    if ((unsigned long)rm >= 0xfffff000ul) { out("error mmap register"); return 2; }
    const volatile u32 *reg = (const volatile u32 *)(rm + 0x320);
    u32 on = *reg;
    int k = on == buf[0] ? 0 : on == buf[1] ? 1 : -1;
    if (k < 0) { out("error register "); outx(on); out(" is neither OSD buffer"); return 2; }
    if (want == 2) k = !k;
    for (int j = 0; j < 2; j++)
        if ((buf[j] & 4095) || (u32)(buf[j] - RAM_LO) >= RAM_SIZE || RAM_SIZE - (buf[j] - RAM_LO) < OSDLEN) { out("error OSD buffer not in RAM"); return 2; }
    int sf = sc(SYS_OPEN, (long)"/dev/stream", 2 | 0x101000 /* O_RDWR | O_SYNC */, 0, 0, 0);
    if (sf < 0) { out("error open /dev/stream"); return 2; }
    long m = sc6(SYS_MMAP2, 0, OSDLEN, 3, 1, sf, (long)(buf[k] >> 12));
    if ((unsigned long)m >= (unsigned long)-4095L) { out("error mmap"); return 2; }
    volatile u16 *o = (volatile u16 *)m;
    u32 x0 = (OSDW - MARK) / 2, y0 = (OSDH - MARK) / 2;
    const u16 mag = 0xf0ff;                                  /* RGBA4444: R 15, G 0, B 15, A 15 */
    for (u32 y = 0; y < MARK; y++) for (u32 x = 0; x < MARK; x++) { u32 i = (y0 + y) * OSDW + x0 + x; marksave[y * MARK + x] = o[i]; o[i] = mag; }
    long ts[2] = {(long)secs_, 0};
    sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    u32 after = *reg, kept = 0;
    for (u32 y = 0; y < MARK; y++) for (u32 x = 0; x < MARK; x++) { u32 i = (y0 + y) * OSDW + x0 + x; if (o[i] == mag) { o[i] = marksave[y * MARK + x]; kept++; } }
    sc(SYS_MUNMAP, m, OSDLEN, 0, 0, 0); sc(SYS_MUNMAP, rm, 0x1000, 0, 0, 0); sc(SYS_CLOSE, sf, 0, 0, 0, 0);
    out("marked "); out(want == 1 ? "shown" : "hidden"); out(" buffer "); outx(buf[k]); out(" (register before "); outx(on);
    out(", after "); outx(after); out("); "); outn(kept); out(" of "); outn(MARK * MARK); out(" mark pixels were still ours");
    return 0;
}

/* --lv-timing ms: when the live view's buffers change, for `ms` milliseconds (at most 5000), read only. Watched in a
 * tight loop, all uncached: the display register 0xf3441220 (the compressed live view buffer the panel shows), one
 * word in each of those three buffers (when the camera writes them), and the middle word of each UYVY buffer (LVA,
 * LVB: the copy --screenshot uses). Prints each change (us since start, what, value), then per source the number of
 * changes and the first and last time. */
#define LVTN 7
#define LVTMAX 6000
static u32 lvtt[LVTMAX], lvtv[LVTMAX]; static u8 lvtw[LVTMAX];
static int lvtiming(const char *s) {
    u32 ms = 0;
    if (!dec(s, &ms) || !ms || ms > 5000) { out("error usage"); return 1; }
    static const u32 cbuf[3] = {0x8eb00000u, 0x8ec34000u, 0x8ed68000u};
    static const char *names[LVTN] = {"shown", "C0", "C1", "C2", "LVA", "LVB", "pos"};
    int f = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (f < 0) { out("error open /dev/mem"); return 2; }
    long rm = sc6(SYS_MMAP2, 0, 0x1000, 1, 1, f, (long)(0xf3441000u >> 12));
    if ((unsigned long)rm >= 0xfffff000ul) { out("error mmap"); return 2; }
    const volatile u32 *w[LVTN];
    w[0] = (const volatile u32 *)(rm + 0x220);
    w[6] = (const volatile u32 *)(rm + 0x210);
    u32 pa[5] = {cbuf[0] + 0x40000, cbuf[1] + 0x40000, cbuf[2] + 0x40000, LVA + LVLEN / 2, LVB + LVLEN / 2};
    long pm[5];
    for (int k = 0; k < 5; k++) {
        if ((u32)(pa[k] - RAM_LO) >= RAM_SIZE) { out("error not in RAM"); return 2; }
        pm[k] = sc6(SYS_MMAP2, 0, 0x1000, 1, 1, f, (long)(pa[k] >> 12));
        if ((unsigned long)pm[k] >= 0xfffff000ul) { out("error mmap"); return 2; }
        w[1 + k] = (const volatile u32 *)(pm[k] + (pa[k] & 0xfff));
    }
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    u32 last[LVTN], cnt[LVTN], first[LVTN], prev[LVTN]; int n = 0;
    for (int k = 0; k < LVTN; k++) { last[k] = w[k][0]; cnt[k] = 0; first[k] = prev[k] = 0; }
    u32 t0 = usnow(), loops = 0;
    for (;;) {
        u32 t = usnow() - t0;
        if (t > ms * 1000u) break;
        loops++;
        for (int k = 0; k < LVTN; k++) {
            u32 v = w[k][0];
            if (v == last[k]) continue;
            last[k] = v;
            if (!cnt[k]) first[k] = t;
            cnt[k]++; prev[k] = t;
            if (k != 6 && n < LVTMAX) { lvtt[n] = t; lvtv[n] = v; lvtw[n] = (u8)k; n++; }
        }
    }
    for (int i = 0; i < n; i++) { sayn(lvtt[i]); say(" "); say(names[lvtw[i]]); say(" "); sayx(lvtv[i]); say("\n"); }
    for (int k = 0; k < LVTN; k++) {
        say("# "); say(names[k]); say(": "); sayn(cnt[k]); say(" changes");
        if (cnt[k] > 1) { say(", first at "); sayn(first[k]); say(" us, last at "); sayn(prev[k]); say(" us"); }
        say("\n");
    }
    for (int k = 0; k < 5; k++) sc(SYS_MUNMAP, pm[k], 0x1000, 0, 0, 0);
    sc(SYS_MUNMAP, rm, 0x1000, 0, 0, 0);
    out("loops "); outn(loops); out(" in "); outn(ms); out(" ms");
    return 0;
}

/* When this helper was built: "yyyy-mm-dd HH:MM:SS", UTC (Gradle's buildHelper passes it). --version prints it; the
 * app runs that (as itself, no root needed) for its info dialog. */
#ifndef A7_BUILT
#define A7_BUILT "unknown"
#endif
static const char built[] = "a7hacks helper built " A7_BUILT;

/* --ram-watch path seconds start:len ...: which words of physical RAM change, and when (for the shutdown screen:
 * av-cam's panel data around a switch-off). start and len in hex, words, at most 16 KB in all, RAM only; read
 * every ~2 ms with plain reads of /dev/mem (opened read only). Words that change 3 times or more in the first
 * 3 s are noisy and left out. As a time mark it also logs the display register 0xf3440770 (its 0x20000 bit goes
 * off at a switch-off). One line per change: ms since start, the address, old > new. Read only. */
#define RWMAX 4096
static u32 rwv[RWMAX]; static u8 rwn[RWMAX];
static int ramwatch(int argc, char **argv) {
    u32 secs_ = 0, ra[16], rl[16], nr = 0, tot = 0;
    if (!dec(argv[3], &secs_) || !secs_ || secs_ > 3600) { out("error usage"); return 1; }
    for (int i = 4; i < argc && nr < 16; i++) {
        u32 a = 0, l = 0; const char *p = argv[i];
        for (; *p && *p != ':'; p++) { if (hexv(*p) < 0) { out("error usage"); return 1; } a = a << 4 | (u32)hexv(*p); }
        if (*p++ != ':') { out("error usage"); return 1; }
        for (; *p; p++) { if (hexv(*p) < 0) { out("error usage"); return 1; } l = l << 4 | (u32)hexv(*p); }
        a &= ~3u; l = (l + 3) & ~3u;
        if (!l || a - RAM_LO >= RAM_SIZE || RAM_SIZE - (a - RAM_LO) < l || tot + l / 4 > RWMAX) { out("error range"); return 1; }
        ra[nr] = a; rl[nr] = l; nr++; tot += l / 4;
    }
    if (!nr) { out("error usage"); return 1; }
    fd = sc(SYS_OPEN, (long)"/dev/mem", 0, 0, 0, 0);
    if (fd < 0) { out("error open /dev/mem"); return 2; }
    int rf = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    long rm = rf < 0 ? -1 : sc6(SYS_MMAP2, 0, 0x1000, 1, 1, rf, (long)(0xf3440000u >> 12));
    if (rf >= 0) sc(SYS_CLOSE, rf, 0, 0, 0, 0);
    if ((unsigned long)rm >= 0xfffff000ul) { out("error mmap register"); return 2; }
    const volatile u32 *mark = (const volatile u32 *)(rm + 0x770);
    owlog = sc(SYS_OPEN, (long)argv[2], 01 | 0100 | 01000, 0644, 0, 0);
    if (owlog < 0) { out("error open output"); return 2; }
    static u8 buf[RWMAX * 4];
    u32 k = 0;
    for (u32 r = 0; r < nr; r++) { if (pread_(ra[r], buf, (int)rl[r]) < 0) { out("error read"); return 2; } for (u32 i = 0; i < rl[r] / 4; i++) { rwv[k] = rd32(buf + 4 * i); rwn[k++] = 0; } }
    u32 t0 = msclock(1), lastmark = *mark, lines = 0, learned = 0;
    owsay("# ram-watch: "); owsayn(nr); owsay(" ranges, "); owsayn(tot); owsay(" words; mark f3440770 = "); owsayx(lastmark); owsay("\n");
    for (;;) {
        u32 now = msclock(1), dt = now - t0;
        if (dt > secs_ * 1000u || lines > 4000) break;
        int learn = dt < 3000;
        if (!learn && !learned) { learned = 1; u32 nn = 0; for (u32 i = 0; i < tot; i++) nn += rwn[i] >= 3; owsay("# "); owsayn(nn); owsay(" noisy words left out\n"); }
        u32 m = *mark;
        if (m != lastmark) { owsayn(dt); owsay(" mark f3440770 "); owsayx(lastmark); owsay(" > "); owsayx(m); owsay("\n"); lastmark = m; lines++; }
        k = 0;
        for (u32 r = 0; r < nr; r++) {
            if (pread_(ra[r], buf, (int)rl[r]) < 0) { owsay("error read\n"); goto done; }
            for (u32 i = 0; i < rl[r] / 4; i++, k++) {
                u32 v = rd32(buf + 4 * i);
                if (v == rwv[k]) continue;
                if (learn) { if (rwn[k] < 255) rwn[k]++; }
                else if (rwn[k] < 3) { owsayn(dt); owsay(" "); owsayx(ra[r] + 4 * i); owsay(" "); owsayx(rwv[k]); owsay(" > "); owsayx(v); owsay("\n"); lines++; }
                rwv[k] = v;
            }
        }
        long ts[2] = {0, 2000000};
        sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    }
done:
    owsay("# end\n");
    sc(SYS_CLOSE, owlog, 0, 0, 0, 0);
    sc(SYS_MUNMAP, rm, 0x1000, 0, 0, 0);
    out("logged "); outn(lines); out(" changes to "); out(argv[2]);
    return 0;
}

/* --arm7-sram path: the 16 KB SRAM of the SoC's small ARM7 core (physical 0x0-0x3fff, /proc/iomem "arm7sram") to
 * path, read twice 100 ms apart through an uncached read-only mapping, in words; says how many words differ
 * between the two reads (something running there changes some). Only this SRAM is read, not arm7io. */
static int arm7sram(const char *path) {
    static u32 a[0x1000], b2[0x1000];
    int f = sc(SYS_OPEN, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0, 0, 0);
    if (f < 0) { out("error open /dev/mem"); return 2; }
    long m = sc6(SYS_MMAP2, 0, 0x4000, 1 /* PROT_READ */, 1 /* MAP_SHARED */, f, 0);
    sc(SYS_CLOSE, f, 0, 0, 0, 0);
    if ((unsigned long)m >= 0xfffff000ul) { out("error mmap "); outn((u32)-m); return 2; }
    const volatile u32 *r = (const volatile u32 *)m;
    for (u32 i = 0; i < 0x1000; i++) a[i] = r[i];
    long ts[2] = {0, 100000000};
    sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0);
    u32 diff = 0, zero = 0, ff = 0;
    for (u32 i = 0; i < 0x1000; i++) { b2[i] = r[i]; if (b2[i] != a[i]) diff++; if (!a[i]) zero++; if (a[i] == 0xffffffffu) ff++; }
    sc(SYS_MUNMAP, m, 0x4000, 0, 0, 0);
    int o = sc(SYS_OPEN, (long)path, 01 | 0100 | 01000, 0644, 0, 0);
    if (o < 0) { out("error open output"); return 2; }
    sc(SYS_WRITE, o, (long)a, sizeof a, 0, 0);
    sc(SYS_CLOSE, o, 0, 0, 0, 0);
    out("written "); out(path); out(": "); outn(diff); out(" of 4096 words changed in 100 ms; "); outn(zero); out(" zero, "); outn(ff); out(" all ones");
    return 0;
}

/* --help (and the list of modes: the comment at the top points here) */
static const char usage[] =
    "Usage (as root):\n"
    "  liba7hacks.so --lens-write id 0               manual lens EXIF info off (the camera behaves as without us)\n"
    "  liba7hacks.so --lens-write id 1 fnum fden anum aden mnum mden [namehex]\n"
    "      focal length fnum/fden, F-number anum/aden, widest aperture as an APEX value (2 log2 N, EXIF\n"
    "      MaxApertureValue: the camera copies it as it is) mnum/mden,\n"
    "      lens name as hex bytes (at most 64). Numbers are decimal.\n"
    "  liba7hacks.so --lens-read id                  show the block\n"
    "  liba7hacks.so --exposure-patcher              run forever: exposure focal length (see the source)\n"
    "  liba7hacks.so --label-patcher                 run forever: menu label (see the source)\n"
    "  liba7hacks.so --label-find [--all]            menu label, read only: show where the label is\n"
    "  liba7hacks.so --font-patcher [--text-only]    run forever: UI font, icon and picture lettering, accent colour\n"
    "                                                (--text-only: the UI font only)\n"
    "  liba7hacks.so --font-restore                  UI font: put the camera's own back\n"
    "  liba7hacks.so --set-clock                     Linux's clock: set it from the camera's own (see the source)\n"
    "  liba7hacks.so --version                       when this helper was built (UTC)\n"
    "  liba7hacks.so --help                          this text\n"
    "  liba7hacks.so --memory-dump path [--all]      copy the UI's own data to path (for study elsewhere;\n"
    "                                                --all: and the driver memory it has mapped, RAM pages only)\n"
    "  liba7hacks.so --memory-changes start len [ms]  which 64 KB blocks of physical memory change (read only)\n"
    "  liba7hacks.so --reg-read addr n               n 32-bit hardware registers from addr on (read only)\n"
    "  liba7hacks.so --mem-find hex start len        where a byte string is in physical RAM (read only)\n"
    "  liba7hacks.so --off-watch path seconds        log what changes around a switch-off (read only)\n"
    "  liba7hacks.so --lv-timing ms                  when the live view buffers change and flip (read only)\n"
    "  liba7hacks.so --ram-watch path secs start:len ...\n"
    "                                                which words of physical RAM change, and when (read only)\n"
    "  liba7hacks.so --arm7-sram path                the ARM7 core's 16 KB SRAM to path (read only)\n"
    "  liba7hacks.so --osd-mark shown|hidden secs    test: a magenta square in the shown/hidden OSD buffer\n"
    "  liba7hacks.so --ui-find hex                   where a byte string is in the UI's own data (read only)\n"
    "  liba7hacks.so --screenshot [osd.png lv.png] both.png\n"
    "                                                Sony's own screen as a PNG: its OSD over the live view (LCD or\n"
    "                                                EVF, as shown); with three names also each on its own\n"
    "  liba7hacks.so --pic-census                    which of our pictures the UI has unpacked now (read only)\n"
    "  liba7hacks.so --pic-bench                     time the picture patcher's steps on the camera (changes nothing)\n"
    "  liba7hacks.so --glyph-cache [--mark-stale | --list] [--all]\n"
    "                                                UI font: show the UI's glyph cache (--mark-stale: and mark it\n"
    "                                                stale; --list: and list, with pictures, the glyphs drawn since;\n"
    "                                                --all: look in every process, not just the UI)\n"
    "\n"
    "Each mode is described in native/a7hacks.c: the comment at the top, and the one above its code. The answer\n"
    "is one line on stdout (the lens modes also write it to /android/dev/a7hacks.st); the patchers report to\n"
    "/android/dev/a7hacks.*.\n"
    "\n"
    "This help text was written by an LLM\n";
static int eqn(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static int eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int run(int argc, char **argv) {
    const char *o = argc >= 2 ? argv[1] : "";
    const char *o2 = argc >= 3 ? argv[2] : "";
    if (argc == 2 && (eq(o, "--help") || eq(o, "-h"))) { say(usage); return 0; }
    if (argc == 2 && eq(o, "--version")) { out(built); out(" UTC"); return 0; }
    if (argc == 2 && eq(o, "--exposure-patcher")) return exposure();
    if (argc == 2 && eq(o, "--label-patcher")) return label();
    if (eq(o, "--font-patcher") && (argc == 2 || (argc == 3 && eq(o2, "--text-only")))) return font(1, argc == 2);
    if (argc == 2 && eq(o, "--font-restore")) return font(0, 1);
    if (argc == 2 && eq(o, "--set-clock")) return setclock();
    if (eq(o, "--screenshot") && (argc == 3 || argc == 5)) return screenshot(argc, argv);
    if (argc == 2 && eq(o, "--pic-census")) return census();
    if (argc == 2 && eq(o, "--pic-bench")) return bench();
    if (eq(o, "--memory-dump") && (argc == 3 || (argc == 4 && eq(argv[3], "--all")))) return memdump(argv[2], argc == 4);
    if (eq(o, "--memory-changes") && (argc == 4 || argc == 5)) return memchanges(argv[2], argv[3], argc == 5 ? argv[4] : 0);
    if (argc == 4 && eq(o, "--reg-read")) return regread(argv[2], argv[3]);
    if (argc == 4 && eq(o, "--off-watch")) return offwatch(argv[2], argv[3]);
    if (argc == 3 && eq(o, "--lv-timing")) return lvtiming(argv[2]);
    if (argc >= 5 && eq(o, "--ram-watch")) return ramwatch(argc, argv);
    if (argc == 3 && eq(o, "--arm7-sram")) return arm7sram(argv[2]);
    if (argc == 4 && eq(o, "--osd-mark")) return osdmark(argv[2], argv[3]);
    if (argc == 3 && eq(o, "--ui-find")) return uifind(argv[2]);
    if (argc == 5 && eq(o, "--mem-find")) return memfind(argv[2], argv[3], argv[4]);
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
    if (mlen == 11 && eqn(msg, "error usage", 11)) out(" (see --help)");
    if (argc == 2 && (eq(argv[1], "--help") || eq(argv[1], "-h"))) return code;     /* the help is all */
    msg[mlen++] = '\n';
    sc(SYS_WRITE, 1, (long)msg, mlen, 0, 0);
    if (argc == 2 && eq(argv[1], "--set-clock")) { msg[mlen - 1] = 0; statusto(CKSTATUS, CKSTATUS ".t", msg); msg[mlen - 1] = '\n'; }
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
