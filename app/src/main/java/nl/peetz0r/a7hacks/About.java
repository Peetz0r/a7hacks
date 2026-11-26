package nl.peetz0r.a7hacks;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Paint;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** The info dialog: when the app and the helper were built (stored in UTC, shown in the camera's local time, the
 *  zone AppLog.setZone took from the camera's clock), and where the project lives. */
final class About {
    private static final int LINK = 0xFF33B5E5;     // Holo blue (android.R.color.holo_blue_light), as a link
    private static final Pattern VERSION = Pattern.compile("built (\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d) UTC");
    private static final String PATTERN = "yyyy-MM-dd HH:mm:ss";
    private static volatile String helperBuilt;     // UTC, once found ("unknown" if not)

    private About() {}

    static void show(final Activity a) {
        TableLayout table = new TableLayout(a);
        row(table, "App", local(BuildConfig.BUILT));
        final TextView helper = row(table, "Helper", helperBuilt != null ? local(helperBuilt) : "...");
        if (helperBuilt == null) {
            final File lib = new File(a.getApplicationInfo().nativeLibraryDir, "liba7hacks.so");
            new Thread() {
                public void run() {
                    helperBuilt = version(lib);
                    // runOnUiThread, not helper.post: before the dialog is attached, Android 4.1's View.post queues
                    // on the calling thread (this one, which never runs it), and --version is that fast
                    a.runOnUiThread(new Runnable() {
                        public void run() { helper.setText(local(helperBuilt)); }
                    });
                }
            }.start();
        }

        TextView link = new TextView(a);    // not clickable (no browser on the camera), but it looks like a link
        link.setText("peetz0r.nl/a7hacks");
        link.setTextSize(15);
        link.setTextColor(LINK);
        link.setPaintFlags(link.getPaintFlags() | Paint.UNDERLINE_TEXT_FLAG);
        Fonts.comic(link, false);
        link.setPadding(0, 16, 0, 0);

        // our own OK, not the dialog's: on the camera that one comes out lower than its background image (a
        // nine-patch) needs, which then gets drawn with its edges through the middle. Like the restart button
        // was: its own height and vertical padding, wider at the sides.
        Button ok = new Button(a);
        ok.setText("OK");
        ok.setTextSize(16.5f);
        Fonts.comic(ok, false);
        ok.setPadding(42, ok.getPaddingTop(), 42, ok.getPaddingBottom());
        // drawn into a layer of its own first: Holo's focused background is translucent, and drawn straight onto
        // the dialog, Skia blends its stretched centre one unit darker than its edges, which the camera's display
        // (6 bits per colour) shows as a darker rectangle around the text. Onto an empty layer, then blended as a
        // whole, it comes out even.
        ok.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        LinearLayout.LayoutParams op = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        op.gravity = Gravity.CENTER_HORIZONTAL;
        op.topMargin = 16;

        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(24, 16, 24, 16);
        box.addView(table);
        box.addView(link);
        box.addView(ok, op);

        final AlertDialog d = new AlertDialog.Builder(a)
            .setIcon(R.drawable.ic_launcher)
            .setTitle(Fonts.styled(a, a.getString(R.string.app_name)))
            .setView(box)
            .create();
        ok.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { d.dismiss(); }
        });
        d.show();
        ok.requestFocus();
    }

    private static TextView row(TableLayout table, String label, String value) {
        TableRow r = new TableRow(table.getContext());
        TextView l = new TextView(table.getContext()), v = new TextView(table.getContext());
        l.setTextSize(15);
        v.setTextSize(15);
        Fonts.comic(l, false);
        Fonts.comic(v, false);
        l.setText(label);
        v.setText(value);
        v.setPadding(24, 0, 0, 0);
        r.addView(l);
        r.addView(v);
        table.addView(r);
        return v;
    }

    /** a time in UTC ("yyyy-MM-dd HH:mm:ss") in local time, in the same notation; anything else as it is */
    static String local(String utc) {
        SimpleDateFormat in = new SimpleDateFormat(PATTERN, Locale.US);
        in.setTimeZone(TimeZone.getTimeZone("UTC"));
        in.setLenient(false);
        try {
            Date t = in.parse(utc);
            SimpleDateFormat out = new SimpleDateFormat(PATTERN, Locale.US);
            out.setTimeZone(TimeZone.getDefault());
            return out.format(t);
        } catch (ParseException e) {
            return utc;
        }
    }

    /** the helper's build time (UTC), from its own --version (it needs no root for that), or why not */
    static String version(File lib) {
        try {
            final Process p = Runtime.getRuntime().exec(new String[] {lib.getPath(), "--version"});
            Thread watchdog = new Thread() {     // it answers at once; never wait on it for long
                public void run() {
                    try { Thread.sleep(5000); p.destroy(); } catch (InterruptedException e) { /* done in time */ }
                }
            };
            watchdog.start();
            try {
                BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), "US-ASCII"));
                String line = r.readLine();
                Matcher m = VERSION.matcher(line != null ? line : "");
                return m.find() ? m.group(1) : "unknown (" + (line != null ? line.trim() : "no answer") + ")";
            } finally {
                watchdog.interrupt();
                p.destroy();
            }
        } catch (Exception e) {
            return "unknown (" + e.getMessage() + ")";
        }
    }
}
