package nl.fixfl.autostart;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;

public class StartService extends Service {
    static final String TAG = "FixflAutostart";
    static final String LOGFILE = "autostart.log";
    static final long MAX_LOG = 512 * 1024;

    static long lastStart = 0;
    static Thread supervisor = null;
    static Thread watcherThread = null;
    static volatile Watcher watcher = null;
    static volatile long lastSamplerTry = 0;
    static File logFile = null;

    // Java-side log of service events. Readable from the Linux side at
    // /android/data/data/nl.fixfl.autostart/files/autostart.log
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

    /** Photo log on the SD card (fixfl.log in the card root, or HAX/ if the root is read-only for us). */
    static synchronized void photoLog(File sd, String line) {
        Log.i(TAG, line);
        for (int attempt = 0; attempt < 2; attempt++) {
            try {
                File f = Starter.logFile(sd);
                File dir = f.getParentFile();
                if (dir != null && !dir.isDirectory()) dir.mkdirs();
                if (f.length() > MAX_LOG) f.delete();
                FileWriter w = new FileWriter(f, true);
                try { w.write(line + "\n"); } finally { w.close(); }
                logFile = f;
                return;
            } catch (IOException e) {
                if (Starter.rootWritable) {
                    Starter.rootWritable = false;      // retry once in HAX/
                    Log.e(TAG, "cannot write fixfl.log in the card root, using HAX/: " + e);
                } else {
                    Log.e(TAG, "cannot write log: " + e);
                    return;
                }
            }
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
            supervisor = new Thread("fixfl-supervisor") {
                @Override
                public void run() { supervise(ctx); }
            };
            supervisor.setDaemon(true);
            supervisor.start();
        }
        return START_NOT_STICKY;
    }

    /**
     * Restart button: stops the root sampler and the watcher; the supervisor starts fresh ones within
     * a couple of seconds. Call from a background thread (the root exec call may block briefly).
     */
    static void restart(Context ctx) {
        jlog(ctx, "restart requested");
        try {
            Starter.stopSampler();
        } catch (Throwable t) {
            jlog(ctx, "stopping the sampler failed: " + t);
        }
        lastSamplerTry = 0;                 // start a new sampler right away
        Thread w = watcherThread;
        if (w != null) w.interrupt();       // the supervisor creates a new watcher
        synchronized (StartService.class) { lastStart = 0; }
    }

    /** Keeps the root sampler and the watcher alive. Runs for as long as the process lives. */
    static void supervise(Context ctx) {
        Watcher.FileSetting setting = new Watcher.FileSetting(Starter.SAMPLE_FILE, 5000);
        boolean loggedWaitSd = false;
        while (true) {
            try {
                // 1. root sampler (retry: the root channel may not be ready right at boot)
                if (!setting.isFresh() && SystemClock.elapsedRealtime() - lastSamplerTry > 30000) {
                    lastSamplerTry = SystemClock.elapsedRealtime();
                    try {
                        Starter.startSampler();
                        jlog(ctx, "sampler start requested; sample file: " + setting.describe());
                    } catch (Throwable t) {
                        jlog(ctx, "sampler start failed: " + t);
                    }
                }
                // 2. watcher, as soon as the SD card is there
                if (watcherThread == null || !watcherThread.isAlive()) {
                    final File sd = Starter.sdRoot();
                    if (sd == null) {
                        if (!loggedWaitSd) { jlog(ctx, "waiting for the SD card"); loggedWaitSd = true; }
                    } else {
                        Watcher w = new Watcher(new File(sd, "DCIM"), setting, new Watcher.Sink() {
                            public void log(String line) { photoLog(sd, line); }
                        }, Starter.lensFiles(sd));
                        watcher = w;
                        watcherThread = w.start();
                        jlog(ctx, "watcher started on " + sd);
                        loggedWaitSd = false;
                    }
                }
            } catch (Throwable t) {
                jlog(ctx, "supervisor error: " + t);
            }
            try { Thread.sleep(2000); } catch (InterruptedException e) { return; }
        }
    }
}
