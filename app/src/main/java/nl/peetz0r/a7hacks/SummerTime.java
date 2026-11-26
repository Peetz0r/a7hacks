package nl.peetz0r.a7hacks;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;

/**
 * Summer time: the camera's own setting (Date/Time Setup, "Daylight Savings") is a switch that its owner has to
 * flip twice a year; the camera knows no rules. With "timezone = Europe/Amsterdam" (any IANA name) in A7HACKS.INI
 * [config], this looks once a minute whether that switch is as the time zone's rules say, and flips it when it
 * isn't: so twice a year, and otherwise never. It does that through Sony's own library (TimeUtil, the one
 * Sony's apps use), which sets the camera's time zone the way its menu does; that is a camera setting, so it
 * stays after a restart like any other.
 *
 * The rules come from IANA's tzdata: the release this app was built with (assets/tzdataYYYYx.tar.gz, IANA's
 * tarball as is; the APK holds it un-gzipped), or a newer one from the root of the SD card: IANA's tarball renamed
 * to an upper case 8.3 name starting with TZ (TZDATA.TGZ, TZ2027A.GZ, or un-gzipped TZDATA.TAR), or a zic input
 * file such as tzdata.zi (TZDATA.ZI). The newest one wins, the built-in one on a tie. The card is looked at again
 * with every check: when its TZ files have changed (another card, a new file), the tzdata is read again.
 *
 * Only the switch: the area (the time zone's standard offset) stays the owner's. If the zone is ahead of UTC by
 * something the switch can't make (the area is off, or the zone's summer time isn't one hour), it says so and
 * leaves the camera alone.
 */
final class SummerTime {
    /** For the info dialog: the tzdata in use, "2026f (built in)" or "2027a (TZDATA.TGZ)"; "not used" without a
     *  timezone in A7HACKS.INI; null until it is read. */
    static volatile String inUse;

    private final Context ctx;
    private String zone;                 // from A7HACKS.INI; null: leave the camera alone
    private TzData tz;
    private String source;               // "built in", "TZDATA.TGZ on the SD card"
    private boolean stopped;
    private String logged;               // the last state logged, so each is logged once
    private String cardSeen;             // the card's tzdata files (names, sizes, times) when the tzdata was read
    private Integer staleOffset;         // after a switch: the clock's old offset, until the helper reports anew
    private long switchedAt;
    private int mismatches;              // ticks in a row on which Sony's numbers and the clock's offset disagreed

    SummerTime(Context ctx) { this.ctx = ctx; }

    /** From A7HACKS.INI (again after a restart): the zone's name, or null. Reads the tzdata again too. */
    synchronized void configure(String name) {
        zone = name == null || name.length() == 0 ? null : name;
        tz = null;
        source = null;
        stopped = false;
        logged = null;
        staleOffset = null;
        mismatches = 0;
        inUse = zone == null ? "not used" : null;
        if (zone == null) AppLog.add("summer time: left alone (no timezone in A7HACKS.INI)");
    }

    /**
     * Reads the tzdata if it hasn't been yet, or again when the card's TZ files have changed (another card, a new
     * file); without a card it keeps what it has. Right at the start, when a card goes in, and with every tick.
     */
    synchronized void card() {
        if (zone != null && !stopped) readTzdata();
    }

    private boolean readTzdata() {
        String card = cardFiles();
        if (tz != null && card != null && !card.equals(cardSeen)) {
            tz = null;
            logged = null;                   // say again which tzdata it is
        }
        if (tz != null) return true;
        if (!load()) {
            stopped = true;
            inUse = "none";
            return false;
        }
        AppLog.add("summer time: tzdata " + inUse);
        return true;
    }

    /**
     * Once a minute, while the clock is known: offset is the camera's own time zone plus summer time (minutes, as
     * the helper's --set-clock reports it, which may be up to a minute old). Returns true when the caller should
     * have the clock read again and call again soon: after a switch, or when the offset looks stale.
     */
    synchronized boolean tick(int offset) {
        if (zone == null || stopped) return false;
        if (staleOffset != null) {
            if (offset == staleOffset && System.currentTimeMillis() - switchedAt < 300000) return false;
            staleOffset = null;
        }
        if (!readTzdata()) return false;
        String z = tz.resolve(zone);
        if (z == null) {
            AppLog.add("summer time: there is no time zone \"" + zone + "\" in tzdata " + tz.version
                + " (a name like Europe/Amsterdam)");
            stopped = true;
            return false;
        }
        int[] cam;
        try {
            cam = Sony.get();
        } catch (Throwable t) {
            AppLog.add("summer time: can't read the camera's time zone (" + t + ")");
            stopped = true;
            return false;
        }
        // the camera's two numbers must be the kind its menu sets, and add up to what its clock driver says; when
        // they don't, the clock's offset may just be older than a change in the menu: have it read again, and only
        // give up if they still disagree
        if ((cam[1] != 0 && cam[1] != 60) || cam[0] < -12 * 60 || cam[0] > 14 * 60) {
            AppLog.add("summer time: the camera's time zone reads " + cam[0] + " + " + cam[1] + " minutes: not touching it");
            stopped = true;
            return false;
        }
        if (cam[0] + cam[1] != offset) {
            if (++mismatches < 3) return true;
            AppLog.add("summer time: the camera's time zone reads " + cam[0] + " + " + cam[1] + " minutes, its clock "
                + offset + ": not touching it");
            stopped = true;
            return false;
        }
        mismatches = 0;
        int now = tz.offset(z, System.currentTimeMillis() / 1000) / 60;
        int want = now - cam[0];
        String name = zone.equals(z) ? zone : zone + " (" + z + ")";
        if (want == cam[1]) {
            once("ok " + now, "summer time: " + name + " is " + utc(now) + " now (tzdata " + tz.version + ", " + source
                + "), and the camera's summer time is " + (want == 0 ? "off" : "on") + ", as it should be");
            return false;
        }
        if (want != 0 && want != 60) {
            once("area " + now + " " + cam[0], "summer time: " + name + " is " + utc(now) + " now, the camera's area "
                + utc(cam[0]) + ": summer time can't make up for that, so it stays " + (cam[1] == 0 ? "off" : "on")
                + " (set the area in the camera's menu)");
            return false;
        }
        int r;
        int[] after;
        try {
            r = Sony.set(cam[0], want);
            after = Sony.get();
        } catch (Throwable t) {
            AppLog.add("summer time: switching it " + (want == 0 ? "off" : "on") + " failed (" + t + ")");
            stopped = true;
            return false;
        }
        if (r != 0 || after[0] != cam[0] || after[1] != want) {
            AppLog.add("summer time: switching it " + (want == 0 ? "off" : "on") + " didn't take (answer " + r
                + ", now " + after[0] + " + " + after[1] + " minutes); leaving it");
            stopped = true;
            return false;
        }
        AppLog.add("summer time switched " + (want == 0 ? "off" : "on") + ": " + name + " is " + utc(now) + " now (tzdata "
            + tz.version + ", " + source + ")");
        logged = "ok " + now;
        staleOffset = offset;
        switchedAt = System.currentTimeMillis();
        return true;
    }

    private void once(String key, String line) {
        if (key.equals(logged)) return;
        logged = key;
        AppLog.add(line);
    }

    static String utc(int minutes) {
        int a = Math.abs(minutes);
        return "UTC" + (minutes < 0 ? "-" : "+") + a / 60 + ":" + (a % 60 < 10 ? "0" : "") + a % 60;
    }

    // ---- the tzdata: built in, or newer from the SD card ----

    private boolean load() {
        String builtIn = null;
        try {
            for (String n : ctx.getAssets().list(""))
                if (n.startsWith("tzdata") && (n.endsWith(".tar") || n.endsWith(".tar.gz"))      // the build un-gzips it
                        && (builtIn == null || n.compareTo(builtIn) > 0))
                    builtIn = n;
        } catch (IOException e) { /* none */ }
        File best = null;
        String bestV = null;
        if (builtIn != null) {
            try {
                bestV = peek(ctx.getAssets().open(builtIn));
            } catch (IOException e) {
                AppLog.add("summer time: the built-in tzdata can't be read (" + e.getMessage() + ")");
            }
        }
        File sd = Starter.sdRoot();
        String[] names = sd == null ? null : sd.list();
        cardSeen = cardFiles(sd, names);
        if (names != null) {
            for (String n : names) {
                String u = n.toUpperCase();
                if (!isTzName(u)) continue;
                if (!n.equals(u)) {
                    AppLog.add("summer time: " + n + " is on the SD card in lower case, which the camera can't open:"
                        + " rename it to " + u + " on a PC");
                    continue;
                }
                File f = new File(sd, n);
                String v;
                try {
                    v = peek(new FileInputStream(f));
                } catch (IOException e) {
                    AppLog.add("summer time: " + n + " on the SD card can't be read (" + e.getMessage() + ")");
                    continue;
                }
                if (v == null) {
                    AppLog.add("summer time: " + n + " on the SD card doesn't say which tzdata it is: skipped");
                    continue;
                }
                if (TzData.newer(v, bestV)) { best = f; bestV = v; }
            }
        }
        if (best != null) {
            try {
                InputStream in = new FileInputStream(best);
                try {
                    tz = TzData.read(in);
                } finally {
                    in.close();
                }
                source = best.getName() + " on the SD card";
                inUse = tz.version + " (" + best.getName() + ")";
                return true;
            } catch (IOException e) {
                AppLog.add("summer time: " + best.getName() + " on the SD card: " + e.getMessage()
                    + (builtIn != null ? "; using the built-in tzdata" : ""));
            }
        }
        if (builtIn == null) {
            AppLog.add("summer time: no tzdata");
            return false;
        }
        try {
            InputStream in = ctx.getAssets().open(builtIn);
            try {
                tz = TzData.read(in);
            } finally {
                in.close();
            }
            source = "built in";
            inUse = tz.version + " (built in)";
            return true;
        } catch (IOException e) {
            AppLog.add("summer time: the built-in tzdata: " + e.getMessage());
            return false;
        }
    }

    /** The card's tzdata files as "NAME size time;..." (any case, so a rename shows too); null without a card. */
    private static String cardFiles() {
        File sd = Starter.sdRoot();
        return sd == null ? null : cardFiles(sd, sd.list());
    }

    private static String cardFiles(File sd, String[] names) {
        if (sd == null || names == null) return null;
        java.util.Arrays.sort(names);
        StringBuilder b = new StringBuilder();
        for (String n : names)
            if (isTzName(n.toUpperCase())) {
                File f = new File(sd, n);
                b.append(n).append(' ').append(f.length()).append(' ').append(f.lastModified()).append(';');
            }
        return b.toString();
    }

    private static boolean isTzName(String u) {
        return u.startsWith("TZ") && (u.endsWith(".TGZ") || u.endsWith(".GZ") || u.endsWith(".TAR") || u.endsWith(".ZI"));
    }

    private static String peek(InputStream in) throws IOException {
        try {
            return TzData.peekVersion(in);
        } finally {
            in.close();
        }
    }

    // ---- Sony's TimeUtil (com.sony.scalar.sysutil.TimeUtil, in the firmware; reached by reflection) ----

    private static final class Sony {
        /** {gmtDiff, summerTimeDiff}: the area's offset and the summer time, in minutes. */
        static int[] get() throws Exception {
            Class<?> c = Class.forName("com.sony.scalar.sysutil.TimeUtil");
            Object z = c.getMethod("getCurrentTimeZone").invoke(null);
            if (z == null) throw new IOException("no answer");
            return new int[] {field(z, "gmtDiff").getInt(z), field(z, "summerTimeDiff").getInt(z)};
        }

        /** Sets the time zone only (no calendar: the clock keeps its UTC); 0 is OK. */
        static int set(int gmtDiff, int summerTimeDiff) throws Exception {
            Class<?> c = Class.forName("com.sony.scalar.sysutil.TimeUtil");
            Class<?> cal = Class.forName("com.sony.scalar.sysutil.PlainCalendar");
            Class<?> pz = Class.forName("com.sony.scalar.sysutil.PlainTimeZone");
            Object z = pz.newInstance();
            field(z, "gmtDiff").setInt(z, gmtDiff);
            field(z, "summerTimeDiff").setInt(z, summerTimeDiff);
            Object r = c.getMethod("setCurrentTime", cal, pz).invoke(c.newInstance(), null, z);
            return ((Integer) r).intValue();
        }

        private static Field field(Object o, String name) throws NoSuchFieldException {
            return o.getClass().getField(name);
        }
    }
}
