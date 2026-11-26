package nl.peetz0r.a7hacks;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

/**
 * Checks the app's tzdata reader (TzData.java) against zdump, which reads the host's compiled zoneinfo: for every
 * zone, the UTC offset just before and just after each transition zdump lists (years FROM..TO), and halfway
 * between them. Both should be the same tzdata version (the host's: rpm -q tzdata). Run from the repo:
 *
 *   javac -d /tmp/tzcheck app/src/main/java/nl/peetz0r/a7hacks/TzData.java tools/TzCheck.java
 *   java -cp /tmp/tzcheck nl.peetz0r.a7hacks.TzCheck app/src/main/assets/tzdata2026f.tar.gz [1970 2040]
 *
 * Prints each difference and a count; exit status 1 if there were any.
 */
public final class TzCheck {
    public static void main(String[] a) throws Exception {
        TzData tz = TzData.read(new FileInputStream(a[0]));
        String from = a.length > 2 ? a[1] : "1970", to = a.length > 2 ? a[2] : "2040";
        System.out.println("tzdata " + tz.version);
        int zones = 0, checks = 0, bad = 0;
        List<String> names = tz.names();
        for (int i = 0; i < names.size(); i += 50) {
            List<String> cmd = new ArrayList<String>();
            cmd.add("zdump"); cmd.add("-v"); cmd.add("-c"); cmd.add(from + "," + to);
            cmd.addAll(names.subList(i, Math.min(names.size(), i + 50)));
            Process p = new ProcessBuilder(cmd).redirectErrorStream(true).start();
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), "UTF-8"));
            String l, zone = null;
            long prevT = Long.MIN_VALUE; int prevOff = 0;
            while ((l = r.readLine()) != null) {
                // Europe/Amsterdam  Sun Mar 29 00:59:59 2026 UT = Sun Mar 29 01:59:59 2026 CET isdst=0 gmtoff=3600
                String[] f = l.trim().split("\\s+");
                if (f.length < 16 || !f[6].equals("UT") || !f[f.length - 1].startsWith("gmtoff=")) continue;
                if (!f[0].equals(zone)) { zone = f[0]; zones++; prevT = Long.MIN_VALUE; }
                if (Long.parseLong(f[5]) < 1800) continue;      // zdump's first lines, far in the past
                long t = utc(f[2], Integer.parseInt(f[3]), f[4], Long.parseLong(f[5]));
                int off = Integer.parseInt(f[f.length - 1].substring(7));
                checks++;
                int got = tz.offset(zone, t);
                if (got != off) { bad++; System.out.println(zone + " " + l.trim() + ": we say " + got); }
                if (prevT != Long.MIN_VALUE && t - prevT > 2) {
                    long mid = prevT + (t - prevT) / 2;
                    checks++;
                    got = tz.offset(zone, mid);
                    if (got != prevOff) { bad++; System.out.println(zone + " halfway after " + prevT + ": " + prevOff + ", we say " + got); }
                }
                prevT = t; prevOff = off;
            }
            p.waitFor();
        }
        System.out.println(zones + " zones, " + checks + " checks, " + bad + " different");
        System.exit(bad == 0 ? 0 : 1);
    }

    private static final String MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";

    private static long utc(String mon, int d, String hms, long y) {
        int m = MONTHS.indexOf(mon) / 3 + 1;
        String[] p = hms.split(":");
        return TzData.days(y, m, d) * 86400L + Integer.parseInt(p[0]) * 3600 + Integer.parseInt(p[1]) * 60 + Integer.parseInt(p[2]);
    }
}
