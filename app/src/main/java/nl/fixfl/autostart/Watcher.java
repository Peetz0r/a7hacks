package nl.fixfl.autostart;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Watches the newest DCIM folder and fills in the focal length of new photos from the SteadyShot setting.
 * Pure Java (no Android classes) so it can be tested on a PC.
 */
public class Watcher {
    public interface Sink { void log(String line); }

    /** Returns the current SteadyShot focal length in mm, or -1 if it cannot be read right now. */
    public interface Setting { int read(); }

    private static class Pending {
        long lastSize = -1;
        final int mm;
        Pending(int mm) { this.mm = mm; }
    }

    private final File dcim;
    private final Setting setting;
    private final Sink sink;
    private final File[] profiles;     // optional lens profile files (lenses.ini), first existing one wins

    private final Set<String> seen = new HashSet<String>();
    private final Map<String, Pending> pending = new LinkedHashMap<String, Pending>();
    private File currentDir = null;
    private boolean baselined = false;
    private int prev = -1;
    private int lastLoggedBad = Integer.MIN_VALUE;
    private final SimpleDateFormat clock = new SimpleDateFormat("HH:mm:ss");


    public Watcher(File dcim, Setting setting, Sink sink) {
        this(dcim, setting, sink, (File[]) null);
    }

    public Watcher(File dcim, Setting setting, Sink sink, File profiles) {
        this(dcim, setting, sink, new File[] {profiles});
    }

    public Watcher(File dcim, Setting setting, Sink sink, File[] profiles) {
        this.dcim = dcim;
        this.setting = setting;
        this.sink = sink;
        this.profiles = profiles;
    }

    public void say(String msg) {
        sink.log("[" + clock.format(new Date()) + "] " + msg);
    }

    private static boolean isPhoto(String name) {
        String u = name.toUpperCase();
        return u.endsWith(".JPG") || u.endsWith(".ARW");
    }

    private File latestDir() {
        String[] names = dcim.list();
        if (names == null) return null;
        String best = null;
        for (String n : names) {
            if (n.toUpperCase().endsWith("MSDCF") && (best == null || n.compareTo(best) > 0)) best = n;
        }
        return best == null ? null : new File(dcim, best);
    }

    /** One polling step. Call once a second. */
    public void tick() {
        int now = setting.read();
        if (now <= 0) {
            if (lastLoggedBad != now) { say("could not read the SteadyShot setting"); lastLoggedBad = now; }
        } else {
            lastLoggedBad = Integer.MIN_VALUE;
        }

        File dir = latestDir();
        if (dir == null) { prev = now; return; }
        String[] listing = dir.list();
        if (listing == null) { prev = now; return; }

        if (!dir.equals(currentDir)) {
            currentDir = dir;
            if (!baselined) {
                // first look after (re)start: everything already there counts as handled
                for (String n : listing) if (isPhoto(n)) seen.add(new File(dir, n).getPath());
                baselined = true;
                say("watching " + dir.getPath() + " (" + seen.size() + " existing files ignored)");
                prev = now;
                return;
            }
            say("watching " + dir.getPath());
        }

        // new files, in name order. The setting is taken from one poll ago: the camera writes the file
        // after the shot, so a change right after the shot must not apply to it.
        List<String> fresh = new ArrayList<String>();
        for (String n : listing) {
            if (!isPhoto(n)) continue;
            String path = new File(dir, n).getPath();
            if (!seen.contains(path) && !pending.containsKey(path)) fresh.add(path);
        }
        Collections.sort(fresh);
        int mm = prev > 0 ? prev : now;
        for (String path : fresh) pending.put(path, new Pending(mm));

        // patch each file once its size stopped changing
        List<String> done = new ArrayList<String>();
        for (Map.Entry<String, Pending> en : pending.entrySet()) {
            String path = en.getKey();
            Pending p = en.getValue();
            long size = new File(path).length();
            if (size > 0 && size == p.lastSize) {
                if (p.mm <= 0) {
                    say(new File(path).getName() + ": setting unknown, skipped");
                } else {
                    LensProfiles.Profile prof = LensProfiles.lookup(profiles, p.mm);
                    ExifPatcher.Outcome o = ExifPatcher.patch(new File(path), p.mm, prof, false);
                    say(new File(path).getName() + ": " + p.mm + "mm"
                        + (prof != null && prof.model != null ? " (" + prof.model + ")" : "")
                        + " -> " + ExifPatcher.describe(o.code) + " (" + o.code + ")"
                        + (o.note.length() > 0 ? " [" + o.note + "]" : ""));
                }
                seen.add(path);
                done.add(path);
            } else {
                p.lastSize = size;
            }
        }
        for (String path : done) pending.remove(path);
        prev = now;
    }

    /** Reads the setting from the file written by the root sampler (see Starter). */
    public static class FileSetting implements Setting {
        private final File file;
        private final long maxAgeMs;
        public FileSetting(File file, long maxAgeMs) { this.file = file; this.maxAgeMs = maxAgeMs; }

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

    /** Runs tick() every second until the returned thread is interrupted. */
    public Thread start() {
        Thread t = new Thread("fixfl-watcher") {
            @Override
            public void run() {
                say("watcher started");
                while (!isInterrupted()) {
                    try {
                        tick();
                    } catch (Throwable e) {
                        say("error: " + e);
                    }
                    try { Thread.sleep(1000); } catch (InterruptedException ie) { return; }
                }
            }
        };
        t.setDaemon(true);
        t.start();
        return t;
    }
}
