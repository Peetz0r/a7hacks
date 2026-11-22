package nl.peetz0r.a7hacks;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.LinkedList;
import java.util.SimpleTimeZone;
import java.util.TimeZone;

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

    /** The camera's time zone (minutes east of UTC, summer time included), for this app's clock: Android itself
     *  has none set, and setting one would be written to flash. Returns the zone's name, e.g. "UTC+1:00". */
    public static synchronized String setZone(int minutes) {
        int a = Math.abs(minutes);
        String name = "UTC" + (minutes < 0 ? "-" : "+") + a / 60 + ":" + (a % 60 < 10 ? "0" : "") + a % 60;
        TimeZone tz = new SimpleTimeZone(minutes * 60000, name);
        TimeZone.setDefault(tz);
        clock.setTimeZone(tz);
        return name;
    }

    /** Changes whenever a line is added. */
    public static synchronized int version() { return version; }

    public static synchronized String text() {
        StringBuilder sb = new StringBuilder();
        for (String l : lines) sb.append(l).append('\n');
        return sb.toString();
    }
}
