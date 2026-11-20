package nl.fixfl.autostart;

import android.content.Context;
import android.os.Environment;
import android.os.SystemClock;

import com.github.ma1co.openmemories.tweak.NativeException;
import com.github.ma1co.openmemories.tweak.Shell;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Random;

public class Starter {
    // The camera's SteadyShot focal length (mm, u16 little endian) lives in the Backup store under this id.
    // Android apps can't read it, so a tiny root loop samples it into a file once a second.
    // The file goes on /dev, which is RAM (the Android side has no /tmp, and its /data is flash).
    // Root sees it as /android/dev/a7hacks.ss, the app (inside the Android chroot) as /dev/a7hacks.ss.
    // A pid file (.p) keeps a second loop from being started. The whole command must stay under 255
    // characters (limit of the root exec call), hence the cd and the short relative names.
    static final File SAMPLE_FILE = new File("/dev/a7hacks.ss");
    static final String SAMPLER =
        "cd /android/dev;read p<a7hacks.ss.p;if [ -n \"$p\" ]&&[ -d /proc/$p ];then :;else " +
        "(while :;do /usr/bin/bk.elf r 10713ca>a7hacks.ss.t 2>&1;mv a7hacks.ss.t a7hacks.ss;sleep 1;done)" +
        ">/dev/null 2>&1 &echo $!>a7hacks.ss.p;fi";
    static final int MAX_COMMAND = 255;

    public static void startSampler() throws NativeException {
        if (SAMPLER.length() > MAX_COMMAND)
            throw new NativeException("command too long");
        Shell.exec(SAMPLER);
    }

    /** Stops the sampler loop and clears its files (the supervisor starts a new one). */
    public static void stopSampler() throws NativeException {
        Shell.exec("cd /android/dev;read p<a7hacks.ss.p;[ -n \"$p\" ]&&kill $p;rm -f a7hacks.ss a7hacks.ss.t a7hacks.ss.p");
    }

    /** Starts "tool option" as a root loop in the background, unless the one in the pid file
     *  /android/dev/a7hacks.KIND.p still runs. */
    private static void startLoop(String tool, String option, String kind) throws NativeException {
        String cmd = "cd /android/dev;read p<a7hacks." + kind + ".p;if [ -n \"$p\" ]&&[ -d /proc/$p ];then :;else "
            + tool + " " + option + " >/dev/null 2>&1 &echo $!>a7hacks." + kind + ".p;fi";
        if (cmd.length() > MAX_COMMAND) throw new NativeException("command too long");
        Shell.exec(cmd);
    }

    /** Stops the root loop in the pid file /android/dev/a7hacks.KIND.p (its changes stay in place). */
    private static void stopLoop(String kind) throws NativeException {
        Shell.exec("cd /android/dev;read p<a7hacks." + kind + ".p;[ -n \"$p\" ]&&kill $p;rm -f a7hacks." + kind + ".p");
    }

    /**
     * Exposure focal length: "liba7hacks.so --exposure-patcher" runs as a root loop next to the sampler. While no
     * electronic lens is attached it makes the camera's ISO AUTO minimum shutter speed use the SteadyShot focal
     * length (otherwise it falls back to 1/60), and it undoes that as soon as one is attached. Status in
     * /dev/a7hacks.ex.
     */
    static final File EXPOSURE_STATUS = new File("/dev/a7hacks.ex");

    static void startExposure(String tool) throws NativeException {
        startLoop(tool, "--exposure-patcher", "ex");
    }

    /** Stops the exposure loop (its change stays in place until a new loop or a reboot takes over). */
    static void stopExposure() throws NativeException {
        stopLoop("ex");
    }

    /** The exposure loop's last status line, or null. */
    static String exposureStatus() {
        return readLine(EXPOSURE_STATUS);
    }

    /**
     * Menu label: "liba7hacks.so --label-patcher" runs as a root loop too. It renames the English menu item
     * "SteadyS. Focal Len." to "Manual Focal Length" in the camera UI's memory (same length; a reboot puts the
     * original back). Status in /dev/a7hacks.ui: "on N" | "waiting MB" | "noui" | "error ...".
     */
    static final File LABEL_STATUS = new File("/dev/a7hacks.ui");

    static void startLabel(String tool) throws NativeException {
        startLoop(tool, "--label-patcher", "ui");
    }

    /** Stops the label loop (the rename stays; the supervisor starts a new loop, which finds it in place). */
    static void stopLabel() throws NativeException {
        stopLoop("ui");
    }

    /** The label loop's last status line, or null. */
    static String labelStatus() {
        return readLine(LABEL_STATUS);
    }

    /**
     * UI font: "liba7hacks.so --font-patcher" runs as a root loop as well. It writes Comic Relief outlines over
     * the Univers ones in the camera's cached copy of its UI font file (same layout, nothing written to flash)
     * and keeps them there, and marks the characters the UI had already drawn (and kept) as stale, so they get
     * drawn again. It does the same for the lettering inside the icons (Sony_DI_Icons.ttf) and in the camera's
     * pictures (the image_*.uxc files: focal lengths, page numbers, AWB, ISO, ...), and turns the orange accent
     * colour green (the pictures and color_cmn.uxc), unless icons is false ("--font-patcher --text-only"). The
     * labels the camera had already drawn and kept as pictures (menu items, page numbers, icons) are taken out
     * of its store, so they get drawn again in the new font when next shown. "--font-restore" puts the camera's
     * own back. Status in /dev/a7hacks.ft:
     * "on N [cM | nocache] [tK | nolabels] [pP | nopics] [aA] [noicons]" | "off [pics]" |
     * "partial N/M" | "off" | "badfile" | "error ...".
     */
    static final File FONT_STATUS = new File("/dev/a7hacks.ft");

    static void startFont(String tool, boolean icons) throws NativeException {
        startLoop(tool, icons ? "--font-patcher" : "--font-patcher --text-only", "ft");
    }

    /** Stops the font loop, leaving its changes in place (the supervisor starts a new loop, which finds them done
     *  and carries on). The loop holds a stop off while it writes, so nothing is left half written. */
    static void stopFont() throws NativeException {
        stopLoop("ft");
    }

    /** Stops the font loop and puts the camera's own font back. */
    static void fontOff(String tool) throws NativeException {
        String cmd = "cd /android/dev;read p<a7hacks.ft.p;[ -n \"$p\" ]&&kill $p;rm -f a7hacks.ft.p;"
            + tool + " --font-restore >/dev/null 2>&1";
        if (cmd.length() > MAX_COMMAND) throw new NativeException("command too long");
        Shell.exec(cmd);
    }

    /** The font loop's last status line, or null. */
    static String fontStatus() {
        return readLine(FONT_STATUS);
    }

    private static String readLine(File file) {
        try {
            FileInputStream in = new FileInputStream(file);
            try {
                byte[] b = new byte[100];
                int n = in.read(b);
                return n > 0 ? new String(b, 0, n, "UTF-8").trim() : null;
            } finally {
                in.close();
            }
        } catch (IOException e) {
            return null;
        }
    }

    /** Root of the SD card as the Android side sees it, or null while no card with a DCIM folder is there. */
    public static File sdRoot() {
        File[] candidates = {
            new File("/storage/sdcard0"), new File("/mnt/sdcard"), Environment.getExternalStorageDirectory()
        };
        for (File c : candidates)
            if (c != null && new File(c, "DCIM").isDirectory()) return c;
        return null;
    }

    /** Candidate lens profile files, in order of preference. */
    static File[] lensFiles(File sd) {
        return new File[] {new File(sd, "lenses.ini"), new File(sd, "HAX/lenses.ini")};
    }

    /**
     * Runs liba7hacks.so --lens-write / --lens-read (shipped as the app's "native library", so Android installs
     * it executable) as root. It answers in /android/dev/a7hacks.st (RAM), tagged with a request number.
     */
    static final class Tool implements LensWriter.Tool {
        private static final File STATUS = new File("/dev/a7hacks.st");     // /android/dev/a7hacks.st for root
        private static final int NUMBERS = 51;     // room for "--lens-write id 1 fnum fden anum aden mnum mden " and spaces
        private final String path;
        private int id = 1 + new Random().nextInt(100000);

        Tool(Context ctx) {
            path = "/android" + ctx.getApplicationInfo().nativeLibraryDir + "/liba7hacks.so";
        }

        String path() { return path; }

        public int nameBudget() {
            return Math.max(0, (MAX_COMMAND - path.length() - NUMBERS) / 2);
        }

        public String run(String args) throws Exception {
            int req = id++;
            String cmd = path + " --lens-write " + req + " " + args;
            if (cmd.length() > MAX_COMMAND) throw new Exception("command too long");
            Shell.exec(cmd);
            String tag = req + " ";
            long until = SystemClock.elapsedRealtime() + 3000;
            while (true) {
                String s = readStatus();
                if (s != null && s.startsWith(tag)) return s.substring(tag.length()).trim();
                if (SystemClock.elapsedRealtime() > until) return "error no answer from " + path;
                Thread.sleep(50);
            }
        }

        private static String readStatus() {
            try {
                FileInputStream in = new FileInputStream(STATUS);
                try {
                    byte[] b = new byte[600];
                    int n = in.read(b);
                    return n > 0 ? new String(b, 0, n, "UTF-8") : null;
                } finally {
                    in.close();
                }
            } catch (IOException e) {
                return null;
            }
        }
    }
}
