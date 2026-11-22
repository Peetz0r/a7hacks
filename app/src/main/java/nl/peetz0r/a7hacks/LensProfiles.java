package nl.peetz0r.a7hacks;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeMap;

/**
 * Lens profiles, keyed by the SteadyShot focal length in mm. INI-style text file, a7hacks.ini in the root of the SD card:
 *
 *   # lines starting with # are ignored
 *   [135]
 *   model = Yashica ML 135mm f/2.8
 *   aperture = 2.8
 *
 * model -> EXIF LensModel (at most 64 bytes), aperture -> MaxApertureValue. Both are optional; the focal length
 * itself always comes from the section name. Other keys are ignored.
 *
 * Options go in a [config] section, each on (the default) or off:
 *
 *   [config]
 *   font = on           # the menu font: Comic Relief (off: the camera's own)
 *   icons = on          # lettering inside the icons and the camera's pictures (AWB, MF, ISO, focal lengths,
 *                       # page numbers, ...) in Comic Relief, with font = on; changing either takes effect at
 *                       # the next start of the camera
 */
public final class LensProfiles {
    public static final class Profile {
        public int mm;
        public String model;
        public double aperture = 0;
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
                if (key.equals("model")) cur.model = val.length() > 120 ? val.substring(0, 120) : val;
                else if (key.equals("aperture")) {
                    double a = Double.parseDouble(val.replace("f/", "").trim());
                    if (a > 0 && a < 1000) cur.aperture = a;
                }
            } catch (NumberFormatException e) { /* ignore this line */ }
        }
        return map;
    }

    /** The keys of the [config] section (lower case), or an empty map. */
    public static Map<String, String> parseOptions(String text) {
        Map<String, String> map = new HashMap<String, String>();
        boolean in = false;
        for (String raw : text.split("\\r?\\n")) {
            String line = raw.trim();
            if (line.length() == 0 || line.startsWith("#")) continue;
            if (line.startsWith("[") && line.endsWith("]")) {
                in = line.substring(1, line.length() - 1).trim().equalsIgnoreCase("config");
                continue;
            }
            int eq = line.indexOf('=');
            if (!in || eq < 0) continue;
            String val = line.substring(eq + 1).trim();
            int hash = val.indexOf('#');
            if (hash >= 0) val = val.substring(0, hash).trim();
            map.put(line.substring(0, eq).trim().toLowerCase(), val);
        }
        return map;
    }

    /** The [config] options from the first of these files that exists; empty if none. */
    public static Map<String, String> loadOptions(File[] files) {
        String text = readFirst(files);
        return text == null ? new HashMap<String, String>() : parseOptions(text);
    }

    private static String readFirst(File[] files) {
        if (files == null) return null;
        for (File f : files) {
            if (f == null || !f.isFile()) continue;
            try {
                BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
                try {
                    StringBuilder sb = new StringBuilder();
                    String l;
                    while ((l = r.readLine()) != null && sb.length() < 100000) sb.append(l).append('\n');
                    return sb.toString();
                } finally {
                    r.close();
                }
            } catch (IOException e) {
                return null;
            }
        }
        return null;
    }

    /** All profiles from the first of these files that exists, sorted by focal length; empty if none. */
    public static TreeMap<Integer, Profile> loadAll(File[] files) {
        TreeMap<Integer, Profile> out = new TreeMap<Integer, Profile>();
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
}
