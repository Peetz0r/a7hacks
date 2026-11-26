package nl.peetz0r.a7hacks;

import android.app.Activity;
import android.content.Context;
import android.content.res.Resources;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.TextPaint;
import android.text.style.MetricAffectingSpan;
import android.view.View;
import android.widget.TextView;

/** The app's own fonts (in assets/fonts, both under the SIL Open Font License): Comic Relief for everything,
 *  Fira Mono for the log. Loaded once; if a font can't be loaded the system font is used instead. */
final class Fonts {
    private static Typeface comic, comicBold, mono;
    private static boolean loaded;

    private Fonts() {}

    private static synchronized void load(Context c) {
        if (loaded) return;
        loaded = true;
        comic = asset(c, "fonts/ComicRelief-Regular.ttf");
        comicBold = asset(c, "fonts/ComicRelief-Bold.ttf");
        mono = asset(c, "fonts/FiraMono-Regular.ttf");
    }

    private static Typeface asset(Context c, String path) {
        try { return Typeface.createFromAsset(c.getAssets(), path); }
        catch (RuntimeException e) { return null; }
    }

    static Typeface comic(Context c) { load(c); return comic; }
    static Typeface comicBold(Context c) { load(c); return comicBold != null ? comicBold : comic; }
    static Typeface mono(Context c) { load(c); return mono != null ? mono : Typeface.MONOSPACE; }

    /** Comic on a text view (bold: the bold face) */
    static void comic(TextView t, boolean bold) {
        Typeface f = bold ? comicBold(t.getContext()) : comic(t.getContext());
        if (f != null) t.setTypeface(f);
        else if (bold) t.setTypeface(null, Typeface.BOLD);
    }

    /** text in Comic as styled text, for views we can't set a typeface on (titles) */
    static CharSequence styled(Context c, CharSequence text) {
        Typeface f = comic(c);
        if (f == null) return text;
        SpannableString s = new SpannableString(text);
        s.setSpan(new FaceSpan(f), 0, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        return s;
    }

    /** The activity's title in Comic: as styled text (the action bar keeps it), and on the title view itself
     *  where there is one (the older title bar) */
    static void title(Activity a, CharSequence text) {
        Typeface f = comic(a);
        if (f == null) { a.setTitle(text); return; }
        a.setTitle(styled(a, text));
        int[] ids = { android.R.id.title, Resources.getSystem().getIdentifier("action_bar_title", "id", "android") };
        for (int id : ids) {
            if (id == 0) continue;
            View v = a.getWindow().getDecorView().findViewById(id);
            if (v instanceof TextView) ((TextView) v).setTypeface(f);
        }
    }

    /** a span that sets the typeface (android.text.style.TypefaceSpan only takes a family name) */
    private static final class FaceSpan extends MetricAffectingSpan {
        private final Typeface face;
        FaceSpan(Typeface face) { this.face = face; }
        private void apply(Paint p) {
            Typeface old = p.getTypeface();
            int style = old != null ? old.getStyle() & ~face.getStyle() : 0;
            p.setTypeface(face);
            if ((style & Typeface.BOLD) != 0) p.setFakeBoldText(true);
        }
        @Override public void updateDrawState(TextPaint p) { apply(p); }
        @Override public void updateMeasureState(TextPaint p) { apply(p); }
    }
}
