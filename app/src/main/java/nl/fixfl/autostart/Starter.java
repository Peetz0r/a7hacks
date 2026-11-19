package nl.fixfl.autostart;

import com.github.ma1co.openmemories.tweak.NativeException;
import com.github.ma1co.openmemories.tweak.Shell;

public class Starter {
    // Paths as seen from the camera's Linux side (outside the Android chroot).
    static final String HAX = "/android/storage/sdcard0/HAX";

    // Runs in the background as root: wait for the SD card, install the helper, start the watcher once.
    // The root exec call (shell_exec_async) rejects commands longer than 255 characters, so this stays
    // short: wait for the SD card, then hand over to HAX/fixflrun.sh, which does the real work.
    static final String SCRIPT =
        "(i=0;while [ ! -f " + HAX + "/fixflrun.sh ]&&[ $i -lt 90 ];do sleep 2;i=$((i+1));done;" +
        "sh " + HAX + "/fixflrun.sh)>/dev/null 2>&1 &";

    // Diagnostic: append "<uptime> <event>" to HAX/fixboot.log so we can see which broadcasts really arrive.
    public static void log(String event) throws NativeException {
        String e = event == null ? "?" : event.replaceAll("[^A-Za-z0-9_.]", "");
        Shell.exec("echo \"$(cat /proc/uptime) " + e + "\" >> " + HAX + "/fixboot.log");
    }

    public static void start() throws NativeException {
        if (SCRIPT.length() > 255)
            throw new NativeException("command too long");
        Shell.exec(SCRIPT);
    }
}
