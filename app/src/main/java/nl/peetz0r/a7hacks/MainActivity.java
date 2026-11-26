package nl.peetz0r.a7hacks;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

import java.util.Map;
import java.util.TreeMap;

public class MainActivity extends Activity {
    private static final int HIGHLIGHT = 0xFF33B5E5;   // current lens: Holo blue (android.R.color.holo_blue_light)
    private static final int UNKNOWN = 0xFFFF4444;     // current focal length has no entry: Holo red (holo_red_light)
    private static final int GAP = 24;                 // between the focal length and the lens name (px)

    private TableLayout lensTable;
    private TextView progressLabel;
    private ProgressBar progressBar;
    private TextView logView;
    private ScrollView logScroll;
    private int shownLog = -1;
    private int shownMm = Integer.MIN_VALUE;
    private TreeMap<Integer, LensProfiles.Profile> shownProfiles;
    private final Handler handler = new Handler();
    private boolean resumed = false;

    private final Runnable refresher = new Runnable() {
        public void run() {
            if (!resumed) return;
            refresh();
            handler.postDelayed(this, 500);
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

        LinearLayout bottom = new LinearLayout(this);       // patching progress at the left, restart at the right
        bottom.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout progress = new LinearLayout(this);
        progress.setOrientation(LinearLayout.VERTICAL);
        progressLabel = new TextView(this);
        progressLabel.setTextSize(13);
        Fonts.comic(progressLabel, false);
        progress.addView(progressLabel);
        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);   // the Holo one
        progress.addView(progressBar, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        pp.setMargins(0, 0, 24, 0);
        bottom.addView(progress, pp);
        showProgress(null);
        // restart and info: icons from the system (public since API 1, so the camera's Android has them)
        View restart = iconButton(android.R.drawable.ic_menu_rotate, "Restart");
        restart.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                new Thread() {
                    public void run() { StartService.restart(MainActivity.this); }
                }.start();
            }
        });
        bottom.addView(restart);
        View info = iconButton(android.R.drawable.ic_menu_info_details, "Info");
        info.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { About.show(MainActivity.this); }
        });
        bottom.addView(info);
        root.addView(bottom);

        setContentView(root);
        // the title in Comic too; the theme leaves it out of the action bar (see styles.xml), so it's switched on here
        if (getActionBar() != null) getActionBar().setDisplayShowTitleEnabled(true);
        Fonts.title(this, getTitle());
        Intent i = new Intent(this, StartService.class);
        i.putExtra("event", "manual");
        startService(i);
    }

    /** A Holo button with a system icon. Narrow at the sides; the vertical padding stays the button's own: squeezed
     *  below it, its background image (a nine-patch) gets drawn with its edges through the middle. */
    private View iconButton(int icon, String description) {
        ImageButton b = new ImageButton(this);
        b.setImageResource(icon);
        b.setContentDescription(description);
        b.setPadding(24, b.getPaddingTop(), 24, b.getPaddingBottom());
        return b;
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        handler.postDelayed(refresher, 500);
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
        showProgress(Starter.fontStatus());
        int mm = new SteadyShot(Starter.SAMPLE_FILE, 5000).read();
        TreeMap<Integer, LensProfiles.Profile> pr = StartService.profiles;
        if (mm != shownMm || pr != shownProfiles) {     // the list only when it changes (no flicker at 2 Hz)
            shownMm = mm; shownProfiles = pr;
            fillLenses(pr, mm);
        }
        int v = AppLog.version();
        if (v != shownLog) {                        // new output (or first load): show it and scroll to the end
            shownLog = v;
            logView.setText(AppLog.text());
            logScroll.post(new Runnable() {
                public void run() { logScroll.fullScroll(View.FOCUS_DOWN); }
            });
        }
    }

    /** The font patcher's progress, from its status line: the font ("partial N/M") and the pictures ("pN/T"
     *  while they're being done), together in one bar; once all is in, a short line; nothing while it isn't
     *  running. (The font's last few glyphs can wait a while for the camera to move on; the pictures go on.) */
    private void showProgress(String ft) {
        int fontDone = -1, fontTotal = -1, picDone = -1, picTotal = -1;
        boolean on = ft != null && ft.startsWith("on ");
        if (ft != null && (on || ft.startsWith("partial "))) {
            String[] w = ft.split(" ");
            if (!on && w.length > 1) {
                String[] f = w[1].split("/");
                if (f.length == 2) { fontDone = num(f[0]); fontTotal = num(f[1]); }
            }
            for (String x : w) {
                int slash = x.indexOf('/');
                if (x.length() > 1 && x.charAt(0) == 'p' && Character.isDigit(x.charAt(1)) && slash > 0) {
                    picDone = num(x.substring(1, slash)); picTotal = num(x.substring(slash + 1));
                }
            }
        }
        if (on && picTotal < 0) {                          // all in
            progressLabel.setText("Patching done");
            progressLabel.setVisibility(View.VISIBLE);
            progressBar.setVisibility(View.GONE);
            return;
        }
        int done = 0, total = 0;
        StringBuilder label = new StringBuilder();
        if (fontTotal > 0 && fontDone >= 0) { done += fontDone; total += fontTotal; label.append("Font ").append(fontDone).append('/').append(fontTotal); }
        if (picTotal > 0 && picDone >= 0) {
            done += picDone; total += picTotal;
            if (label.length() > 0) label.append("  ");
            label.append("Pictures ").append(picDone).append('/').append(picTotal);
        }
        if (total <= 0) {
            progressLabel.setVisibility(View.INVISIBLE);
            progressBar.setVisibility(View.INVISIBLE);
            return;
        }
        progressLabel.setText(label.toString());
        progressBar.setMax(total);
        progressBar.setProgress(Math.min(done, total));
        progressLabel.setVisibility(View.VISIBLE);
        progressBar.setVisibility(View.VISIBLE);
    }

    private static int num(String s) {
        try { return Integer.parseInt(s.trim()); } catch (NumberFormatException e) { return -1; }
    }

    /** Every lens from A7HACKS.INI by focal length; the current one highlighted, an undefined one added in red. */
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
            t.setText(profiles == null ? "Waiting for the SD card" : "No lenses in A7HACKS.INI");
            lensTable.addView(t);
            return;
        }
        for (Map.Entry<Integer, String> e : rows.entrySet()) {
            boolean cur = e.getKey().intValue() == mm;
            TableRow row = new TableRow(this);
            TextView num = new TextView(this);
            num.setTextSize(15);
            num.setGravity(Gravity.RIGHT);
            num.setText(e.getKey() + " mm");
            num.setPadding(6, 0, 0, 0);
            TextView name = new TextView(this);
            name.setTextSize(15);
            name.setPadding(GAP, 0, 6, 0);      // padding, not spaces: a bold space is wider than a regular one
            Fonts.comic(num, false);
            Fonts.comic(name, false);
            name.setText(e.getValue());
            if (cur) {
                row.setBackgroundColor(unknown ? UNKNOWN : HIGHLIGHT);
                num.setTextColor(0xFFFFFFFF);       // white on the blue, as Holo does
                name.setTextColor(0xFFFFFFFF);
                Fonts.comic(num, true);
                Fonts.comic(name, true);
            }
            row.addView(num);
            row.addView(name);
            lensTable.addView(row);
        }
    }
}
