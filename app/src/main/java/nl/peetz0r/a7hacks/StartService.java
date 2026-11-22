package nl.peetz0r.a7hacks;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.SystemClock;

import java.io.File;
import java.io.FileOutputStream;
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
    /** From lenses.ini [camera] font: true = Comic Relief, false = the camera's own; null until lenses.ini is read. */
    static volatile Boolean fontWanted = null;
    /** From lenses.ini [camera] icons: false = keep the icons' own lettering. */
    static volatile boolean iconsWanted = true;
    static volatile boolean restartRequested = false;
    /** Lens profiles from lenses.ini, read once (again after Restart); null until read. */
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
     * supervisor then starts new ones, reads lenses.ini again (so a changed font or icons option takes effect)
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
        restartRequested = true;
        synchronized (StartService.class) { lastStart = 0; }
    }

    /** Reads lenses.ini once the SD card is there. Returns false while there is no card. */
    static boolean loadProfiles() {
        File sd = Starter.sdRoot();
        if (sd == null) return false;
        File[] files = Starter.lensFiles(sd);
        TreeMap<Integer, LensProfiles.Profile> p = LensProfiles.loadAll(files);
        String font = LensProfiles.loadOptions(files).get("font");
        String icons = LensProfiles.loadOptions(files).get("icons");
        iconsWanted = !(icons != null && (icons.equalsIgnoreCase("original") || icons.equalsIgnoreCase("off")
            || icons.equalsIgnoreCase("univers") || icons.equalsIgnoreCase("sony")));
        fontWanted = !(font != null && (font.equalsIgnoreCase("univers") || font.equalsIgnoreCase("off")
            || font.equalsIgnoreCase("original")));
        profiles = p;
        File used = null;
        for (File f : files) if (f.isFile()) { used = f; break; }
        AppLog.add(used == null ? "no lenses.ini on the SD card" : "read " + used.getName() + ": " + p.size() + " lenses");
        return true;
    }

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
        long started = SystemClock.elapsedRealtime();
        boolean loggedWaitSd = false, loggedStopped = false, exposureStopped = false;
        String exposureKind = null, labelKind = null, fontKind = null;
        boolean fontStopped = false;
        AppLog.add("started");
        while (true) {
            try {
                if (restartRequested) {
                    restartRequested = false;
                    profiles = null;
                    writer = new LensWriter(setting, lookup, tool, log);
                    started = SystemClock.elapsedRealtime();
                    loggedStopped = false;
                    exposureStopped = false;
                    exposureKind = null;
                    fontStopped = false;
                    fontKind = null;
                    labelKind = null;
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
                // 1d. UI font, once lenses.ini has been read (it can switch it off)
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
                if (ft != null) {
                    String kind = ft.startsWith("on ") ? "on" : ft.startsWith("partial") ? "partial"
                        : ft.startsWith("off") ? "off" : ft;
                    if (!kind.equals(fontKind)) {
                        if (kind.equals("on")) {
                            // "on N", "on N cM" (M cached characters marked for drawing again), "on N nocache",
                            // each possibly followed by "noicons" (the icon font isn't the one this was made for)
                            String[] f = ft.split(" ");
                            String c = f.length > 2 ? f[2] : "";
                            if (ft.endsWith(" noicons"))
                                AppLog.add("icons: keeping their own lettering (the icon font isn't the one this was made for)");
                            if (c.equals("nocache"))
                                AppLog.add("menu font: Comic Relief (the camera's character cache wasn't found, so the"
                                    + " characters it had already drawn stay in Univers)");
                            else if (c.startsWith("c") && !c.equals("c0"))
                                AppLog.add("menu font: Comic Relief (" + c.substring(1)
                                    + " characters the camera had kept from Univers get drawn again)");
                            else
                                AppLog.add("menu font: Comic Relief");
                            // then "tK" (K labels the camera had kept as pictures get drawn again when next shown)
                            // or "nolabels" (that store wasn't found)
                            for (String w : f) {
                                if (w.equals("nolabels"))
                                    AppLog.add("labels: the camera's store of drawn labels wasn't found, so the ones"
                                        + " it had already drawn stay in Univers");
                                else if (w.length() > 1 && w.charAt(0) == 't' && Character.isDigit(w.charAt(1)))
                                    AppLog.add("labels: " + w.substring(1) + " the camera had already drawn get drawn"
                                        + " again in Comic Relief when next shown");
                                else if (w.length() > 1 && w.charAt(0) == 'p' && Character.isDigit(w.charAt(1)))
                                    AppLog.add("pictures: " + w.substring(1) + " of the camera's pictures with lettering"
                                        + " (focal lengths, page numbers, AWB, ISO, ...) now have it in Comic Relief");
                                else if (w.length() > 1 && w.charAt(0) == 'a' && Character.isDigit(w.charAt(1)))
                                    AppLog.add("accent colour: the orange of the camera's pictures and of the text it"
                                        + " draws in colour is now green");
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
                // 2. lenses.ini, once the card is there
                if (profiles == null && !loadProfiles() && !loggedWaitSd) {
                    AppLog.add("waiting for the SD card (for lenses.ini)");
                    loggedWaitSd = true;
                }
                // 3. the lens info; without a card, start after 20 s with the focal length only
                if (profiles != null || SystemClock.elapsedRealtime() - started > 20000) {
                    writer.tick(SystemClock.elapsedRealtime());
                    if (writer.isStopped() && !loggedStopped) {
                        jlog(ctx, "lens writer stopped");
                        loggedStopped = true;
                    }
                }
            } catch (Throwable t) {
                jlog(ctx, "supervisor error: " + t);
            }
            try { Thread.sleep(1000); } catch (InterruptedException e) { return; }
        }
    }
}
