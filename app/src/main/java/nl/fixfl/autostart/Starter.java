package nl.fixfl.autostart;

import android.os.Environment;

import com.github.ma1co.openmemories.tweak.NativeException;
import com.github.ma1co.openmemories.tweak.Shell;

import java.io.File;

public class Starter {
    // The camera's SteadyShot focal length (mm, u16 little endian) lives in the Backup store under this id.
    // Android apps can't read it, so a tiny root loop samples it into a file once a second.
    // The file goes on /dev, which is RAM (the Android side has no /tmp, and its /data is flash).
    // Root sees it as /android/dev/ss, the app (inside the Android chroot) as /dev/ss.
    // A pid file keeps a second loop from being started. The whole command must stay under 255 characters
    // (limit of the root exec call).
    static final File SAMPLE_FILE = new File("/dev/ss");
    static final String SAMPLER =
        "read p</android/dev/ss.p;if [ -n \"$p\" ]&&[ -d /proc/$p ];then :;else " +
        "(while :;do /usr/bin/bk.elf r 10713ca>/android/dev/ss.t 2>&1;mv /android/dev/ss.t /android/dev/ss;sleep 1;done)" +
        ">/dev/null 2>&1 &echo $!>/android/dev/ss.p;fi";

    public static void startSampler() throws NativeException {
        if (SAMPLER.length() > 255)
            throw new NativeException("command too long");
        Shell.exec(SAMPLER);
    }

    /** Stops the sampler loop and clears its files (the supervisor starts a new one). */
    public static void stopSampler() throws NativeException {
        Shell.exec("read p</android/dev/ss.p;[ -n \"$p\" ]&&kill $p;rm -f /android/dev/ss /android/dev/ss.t /android/dev/ss.p");
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

    // Files live in the root of the SD card. If the card refuses to create files there (some firmware
    // only lets apps write inside certain folders) the log falls back to HAX/ and says so.
    static volatile boolean rootWritable = true;

    static File logFile(File sd) {
        return rootWritable ? new File(sd, "fixfl.log") : new File(sd, "HAX/fixfl.log");
    }

    /** Candidate lens profile files, in order of preference. */
    static File[] lensFiles(File sd) {
        return new File[] {new File(sd, "lenses.ini"), new File(sd, "HAX/lenses.ini")};
    }

    /** The log file to show/read: whichever exists (root first). */
    static File existingLog(File sd) {
        File a = new File(sd, "fixfl.log");
        if (a.isFile()) return a;
        File b = new File(sd, "HAX/fixfl.log");
        return b.isFile() ? b : a;
    }
}
