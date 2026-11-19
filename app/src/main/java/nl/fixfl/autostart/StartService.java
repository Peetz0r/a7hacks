package nl.fixfl.autostart;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.SystemClock;

import java.io.FileOutputStream;

public class StartService extends Service {
    static final String LOGFILE = "autostart.log";

    // Java-side log that doesn't depend on the root shell. Readable from the Linux side at
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

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(final Intent intent, int flags, int startId) {
        final String event = intent == null ? "null" : String.valueOf(intent.getStringExtra("event"));
        jlog(this, "event " + event);
        new Thread() {
            @Override
            public void run() {
                // The root exec channel may not be ready right at boot, so retry for ~2 minutes.
                for (int n = 1; n <= 60; n++) {
                    try {
                        Starter.log(event);
                        Starter.start();
                        jlog(StartService.this, "start ok (try " + n + ")");
                        return;
                    } catch (Throwable t) {
                        jlog(StartService.this, "try " + n + " failed: " + t);
                        try { Thread.sleep(2000); } catch (InterruptedException e) { return; }
                    }
                }
            }
        }.start();
        return START_NOT_STICKY;
    }
}
