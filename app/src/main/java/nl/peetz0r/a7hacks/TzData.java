package nl.peetz0r.a7hacks;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.GZIPInputStream;

/**
 * IANA's time zone database (tzdata), read the way zic reads it: the Rule, Zone and Link lines of its source files.
 * Only what is needed to answer one question: how far from UTC is a zone at a given moment (standard time plus
 * summer time). Takes either IANA's own release tarball (tzdataYYYYx.tar.gz, as downloaded, or un-gzipped: the
 * Android build un-gzips a .gz asset, so the APK holds tzdataYYYYx.tar) or one zic input file,
 * such as the tzdata.zi a tz build makes (Linux distributions ship it as /usr/share/zoneinfo/tzdata.zi).
 * Keywords may be shortened as zic allows ("R", "Ja", "lastSu"), which is how tzdata.zi is written.
 */
final class TzData {
    /** The files of a tzdata tarball that zic reads by default (not backzone, which only changes the past). */
    private static final String[] PARTS = {"africa", "antarctica", "asia", "australasia", "europe", "northamerica",
        "southamerica", "etcetera", "backward"};
    private static final int MAX_FILE = 4 << 20, MAX_TOTAL = 16 << 20;

    private static final String[] MONTHS = {"January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December"};
    private static final String[] DAYS = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
        "Saturday"};

    /** "2026f", or "unknown" for a zic input file that doesn't say. */
    final String version;
    private final Map<String, List<Rule>> rules = new HashMap<String, List<Rule>>();
    private final Map<String, List<Era>> zones = new HashMap<String, List<Era>>();
    private final Map<String, String> links = new HashMap<String, String>();

    /** One Rule line. ON: kind 0 = day d, 1 = the last weekday w, 2 = the first w on or after d, 3 = the last w on
     *  or before d. AT in seconds, with zic's suffix: 'w' wall clock, 's' standard time, 'u' UTC. */
    private static final class Rule {
        int from, to, month, kind, d, w, at, save;
        char atType;
    }

    /** One Zone line or continuation line: standard offset, rules (a name, or a fixed save), until when. */
    private static final class Era {
        int stdoff;
        String rules;          // null: "-" or a fixed amount (save)
        int save;
        boolean hasUntil;
        int uy, um = 1, ukind, ud = 1, uw, uat;
        char uType = 'w';
    }

    private TzData(String version) { this.version = version; }

    /** What a stream holds: 'g' a gzipped tarball, 't' a tarball, 'z' anything else (a zic input file). */
    private static char kind(java.io.BufferedInputStream b) throws IOException {
        byte[] h = new byte[512];
        b.mark(h.length);
        int n = 0, k;
        while (n < h.length && (k = b.read(h, n, h.length - n)) > 0) n += k;
        b.reset();
        if (n >= 2 && (h[0] & 255) == 0x1f && (h[1] & 255) == 0x8b) return 'g';
        if (n == 512 && new String(h, 257, 5, "US-ASCII").equals("ustar")) return 't';
        return 'z';
    }

    /** Reads a tarball (gzipped or not) or a zic input file (text). */
    static TzData read(InputStream in) throws IOException {
        java.io.BufferedInputStream b = new java.io.BufferedInputStream(in);
        char k = kind(b);
        if (k != 'z') return readTarball(k == 'g' ? new GZIPInputStream(b) : b);
        String text = readText(b, MAX_FILE);
        String v = "unknown";
        BufferedReader r = new BufferedReader(new StringReader(text));
        for (int i = 0; i < 5; i++) {
            String l = r.readLine();
            if (l == null) break;
            if (l.startsWith("# version ")) { v = l.substring(10).trim(); break; }
        }
        TzData t = new TzData(v);
        t.parse("zic input", text);
        t.check();
        return t;
    }

    /** Only the version: the "version" file of a tarball, or the "# version" line of a zic input file; null if none. */
    static String peekVersion(InputStream in) throws IOException {
        java.io.BufferedInputStream b = new java.io.BufferedInputStream(in);
        char k = kind(b);
        if (k != 'z') {
            Map<String, String> files = untar(k == 'g' ? new GZIPInputStream(b) : b, true);
            String v = files.get("version");
            return v == null ? null : v.trim();
        }
        BufferedReader r = new BufferedReader(new InputStreamReader(b, "UTF-8"));
        for (int i = 0; i < 5; i++) {
            String l = r.readLine();
            if (l == null) break;
            if (l.startsWith("# version ")) return l.substring(10).trim();
        }
        return null;
    }

    private static TzData readTarball(InputStream in) throws IOException {
        Map<String, String> files = untar(in, false);
        String v = files.get("version");
        if (v == null || v.trim().length() == 0) throw new IOException("no version file: not a tzdata tarball?");
        TzData t = new TzData(v.trim());
        for (String p : PARTS) {
            String text = files.get(p);
            if (text == null) throw new IOException("no " + p + " in the tarball");
            t.parse(p, text);
        }
        t.check();
        return t;
    }

    /** The files we want from a tar stream, by name (without directories). versionOnly: stop at "version". */
    private static Map<String, String> untar(InputStream in, boolean versionOnly) throws IOException {
        Map<String, String> out = new HashMap<String, String>();
        byte[] h = new byte[512];
        long total = 0;
        while (true) {
            if (!readFully(in, h)) break;
            boolean zero = true;
            for (byte x : h) if (x != 0) { zero = false; break; }
            if (zero) break;
            int nl = 0;
            while (nl < 100 && h[nl] != 0) nl++;
            String name = new String(h, 0, nl, "UTF-8");
            long size = 0;
            for (int i = 124; i < 136 && h[i] != 0; i++)
                if (h[i] >= '0' && h[i] <= '7') size = size * 8 + (h[i] - '0');
                else if (h[i] != ' ') throw new IOException("bad tar header");
            char type = (char) h[156];
            if (size < 0 || size > MAX_FILE * 4L) throw new IOException("tar member too big");
            String base = name.substring(name.lastIndexOf('/') + 1);
            boolean want = (type == '0' || type == 0) && size <= MAX_FILE
                && (base.equals("version") || (!versionOnly && isPart(base)));
            long padded = (size + 511) & ~511L;
            if (want) {
                total += size;
                if (total > MAX_TOTAL) throw new IOException("tarball too big");
                byte[] data = new byte[(int) size];
                if (!readFully(in, data)) throw new IOException("tarball cut short");
                out.put(base, new String(data, "UTF-8"));
                skip(in, padded - size);
                if (versionOnly && base.equals("version")) break;
            } else skip(in, padded);
        }
        return out;
    }

    private static boolean isPart(String n) {
        for (String p : PARTS) if (p.equals(n)) return true;
        return false;
    }

    private static boolean readFully(InputStream in, byte[] b) throws IOException {
        int n = 0;
        while (n < b.length) {
            int k = in.read(b, n, b.length - n);
            if (k < 0) {
                if (n == 0) return false;
                throw new IOException("cut short");
            }
            n += k;
        }
        return true;
    }

    private static void skip(InputStream in, long n) throws IOException {
        byte[] b = new byte[4096];
        while (n > 0) {
            int k = in.read(b, 0, (int) Math.min(b.length, n));
            if (k < 0) throw new IOException("cut short");
            n -= k;
        }
    }

    private static String readText(InputStream in, int max) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        int k;
        while ((k = in.read(b)) > 0) {
            o.write(b, 0, k);
            if (o.size() > max) throw new IOException("file too big");
        }
        return o.toString("UTF-8");
    }

    // ---- parsing ----

    private void parse(String file, String text) throws IOException {
        String[] lines = text.split("\n");
        List<Era> cont = null;         // the zone whose last line had an UNTIL: the next line continues it
        for (int i = 0; i < lines.length; i++) {
            String l = lines[i];
            int hash = l.indexOf('#');
            if (hash >= 0) l = l.substring(0, hash);
            l = l.trim();
            if (l.length() == 0) continue;
            String[] f = l.split("[ \\t\\r\\f]+");
            try {
                if (cont != null) {
                    Era e = era(f, 0);
                    cont.add(e);
                    if (!e.hasUntil) cont = null;
                    continue;
                }
                int kw = word(f[0], new String[] {"Rule", "Zone", "Link"});
                if (kw == 0) {
                    if (f.length != 10) throw new IOException("a Rule line has 10 fields");
                    Rule r = rule(f);
                    List<Rule> list = rules.get(f[1]);
                    if (list == null) rules.put(f[1], list = new ArrayList<Rule>());
                    list.add(r);
                } else if (kw == 1) {
                    if (f.length < 5) throw new IOException("a Zone line has at least 5 fields");
                    if (zones.containsKey(f[1]) || links.containsKey(f[1])) throw new IOException("zone " + f[1] + " twice");
                    List<Era> list = new ArrayList<Era>();
                    Era e = era(f, 2);
                    list.add(e);
                    zones.put(f[1], list);
                    if (e.hasUntil) cont = list;
                } else {
                    if (f.length != 3) throw new IOException("a Link line has 3 fields");
                    if (zones.containsKey(f[2]) || links.containsKey(f[2])) throw new IOException("zone " + f[2] + " twice");
                    links.put(f[2], f[1]);
                }
            } catch (IOException e) {
                throw new IOException(file + " line " + (i + 1) + ": " + e.getMessage());
            } catch (RuntimeException e) {
                throw new IOException(file + " line " + (i + 1) + ": " + e);
            }
        }
        if (cont != null) throw new IOException(file + ": the last zone isn't finished");
    }

    /** Every link leads to a zone, every zone's rules exist. */
    private void check() throws IOException {
        if (zones.isEmpty()) throw new IOException("no zones");
        for (String n : links.keySet()) if (resolve(n) == null) throw new IOException("link " + n + " leads nowhere");
        for (Map.Entry<String, List<Era>> z : zones.entrySet())
            for (Era e : z.getValue())
                if (e.rules != null && !rules.containsKey(e.rules))
                    throw new IOException("zone " + z.getKey() + ": no rules " + e.rules);
    }

    private static Rule rule(String[] f) throws IOException {
        Rule r = new Rule();
        r.from = year(f[2], Integer.MIN_VALUE + 1);
        int t = f[3].length() > 0 && Character.isLetter(f[3].charAt(0)) ? word(f[3], new String[] {"only", "maximum"}) : -1;
        r.to = t == 0 ? r.from : t == 1 ? Integer.MAX_VALUE : year(f[3], 0);
        if (r.to < r.from) throw new IOException("TO before FROM");
        if (!f[4].equals("-")) throw new IOException("TYPE must be -");
        r.month = word(f[5], MONTHS) + 1;
        int[] on = on(f[6]);
        r.kind = on[0]; r.d = on[1]; r.w = on[2];
        r.atType = suffix(f[7], "wsugz");
        r.at = hms(strip(f[7], "wsugz"));
        if (r.atType == 'g' || r.atType == 'z') r.atType = 'u';
        r.save = hms(strip(f[8], "sd"));
        return r;
    }

    /** A zone line's fields from STDOFF on (at index i). */
    private static Era era(String[] f, int i) throws IOException {
        if (f.length < i + 3 || f.length > i + 7) throw new IOException("wrong number of fields");
        Era e = new Era();
        e.stdoff = hms(f[i]);
        String r = f[i + 1];
        if (r.equals("-")) e.rules = null;
        else if (Character.isDigit(r.charAt(0)) || (r.charAt(0) == '-' && r.length() > 1)) e.save = hms(strip(r, "sd"));
        else e.rules = r;
        if (f.length > i + 3) {
            e.hasUntil = true;
            e.uy = year(f[i + 3], 0);
            if (f.length > i + 4) e.um = word(f[i + 4], MONTHS) + 1;
            if (f.length > i + 5) { int[] on = on(f[i + 5]); e.ukind = on[0]; e.ud = on[1]; e.uw = on[2]; }
            if (f.length > i + 6) {
                e.uType = suffix(f[i + 6], "wsugz");
                if (e.uType == 'g' || e.uType == 'z') e.uType = 'u';
                e.uat = hms(strip(f[i + 6], "wsugz"));
            }
        }
        return e;
    }

    /** A year, or "minimum" / "maximum" (shortened as zic allows). */
    private static int year(String s, int min) throws IOException {
        if (Character.isLetter(s.charAt(0))) {
            int w = word(s, new String[] {"minimum", "maximum"});
            return w == 0 ? min : Integer.MAX_VALUE;
        }
        try {
            return Integer.parseInt(s);
        } catch (NumberFormatException e) {
            throw new IOException("bad year " + s);
        }
    }

    /** ON: {kind, day, weekday} */
    private static int[] on(String s) throws IOException {
        if (Character.isDigit(s.charAt(0))) return new int[] {0, num(s), 0};
        if (s.length() > 4 && s.substring(0, 4).equalsIgnoreCase("last")) return new int[] {1, 0, word(s.substring(4), DAYS)};
        int ge = s.indexOf(">="), le = s.indexOf("<=");
        int p = ge >= 0 ? ge : le;
        if (p <= 0) throw new IOException("bad day " + s);
        return new int[] {ge >= 0 ? 2 : 3, num(s.substring(p + 2)), word(s.substring(0, p), DAYS)};
    }

    private static int num(String s) throws IOException {
        try {
            return Integer.parseInt(s);
        } catch (NumberFormatException e) {
            throw new IOException("bad number " + s);
        }
    }

    /** The index of the one word s starts (case aside), as zic matches its keywords. */
    private static int word(String s, String[] words) throws IOException {
        int found = -1;
        for (int i = 0; i < words.length; i++) {
            if (words[i].equalsIgnoreCase(s)) return i;
            if (s.length() <= words[i].length() && words[i].substring(0, s.length()).equalsIgnoreCase(s)) {
                if (found >= 0) throw new IOException("\"" + s + "\" could be either " + words[found] + " or " + words[i]);
                found = i;
            }
        }
        if (found < 0) throw new IOException("unknown word " + s);
        return found;
    }

    private static char suffix(String s, String allowed) {
        char c = Character.toLowerCase(s.charAt(s.length() - 1));
        return allowed.indexOf(c) >= 0 && s.length() > 1 ? c : allowed.charAt(0);
    }

    private static String strip(String s, String allowed) {
        char c = Character.toLowerCase(s.charAt(s.length() - 1));
        return allowed.indexOf(c) >= 0 && s.length() > 1 ? s.substring(0, s.length() - 1) : s;
    }

    /** [-]h[:mm[:ss[.frac]]] in seconds; "-" is 0. */
    private static int hms(String s) throws IOException {
        if (s.equals("-")) return 0;
        boolean neg = s.startsWith("-");
        if (neg) s = s.substring(1);
        String[] p = s.split(":");
        if (p.length > 3 || p[0].length() == 0) throw new IOException("bad time " + s);
        int dot = p[p.length - 1].indexOf('.');
        if (dot >= 0) p[p.length - 1] = p[p.length - 1].substring(0, dot);
        int v = 0;
        for (int i = 0; i < 3; i++) {
            int x = i < p.length ? num(p[i]) : 0;
            if (x < 0 || (i > 0 && x > 59)) throw new IOException("bad time " + s);
            v = v * 60 + x;
        }
        return neg ? -v : v;
    }

    // ---- answering ----

    /** The zone a name (or a link, followed) stands for, matched without regard to case if need be; null if none. */
    String resolve(String name) {
        String n = name;
        for (int i = 0; i < 10 && n != null; i++) {
            if (zones.containsKey(n)) return n;
            n = links.get(n);
        }
        if (n != null) return null;
        for (String z : zones.keySet()) if (z.equalsIgnoreCase(name)) return z;
        for (String z : links.keySet()) if (z.equalsIgnoreCase(name)) return resolve(z);
        return null;
    }

    /** Every zone and link name. */
    List<String> names() {
        List<String> n = new ArrayList<String>(zones.keySet());
        n.addAll(links.keySet());
        Collections.sort(n);
        return n;
    }

    /** How far the zone is ahead of UTC at t (seconds since 1970, UTC), in seconds: standard time plus summer time. */
    int offset(String zone, long t) {
        List<Era> eras = zones.get(resolve(zone));
        if (eras == null) throw new IllegalArgumentException("no zone " + zone);
        long start = Long.MIN_VALUE;          // when the era began, as written (local), for save()
        for (Era e : eras) {
            if (e.hasUntil && t >= until(e)) {
                start = day(e.uy, e.um, e.ukind, e.ud, e.uw) * 86400L + e.uat;
                continue;
            }
            return e.stdoff + save(e, t, start);
        }
        throw new IllegalStateException("zone " + zone + " ends");    // check() makes sure the last era has no UNTIL
    }

    /** When an era ends, in UTC. A wall clock time counts with the summer time of the rule transitions before it,
     *  as zic does: a transition at that very moment (or later) is the next era's business. */
    private long until(Era e) {
        long local = day(e.uy, e.um, e.ukind, e.ud, e.uw) * 86400L + e.uat;
        if (e.uType == 'u') return local;
        long std = local - e.stdoff;
        if (e.uType == 's') return std;
        if (e.rules == null) return std - e.save;
        List<Rule> rs = rules.get(e.rules);
        int save = 0;
        for (long[] x : transitions(rs, e.uy)) {
            Rule r = rs.get((int) x[1]);
            long u = r.atType == 'u' ? x[0] : r.atType == 's' ? x[0] - e.stdoff : x[0] - e.stdoff - save;
            if (u >= std - save) break;
            save = r.save;
        }
        return std - save;
    }

    /** The summer time (seconds) in force under an era's rules at t: that of the latest transition at or before t.
     *  A transition written for the moment the era began (start, local as written), or before, is in force from its
     *  start, as zic has it (America/Indiana/Knox 2006 Apr 2 2:00: EST until then, CDT from then). */
    private int save(Era e, long t, long start) {
        if (e.rules == null) return e.save;
        List<Rule> rs = rules.get(e.rules);
        int save = 0;
        for (long[] x : transitions(rs, yearOf(t) + 1)) {
            Rule r = rs.get((int) x[1]);
            long u = r.atType == 'u' ? x[0] : r.atType == 's' ? x[0] - e.stdoff : x[0] - e.stdoff - save;
            if (u > t && x[0] > start) break;
            save = r.save;
        }
        return save;
    }

    /** A rule set's transitions up to the end of year last (at most 500 years back), in order: {local seconds as
     *  written (in the rule's own kind of time), index of the rule}. */
    private static List<long[]> transitions(List<Rule> rs, int last) {
        int first = Integer.MAX_VALUE;
        for (Rule r : rs) first = Math.min(first, r.from);
        first = Math.max(first, last - 500);
        List<long[]> ts = new ArrayList<long[]>();
        for (int i = 0; i < rs.size(); i++) {
            Rule r = rs.get(i);
            for (int y = Math.max(r.from, first); y <= Math.min(r.to, last); y++)
                ts.add(new long[] {day(y, r.month, r.kind, r.d, r.w) * 86400L + r.at, i});
        }
        Collections.sort(ts, new java.util.Comparator<long[]>() {
            public int compare(long[] a, long[] b) { return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0; }
        });
        return ts;
    }

    /** Days since 1970-01-01 (proleptic Gregorian), from year, month (1..12) and day (may run past the month). */
    static long days(long y, int m, int d) {
        y -= m <= 2 ? 1 : 0;
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400;
        long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    private static int weekday(long days) { return (int) (((days + 4) % 7 + 7) % 7); }     // 0 Sunday

    private static long day(int y, int m, int kind, int d, int w) {
        if (kind == 0) return days(y, m, d);
        if (kind == 1) {
            long l = (m == 12 ? days(y + 1, 1, 1) : days(y, m + 1, 1)) - 1;
            return l - (weekday(l) - w + 7) % 7;
        }
        long x = days(y, m, d);
        return kind == 2 ? x + (w - weekday(x) + 7) % 7 : x - (weekday(x) - w + 7) % 7;
    }

    /** The year t (seconds since 1970, UTC) falls in (the inverse of days(), as in Howard Hinnant's civil_from_days). */
    private static int yearOf(long t) {
        long z = (t >= 0 ? t / 86400 : -((-t + 86399) / 86400)) + 719468;
        long era = (z >= 0 ? z : z - 146096) / 146097;
        long doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        long y = yoe + era * 400 + (mp >= 10 ? 1 : 0);
        return (int) Math.max(Integer.MIN_VALUE / 2, Math.min(Integer.MAX_VALUE / 2, y));
    }

    /** Is a newer than b? "2027a" > "2026z", "2026za" > "2026z". */
    static boolean newer(String a, String b) {
        if (a == null) return false;
        if (b == null) return true;
        int ya = lead(a), yb = lead(b);
        if (ya != yb) return ya > yb;
        String la = a.substring(Math.min(a.length(), 4)), lb = b.substring(Math.min(b.length(), 4));
        if (la.length() != lb.length()) return la.length() > lb.length();
        return la.compareTo(lb) > 0;
    }

    private static int lead(String v) {
        try {
            return Integer.parseInt(v.substring(0, Math.min(v.length(), 4)));
        } catch (NumberFormatException e) {
            return -1;
        }
    }
}
