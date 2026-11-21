package nl.fixfl.autostart;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

import java.util.Map;
import java.util.TreeMap;

public class MainActivity extends Activity {
    private static final int HIGHLIGHT = 0xFFFFD54F;   // current lens
    private static final int UNKNOWN = 0xFFFF7043;     // current focal length has no entry
    private static final String GAP = "    ";

    private TableLayout lensTable;
    private TextView logView;
    private ScrollView logScroll;
    private int shownLog = -1;
    private final Handler handler = new Handler();
    private boolean resumed = false;

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

        lensTable = new TableLayout(this);
        root.addView(lensTable);

        LinearLayout frame = new LinearLayout(this);       // thin border around the log
        frame.setBackgroundColor(0xFF606060);
        frame.setPadding(1, 1, 1, 1);
        final ScrollView sv = new ScrollView(this);
        logScroll = sv;
        sv.setBackgroundColor(0xFF101010);
        logView = new TextView(this);
        logView.setTextSize(12);
        logView.setTypeface(Fonts.mono(this));             // the log: a monospaced font (Fira Mono)
        logView.setTextColor(0xFFCCCCCC);
        logView.setPadding(6, 4, 6, 4);
        sv.addView(logView);
        frame.addView(sv, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.MATCH_PARENT));
        LinearLayout.LayoutParams fp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        fp.setMargins(0, 8, 0, 8);
        root.addView(frame, fp);

        LinearLayout bottom = new LinearLayout(this);       // last key at the left, restart at the right
        bottom.addView(new TextView(this), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        Button restart = new Button(this);
        restart.setText("Restart");
        restart.setTextSize(16.5f);
        Fonts.comic(restart, false);
        restart.setMinHeight(0);
        restart.setMinWidth(0);
        restart.setPadding(42, 0, 42, 0);
        restart.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                new Thread() {
                    public void run() { StartService.restart(MainActivity.this); }
                }.start();
            }
        });
        bottom.addView(restart, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, 48));
        root.addView(bottom);

        setContentView(root);
        Fonts.title(this, getTitle());                     // the title bar in Comic too
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
        // MENU, or the shutter half-press (the camera reports it as key 0, scan 517): back to the camera's menu
        if (keyCode == KeyEvent.KEYCODE_MENU || event.getScanCode() == 517 || keyCode == KeyEvent.KEYCODE_FOCUS) {
            Navigation.toMenu(this);
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private void refresh() {
        int mm = new SteadyShot(Starter.SAMPLE_FILE, 5000).read();
        fillLenses(StartService.profiles, mm);
        int v = AppLog.version();
        if (v != shownLog) {                        // new output (or first load): show it and scroll to the end
            shownLog = v;
            logView.setText(AppLog.text());
            logScroll.post(new Runnable() {
                public void run() { logScroll.fullScroll(View.FOCUS_DOWN); }
            });
        }
    }

    /** Every lens from lenses.ini by focal length; the current one highlighted, an undefined one added in red. */
    private void fillLenses(TreeMap<Integer, LensProfiles.Profile> profiles, int mm) {
        lensTable.removeAllViews();
        TreeMap<Integer, String> rows = new TreeMap<Integer, String>();
        if (profiles != null) {
            for (Map.Entry<Integer, LensProfiles.Profile> e : profiles.entrySet()) {
                String m = e.getValue().model;
                rows.put(e.getKey(), m != null ? m : "(no model)");
            }
        }
        boolean unknown = mm > 0 && !rows.containsKey(Integer.valueOf(mm));
        if (unknown) rows.put(Integer.valueOf(mm), "Unknown");
        if (rows.isEmpty()) {
            TextView t = new TextView(this);
            t.setTextSize(15);
            Fonts.comic(t, false);
            t.setText(profiles == null ? "Waiting for the SD card" : "No lenses in lenses.ini");
            lensTable.addView(t);
            return;
        }
        for (Map.Entry<Integer, String> e : rows.entrySet()) {
            boolean cur = e.getKey().intValue() == mm;
            TableRow row = new TableRow(this);
            TextView num = new TextView(this);
            num.setTextSize(15);
            num.setGravity(Gravity.RIGHT);
            num.setText(" " + e.getKey() + " mm");
            TextView name = new TextView(this);
            name.setTextSize(15);
            Fonts.comic(num, false);
            Fonts.comic(name, false);
            name.setText(GAP + e.getValue() + " ");
            if (cur) {
                row.setBackgroundColor(unknown ? UNKNOWN : HIGHLIGHT);
                num.setTextColor(0xFF000000);       // dark text on the bright highlight
                name.setTextColor(0xFF000000);
                Fonts.comic(num, true);
                Fonts.comic(name, true);
            }
            row.addView(num);
            row.addView(name);
            lensTable.addView(row);
        }
    }
}
