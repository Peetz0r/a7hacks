package nl.peetz0r.a7hacks;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.SystemClock;

import java.io.File;
import java.io.FileOutputStream;
import java.util.Arrays;
import java.util.TreeMap;

public class StartService extends Service {
    static final String TAG = "A7hacks";
    static final String LOGFILE = "a7hacks.log";

    static long lastStart = 0;
    static Thread supervisor = null;
    static volatile long lastSamplerTry = 0;
    static volatile long lastExposureTry = 0;
    static volatile long lastLabelTry = 0;
    static volatile long lastFontTry = 0;
    static volatile long lastClockTry = 0;
    static volatile long lastSummerTry = 0;
    /** From A7HACKS.INI [config] font: true = Comic Relief, false = the camera's own; null until A7HACKS.INI is read. */
    static volatile Boolean fontWanted = null;
    /** From A7HACKS.INI [config] icons: false = keep the icons' own lettering. */
    static volatile boolean iconsWanted = true;
    /** From A7HACKS.INI [config] timezone: an IANA time zone name for the summer time switch; null: leave it. */
    static volatile String timezoneWanted = null;
    static volatile boolean restartRequested = false;
    /** Lens profiles from A7HACKS.INI, read once (again after Restart); null until read. */
    static volatile TreeMap<Integer, LensProfiles.Profile> profiles = null;

    // Java-side log of service events (start, failures) in the app's private storage, not on the SD card.
    // Readable from the Linux side at /android/data/data/nl.peetz0r.a7hacks/files/a7hacks.log
    @SuppressWarnings("deprecation")
    static synchronized void jlog(Context c, String msg) {
        try {
            FileOutputStream f = c.openFileOutput(LOGFILE, Context.MODE_APPEND | Context.MODE_WORLD_READABLE);
            f.write(((SystemClock.elapsedRealtime() / 1000) + "s " + msg + "\n").getBytes());
            f.close();
        } catch (Throwable t) {
            // nothing else we can do
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(final Intent intent, int flags, int startId) {
        final String event = intent == null ? "null" : String.valueOf(intent.getStringExtra("event"));
        long now = SystemClock.elapsedRealtime();
        synchronized (StartService.class) {
            // manual launches always go through; the burst of PACKAGE_ADDED broadcasts at boot only once
            if (!"manual".equals(event) && lastStart != 0 && now - lastStart < 60000) {
                jlog(this, "event " + event + " (ignored, started recently)");
                return START_NOT_STICKY;
            }
            lastStart = now;
            jlog(this, "event " + event);
            if (supervisor != null && supervisor.isAlive()) return START_NOT_STICKY;
            final Context ctx = getApplicationContext();
            supervisor = new Thread("a7hacks-supervisor") {
                @Override
                public void run() { supervise(ctx); }
            };
            supervisor.setDaemon(true);
            supervisor.start();
        }
        return START_NOT_STICKY;
    }

    /**
     * Restart button: stops the root sampler and all three liba7hacks.so loops (--exposure-patcher, --label-patcher, --font-patcher); the
     * supervisor then starts new ones, reads A7HACKS.INI again (so a changed font or icons option takes effect)
     * and writes the lens info afresh. Call from a background thread (the root exec call may block briefly).
     */
    static void restart(Context ctx) {
        jlog(ctx, "restart requested");
        AppLog.add("restart");
        try {
            Starter.stopSampler();
        } catch (Throwable t) {
            jlog(ctx, "stopping the sampler failed: " + t);
        }
        try {
            Starter.stopExposure();
        } catch (Throwable t) {
            jlog(ctx, "stopping the exposure loop failed: " + t);
        }
        try {
            Starter.stopLabel();
        } catch (Throwable t) {
            jlog(ctx, "stopping the label loop failed: " + t);
        }
        try {
            Starter.stopFont();
        } catch (Throwable t) {
            jlog(ctx, "stopping the font loop failed: " + t);
        }
        lastSamplerTry = 0;                 // start new ones right away
        lastExposureTry = 0;
        lastLabelTry = 0;
        lastFontTry = 0;
        lastClockTry = 0;
        lastSummerTry = 0;
        restartRequested = true;
        synchronized (StartService.class) { lastStart = 0; }
    }

    /** A [config] option: on (the default) or off; anything else is logged and counts as on. */
    private static boolean onOff(java.util.Map<String, String> options, String key) {
        String v = options.get(key);
        if (v == null || v.equals("on")) return true;
        if (v.equals("off")) return false;
        AppLog.add("A7HACKS.INI: " + key + " = " + v + "? (on or off), using on");
        return true;
    }

    /** Reads A7HACKS.INI once the SD card is there. Returns false while there is no card. */
    static boolean loadProfiles() {
        File sd = Starter.sdRoot();
        if (sd == null) return false;
        File[] files = Starter.configFiles(sd);
        TreeMap<Integer, LensProfiles.Profile> p = LensProfiles.loadAll(files);
        java.util.Map<String, String> options = LensProfiles.loadOptions(files);
        fontWanted = onOff(options, "font");
        iconsWanted = onOff(options, "icons");
        timezoneWanted = options.get("timezone");
        profiles = p;
        File used = null;
        for (File f : files) if (f.isFile()) { used = f; break; }
        if (used != null) AppLog.add("read " + used.getName() + ": " + p.size() + " lenses");
        else {
            // The camera's SD card layer only opens names stored in capitals. A file a PC saved as a7hacks.ini
            // (lower case) is listed, but can't be opened: say so instead of only "not there".
            String hidden = null;
            String[] names = sd.list();
            if (names != null) for (String n : names) if (n.equalsIgnoreCase("a7hacks.ini")) { hidden = n; break; }
            AppLog.add(hidden == null ? "no A7HACKS.INI on the SD card"
                : hidden + " is on the SD card in lower case, which the camera can't open: rename it to A7HACKS.INI on a PC");
        }
        return true;
    }

    /** "3 min 33 s", "41 s" */
    static String took(int s) { return (s >= 60 ? s / 60 + " min " : "") + s % 60 + " s"; }

    /** Keeps the root sampler running and the camera's lens info up to date. Runs for as long as the process lives. */
    static void supervise(final Context ctx) {
        final SteadyShot setting = new SteadyShot(Starter.SAMPLE_FILE, 5000);
        final Starter.Tool tool = new Starter.Tool(ctx);
        jlog(ctx, "tool " + tool.path());
        LensWriter.Profiles lookup = new LensWriter.Profiles() {
            public LensProfiles.Profile get(int mm) {
                TreeMap<Integer, LensProfiles.Profile> p = profiles;
                return p == null ? null : p.get(Integer.valueOf(mm));
            }
        };
        LensWriter.Log log = new LensWriter.Log() {
            public void log(String line) { AppLog.add(line); }
        };
        LensWriter writer = new LensWriter(setting, lookup, tool, log);
        boolean loggedWaitSd = false, loggedStopped = false, exposureStopped = false;
        String exposureKind = null, labelKind = null, fontKind = null;
        boolean fontStopped = false;
        String picturesLogged = null;          // the "pN" last logged
        boolean accentLogged = false;
        boolean clockLogged = false;
        String irisLogged = null;
        String clockSeen = null;
        Integer clockOffset = null;            // the camera's time zone plus summer time (minutes), once known
        final SummerTime summer = new SummerTime(ctx);
        boolean summerConfigured = false;
        boolean cardIn = false;                // for the summer time's tzdata: read it as soon as a card goes in
        final String clockOld = Starter.clockStatus();   // an answer from before (a restart): not news
        AppLog.add("started");
        while (true) {
            try {
                if (restartRequested) {
                    restartRequested = false;
                    profiles = null;
                    writer = new LensWriter(setting, lookup, tool, log);
                    loggedStopped = false;
                    exposureStopped = false;
                    exposureKind = null;
                    fontStopped = false;
                    fontKind = null;
                    labelKind = null;
                    picturesLogged = null;
                    accentLogged = false;
                    clockLogged = false;
                    irisLogged = null;
                    summerConfigured = false;
                }
                // The SD card is the way out: without one, nothing of ours runs as root. Should the helper ever make
                // the camera restart, over and over (the app starts with it), take the card out, uninstall the
                // app, and the card can go back in. So A7HACKS.INI (the card) first; all the rest waits for it.
                if (profiles == null && !loadProfiles()) {
                    if (!loggedWaitSd) {
                        AppLog.add("waiting for the SD card: nothing starts without one");
                        loggedWaitSd = true;
                    }
                    try { Thread.sleep(1000); } catch (InterruptedException e) { return; }
                    continue;
                }
                boolean in = Starter.sdRoot() != null;
                if (!summerConfigured) {
                    summer.configure(timezoneWanted);
                    summerConfigured = true;
                    summer.card();
                } else if (in && !cardIn) summer.card();
                cardIn = in;
                // 0. the clock: Linux's clock from the camera's own, now and then every minute (it stands still
                //    while the camera sleeps); the answer comes a moment later
                if (SystemClock.elapsedRealtime() - lastClockTry > 60000) {
                    lastClockTry = SystemClock.elapsedRealtime();
                    try {
                        Starter.setClock(tool.path());
                    } catch (Throwable t) {
                        jlog(ctx, "clock start failed: " + t);
                    }
                }
                String ck = Starter.clockStatus();
                if (ck != null && !ck.equals(clockSeen)) {
                    clockSeen = ck;
                    String[] w = ck.split(" ");
                    String zone = null;
                    for (String x : w)
                        if (x.length() > 1 && (x.charAt(0) == '+' || x.charAt(0) == '-')) {
                            try {
                                int off = Integer.parseInt(x.substring(x.charAt(0) == '+' ? 1 : 0));
                                zone = AppLog.setZone(off);
                                clockOffset = ck.startsWith("clock set") || ck.startsWith("clock same") ? off : null;
                            }
                            catch (NumberFormatException e) { /* not the offset */ }
                        }
                    if (ck.equals(clockOld)) { /* only the zone */ }
                    else if (ck.startsWith("clock set") && w.length >= 4)
                        AppLog.add("clock set from the camera: " + w[2] + " " + w[3] + (zone != null ? " " + zone : ""));
                    else if (!ck.startsWith("clock same"))
                        AppLog.add("clock: " + ck);
                }
                // 1. root sampler (retry: the root channel may not be ready right at boot)
                if (!setting.isFresh() && SystemClock.elapsedRealtime() - lastSamplerTry > 30000) {
                    lastSamplerTry = SystemClock.elapsedRealtime();
                    try {
                        Starter.startSampler();
                        jlog(ctx, "sampler start requested; sample file: " + setting.describe());
                    } catch (Throwable t) {
                        jlog(ctx, "sampler start failed: " + t);
                        AppLog.add("could not start the SteadyShot sampler: " + t);
                    }
                }
                // 1b. exposure loop (ISO AUTO minimum shutter speed); starting it again is a no-op while it runs
                if (!exposureStopped && SystemClock.elapsedRealtime() - lastExposureTry > 30000) {
                    lastExposureTry = SystemClock.elapsedRealtime();
                    try {
                        Starter.startExposure(tool.path());
                    } catch (Throwable t) {
                        jlog(ctx, "exposure loop start failed: " + t);
                    }
                }
                String ex = Starter.exposureStatus();
                if (ex != null) {
                    String kind = ex.startsWith("on ") ? "on" : ex;
                    if (!kind.equals(exposureKind)) {
                        if (kind.equals("on"))
                            AppLog.add("ISO AUTO minimum shutter speed follows the SteadyShot focal length");
                        else if (kind.equals("lens"))
                            AppLog.add("electronic lens attached: ISO AUTO uses the lens's own focal length");
                        else if (kind.equals("off")) {
                            if (exposureKind != null)
                                AppLog.add("ISO AUTO minimum shutter speed back to the camera's default");
                        } else if (kind.equals("badsig") || kind.equals("badsite")) {
                            AppLog.add("not changing the ISO AUTO minimum shutter speed: camera code is not as expected (" + kind + ")");
                            exposureStopped = true;
                        } else {
                            AppLog.add("ISO AUTO minimum shutter speed: " + ex);
                        }
                        exposureKind = kind;
                    }
                }
                // 1c. menu label loop ("SteadyS. Focal Len." -> "Manual Focal Length"); also a no-op while it runs
                if (SystemClock.elapsedRealtime() - lastLabelTry > 30000) {
                    lastLabelTry = SystemClock.elapsedRealtime();
                    try {
                        Starter.startLabel(tool.path());
                    } catch (Throwable t) {
                        jlog(ctx, "label loop start failed: " + t);
                    }
                }
                String lb = Starter.labelStatus();
                if (lb != null) {
                    String kind = lb.startsWith("on ") ? "on" : lb.startsWith("waiting") ? "waiting" : lb;
                    if (!kind.equals(labelKind)) {
                        if (kind.equals("on")) {
                            String n = lb.substring(3).trim();
                            AppLog.add("menu item \"SteadyS. Focal Len.\" renamed to \"Manual Focal Length\""
                                + (n.equals("1") ? "" : " (" + n + " copies)"));
                        } else if (kind.equals("waiting")) {
                            AppLog.add("menu label: the English menu text isn't in the camera's memory (another language?);"
                                + " it gets renamed as soon as it is (looked through " + lb.substring(7).trim() + " MB)");
                        } else if (kind.equals("noui")) {
                            AppLog.add("menu label: camera UI not found yet");
                        } else {
                            AppLog.add("menu label: " + lb);
                        }
                        labelKind = kind;
                    }
                }
                // 1d. UI font (A7HACKS.INI can switch it off)
                Boolean fw = fontWanted;
                String ft = Starter.fontStatus();
                if (fw != null && !fontStopped) {
                    if (fw) {
                        if (SystemClock.elapsedRealtime() - lastFontTry > 30000) {
                            lastFontTry = SystemClock.elapsedRealtime();
                            try {
                                Starter.startFont(tool.path(), iconsWanted);
                            } catch (Throwable t) {
                                jlog(ctx, "font loop start failed: " + t);
                            }
                        }
                    } else if (ft != null && (ft.startsWith("on") || ft.startsWith("partial"))) {
                        try {
                            Starter.fontOff(tool.path());
                        } catch (Throwable t) {
                            jlog(ctx, "font off failed: " + t);
                        }
                    }
                }
                // the pictures (and the accent colour) once they're all in: "pN" (while they're still being
                // done it reads "pN/T", which the screen shows as progress)
                if (ft != null && ft.startsWith("on ")) {
                    String took = "";              // "sS": how long the patcher took to get the pictures in
                    for (String w : ft.split(" "))
                        if (w.length() > 1 && w.charAt(0) == 's' && Character.isDigit(w.charAt(1)))
                            took = " (in " + took(Integer.parseInt(w.substring(1))) + ")";
                    for (String w : ft.split(" ")) {
                        if (w.length() > 1 && w.charAt(0) == 'p' && Character.isDigit(w.charAt(1)) && w.indexOf('/') < 0
                                && !w.equals(picturesLogged)) {
                            picturesLogged = w;
                            AppLog.add("pictures: " + w.substring(1) + " of the camera's pictures (focal lengths, page"
                                + " numbers, AWB, ISO, ...) now have their lettering in Comic Relief and the accent colour in green" + took);
                        } else if (w.length() > 1 && w.charAt(0) == 'i' && Character.isDigit(w.charAt(1)) && !w.equals(irisLogged)) {
                            irisLogged = w;
                            AppLog.add("live view: the focal length (" + w.substring(1) + " mm) where the aperture would be (F--)");
                        } else if (w.length() > 1 && w.charAt(0) == 'h' && Character.isDigit(w.charAt(1)) && !clockLogged) {
                            clockLogged = true;
                            AppLog.add("playback: the time in 24 hours, with the date as set");
                        } else if (w.length() > 1 && w.charAt(0) == 'a' && Character.isDigit(w.charAt(1)) && !accentLogged) {
                            accentLogged = true;
                            AppLog.add("accent colour: the text the camera draws in orange is now green");
                        }
                    }
                }
                if (ft != null) {
                    String kind = ft.startsWith("on ") ? "on" : ft.startsWith("partial") ? "partial"
                        : ft.startsWith("off") ? "off" : ft;
                    if (!kind.equals(fontKind)) {
                        if (kind.equals("on")) {
                            // "on N", "on N cM" (M cached characters marked for drawing again), "on N nocache",
                            // each possibly followed by "noicons" (the icon font isn't the one this was made for)
                            String[] f = ft.split(" ");
                            String c = f.length > 2 ? f[2] : "";
                            String fin = "";       // "fF": seconds the patcher took to get the font in
                            for (String w : f)
                                if (w.length() > 1 && w.charAt(0) == 'f' && Character.isDigit(w.charAt(1)))
                                    fin = ", in " + took(Integer.parseInt(w.substring(1)));
                            if (Arrays.asList(f).contains("noicons"))
                                AppLog.add("icons: keeping their own lettering (the icon font isn't the one this was made for)");
                            if (c.equals("nocache"))
                                AppLog.add("menu font: Comic Relief (the camera's character cache wasn't found, so the"
                                    + " characters it had already drawn stay in Univers" + fin + ")");
                            else if (c.startsWith("c") && !c.equals("c0"))
                                AppLog.add("menu font: Comic Relief (" + c.substring(1)
                                    + " characters the camera had kept from Univers get drawn again" + fin + ")");
                            else
                                AppLog.add("menu font: Comic Relief" + (fin.length() > 0 ? " (" + fin.substring(2) + ")" : ""));
                            // then "tK" (K labels the camera had kept as pictures get drawn again when next shown)
                            // or "nolabels" (that store wasn't found)
                            for (String w : f) {
                                if (w.equals("nolabels"))
                                    AppLog.add("labels: the camera's store of drawn labels wasn't found, so the ones"
                                        + " it had already drawn stay in Univers");
                                else if (w.length() > 1 && w.charAt(0) == 't' && Character.isDigit(w.charAt(1)))
                                    AppLog.add("labels: " + w.substring(1) + " the camera had already drawn get drawn"
                                        + " again in Comic Relief when next shown");
                                else if (w.equals("nopics"))
                                    AppLog.add("pictures: keeping their own lettering (the camera's picture files"
                                        + " aren't the ones this was made for)");
                            }
                        } else if (kind.equals("partial"))
                            AppLog.add("menu font: Comic Relief, " + ft.substring(8).trim()
                                + " parts in place (the rest once the camera has finished reading them)");
                        else if (kind.equals("off")) {
                            if (fontKind != null) AppLog.add(ft.equals("off pics")
                                ? "menu font: back to the camera's own (the pictures at the next start of the camera)"
                                : "menu font: back to the camera's own");
                        } else if (kind.equals("restoring")) {
                            // transient
                        } else {
                            AppLog.add("menu font: not changing it (" + ft + ")");
                            fontStopped = true;
                        }
                        fontKind = kind;
                    }
                }
                // 2. the lens info
                writer.tick(SystemClock.elapsedRealtime());
                // 3. summer time (A7HACKS.INI timezone): once a minute, once Linux's clock is the camera's
                if (clockOffset != null && SystemClock.elapsedRealtime() - lastSummerTry > 60000) {
                    lastSummerTry = SystemClock.elapsedRealtime();
                    if (summer.tick(clockOffset)) {            // read the clock again, and look again in 5 s
                        lastClockTry = 0;
                        lastSummerTry = SystemClock.elapsedRealtime() - 55000;
                    }
                }
                if (writer.isStopped() && !loggedStopped) {
                    jlog(ctx, "lens writer stopped");
                    loggedStopped = true;
                }
            } catch (Throwable t) {
                jlog(ctx, "supervisor error: " + t);
            }
            try { Thread.sleep(1000); } catch (InterruptedException e) { return; }
        }
    }
}
