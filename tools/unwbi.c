/* unwbi - unpacks the camera's boot snapshot (flash partition 5, nflasha5: "WBI1") into its memory pages.
 *
 * The A7 II doesn't cold boot: it restores a RAM image Sony made at the factory. nflasha5 holds it: a
 * 0x40-byte header ("WBI1", ..., the resume address 0x80052bbc, ...), zeros up to 0x40000, then ~1420 sections
 * (count_sections_p2 in its kernel log), each a run of 4 KB pages, each page one block of Sony's LZ77 (the
 * same as in the .uxc files: 0xf0 + flag bytes, 2-byte matches of length 3..16/32/64 and distance up to 4095,
 * distance 0 ends the block; or 0x0f + raw bytes), sections starting on 512-byte boundaries. Their
 * physical addresses aren't in this file (only the page contents), so the output is just the pages in order.
 *
 *   cc -O2 -o unwbi unwbi.c && ./unwbi nflasha5 0x40000 pages.bin index.txt
 *
 * In it, the kernel's timekeeping (Linux 3.0.27; xtime is 0x4041eb60 + 0x40, found from get_seconds) sits at
 * pages.bin offset 0x302ba0: xtime 65.553302003 s, wall_to_monotonic 0, total_sleep_time 0.008691999 s, and
 * jiffies_64 (0x403ce240, at 0x2b2240) 0xffffa46c = INITIAL_JIFFIES + 6556 at HZ 100: the snapshot was taken
 * 65.56 s after that boot started.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const int LENS[16] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};
int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        fputs("usage: unwbi image start out.bin index.txt\n"
              "Unpacks the camera's boot snapshot (flash partition 5, nflasha5: \"WBI1\") into its 4 KB memory pages:\n"
              "  image    the partition image (nflasha5)\n"
              "  start    where the sections begin (0x40000)\n"
              "  out.bin  the pages, in order (their physical addresses aren't in the image)\n"
              "  index.txt  one line per section\n"
              "Example: ./unwbi nflasha5 0x40000 pages.bin index.txt (the comment at the top of unwbi.c has more).\n"
              "\n"
              "This help text was written by an LLM\n", stdout);
        return 0;
    }
    if (argc != 5) { fprintf(stderr, "usage: unwbi image start out.bin index.txt (see --help)\n"); return 1; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    unsigned char *d = malloc(n + 8);
    if (fread(d, 1, n, f) != (size_t)n) { perror("read"); return 1; }
    fclose(f);
    long i = strtol(argv[2], 0, 0), blk = 0;
    FILE *o = fopen(argv[3], "wb"), *ix = fopen(argv[4], "w");
    unsigned char out[65536];
    while (i < n) {
        long st = i; int L = 0;
        if (d[i] == 0x0f) {                                  /* stored */
            L = d[i + 2] | d[i + 3] << 8;
            for (int k = 0; k < L; k++) out[k] = d[i + 4 + k];
            i += 4 + L;
        } else if (d[i] == 0xf0) {                           /* compressed */
            i++; int done = 0;
            while (!done) {
                int fl = d[i++];
                for (int b = 0; b < 8 && !done; b++) {
                    if (fl >> b & 1) {
                        int b0 = d[i], b1 = d[i + 1]; i += 2;
                        int m = LENS[b0 >> 4], dist = (b0 & 15) << 8 | b1;
                        if (!dist) { done = 1; break; }
                        for (int k = 0; k < m && L < (int)sizeof out; k++) { out[L] = out[L - dist]; L++; }
                    } else out[L++] = d[i++];
                }
            }
        } else if (d[i] == 0) {                              /* padding: the next section */
            long j = (i + 0x1ff) & ~0x1ffL;
            while (j < n && !d[j]) j += 0x200;
            if (j >= n) break;
            fprintf(ix, "section at %lx\n", j); i = j; continue;
        } else break;                                        /* 0xff: erased flash, the end */
        fwrite(out, 1, L, o);
        fprintf(ix, "%ld %lx %d\n", blk++, st, L);
    }
    fclose(o); fclose(ix);
    fprintf(stderr, "%ld pages\n", blk);
    return 0;
}
