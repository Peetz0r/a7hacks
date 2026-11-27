package nl.peetz0r.a7hacks;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.RelativeSizeSpan;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.github.ma1co.openmemories.tweak.NativeException;
import com.github.ma1co.openmemories.tweak.Shell;

import java.util.Locale;

/**
 * Focus peaking's custom colour. The helper's font patcher makes the peaking colour menu's third choice (Sony's
 * "Yellow", renamed "Custom" by the label patcher) use the colour in /android/dev/a7hacks.pk (RAM): "YYCBCR", in
 * Y, Cb, Cr as the camera's display uses them (BT.601, 16..235). The colour is kept in the app's settings (RGB);
 * none set: no file, Sony's Yellow. The file is written at every start of the app's service (it doesn't survive
 * a reboot) and, while the picker is open, as it moves: the helper shows a new colour on the camera at once when
 * Custom is the one in use.
 */
final class PeakingColor {
    private static final String PREFS = "a7hacks";
    private static final String KEY = "peakingColor";
    static final int SONY_YELLOW = 0xFFFFFF00;          // what Sony's Yellow (Y 210, Cb 16, Cr 146) is in RGB
    private static final int FOCUS = 0xFF33B5E5;        // Holo blue (holo_blue_light): the focused slider's name

    private PeakingColor() {}

    /** the custom colour (RGB, opaque), or Sony's Yellow if none is set */
    static int get(Context c) {
        SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return p.contains(KEY) ? p.getInt(KEY, SONY_YELLOW) | 0xFF000000 : SONY_YELLOW;
    }

    static boolean isSet(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).contains(KEY);
    }

    private static void save(Context c, int rgb) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt(KEY, rgb | 0xFF000000).commit();
    }

    /** BT.601 Y, Cb, Cr (limited range, as the camera uses them) of an RGB colour: 0xYYCBCR */
    static int ycbcr(int rgb) {
        double r = Color.red(rgb), g = Color.green(rgb), b = Color.blue(rgb);
        int y = clamp(Math.round(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255));
        int cb = clamp(Math.round(128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255));
        int cr = clamp(Math.round(128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255));
        return y << 16 | cb << 8 | cr;
    }

    private static int clamp(long v) { return (int) Math.max(0, Math.min(255, v)); }

    static String hex(int rgb) { return String.format(Locale.US, "#%06X", rgb & 0xFFFFFF); }

    /** the helper's file: this colour (null: none, Sony's Yellow). Root writes it (it's root's RAM disk), via a
     *  temporary file and a rename so the helper never reads half of it. */
    static void write(Integer rgb) throws NativeException {
        if (rgb == null) Shell.exec("rm -f /android/dev/a7hacks.pk");
        else Shell.exec("cd /android/dev;echo " + String.format(Locale.US, "%06x", ycbcr(rgb)) + ">a7hacks.pk.t;mv a7hacks.pk.t a7hacks.pk");
    }

    /** at the service's start: the colour set, for the helper (Sony's Yellow if none) */
    static void apply(Context c) {
        try {
            write(isSet(c) ? Integer.valueOf(get(c)) : null);
            if (isSet(c)) AppLog.add("focus peaking: Custom is " + hex(get(c)));
        } catch (NativeException e) {
            AppLog.add("focus peaking: could not set the custom colour: " + e.getMessage());
        }
    }

    /** a square in that colour, centred in a box, like a player's stop symbol (a thin grey edge: black shows too) */
    static Drawable symbol(Context c, int rgb, int box, int square) {
        Bitmap bm = Bitmap.createBitmap(box, box, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(bm);
        Paint p = new Paint();
        int o = (box - square) / 2;
        p.setColor(0xFF808080);
        cv.drawRect(o, o, o + square, o + square, p);
        p.setColor(rgb | 0xFF000000);
        cv.drawRect(o + 1, o + 1, o + square - 1, o + square - 1, p);
        BitmapDrawable d = new BitmapDrawable(c.getResources(), bm);
        d.setFilterBitmap(false);
        return d;
    }

    /** the main screen's button symbol: the size of the system's menu icons next to it (Restart's), the square half
     *  of it, so the buttons come out the same size */
    static Drawable symbol(Context c, int rgb) {
        Drawable ref = c.getResources().getDrawable(android.R.drawable.ic_menu_rotate);
        int box = ref.getIntrinsicWidth() > 0 ? ref.getIntrinsicWidth() : 48;
        return symbol(c, rgb, box, box / 2);
    }

    /** writes the colour shown (null: none, Sony's Yellow) at most every 200 ms, one write at a time and in order,
     *  off the main thread (each write is a root command) */
    private static final class Writer {
        private final Handler handler = new Handler();
        private Integer pending, written;
        private boolean dirty, busy;
        private final Runnable flush = new Runnable() {
            public void run() {
                if (busy || !dirty) return;
                final Integer v = pending;
                dirty = false;
                if (v == null ? written == null : v.equals(written)) return;
                busy = true;
                new Thread() {
                    public void run() {
                        try { write(v); } catch (NativeException e) { /* the picker goes on; the log has the start's */ }
                        handler.post(new Runnable() {
                            public void run() { written = v; busy = false; if (dirty) handler.postDelayed(flush, 200); }
                        });
                    }
                }.start();
            }
        };
        Writer(Integer now) { written = now; }
        void show(Integer rgb) {
            pending = rgb == null ? null : Integer.valueOf(rgb.intValue() | 0xFF000000);
            dirty = true;
            handler.removeCallbacks(flush);
            if (!busy) handler.post(flush);
        }
    }

    private static final int[] PRESETS = {
        0xFFFF0000, 0xFFFF8000, 0xFFFFFF00, 0xFF00FF00, 0xFF0000FF, 0xFF8000FF, 0xFF000000, 0xFFFFFFFF,
    };
    private static final String[] PRESET_NAMES = { "Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Black", "White" };

    /** The picker: hue, saturation and brightness on sliders (left and right move them), a row of ready colours,
     *  and the colour itself with its RGB value. OK keeps it; Cancel (or Back) puts the one before back. done runs
     *  after either, on the main thread. */
    static void pick(final Activity a, final Runnable done) {
        final int before = get(a);
        final boolean wasSet = isSet(a);
        final Writer writer = new Writer(wasSet ? Integer.valueOf(before) : null);
        final float[] hsv = new float[3];
        Color.colorToHSV(before, hsv);

        final ImageView swatch = new ImageView(a);
        final TextView value = new TextView(a);
        value.setTextSize(13);
        Fonts.comic(value, false);
        value.setGravity(Gravity.CENTER_HORIZONTAL);

        final SeekBar[] bars = new SeekBar[3];
        final boolean[] setting = new boolean[1];       // the sliders being set from a ready colour: no feedback
        final Runnable update = new Runnable() {
            public void run() {
                int rgb = Color.HSVToColor(hsv);
                swatch.setImageDrawable(symbol(a, rgb, 64, 56));
                value.setText(hex(rgb));
                writer.show(Integer.valueOf(rgb));
            }
        };
        // one line per slider, its name at the left: the focused one's name in Holo blue and bold (the camera has no
        // touch screen, and Holo's own focus on a slider hardly shows)
        final int[] max = { 360, 100, 100 };
        String[] names = { "Hue", "Saturation", "Brightness" };
        LinearLayout sliders = new LinearLayout(a);
        sliders.setOrientation(LinearLayout.VERTICAL);
        for (int i = 0; i < 3; i++) {
            final int k = i;
            final TextView l = new TextView(a);
            l.setText(names[i]);
            l.setTextSize(14);
            Fonts.comic(l, false);
            final int plain = l.getCurrentTextColor();
            SeekBar s = new SeekBar(a);
            s.setMax(max[i]);
            s.setKeyProgressIncrement(i == 0 ? 5 : 2);
            s.setProgress(Math.round(k == 0 ? hsv[0] : hsv[k] * 100));
            s.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar sb, int p, boolean user) {
                    if (setting[0]) return;
                    hsv[k] = k == 0 ? Math.min(p, 359.99f) : p / 100f;
                    update.run();
                }
                public void onStartTrackingTouch(SeekBar sb) {}
                public void onStopTrackingTouch(SeekBar sb) {}
            });
            s.setOnFocusChangeListener(new View.OnFocusChangeListener() {
                public void onFocusChange(View v, boolean has) {
                    l.setTextColor(has ? FOCUS : plain);
                    Fonts.comic(l, has);
                }
            });
            bars[i] = s;
            LinearLayout line = new LinearLayout(a);
            line.setGravity(Gravity.CENTER_VERTICAL);
            line.addView(l, new LinearLayout.LayoutParams(110, LinearLayout.LayoutParams.WRAP_CONTENT));
            line.addView(s, new LinearLayout.LayoutParams(300, LinearLayout.LayoutParams.WRAP_CONTENT));
            sliders.addView(line);
        }

        LinearLayout preview = new LinearLayout(a);
        preview.setOrientation(LinearLayout.VERTICAL);
        preview.setGravity(Gravity.CENTER_HORIZONTAL);
        preview.addView(swatch);
        preview.addView(value);

        LinearLayout top = new LinearLayout(a);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.addView(preview);
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        sp.leftMargin = 16;
        top.addView(sliders, sp);

        LinearLayout presets = new LinearLayout(a);
        presets.setGravity(Gravity.CENTER_HORIZONTAL);
        for (int i = 0; i < PRESETS.length; i++) {
            final int rgb = PRESETS[i];
            ImageButton b = new ImageButton(a);
            b.setImageDrawable(symbol(a, rgb, 28, 22));
            b.setContentDescription(PRESET_NAMES[i]);
            b.setPadding(8, b.getPaddingTop(), 8, b.getPaddingBottom());
            b.setLayerType(View.LAYER_TYPE_SOFTWARE, null);     // see About's OK: an even focused background
            b.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    Color.colorToHSV(rgb, hsv);
                    setting[0] = true;
                    bars[0].setProgress(Math.round(hsv[0]));
                    bars[1].setProgress(Math.round(hsv[1] * 100));
                    bars[2].setProgress(Math.round(hsv[2] * 100));
                    setting[0] = false;
                    update.run();
                }
            });
            presets.addView(b);
        }

        Button cancel = button(a, "Cancel");
        Button ok = button(a, "OK");
        LinearLayout buttons = new LinearLayout(a);
        buttons.setGravity(Gravity.CENTER_HORIZONTAL);
        buttons.addView(cancel);
        LinearLayout.LayoutParams okp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        okp.leftMargin = 24;
        buttons.addView(ok, okp);

        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(16, 8, 16, 8);
        box.addView(top);
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        pp.topMargin = 4;
        box.addView(presets, pp);
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        bp.topMargin = 4;
        box.addView(buttons, bp);

        // the title smaller than the info dialog's (the same title view, the text at 80%)
        CharSequence t = Fonts.styled(a, "Focus Peaking Custom Color");
        SpannableString title = new SpannableString(t);
        title.setSpan(new RelativeSizeSpan(0.8f), 0, title.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);

        final boolean[] kept = new boolean[1];
        final AlertDialog d = new AlertDialog.Builder(a).setTitle(title).setView(box).create();
        ok.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                int rgb = Color.HSVToColor(hsv) | 0xFF000000;
                save(a, rgb);
                writer.show(Integer.valueOf(rgb));
                kept[0] = true;
                AppLog.add("focus peaking: Custom is " + hex(rgb));
                d.dismiss();
            }
        });
        cancel.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { d.dismiss(); }
        });
        d.setOnDismissListener(new DialogInterface.OnDismissListener() {
            public void onDismiss(DialogInterface di) {
                if (!kept[0]) writer.show(wasSet ? Integer.valueOf(before) : null);   // Cancel, Back: the one from before
                if (done != null) done.run();
            }
        });
        update.run();
        d.show();
        bars[0].requestFocus();
    }

    /** our own dialog button, as About's OK (its own height, wider at the sides, drawn in a layer of its own) */
    private static Button button(Context c, String text) {
        Button b = new Button(c);
        b.setText(text);
        b.setTextSize(16.5f);
        Fonts.comic(b, false);
        b.setPadding(42, b.getPaddingTop(), 42, b.getPaddingBottom());
        b.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        return b;
    }
}
