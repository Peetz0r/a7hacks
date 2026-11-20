package nl.fixfl.autostart;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Map;

/**
 * Lens profiles, keyed by the SteadyShot focal length in mm. INI-style text file, lenses.ini in the root of the SD card:
 *
 *   # lines starting with # are ignored
 *   [135]
 *   model = Yashica ML 135mm f/2.8
 *   make = Yashica
 *   aperture = 2.8
 *   crop = 1.0
 *
 * model -> LensModel, make -> LensMake, aperture -> MaxApertureValue and LensSpecification,
 * crop -> FocalLengthIn35mmFilm = mm * crop (default 1.0, the A7 II is full frame). All keys are optional.
 */
public final class LensProfiles {
    public static final class Profile {
        public int mm;
        public String model, make;
        public double aperture = 0, crop = 1.0;
        public boolean isEmpty() { return model == null && make == null && aperture <= 0; }
    }

    private LensProfiles() {}

    public static Map<Integer, Profile> parse(String text) {
        Map<Integer, Profile> map = new HashMap<Integer, Profile>();
        Profile cur = null;
        for (String raw : text.split("\\r?\\n")) {
            String line = raw.trim();
            if (line.length() == 0 || line.startsWith("#")) continue;
            if (line.startsWith("[") && line.endsWith("]")) {
                cur = null;
                try {
                    int mm = Integer.parseInt(line.substring(1, line.length() - 1).trim());
                    if (mm >= 1 && mm <= 9999) { cur = new Profile(); cur.mm = mm; map.put(mm, cur); }
                } catch (NumberFormatException e) { /* ignore this section */ }
                continue;
            }
            int eq = line.indexOf('=');
            if (cur == null || eq < 0) continue;
            String key = line.substring(0, eq).trim().toLowerCase();
            String val = line.substring(eq + 1).trim();
            if (val.length() == 0) continue;
            try {
                if (key.equals("model")) cur.model = clip(val);
                else if (key.equals("make")) cur.make = clip(val);
                else if (key.equals("aperture")) cur.aperture = Double.parseDouble(val.replace("f/", "").trim());
                else if (key.equals("crop")) cur.crop = Double.parseDouble(val);
            } catch (NumberFormatException e) { /* ignore this line */ }
        }
        return map;
    }

    /** All profiles from the first of these files that exists, sorted by focal length; empty if none. */
    public static java.util.TreeMap<Integer, Profile> loadAll(File[] files) {
        java.util.TreeMap<Integer, Profile> out = new java.util.TreeMap<Integer, Profile>();
        if (files == null) return out;
        for (File f : files) {
            if (f == null || !f.isFile()) continue;
            try {
                BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
                try {
                    StringBuilder sb = new StringBuilder();
                    String l;
                    while ((l = r.readLine()) != null && sb.length() < 100000) sb.append(l).append('\n');
                    out.putAll(parse(sb.toString()));
                } finally {
                    r.close();
                }
            } catch (IOException e) { /* show what we have */ }
            break;
        }
        return out;
    }

    private static String clip(String s) { return s.length() > 120 ? s.substring(0, 120) : s; }

    /** Like lookup(File, int), using the first of these files that exists (e.g. card root, then HAX/). */
    public static Profile lookup(File[] files, int mm) {
        if (files == null) return null;
        for (File f : files) if (f != null && f.isFile()) return lookup(f, mm);
        return null;
    }

    /** The profile for this focal length from the given file, or null (no file, no such section, unreadable). */
    public static Profile lookup(File file, int mm) {
        if (file == null || !file.isFile()) return null;
        try {
            BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(file), "UTF-8"));
            try {
                StringBuilder sb = new StringBuilder();
                String l;
                while ((l = r.readLine()) != null && sb.length() < 100000) sb.append(l).append('\n');
                Profile p = parse(sb.toString()).get(mm);
                return p != null && !p.isEmpty() ? p : null;
            } finally {
                r.close();
            }
        } catch (IOException e) {
            return null;
        }
    }
}
