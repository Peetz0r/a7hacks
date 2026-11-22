package nl.peetz0r.a7hacks;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * The camera's SteadyShot focal length, as sampled once a second by the root loop (see Starter).
 * Pure Java (no Android classes) so it can be tested on a PC.
 */
public final class SteadyShot implements LensWriter.Setting {
    private final File file;
    private final long maxAgeMs;

    public SteadyShot(File file, long maxAgeMs) { this.file = file; this.maxAgeMs = maxAgeMs; }

    /** True if the sampler wrote the file recently. */
    public boolean isFresh() {
        long m = file.lastModified();
        return m != 0 && Math.abs(System.currentTimeMillis() - m) <= maxAgeMs;
    }

    /** For the log: why the setting can't be read. */
    public String describe() {
        return file + " exists=" + file.exists() + " canRead=" + file.canRead() + " length=" + file.length()
            + " ageMs=" + (file.lastModified() == 0 ? -1 : System.currentTimeMillis() - file.lastModified());
    }

    /** The focal length in mm, or -1 if it cannot be read right now. */
    public int read() {
        if (!isFresh()) return -1;
        try {
            FileInputStream in = new FileInputStream(file);
            byte[] b = new byte[256];
            int n;
            try { n = in.read(b); } finally { in.close(); }
            if (n <= 0) return -1;
            return parse(new String(b, 0, n, "UTF-8"));
        } catch (IOException e) {
            return -1;
        }
    }

    /** Parses bk.elf output: "id:010713ca size:2 data:" then "1C,00," (low byte first). Returns mm or -1. */
    public static int parse(String out) {
        String[] words = out.trim().split("\\s+");
        if (words.length == 0) return -1;
        String last = words[words.length - 1];
        String[] parts = last.split(",");
        if (parts.length < 2) return -1;
        String lo = parts[0], hi = parts[1];
        if (!lo.matches("[0-9A-Fa-f]{2}") || !hi.matches("[0-9A-Fa-f]{2}")) return -1;
        int v = Integer.parseInt(hi + lo, 16);
        return v > 0 ? v : -1;
    }
}
