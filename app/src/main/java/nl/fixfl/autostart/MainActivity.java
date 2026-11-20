package nl.fixfl.autostart;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.BackgroundColorSpan;
import android.text.style.StyleSpan;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.util.Map;

public class MainActivity extends Activity {
    private static final int HIGHLIGHT = 0xFFFFD54F;

    private TextView statusView, lensView, logView, keyView;
    private final Handler handler = new Handler();
    private boolean resumed = false;
    private String lastKey = "-";

    private final Runnable refresher = new Runnable() {
        public void run() {
            if (!resumed) return;
            refresh();
            handler.postDelayed(this, 2000);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(16, 12, 16, 12);

        statusView = new TextView(this);
        statusView.setTextSize(15);
        root.addView(statusView);

        Button restart = new Button(this);
        restart.setText("Restart");
        restart.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                new Thread() {
                    public void run() { StartService.restart(MainActivity.this); }
                }.start();
                statusView.setText("Restarting...");
            }
        });
        root.addView(restart);

        lensView = new TextView(this);
        lensView.setTextSize(15);
        root.addView(lensView);

        ScrollView sv = new ScrollView(this);
        logView = new TextView(this);
        logView.setTextSize(12);
        sv.addView(logView);
        root.addView(sv, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        keyView = new TextView(this);
        keyView.setTextSize(11);
        root.addView(keyView);

        setContentView(root);
        Intent i = new Intent(this, StartService.class);
        i.putExtra("event", "manual");
        startService(i);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        handler.postDelayed(refresher, 2000);
    }

    @Override
    protected void onPause() {
        resumed = false;
        handler.removeCallbacks(refresher);
        super.onPause();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        lastKey = "key " + keyCode + ", scan " + event.getScanCode();
        keyView.setText("Last key: " + lastKey);
        if (keyCode == KeyEvent.KEYCODE_MENU) {
            Navigation.toMenu(this);                 // back to the camera's application menu
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_FOCUS || keyCode == KeyEvent.KEYCODE_CAMERA) {
            if (event.getRepeatCount() == 0) {       // shutter half-press: back to shooting
                new Thread() {
                    public void run() { Navigation.toShooting(); }
                }.start();
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private void refresh() {
        Watcher.FileSetting fs = new Watcher.FileSetting(Starter.SAMPLE_FILE, 5000);
        int mm = fs.read();
        File sd = Starter.sdRoot();
        statusView.setText("SteadyShot focal length: " + (mm > 0 ? mm + " mm" : "not available (yet)")
                + "\nSD card: " + (sd == null ? "not found" : sd.getPath()));
        lensView.setText(lensList(sd, mm));
        logView.setText(logTail(sd));
        keyView.setText("Last key: " + lastKey);
    }

    /** Every lens from lenses.ini, one per line, the one matching the current focal length highlighted. */
    private CharSequence lensList(File sd, int mm) {
        SpannableStringBuilder sb = new SpannableStringBuilder();
        if (sd == null) return "Lenses: (no SD card)";
        Map<Integer, LensProfiles.Profile> all = LensProfiles.loadAll(Starter.lensFiles(sd));
        sb.append("Lenses (lenses.ini):\n");
        if (all.isEmpty()) {
            sb.append("  none found - create lenses.ini in the SD card root\n");
        }
        for (Map.Entry<Integer, LensProfiles.Profile> e : all.entrySet()) {
            LensProfiles.Profile p = e.getValue();
            String line = " " + e.getKey() + " mm  " + (p.model != null ? p.model : "(no model)")
                    + (p.aperture > 0 ? "  f/" + p.aperture : "") + " ";
            int start = sb.length();
            sb.append(line);
            if (e.getKey().intValue() == mm) {
                sb.setSpan(new BackgroundColorSpan(HIGHLIGHT), start, start + line.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
                sb.setSpan(new StyleSpan(Typeface.BOLD), start, start + line.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
            sb.append("\n");
        }
        if (mm > 0 && !all.containsKey(Integer.valueOf(mm))) {
            sb.append("  (no entry for the current " + mm + " mm)\n");
        }
        return sb;
    }

    private String logTail(File sd) {
        if (sd == null) return "";
        File f = Starter.existingLog(sd);
        try {
            long len = f.length();
            FileInputStream in = new FileInputStream(f);
            try {
                long skip = Math.max(0, len - 3000);
                in.skip(skip);
                byte[] b = new byte[(int) (len - skip)];
                int n = in.read(b);
                String t = new String(b, 0, Math.max(n, 0), "UTF-8");
                int nl = skip > 0 ? t.indexOf('\n') : -1;
                return nl >= 0 ? t.substring(nl + 1) : t;
            } finally {
                in.close();
            }
        } catch (Throwable e) {
            return "(no log yet)";
        }
    }
}
