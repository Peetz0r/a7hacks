package nl.fixfl.autostart;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.LinkedList;

/** The log shown in the app. Kept in memory only (nothing is written to the SD card). */
public final class AppLog {
    private static final int MAX_LINES = 300;
    private static final LinkedList<String> lines = new LinkedList<String>();
    private static final SimpleDateFormat clock = new SimpleDateFormat("HH:mm:ss");
    private static int version = 0;

    private AppLog() {}

    public static synchronized void add(String msg) {
        lines.add("[" + clock.format(new Date()) + "] " + msg);
        while (lines.size() > MAX_LINES) lines.removeFirst();
        version++;
        android.util.Log.i(StartService.TAG, msg);
    }

    /** Changes whenever a line is added. */
    public static synchronized int version() { return version; }

    public static synchronized String text() {
        StringBuilder sb = new StringBuilder();
        for (String l : lines) sb.append(l).append('\n');
        return sb.toString();
    }
}
